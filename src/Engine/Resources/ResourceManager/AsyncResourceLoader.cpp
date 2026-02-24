// Copyright (c) 2025, WH, All rights reserved.
#include "AsyncResourceLoader.h"

#include "AsyncPool.h"
#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "Hashing.h"

#include <algorithm>
#include <cmath>
#include <utility>

AsyncResourceLoader::AsyncResourceLoader()
    : iLoadsPerUpdate(static_cast<size_t>(std::ceil(static_cast<double>(Async::pool().thread_count()) * (1. / 4.)))),
      iLoadsPerUpdateFloor(iLoadsPerUpdate) {}

AsyncResourceLoader::~AsyncResourceLoader() { shutdown(); }

void AsyncResourceLoader::shutdown() {
    if(this->bShuttingDown) return;

    this->bShuttingDown = true;

    // interrupt all in-flight resources, then move the vector out so we can wait without holding the lock
    std::vector<InFlightResource> inFlight;
    {
        Sync::scoped_lock lock(this->m_inFlightMutex);
        for(auto &entry : this->m_inFlight) {
            entry.resource->interruptLoad();
        }
        inFlight = std::move(this->m_inFlight);
    }

    // block until all in-flight work finishes
    for(auto &entry : inFlight) {
        entry.future.wait();
    }
    this->m_inFlightCount.store(0, std::memory_order_relaxed);

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
    if(this->bShuttingDown) return;

    auto future = Async::submit(
        [resource] {
            if(!resource->isInterrupted()) {
                resource->loadAsync();
            }
        },
        Lane::Background);

    {
        Sync::scoped_lock lock(this->m_inFlightMutex);
        this->m_inFlight.push_back({resource, std::move(future)});
    }
    this->m_inFlightCount.fetch_add(1, std::memory_order_relaxed);
}

void AsyncResourceLoader::update(bool lowLatency) {
    const bool debug = cv::debug_rm.getBool();

    const size_t amountToProcess = lowLatency ? 1 : this->iLoadsPerUpdate;

    // 1. collect ready resources under lock
    std::vector<Resource *> readyResources;
    {
        Sync::scoped_lock lock(this->m_inFlightMutex);
        for(auto it = this->m_inFlight.begin();
            it != this->m_inFlight.end() && readyResources.size() < amountToProcess;) {
            if(!it->future.is_ready()) {
                ++it;
                continue;
            }

            Resource *rs = it->resource;
            it->future.get();
            it = this->m_inFlight.erase(it);
            this->m_inFlightCount.fetch_sub(1, std::memory_order_relaxed);

            if(!rs->isInterrupted()) {
                readyResources.push_back(rs);
            }
        }

        if(readyResources.empty() && !lowLatency) {
            // decay back to default
            this->iLoadsPerUpdate =
                static_cast<size_t>(std::max(std::floor(static_cast<double>(this->iLoadsPerUpdate) * (15. / 16.)),
                                             static_cast<double>(this->iLoadsPerUpdateFloor)));
        }
    }

    // 2. sync init outside lock (GPU uploads happen here)
    for(Resource *rs : readyResources) {
        logIf(debug, "Sync init for {:s}", rs->getDebugIdentifier());
        rs->load();
    }

    // 3. handle pending reloads for completed resources
    for(Resource *rs : readyResources) {
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

    // 4. process async destroy queue
    std::vector<ToDestroy> resourcesReadyForDestroy;

    {
        Sync::scoped_lock lock(this->asyncDestroyMutex);
        for(ssize_t i = 0; i < this->asyncDestroyQueue.size(); i++) {
            bool canBeDestroyed = !isLoadingResource(this->asyncDestroyQueue[i].rs);

            if(canBeDestroyed) {
                resourcesReadyForDestroy.push_back(this->asyncDestroyQueue[i]);
                this->asyncDestroyQueue.erase(this->asyncDestroyQueue.begin() + i);

                if(resourcesReadyForDestroy.size() >= amountToProcess) {
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

bool AsyncResourceLoader::waitForResource(Resource *resource) {
    Async::Future<void> future;
    {
        Sync::scoped_lock lock(this->m_inFlightMutex);
        auto it = std::ranges::find(this->m_inFlight, resource, &InFlightResource::resource);
        if(it == this->m_inFlight.end()) return false;
        future = std::move(it->future);
        this->m_inFlight.erase(it);
    }
    this->m_inFlightCount.fetch_sub(1, std::memory_order_relaxed);

    // block until async phase completes
    future.wait();

    if(!resource->isInterrupted()) {
        resource->load();
    }

    // handle pending reloads
    bool needsReload = false;
    {
        Sync::scoped_lock lock(this->pendingReloadsMutex);
        needsReload = this->pendingReloads.erase(resource) > 0;
    }
    if(needsReload) {
        resource->release();
        requestAsyncLoad(resource);
    }

    return true;
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
    Sync::scoped_lock lock(this->m_inFlightMutex);
    return std::ranges::any_of(this->m_inFlight, [resource](const auto &e) { return e.resource == resource; });
}
