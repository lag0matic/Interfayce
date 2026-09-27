#pragma once
#include <array>
#include <openvr.h>

namespace interfayce {
// Retain ownership through button-up, including when aim leaves the surface or
// the toggle is turned off. Tracking loss or unavailable support releases it.
struct InputBlockOwnership {
    bool claimed{};
    bool Update(bool enabled, bool supported, bool tracked, bool hovering, bool held) {
        claimed = supported && tracked && ((enabled && hovering) || (claimed && held));
        return claimed;
    }
};

struct DesktopBlockButton {
    static constexpr float Left = 422, Top = 345, Right = 726, Bottom = 376;
    static constexpr bool Contains(float x, float y) {
        return x >= Left && x <= Right && y >= Top && y <= Bottom;
    }
};

class DesktopInputBlocking {
public:
    void Initialize();
    void Toggle();
    void Refresh();
    bool Enabled() const { return enabled_; }
    bool Available() const { return available_ && !inputFailed_; }
    vr::EVRInputError UpdateInput(vr::IVRSystem* system, vr::VRActionSetHandle_t actionSet,
        const std::array<bool, 2>& tracked, const std::array<bool, 2>& hovering,
        const std::array<bool, 2>& held);
    bool Claim(size_t hand, bool tracked, bool hovering, bool held) {
        return ownership_[hand].Update(enabled_, Available(), tracked, hovering, held);
    }
private:
    std::array<vr::VRInputValueHandle_t, 2> hands_{};
    bool enabled_{};
    bool available_{};
    bool inputFailed_{};
    std::array<InputBlockOwnership, 2> ownership_{};
};
} // namespace interfayce
