#pragma once
#include <windows.h>
#include <chrono>
#include <cstdlib>

namespace interfayce {
class DesktopClickFilter {
public:
    using Clock = std::chrono::steady_clock;
    void Begin(POINT point, Clock::time_point now = Clock::now()) {
        anchor_ = point;
        began_ = now;
        active_ = true;
        dragging_ = false;
    }
    POINT Move(POINT point, Clock::time_point now = Clock::now()) {
        if (!active_) return point;
        if (!dragging_ && now - began_ >= std::chrono::milliseconds(150)
            && (std::abs(point.x - anchor_.x) >= 16 || std::abs(point.y - anchor_.y) >= 16)) {
            dragging_ = true;
        }
        return dragging_ ? point : anchor_;
    }
    POINT End(POINT point) {
        const auto result = active_ && !dragging_ ? anchor_ : point;
        active_ = false;
        return result;
    }
private:
    POINT anchor_{};
    Clock::time_point began_{};
    bool active_{};
    bool dragging_{};
};
} // namespace interfayce
