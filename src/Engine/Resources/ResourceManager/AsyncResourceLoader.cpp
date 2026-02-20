// Copyright (c) 2025, WH, All rights reserved.
#include "AsyncResourceLoader.h"

#include "ConVar.h"
#include "Engine.h"
#include "Thread.h"
#include "Timing.h"
#include "Logging.h"
#include "SyncJthread.h"
#include "Hashing.h"

#include <algorithm>
#include <utility>

//==================================
// LOADER THREAD
//==================================
class AsyncResourceLoader::LoaderThread final {
   public:
    // this is set on AsyncResourceLoader creation
    static inline AsyncResourceLoader *loader_ptr{nullptr};

    std::atomic<uint64_t> last_active;

    LoaderThread(size_t index) noexcept : last_active(Timing::getTicksMS()), thread(worker_loop, this, index) {}

    [[nodiscard]] bool isReady() const noexcept { return this->thread.joinable(); }

    [[nodiscard]] bool isIdleTooLong() const noexcept {
        const uint64_t lastActive = this->last_active.load(std::memory_order_relaxed);
        const uint64_t now = Timing::getTicksMS();
        return now - lastActive > IDLE_TIMEOUT;
    }

   private:
    Sync::jthread thread;

    static void worker_loop(const Sync::stop_token &stoken, LoaderThread *this_, const size_t thread_index) noexcept {
        loader_ptr->iActiveThreadCount.fetch_add(1, std::memory_order_relaxed);

        logIfCV(debug_rm, "Thread #{} started", thread_index);

        const UString loaderThreadName{fmt::format("res_ldr_thr{}", (thread_index % loader_ptr->iMaxThreads) + 1)};
        McThread::set_current_thread_name(loaderThreadName);
        McThread::set_current_thread_prio(
            McThread::Priority::NORMAL);  // reset priority (don't inherit from main thread)

        while(!stoken.stop_requested()) {
            const bool debug = cv::debug_rm.getBool();

            auto work = loader_ptr->getNextPendingWork();
            if(!work) {
                // yield in case we're sharing a logical CPU, like on a single-core system
                Timing::sleepMS(1);

                Sync::unique_lock lock(loader_ptr->workAvailableMutex);

                // wait indefinitely until work is available or stop is requested
                loader_ptr->workAvailable.wait(
                    lock, stoken, []() { return loader_ptr->iActiveWorkCount.load(std::memory_order_acquire) > 0; });

                continue;
            }

            // notify that this thread completed work
            this_->last_active.store(Timing::getTicksMS(), std::memory_order_release);

            Resource *resource = work->resource;
            const bool interrupted = resource->isInterrupted();

            std::string debugName;
            if(debug) {
                debugName = resource->getDebugIdentifier();
                if(interrupted) {
                    debugLog("Thread #{} skipping (interrupted) workID {} {:s}", thread_index, work->workId, debugName);
                } else {
                    debugLog("Thread #{} loading workID {} {:s}", thread_index, work->workId, debugName);
                }
            }

            if(interrupted) {
                work->state = WorkState::ASYNC_INTERRUPTED;
            } else {
                work->state = WorkState::ASYNC_IN_PROGRESS;

                resource->loadAsync();

                logIf(debug, "Thread #{} finished async loading {:s}", thread_index, debugName);

                work->state = WorkState::ASYNC_COMPLETE;
            }

            loader_ptr->markWorkAsyncComplete(std::move(work));

            // yield again before loop
            Timing::sleepMS(0);
        }

        loader_ptr->iActiveThreadCount.fetch_sub(1, std::memory_order_acq_rel);

        logIfCV(debug_rm, "Thread #{} exiting", thread_index);
    }
};

//==================================
// ASYNC RESOURCE LOADER
//==================================

AsyncResourceLoader::AsyncResourceLoader()
    : iMaxThreads(std::clamp<size_t>(McThread::get_logical_cpu_count() - 1, 1, HARD_THREADCOUNT_LIMIT)),
      iLoadsPerUpdate(static_cast<size_t>(std::ceil(static_cast<double>(this->iMaxThreads) * (1. / 4.)))),
      lastCleanupTime(Timing::getTicksMS()) {
    // init loader threads parent ref
    LoaderThread::loader_ptr = this;

    // sanity lock
    Sync::scoped_lock lock(this->threadsMutex);

    // pre-create the maximum amount of threads for better startup responsiveness
    for(size_t i = 0; i < this->iMaxThreads; ++i) {
        const size_t idx = this->iTotalThreadsCreated.fetch_add(1, std::memory_order_relaxed);
        auto loaderThread = std::make_unique<LoaderThread>(idx);

        if(!loaderThread->isReady()) {
            engine->showMessageErrorFatal("Resource Manager Error", "Couldn't create core loader threads!");
            fubar_abort();  // there is no point in continuing even an inch further
        } else {
            logIfCV(debug_rm, "Created initial thread {}", idx);
            this->threadpool[idx] = std::move(loaderThread);
        }
    }
}

AsyncResourceLoader::~AsyncResourceLoader() { shutdown(); }

