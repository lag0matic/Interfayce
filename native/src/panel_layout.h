#pragma once

namespace interfayce::panel {
inline constexpr unsigned Width = 768;
inline constexpr unsigned Height = 424;
inline constexpr float ContentOffset = 40;
inline constexpr float HeaderBottom = 82;
inline constexpr float ContentTop = HeaderBottom + ContentOffset;
// The status row is deliberately outside every existing content hit target.
constexpr float ContentY(float y) {
    return y >= ContentTop ? y - ContentOffset : y > HeaderBottom ? -1000.0F : y;
}
static_assert(ContentY(49) == 49);
static_assert(ContentY(102) == -1000);
static_assert(ContentY(327) == 287);
}
