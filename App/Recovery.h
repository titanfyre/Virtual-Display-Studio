#pragma once
#include <cstdint>
// Pure failure accounting, shared with the driver's swapchain recovery path.
struct DisplayFailureBudget {
    std::uint64_t window=0;
    unsigned failures=0;
    bool blocked=false;
    bool Fail(std::uint64_t now) noexcept {
        if(blocked)return true;
        if(!failures||now<window||now-window>60000){window=now;failures=0;}
        blocked=++failures>=5;
        return blocked;
    }
};