void AsyncResourceLoader::shutdown() {
    if(this->bShuttingDown) return;

    this->bShuttingDown = true;

    // stop threads
    {
        Sync::scoped_lock lock(this->threadsMutex);

        // clear threadpool vector
        this->threadpool.clear();
    }

    // wake them up from their slumber if they are still sleeping, outside the lock
    this->workAvailable.notify_all();

    // cleanup remaining work items
    {
        Sync::scoped_lock lock(this->workQueueMutex);
        while(!this->pendingWork.empty()) {
            this->pendingWork.pop();
        }
        while(!this->asyncCompleteWork.empty()) {
            this->asyncCompleteWork.pop();
        }
    }

    // cleanup loading resources tracking
    {
        Sync::scoped_lock lock(this->loadingResourcesMutex);
        this->loadingResourcesSet.clear();
    }

    {
        Sync::scoped_lock lock(this->pendingReloadsMutex);
        this->pendingReloads.clear();
    }

    // cleanup async destroy queue
    for(auto &[rs, del] : this->asyncDestroyQueue) {
        rs->release();
        if(del) {
            SAFE_DELETE(rs);
        }
    }
    this->asyncDestroyQueue.clear();
}

void AsyncResourceLoader::requestAsyncLoad(Resource *resource) {
    auto work = std::make_unique<LoadingWork>(resource, this->iWorkIdCounter.fetch_add(1, std::memory_order_relaxed));

    // add to tracking set
    {
        Sync::scoped_lock lock(this->loadingResourcesMutex);
        this->loadingResourcesSet.insert(resource);
    }

    // add to work queue
    {
        Sync::scoped_lock lock(this->workQueueMutex);
        this->pendingWork.push(std::move(work));
    }

    this->iActiveWorkCount.fetch_add(1, std::memory_order_relaxed);
    ensureThreadAvailable();
    this->workAvailable.notify_one();
}

void AsyncResourceLoader::update(bool lowLatency) {
    if(!lowLatency) cleanupIdleThreads();
    const bool debug = cv::debug_rm.getBool();

    const size_t amountToProcess = lowLatency ? 1 : this->iLoadsPerUpdate;

    // process completed async work
    size_t numProcessed = 0;

    while(numProcessed < amountToProcess) {
        auto work = getNextAsyncCompleteWork();
        if(!work) {
            if(!lowLatency) {
                // decay back to default
                this->iLoadsPerUpdate =
                    static_cast<size_t>(std::max(std::floor(static_cast<double>(this->iLoadsPerUpdate) * (15. / 16.)),
                                                 std::ceil(static_cast<double>(this->iMaxThreads) * (1. / 4.))));
            }
            break;
        }

        Resource *rs = work->resource;
        // remove from tracking set (before load(), since that might want to reload itself)
        {
            Sync::scoped_lock lock(this->loadingResourcesMutex);
            this->loadingResourcesSet.erase(rs);
        }

        const bool interrupted = (work->state == WorkState::ASYNC_INTERRUPTED) || rs->isInterrupted();
        if(!interrupted) {
            logIf(debug, "Sync init for {:s}", rs->getDebugIdentifier());
            rs->load();
        } else {
            logIf(debug, "Skipping sync init for {:s}", rs->getDebugIdentifier());
        }

        work->state =
            WorkState::SYNC_COMPLETE;  // this is currently pointless since work is destroyed like 5 lines later

        this->iActiveWorkCount.fetch_sub(1, std::memory_order_acq_rel);
        if(!interrupted) numProcessed++;

        // if a reload was requested while this resource was being loaded, do it now
        bool needsReload = false;
        {
            Sync::scoped_lock lock(this->pendingReloadsMutex);
            needsReload = this->pendingReloads.erase(rs) > 0;
        }
        if(needsReload) {
            logIf(debug, "Executing deferred reload for {:s}", rs->getDebugIdentifier());
            rs->release();
            requestAsyncLoad(rs);
        }
    }

    // process async destroy queue
    std::vector<ToDestroy> resourcesReadyForDestroy;

    {
        Sync::scoped_lock lock(this->asyncDestroyMutex);
        for(size_t i = 0; i < this->asyncDestroyQueue.size(); i++) {
            bool canBeDestroyed = true;
            auto &current = this->asyncDestroyQueue[i];

            {
                Sync::scoped_lock loadingLock(this->loadingResourcesMutex);
                if(this->loadingResourcesSet.contains(current.rs)) {
                    canBeDestroyed = false;
                }
            }

            if(canBeDestroyed) {
                resourcesReadyForDestroy.push_back(current);
                this->asyncDestroyQueue.erase(this->asyncDestroyQueue.begin() + i);

                if(resourcesReadyForDestroy.size() >= amountToProcess) {
                    // also respect amount to process per update here, break early if we'd try to destroy more than
                    // that many resources
                    break;
                }
                i--;
            }
        }
    }

    for(auto &[rs, deletable] : resourcesReadyForDestroy) {
        logIf(debug, "Async destroy of resource {:s} (delete: {})", rs->getDebugIdentifier(), deletable);
        rs->release();
        if(deletable) {
            SAFE_DELETE(rs);
        }
    }
}

