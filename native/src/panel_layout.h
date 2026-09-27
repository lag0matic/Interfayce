#pragma once

namespace interfayce::panel {
inline constexpr unsigned Width = 768;
inline constexpr unsigned Height = 452;
inline constexpr float ContentOffset = 68;
inline constexpr float MetricsHeight = 28;
inline constexpr float HeaderBottom = 82;
inline constexpr float ContentTop = HeaderBottom + ContentOffset;
struct Bounds {
    float left, top, right, bottom;
    constexpr bool Contains(float x, float y) const {
        return x >= left && x <= right && y >= top && y <= bottom;
    }
};
inline constexpr Bounds MusicPlayer{42, 181, 242, 221};
inline constexpr Bounds MusicBroadcastSource{254, 181, 482, 221};
static_assert(MusicPlayer.Contains(140, 200));
static_assert(!MusicPlayer.Contains(248, 200));
static_assert(MusicBroadcastSource.Contains(370, 200));
static_assert(!MusicBroadcastSource.Contains(520, 225));
// The status row is deliberately outside every existing content hit target.
constexpr float ContentY(float y) {
    return y >= ContentTop ? y - ContentOffset : y > HeaderBottom ? -1000.0F : y;
}
static_assert(ContentY(49) == 49);
static_assert(ContentY(102) == -1000);
static_assert(ContentY(355) == 287);
}
