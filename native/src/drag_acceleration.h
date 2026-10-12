#pragma once
#include <algorithm>
#include <cmath>
namespace interfayce {
class DragAcceleration {
public:
    void Reset() { speed_ = 0; }
    // Input is physical tracking-space displacement, never our translated origin.
    float Gain(float distance, float seconds, bool enabled, float maximum) {
        if (!std::isfinite(distance) || !std::isfinite(seconds) || seconds <= 0
            || seconds > 0.15F || distance < 0 || distance > 0.30F
            || distance / seconds > 5.0F) { Reset(); return 0; }
        if (!enabled) { Reset(); return 1; }
        const float instant = distance / seconds;
        // Smooth ramp-up, immediate precision when slowing/stopping.
        speed_ += (instant - speed_) * (1.0F - std::exp(-seconds / 0.06F));
        const float speed = (std::min)(instant, speed_);
        const float t = std::clamp((speed - 0.15F) / 0.85F, 0.0F, 1.0F);
        if (!std::isfinite(maximum)) maximum = 4;
        return 1 + (std::clamp(maximum, 1.0F, 6.0F) - 1) * t * t * (3 - 2 * t);
    }
private:
    float speed_{};
};
}
