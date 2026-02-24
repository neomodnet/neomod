#pragma once

#include "Resource.h"
#include "AsyncFuture.h"
#include "SyncMutex.h"
#include "Hashing.h"

#include <algorithm>
#include <atomic>
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

    // block until a specific resource's async load completes, then run sync init.
    // returns true if the resource was found in-flight; false if it wasn't loading.
    bool waitForResource(Resource *resource);

    // status queries
    [[nodiscard]] inline bool isLoading() const { return this->m_inFlightCount.load(std::memory_order_acquire) > 0; }
    [[nodiscard]] bool isLoadingResource(const Resource *resource) const;
    [[nodiscard]] size_t getNumInFlight() const { return this->m_inFlightCount.load(std::memory_order_acquire); }
    [[nodiscard]] inline size_t getNumAsyncDestroyQueue() const { return this->asyncDestroyQueue.size(); }
    [[nodiscard]] inline size_t getMaxPerUpdate() const { return this->iLoadsPerUpdate; }

   private:
    struct InFlightResource {
        Resource *resource;
        Async::Future<void> future;
    };

    // in-flight resources (insertion-ordered)
    std::vector<InFlightResource> m_inFlight;
    mutable Sync::mutex m_inFlightMutex;
    std::atomic<size_t> m_inFlightCount{0};

    // how many resources to load on update()
    size_t iLoadsPerUpdate;

    // floor value for iLoadsPerUpdate decay
    size_t iLoadsPerUpdateFloor;

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
