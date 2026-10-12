#pragma once
#include <algorithm>
#include <cmath>
namespace interfayce {
class DragAcceleration {
public:
    void Reset() { speed_ = 0; pendingSeconds_ = 0; }
    float Gain(float distance, float seconds, bool enabled, float maximum) {
        if (!enabled) { Reset(); return 1; } // Normal is always unmodified.
        if (!std::isfinite(distance) || !std::isfinite(seconds) || seconds <= 0
            || distance < 0 || distance > 0.30F) { Reset(); return 0; }
        if (seconds > 0.15F) { Reset(); return 1; } // Keep motion; don't boost a stall.
        pendingSeconds_ += seconds;
        // SteamVR may return the same pose across multiple overlay iterations.
        // Account for their elapsed time when a fresh pose arrives, rather than
        // treating the entire movement as occurring in the final short iteration.
        if (distance == 0) return 1;
        const float elapsed = pendingSeconds_;
        pendingSeconds_ = 0;
        const float instant = distance / elapsed;
        speed_ += (instant - speed_) * (1.0F - std::exp(-elapsed / 0.06F));
        const float speed = (std::min)(instant, speed_);
        const float t = std::clamp((speed - 0.15F) / 0.85F, 0.0F, 1.0F);
        if (!std::isfinite(maximum)) maximum = 4;
        return 1 + (std::clamp(maximum, 1.0F, 6.0F) - 1) * t * t * (3 - 2 * t);
    }
private:
    float speed_{};
    float pendingSeconds_{};
};
}
