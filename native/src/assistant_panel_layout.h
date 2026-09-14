#pragma once
#include <cstddef>

namespace interfayce::assistant_panel {
struct Bounds {
    float left, top, right, bottom;
    constexpr bool Contains(float x, float y) const {
        return x >= left && x <= right && y >= top && y <= bottom;
    }
};
constexpr Bounds Choice(std::size_t index) {
    const float left = 145.0F + static_cast<float>(index) * 164.0F;
    return {left, 261, left + 150, 313};
}
}
