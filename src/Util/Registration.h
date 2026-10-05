#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "noinclude.h"
#include "types.h"

#include <utility>

namespace Mc {
// What a service keeps on a caller's behalf (a callback, a listener, an overlay) lasts as long as the Registration it
// returned for it: destroying or resetting the Registration removes and destroys that before returning, and nothing of
// it runs afterwards. A service hands one out from the call that takes the callback, with an end function of its own,
// and outlives every Registration it handed out.
class [[nodiscard]] Registration {
   public:
    // REVOKE: remove and destroy it now. DETACH: the service keeps it until it's done with it
    enum class End : u8 { REVOKE, DETACH };
    using EndFn = void (*)(void *service, u64 id, End how);

    Registration() = default;
    Registration(EndFn end, void *service, u64 id) : endFn(end), service(service), id(id) {}
    Registration(Registration &&other) noexcept
        : endFn(std::exchange(other.endFn, nullptr)), service(other.service), id(other.id) {}
    Registration &operator=(Registration &&other) noexcept {
        if(this != &other) {
            this->reset();
            this->endFn = std::exchange(other.endFn, nullptr);
            this->service = other.service;
            this->id = other.id;
        }
        return *this;
    }
    Registration(const Registration &) = delete;
    Registration &operator=(const Registration &) = delete;
    ~Registration() { this->reset(); }

    void reset() { this->end(End::REVOKE); }

    // for a one-shot registration (a toast's click callback, a read's completion) of code that never gets unloaded:
    // the service drops it once it's done with it
    MC_UNREVOCABLE void detach() { this->end(End::DETACH); }

   private:
    void end(End how) {
        if(const EndFn fn = std::exchange(this->endFn, nullptr)) fn(this->service, this->id, how);
    }

    EndFn endFn{nullptr};
    void *service{nullptr};
    u64 id{0};
};
}  // namespace Mc
