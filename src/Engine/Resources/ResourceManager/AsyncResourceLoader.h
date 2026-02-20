#pragma once

#include "Resource.h"
#include "SyncCV.h"
#include "Hashing.h"

#include <algorithm>
#include <atomic>
#include <queue>
#include <vector>

class ConVar;

class AsyncResourceLoader final {
    NOCOPY_NOMOVE(AsyncResourceLoader)

   public:
    AsyncResourceLoader();
    ~AsyncResourceLoader();

    // main interface for ResourceManager
    inline void setMaxPerUpdate(size_t num) { this->iLoadsPerUpdate = std::clamp<size_t>(num, 1, 512); }
    void requestAsyncLoad(Resource *resource);
    void update(bool lowLatency);
    void shutdown();

    // resource lifecycle management
    void scheduleAsyncDestroy(Resource *resource, bool shouldDelete);
    void reloadResources(const std::vector<Resource *> &resources);

    // status queries
    [[nodiscard]] inline bool isLoading() const { return this->iActiveWorkCount.load(std::memory_order_acquire) > 0; }
    [[nodiscard]] bool isLoadingResource(const Resource *resource) const;
    [[nodiscard]] size_t getNumLoadingWork() const { return this->iActiveWorkCount.load(std::memory_order_acquire); }
    [[nodiscard]] size_t getNumActiveThreads() const {
        return this->iActiveThreadCount.load(std::memory_order_acquire);
    }
    [[nodiscard]] inline size_t getNumLoadingWorkAsyncDestroy() const { return this->asyncDestroyQueue.size(); }
    [[nodiscard]] inline size_t getMaxPerUpdate() const { return this->iLoadsPerUpdate; }

   private:
    enum class WorkState : uint8_t {
        PENDING = 0,
        ASYNC_IN_PROGRESS = 1,
        ASYNC_INTERRUPTED = 2,
        ASYNC_COMPLETE = 3,
        SYNC_COMPLETE = 4
    };

    struct LoadingWork {
        Resource *resource;
        size_t workId;
        WorkState state{WorkState::PENDING};

        LoadingWork(Resource *res, size_t id) : resource(res), workId(id) {}
    };

    class LoaderThread;
    friend class LoaderThread;

    // thread management
    void ensureThreadAvailable();
    void cleanupIdleThreads();

    // work queue management
    std::unique_ptr<LoadingWork> getNextPendingWork();
    void markWorkAsyncComplete(std::unique_ptr<LoadingWork> work);
    std::unique_ptr<LoadingWork> getNextAsyncCompleteWork();

    // set during ctor, dependent on hardware
    const size_t iMaxThreads;
    static constexpr const size_t HARD_THREADCOUNT_LIMIT{32};
    // always keep one thread around to avoid unnecessary thread creation/destruction spikes for spurious loads
    static constexpr const size_t MIN_NUM_THREADS{1};

    // how many resources to load on update()
    // default is == max # threads (or 1 during gameplay)
    size_t iLoadsPerUpdate;

    // thread idle configuration
    static constexpr uint64_t IDLE_GRACE_PERIOD{1000};  // 1 sec
    static constexpr uint64_t IDLE_TIMEOUT{15000};      // 15 sec
    uint64_t lastCleanupTime;

    // thread pool
    Hash::flat::map<size_t, std::unique_ptr<LoaderThread>> threadpool;  // index to thread
    mutable Sync::mutex threadsMutex;

    // thread lifecycle tracking
    std::atomic<size_t> iActiveThreadCount{0};
    std::atomic<size_t> iTotalThreadsCreated{0};

    // separate queues for different work states (avoids O(n) scanning)
    std::queue<std::unique_ptr<LoadingWork>> pendingWork;
    std::queue<std::unique_ptr<LoadingWork>> asyncCompleteWork;

    // single mutex for both work queues (they're accessed in sequence, not concurrently)
    mutable Sync::mutex workQueueMutex;

    // fast lookup for checking if a resource is being loaded
    Hash::flat::set<const Resource *> loadingResourcesSet;
    mutable Sync::mutex loadingResourcesMutex;

    // atomic counters for efficient status queries
    std::atomic<size_t> iActiveWorkCount{0};
    std::atomic<size_t> iWorkIdCounter{0};

    // work notification
    Sync::condition_variable_any workAvailable;
    Sync::mutex workAvailableMutex;

    // resources that had a reload requested while they were still being loaded;
    // will be released + re-queued for async load after their current load completes
    Hash::flat::set<Resource *> pendingReloads;
    Sync::mutex pendingReloadsMutex;

    // async destroy queue
    struct ToDestroy {
        Resource *rs;
        bool shouldDelete;
    };

    std::vector<ToDestroy> asyncDestroyQueue;
    Sync::mutex asyncDestroyMutex;

    // lifecycle flags
    bool bShuttingDown{false};
};
