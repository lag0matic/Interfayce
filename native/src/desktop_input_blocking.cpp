#include "desktop_input_blocking.h"

namespace interfayce {
namespace {
constexpr const char* Section = "com.lag0matic.interfayce";
constexpr const char* Preference = "desktopBlockGameInput";
}
void DesktopInputBlocking::Initialize() {
    vr::EVRSettingsError error{};
    enabled_ = vr::VRSettings()->GetBool(Section, Preference, &error);
    if (error != vr::VRSettingsError_None) enabled_ = false;
    vr::VRInput()->GetInputSourceHandle("/user/hand/left", &hands_[0]);
    vr::VRInput()->GetInputSourceHandle("/user/hand/right", &hands_[1]);
    Refresh();
}
void DesktopInputBlocking::Refresh() {
    vr::EVRSettingsError error{};
    available_ = vr::VRSettings()->GetBool(vr::k_pch_SteamVR_Section,
        vr::k_pch_SteamVR_AllowGlobalActionSetPriority, &error);
    available_ = available_ && error == vr::VRSettingsError_None;
}
void DesktopInputBlocking::Toggle() {
    inputFailed_ = false;
    const bool wanted = !enabled_;
    vr::EVRSettingsError error{};
    vr::VRSettings()->SetBool(Section, Preference, wanted, &error);
    if (error != vr::VRSettingsError_None) return;
    enabled_ = wanted;
    if (wanted) {
        // This enables SteamVR's prerequisite. Turning our toggle off stops
        // requesting priority; it does not disable support for other overlays.
        vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section,
            vr::k_pch_SteamVR_AllowGlobalActionSetPriority, true, &error);
        if (error != vr::VRSettingsError_None) { available_ = false; return; }
    }
    Refresh();
}
vr::EVRInputError DesktopInputBlocking::UpdateInput(vr::IVRSystem* system,
        vr::VRActionSetHandle_t actionSet, const std::array<bool, 2>& tracked,
        const std::array<bool, 2>& hovering, const std::array<bool, 2>& held) {
    std::array<vr::VRActiveActionSet_t, 2> sets{};
    for (size_t hand = 0; hand < sets.size(); ++hand) {
        sets[hand].ulActionSet = actionSet;
        sets[hand].ulRestrictedToDevice = hands_[hand];
        bool physicallyHeld = false;
        if (enabled_ || ownership_[hand].claimed) {
            const auto device = system->GetTrackedDeviceIndexForControllerRole(hand == 0
                ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand);
            vr::VRControllerState_t state{};
            if (device != vr::k_unTrackedDeviceIndexInvalid
                && system->GetControllerState(device, &state, sizeof(state))) {
                const auto clickMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)
                    | vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad);
                physicallyHeld = (state.ulButtonPressed & clickMask) != 0;
            }
        }
        const bool claim = Claim(hand, tracked[hand] && hands_[hand] != vr::k_ulInvalidInputValueHandle,
            hovering[hand], held[hand] || physicallyHeld);
        sets[hand].nPriority = claim ? vr::k_nActionSetOverlayGlobalPriorityMin : 0;
    }
    // Preserve the original unrestricted input path whenever nothing is claimed.
    const bool claiming = sets[0].nPriority != 0 || sets[1].nPriority != 0;
    if (!claiming) sets[0].ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
    auto error = vr::VRInput()->UpdateActionState(sets.data(), sizeof(sets[0]), claiming ? 2 : 1);
    if (error != vr::VRInputError_None && claiming) {
        inputFailed_ = true;
        for (auto& owner : ownership_) owner.claimed = false;
        sets[0].ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
        sets[0].nPriority = 0;
        error = vr::VRInput()->UpdateActionState(sets.data(), sizeof(sets[0]), 1);
    }
    return error;
}
} // namespace interfayce