void AsyncResourceLoader::scheduleAsyncDestroy(Resource *resource, bool shouldDelete) {
    logIfCV(debug_rm, "Scheduled async destroy of {:s}", resource->getDebugIdentifier());

    // destroy cancels any pending reload
    {
        Sync::scoped_lock lock(this->pendingReloadsMutex);
        this->pendingReloads.erase(resource);
    }

    Sync::scoped_lock lock(this->asyncDestroyMutex);
    this->asyncDestroyQueue.emplace_back(ToDestroy{.rs = resource, .shouldDelete = shouldDelete});
}

void AsyncResourceLoader::reloadResources(const std::vector<Resource *> &resources) {
    const bool debug = cv::debug_rm.getBool();
    if(resources.empty()) {
        logIf(debug, "W: reloadResources with empty resources vector!");
        return;
    }

    logIf(debug, "Async reloading {} resources", resources.size());

    Hash::flat::set<Resource *> resourcesToReload;
    for(Resource *rs : resources) {
        if(rs == nullptr) continue;

        logIf(debug, "Async reloading {:s}", rs->getDebugIdentifier());

        if(isLoadingResource(rs)) {
            // can't reload right now; defer until the current load completes
            Sync::scoped_lock lock(this->pendingReloadsMutex);
            this->pendingReloads.insert(rs);
            logIf(debug, "Resource {:s} is currently being loaded, deferring reload", rs->getDebugIdentifier());
        } else {
            if(const auto &[_, newlyInserted] = resourcesToReload.insert(rs); newlyInserted) {
                rs->release();
            } else if(debug) {
                debugLog("W: skipping duplicate pending reload");
            }
        }
    }

    for(Resource *rs : resourcesToReload) {
        requestAsyncLoad(rs);
    }
}

bool AsyncResourceLoader::isLoadingResource(const Resource *resource) const {
    Sync::scoped_lock lock(this->loadingResourcesMutex);
    return this->loadingResourcesSet.contains(resource);
}

void AsyncResourceLoader::ensureThreadAvailable() {
    size_t activeThreads = this->iActiveThreadCount.load(std::memory_order_acquire);
    size_t activeWorkCount = this->iActiveWorkCount.load(std::memory_order_acquire);

    if(activeWorkCount > activeThreads && activeThreads < this->iMaxThreads) {
        Sync::scoped_lock lock(this->threadsMutex);

        if(this->threadpool.size() < this->iMaxThreads) {
            const bool debug = cv::debug_rm.getBool();

            const size_t idx = this->iTotalThreadsCreated.fetch_add(1, std::memory_order_relaxed);
            auto loaderThread = std::make_unique<LoaderThread>(idx);

            if(!loaderThread->isReady()) {
                logIf(debug, "W: Couldn't create dynamic thread!");
            } else {
                logIf(debug, "Created dynamic thread #{} (total: {})", idx, this->threadpool.size() + 1);

                this->threadpool[idx] = std::move(loaderThread);
            }
        }
    }
}

void AsyncResourceLoader::cleanupIdleThreads() {
    if(this->threadpool.size() <= MIN_NUM_THREADS) return;

    // only run cleanup periodically to avoid overhead
    const uint64_t now = Timing::getTicksMS();
    if(now - this->lastCleanupTime < IDLE_GRACE_PERIOD) {
        return;
    }
    this->lastCleanupTime = now;

    // don't cleanup if we still have work
    // TODO: does this make things better or worse in reality?
    // if(this->iActiveWorkCount.load(std::memory_order_acquire) > 0) return;

    Sync::scoped_lock lock(this->threadsMutex);

    if(this->threadpool.size() <= MIN_NUM_THREADS) return;  // check under lock again

    const bool debug = cv::debug_rm.getBool();

    // find threads that have been idle too long
    for(auto &[idx, thread] : this->threadpool) {
        if(thread->isIdleTooLong()) {
            logIf(debug, "Removing idle thread #{} (idle timeout exceeded, pool size: {} -> {})", idx,
                  this->threadpool.size(), this->threadpool.size() - 1);
            this->threadpool.erase(idx);
            break;  // remove only one thread at a time
        }
    }
}

std::unique_ptr<AsyncResourceLoader::LoadingWork> AsyncResourceLoader::getNextPendingWork() {
    Sync::scoped_lock lock(this->workQueueMutex);

    if(this->pendingWork.empty()) return nullptr;

    auto work = std::move(this->pendingWork.front());
    this->pendingWork.pop();
    return work;
}

void AsyncResourceLoader::markWorkAsyncComplete(std::unique_ptr<LoadingWork> work) {
    Sync::scoped_lock lock(this->workQueueMutex);
    this->asyncCompleteWork.push(std::move(work));
}

std::unique_ptr<AsyncResourceLoader::LoadingWork> AsyncResourceLoader::getNextAsyncCompleteWork() {
    Sync::scoped_lock lock(this->workQueueMutex);

    if(this->asyncCompleteWork.empty()) return nullptr;

    auto work = std::move(this->asyncCompleteWork.front());
    this->asyncCompleteWork.pop();
    return work;
}
