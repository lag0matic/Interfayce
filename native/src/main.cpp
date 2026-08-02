#include <Windows.h>
#include <openvr.h>

#include "overlay_renderer.h"
#include "desktop_surface_manager.h"
#include "desktop_surface_registry.h"

#include <chrono>
#include <cstdio>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr char kAppKey[] = "com.lag0matic.interfayce";
constexpr char kActionSetPath[] = "/actions/interfayce";
constexpr char kLeftDragActionPath[] = "/actions/interfayce/in/left_drag";
constexpr char kRightDragActionPath[] = "/actions/interfayce/in/right_drag";
constexpr char kRightUiClickActionPath[] = "/actions/interfayce/in/right_ui_click";
constexpr char kWristOverlayKey[] = "com.lag0matic.interfayce.wrist.panel";
constexpr char kCursorOverlayKey[] = "com.lag0matic.interfayce.wrist.cursor";
constexpr wchar_t kShutdownEventName[] = L"Local\\InterfayceOverlayShutdown";

enum class DragHand { None, Left, Right };

struct Vector3 {
    float x{};
    float y{};
    float z{};
};

std::filesystem::path ExecutableDirectory(char* executablePath) {
    return std::filesystem::absolute(executablePath).parent_path();
}

void LaunchSpotifyControl(const std::filesystem::path& projectRoot, const wchar_t* operation) {
    const auto sourceDirectory = (projectRoot / "src").wstring();
    std::wstring command = L"cmd.exe /d /s /c \"set \"PYTHONPATH=" + sourceDirectory
        + L"\" && python -m interfayce spotify-control " + operation + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, projectRoot.wstring().c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void RefreshSpotifyArt(const std::filesystem::path& projectRoot, const std::filesystem::path& outputPath) {
    const auto sourceDirectory = (projectRoot / "src").wstring();
    std::wstring command = L"cmd.exe /d /s /c \"set \"PYTHONPATH=" + sourceDirectory
        + L"\" && python -m interfayce spotify-art --output \"" + outputPath.wstring() + L"\"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, projectRoot.wstring().c_str(), &startup, &process)) {
        WaitForSingleObject(process.hProcess, 3000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

std::wstring ReadSpotifyNowPlaying(const std::filesystem::path& projectRoot) {
    const auto sourceDirectory = (projectRoot / "src").string();
    const std::string command = "cmd.exe /d /s /c \"set \"PYTHONPATH=" + sourceDirectory
        + "\" && python -m interfayce spotify-current\"";
    std::wstring result;
    if (FILE* pipe = _popen(command.c_str(), "r")) {
        char buffer[1024]{};
        if (std::fgets(buffer, sizeof(buffer), pipe)) {
            std::string line(buffer);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            const auto separator = line.find('\t');
            if (separator != std::string::npos) line.replace(separator, 1, " - ");
            result.assign(line.begin(), line.end());
        }
        _pclose(pipe);
    }
    return result;
}

std::wstring ReadControllerBatteryLine(vr::IVRSystem* system) {
    const auto batteryText = [&](vr::ETrackedControllerRole role, const wchar_t* label) {
        const auto index = system->GetTrackedDeviceIndexForControllerRole(role);
        if (index == vr::k_unTrackedDeviceIndexInvalid || !system->IsTrackedDeviceConnected(index)) {
            return std::wstring(label) + L" --";
        }
        const auto percent = static_cast<int>(std::lround(
            system->GetFloatTrackedDeviceProperty(index, vr::Prop_DeviceBatteryPercentage_Float) * 100.0F));
        return std::wstring(label) + L" " + std::to_wstring(percent) + L"%";
    };
    return batteryText(vr::TrackedControllerRole_LeftHand, L"L") + L"    "
        + batteryText(vr::TrackedControllerRole_RightHand, L"R");
}

std::wstring DesktopSurfaceLine(size_t count) {
    if (count == 0) return L"No open surfaces";
    return std::to_wstring(count) + (count == 1 ? L" open surface" : L" open surfaces");
}

struct SlimeRigStatus {
    std::array<std::wstring, 8> slots{};
    bool mountReady{};
};

SlimeRigStatus ReadSlimeTrackerBatteries(const std::filesystem::path& projectRoot) {
    SlimeRigStatus status;
    const std::string command = "node \"" + (projectRoot / "tools" / "slimevr_probe.cjs").string()
        + "\" --summary";
    if (FILE* pipe = _popen(command.c_str(), "r")) {
        char buffer[512]{};
        if (std::fgets(buffer, sizeof(buffer), pipe)) {
            std::string line(buffer);
            size_t start = 0;
            for (size_t index = 0; index < status.slots.size() && start <= line.size(); ++index) {
                const auto end = line.find('\t', start);
                const auto value = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
                status.slots[index].assign(value.begin(), value.end());
                while (!status.slots[index].empty() && (status.slots[index].back() == L'\n' || status.slots[index].back() == L'\r')) {
                    status.slots[index].pop_back();
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
            if (start <= line.size()) status.mountReady = line.substr(start).starts_with("MOUNT_OK");
        }
        _pclose(pipe);
    }
    return status;
}

void LaunchSlimeReset(const std::filesystem::path& projectRoot, const wchar_t* kind) {
    std::wstring command = L"node \"" + (projectRoot / "tools" / "slimevr_reset.cjs").wstring()
        + L"\" " + kind;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, projectRoot.wstring().c_str(), &startup, &process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
}

std::optional<Vector3> ReadControllerPosition(vr::IVRSystem* system, DragHand hand) {
    const auto role = hand == DragHand::Left ? vr::TrackedControllerRole_LeftHand
                                             : vr::TrackedControllerRole_RightHand;
    const auto index = system->GetTrackedDeviceIndexForControllerRole(role);
    if (index == vr::k_unTrackedDeviceIndexInvalid) {
        return std::nullopt;
    }
    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding, 0.0F, poses.data(), static_cast<uint32_t>(poses.size()));
    const auto& pose = poses[index];
    if (!pose.bDeviceIsConnected || !pose.bPoseIsValid
        || pose.eTrackingResult != vr::TrackingResult_Running_OK) {
        return std::nullopt;
    }
    const auto& matrix = pose.mDeviceToAbsoluteTracking;
    return Vector3{matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
}

std::optional<vr::HmdMatrix34_t> ReadControllerPose(vr::IVRSystem* system, DragHand hand) {
    const auto role = hand == DragHand::Left ? vr::TrackedControllerRole_LeftHand
                                             : vr::TrackedControllerRole_RightHand;
    const auto index = system->GetTrackedDeviceIndexForControllerRole(role);
    if (index == vr::k_unTrackedDeviceIndexInvalid) {
        return std::nullopt;
    }
    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding, 0.0F, poses.data(), static_cast<uint32_t>(poses.size()));
    const auto& pose = poses[index];
    if (!pose.bDeviceIsConnected || !pose.bPoseIsValid
        || pose.eTrackingResult != vr::TrackingResult_Running_OK) {
        return std::nullopt;
    }
    return pose.mDeviceToAbsoluteTracking;
}

vr::HmdMatrix34_t TranslateOrigin(const vr::HmdMatrix34_t& baseline, const Vector3 offset) {
    auto translated = baseline;
    for (int row = 0; row < 3; ++row) {
        translated.m[row][3] += baseline.m[row][0] * offset.x
            + baseline.m[row][1] * offset.y + baseline.m[row][2] * offset.z;
    }
    return translated;
}

// The panel is authored in its own flat XY plane. This rotates that plane
// into the inner-wrist "watch check" orientation relative to a left Index
// controller, then offsets it above the wrist rather than the back hand.
vr::HmdMatrix34_t InnerLeftWristTransform() {
    constexpr float kDiagonal = 0.70710678F;
    vr::HmdMatrix34_t transform{};
    transform.m[1][0] = -kDiagonal;
    transform.m[2][0] = kDiagonal;
    transform.m[1][1] = -kDiagonal;
    transform.m[2][1] = -kDiagonal;
    transform.m[0][2] = 1.0F;
    transform.m[0][3] = 0.005F;
    transform.m[1][3] = 0.071F;
    transform.m[2][3] = 0.189F;
    return transform;
}

vr::HmdMatrix34_t MultiplyTransforms(const vr::HmdMatrix34_t& left, const vr::HmdMatrix34_t& right) {
    vr::HmdMatrix34_t result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row][column] = left.m[row][0] * right.m[0][column]
                + left.m[row][1] * right.m[1][column] + left.m[row][2] * right.m[2][column];
        }
        result.m[row][3] = left.m[row][0] * right.m[0][3]
            + left.m[row][1] * right.m[1][3] + left.m[row][2] * right.m[2][3] + left.m[row][3];
    }
    return result;
}

struct PointerRay {
    Vector3 source;
    Vector3 direction;
};

std::optional<PointerRay> ReadRightPointerRay(vr::IVRSystem* system) {
    const auto device = system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
    const auto devicePose = ReadControllerPose(system, DragHand::Right);
    if (device == vr::k_unTrackedDeviceIndexInvalid || !devicePose) {
        return std::nullopt;
    }
    char modelName[256]{};
    system->GetStringTrackedDeviceProperty(
        device, vr::Prop_RenderModelName_String, modelName, sizeof(modelName));
    vr::VRInputValueHandle_t devicePath = vr::k_ulInvalidInputValueHandle;
    vr::VRInput()->GetInputSourceHandle("/user/hand/right", &devicePath);
    vr::RenderModel_ControllerMode_State_t controllerMode{};
    vr::RenderModel_ComponentState_t tipState{};
    if (modelName[0] != '\0' && devicePath != vr::k_ulInvalidInputValueHandle
        && vr::VRRenderModels()->GetComponentStateForDevicePath(
            modelName, "tip", devicePath, &controllerMode, &tipState)) {
        const auto tipPose = MultiplyTransforms(*devicePose, tipState.mTrackingToComponentLocal);
        return PointerRay{
            {tipPose.m[0][3], tipPose.m[1][3], tipPose.m[2][3]},
            {-tipPose.m[0][2], -tipPose.m[1][2], -tipPose.m[2][2]},
        };
    }
    constexpr float kFallbackOffsetMeters = 0.060F;
    return PointerRay{
        {devicePose->m[0][3] + devicePose->m[0][1] * kFallbackOffsetMeters,
         devicePose->m[1][3] + devicePose->m[1][1] * kFallbackOffsetMeters,
         devicePose->m[2][3] + devicePose->m[2][1] * kFallbackOffsetMeters},
        {devicePose->m[0][1], devicePose->m[1][1], devicePose->m[2][1]},
    };
}

vr::HmdMatrix34_t HeadsetCalibrationTransform() {
    vr::HmdMatrix34_t transform{};
    transform.m[0][0] = 1.0F;
    transform.m[1][1] = 1.0F;
    transform.m[2][2] = 1.0F;
    transform.m[2][3] = -0.65F;
    return transform;
}

void DestroyStaleOverlay(const char* key) {
    vr::VROverlayHandle_t staleOverlay = vr::k_ulOverlayHandleInvalid;
    if (vr::VROverlay()->FindOverlay(key, &staleOverlay) == vr::VROverlayError_None) {
        vr::VROverlay()->DestroyOverlay(staleOverlay);
    }
}
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool requestShutdown = argc > 1 && std::string_view(argv[1]) == "--shutdown";
    if (requestShutdown) {
        const HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kShutdownEventName);
        if (event == nullptr) {
            std::cerr << "No running Interfayce host accepted the shutdown request.\n";
            CoUninitialize();
            return 1;
        }
        SetEvent(event);
        CloseHandle(event);
        CoUninitialize();
        return 0;
    }
    const bool desktopSourcesProbe = argc > 1 && std::string_view(argv[1]) == "--desktop-sources";
    if (desktopSourcesProbe) {
        interfayce::DesktopSurfaceManager manager;
        for (const auto& source : manager.EnumerateSources()) {
            std::wcout << (source.kind == interfayce::DesktopSource::Kind::Display
                    ? L"DISPLAY\t" : L"APPLICATION\t")
                       << source.label << L'\t' << source.detail << L'\n';
        }
        CoUninitialize();
        return 0;
    }
    const bool desktopCaptureProbe = argc > 1 && std::string_view(argv[1]) == "--desktop-capture-probe";
    if (desktopCaptureProbe) {
        Microsoft::WRL::ComPtr<ID3D11Device> captureDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> captureContext;
        D3D_FEATURE_LEVEL featureLevel{};
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &captureDevice, &featureLevel, &captureContext))) {
            std::cerr << "Desktop capture probe could not create a D3D11 device.\n";
            CoUninitialize();
            return 1;
        }
        interfayce::DesktopSurfaceManager manager;
        const auto displays = manager.EnumerateDisplays();
        interfayce::DesktopCapture capture;
        if (displays.empty() || !capture.Start(captureDevice.Get(), displays.front())) {
            std::cerr << "Desktop capture probe could not start Windows Graphics Capture.\n";
            CoUninitialize();
            return 1;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool receivedFrame = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = capture.Update();
            if (result == interfayce::DesktopCapture::UpdateResult::FrameCopied) {
                receivedFrame = true;
                break;
            }
            if (result == interfayce::DesktopCapture::UpdateResult::Closed
                || result == interfayce::DesktopCapture::UpdateResult::Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        capture.Stop();
        std::cout << "Desktop capture probe " << (receivedFrame ? "received a GPU frame" : "timed out") << ".\n";
        CoUninitialize();
        return receivedFrame ? 0 : 1;
    }
    const bool probeOnly = argc > 1 && std::string_view(argv[1]) == "--probe";
    const bool overlayProbe = argc > 1 && std::string_view(argv[1]) == "--overlay-probe";
    const bool inputCapture = argc > 1 && std::string_view(argv[1]) == "--input-capture";
    const bool dragPreview = argc > 1 && std::string_view(argv[1]) == "--drag-preview";
    const bool temporaryDrag = argc > 1 && std::string_view(argv[1]) == "--temporary-drag";
    const bool sessionDrag = argc == 1
        || (argc > 1 && std::string_view(argv[1]) == "--session-drag");
    const bool showBindings = argc > 1 && std::string_view(argv[1]) == "--show-bindings";
    const bool rawPanel = argc > 1 && std::string_view(argv[1]) == "--raw-panel";
    const bool headsetPanel = rawPanel
        || (argc > 1 && std::string_view(argv[1]) == "--headset-panel");
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&initError, vr::VRApplication_Overlay);
    if (initError != vr::VRInitError_None || system == nullptr) {
        std::cerr << "SteamVR overlay initialization failed: "
                  << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << '\n';
        return 1;
    }

    const auto directory = ExecutableDirectory(argv[0]);
    const auto projectRoot = directory.parent_path().parent_path().parent_path();
    const auto musicArtPath = projectRoot / "native" / "build" / "spotify-art.jpg";
    const auto actionManifest = directory / "assets" / "steamvr" / "actions.json";
    vr::VRApplications()->IdentifyApplication(GetCurrentProcessId(), kAppKey);
    const auto actionError = vr::VRInput()->SetActionManifestPath(actionManifest.string().c_str());
    if (actionError != vr::VRInputError_None) {
        std::cerr << "Could not load Interfayce actions: " << static_cast<int>(actionError) << '\n';
    }

    vr::VRActionSetHandle_t actionSet = vr::k_ulInvalidActionSetHandle;
    const auto actionSetError = vr::VRInput()->GetActionSetHandle(kActionSetPath, &actionSet);
    if (actionSetError != vr::VRInputError_None) {
        std::cerr << "Could not acquire Interfayce action set: "
                  << static_cast<int>(actionSetError) << '\n';
    }

    vr::VRActionHandle_t leftDragAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t rightDragAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t rightUiClickAction = vr::k_ulInvalidActionHandle;
    const auto leftHandleError = vr::VRInput()->GetActionHandle(kLeftDragActionPath, &leftDragAction);
    const auto rightHandleError = vr::VRInput()->GetActionHandle(kRightDragActionPath, &rightDragAction);
    const auto rightUiClickError = vr::VRInput()->GetActionHandle(
        kRightUiClickActionPath, &rightUiClickAction);

    interfayce::OverlayRenderer renderer;
    if (!rawPanel && !renderer.Initialize(system)) {
        std::cerr << "Could not initialize the Interfayce D3D11 panel texture.\n";
        vr::VR_Shutdown();
        return 1;
    }

    if (probeOnly) {
        std::cout << "Interfayce native overlay probe completed.\n";
        vr::VR_Shutdown();
        return actionError == vr::VRInputError_None && actionSetError == vr::VRInputError_None
                && leftHandleError == vr::VRInputError_None && rightHandleError == vr::VRInputError_None
                && rightUiClickError == vr::VRInputError_None
            ? 0
            : 1;
    }

    // A forced process stop can leave an overlay registered until SteamVR's
    // cleanup cycle. Reclaim only Interfayce's own keys before recreating it.
    DestroyStaleOverlay(kWristOverlayKey);
    DestroyStaleOverlay(kCursorOverlayKey);

    vr::VROverlayHandle_t wristOverlay = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t cursorOverlay = vr::k_ulOverlayHandleInvalid;
    const auto panelTexture = renderer.Texture();

    const auto wristError = vr::VROverlay()->CreateOverlay(
        kWristOverlayKey, "Interfayce Wrist", &wristOverlay);
    if (wristError != vr::VROverlayError_None) {
        std::cerr << "Could not create Interfayce wrist overlay: "
                  << static_cast<int>(wristError) << '\n';
        vr::VR_Shutdown();
        return 1;
    }
    vr::VROverlay()->SetOverlayWidthInMeters(
        wristOverlay, rawPanel ? 0.80F : headsetPanel ? 0.55F : 0.205F);
    // SteamVR's built-in overlay mouse only exists in the dashboard. Interfayce
    // uses its own action-driven controller ray so the panel works in-world.
    vr::VROverlay()->SetOverlayInputMethod(wristOverlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(wristOverlay, 10);
    if (vr::VROverlay()->CreateOverlay(kCursorOverlayKey, "Interfayce wrist cursor", &cursorOverlay)
        != vr::VROverlayError_None) {
        std::cerr << "Could not create Interfayce wrist cursor overlay.\n";
        vr::VROverlay()->DestroyOverlay(wristOverlay);
        vr::VR_Shutdown();
        return 1;
    }
    vr::VROverlay()->SetOverlayWidthInMeters(cursorOverlay, 0.007F);
    vr::VROverlay()->SetOverlayInputMethod(cursorOverlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(cursorOverlay, 11);
    std::vector<uint8_t> cursorPixels(32U * 32U * 4U);
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const auto pixel = (y * 32U + x) * 4U;
            const auto dx = static_cast<int>(x) - 16;
            const auto dy = static_cast<int>(y) - 16;
            const bool inside = dx * dx + dy * dy <= 180;
            cursorPixels[pixel + 0] = 40;
            cursorPixels[pixel + 1] = 230;
            cursorPixels[pixel + 2] = 255;
            cursorPixels[pixel + 3] = inside ? 255 : 0;
        }
    }
    vr::VROverlay()->SetOverlayRaw(cursorOverlay, cursorPixels.data(), 32, 32, 4);
    std::vector<uint8_t> rawVisibilityCard;
    if (rawPanel) {
        constexpr uint32_t kCardSize = 512;
        rawVisibilityCard.resize(kCardSize * kCardSize * 4U);
        for (uint32_t y = 0; y < kCardSize; ++y) {
            for (uint32_t x = 0; x < kCardSize; ++x) {
                const auto pixel = (y * kCardSize + x) * 4U;
                const bool cross = std::abs(static_cast<int>(x) - static_cast<int>(kCardSize / 2)) < 18
                    || std::abs(static_cast<int>(y) - static_cast<int>(kCardSize / 2)) < 18;
                rawVisibilityCard[pixel + 0] = cross ? 255 : 235;
                rawVisibilityCard[pixel + 1] = cross ? 255 : 0;
                rawVisibilityCard[pixel + 2] = cross ? 255 : 0;
                rawVisibilityCard[pixel + 3] = 255;
            }
        }
        const auto rawError = vr::VROverlay()->SetOverlayRaw(
            wristOverlay, rawVisibilityCard.data(), kCardSize, kCardSize, 4);
        if (rawError != vr::VROverlayError_None) {
            std::cerr << "Raw visibility card upload failed: "
                      << vr::VROverlay()->GetOverlayErrorNameFromEnum(rawError) << '\n';
        }
    } else {
        const auto wristTextureError = vr::VROverlay()->SetOverlayTexture(wristOverlay, &panelTexture);
        if (wristTextureError != vr::VROverlayError_None) {
            std::cerr << "Wrist texture upload failed: "
                      << vr::VROverlay()->GetOverlayErrorNameFromEnum(wristTextureError) << '\n';
        }
    }
    const auto wristTransform = headsetPanel ? HeadsetCalibrationTransform() : InnerLeftWristTransform();
    bool wristAttached = false;
    const auto attachWristOverlay = [&]() {
        const auto device = headsetPanel ? vr::k_unTrackedDeviceIndex_Hmd
            : system->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
        if (device == vr::k_unTrackedDeviceIndexInvalid) {
            return false;
        }
        const auto attachError = vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
            wristOverlay, device, &wristTransform);
        if (attachError != vr::VROverlayError_None) {
            std::cerr << "Could not attach Interfayce wrist panel to the left controller: "
                      << static_cast<int>(attachError) << '\n';
            return false;
        }
        const auto showError = vr::VROverlay()->ShowOverlay(wristOverlay);
        if (showError != vr::VROverlayError_None) {
            std::cerr << "Wrist show failed: "
                      << vr::VROverlay()->GetOverlayErrorNameFromEnum(showError) << '\n';
            return false;
        }
        std::cout << "Interfayce wrist panel attached to "
                  << (headsetPanel ? "headset calibration view" : "left controller") << ".\n";
        return true;
    };
    wristAttached = attachWristOverlay();

    if (showBindings) {
        vr::VRActiveActionSet_t bindingSet{};
        bindingSet.ulActionSet = actionSet;
        const auto bindingError = vr::VRInput()->ShowBindingsForActionSet(
            &bindingSet, sizeof(bindingSet), 1, vr::k_ulInvalidInputValueHandle);
        if (bindingError != vr::VRInputError_None) {
            std::cerr << "Could not show Interfayce bindings: " << static_cast<int>(bindingError) << '\n';
        }
    }

    if (overlayProbe) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        vr::VROverlay()->DestroyOverlay(wristOverlay);
        vr::VROverlay()->DestroyOverlay(cursorOverlay);
        std::cout << "Interfayce tracked-overlay probe completed.\n";
        vr::VR_Shutdown();
        return 0;
    }

    interfayce::DesktopSurfaceManager desktopSourceManager;
    interfayce::DesktopSurfaceRegistry desktopSurfaces;
    if (!rawPanel && !desktopSurfaces.Initialize(system, renderer.Device())) {
        std::cerr << "Could not initialize desktop surface registry.\n";
    }

    std::cout << "Interfayce overlay host is running.\n";
    const HANDLE shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, kShutdownEventName);
    bool running = true;
    const bool boundedCapture = inputCapture || dragPreview || temporaryDrag
        || (argc > 1 && std::string_view(argv[1]) == "--session-drag");
    const auto captureDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds((temporaryDrag || sessionDrag) ? 120 : 30);
    bool printedInputDiagnostics = false;
    DragHand activeDragHand = DragHand::None;
    std::optional<Vector3> lastDragPosition;
    Vector3 previewOffset{};
    std::optional<vr::HmdMatrix34_t> temporaryBaseline;
    std::optional<vr::HmdMatrix34_t> sessionBaseline;
    bool dragFaulted = false;
    int selectedDeck = 2;
    std::wstring musicLine;
    std::wstring desktopLine = DesktopSurfaceLine(0);
    interfayce::DesktopPanelState desktopPanel;
    std::wstring rigLine;
    std::array<std::wstring, 8> rigSlots;
    bool mountReady = false;
    auto nextMusicPoll = std::chrono::steady_clock::now();
    auto nextRigPoll = std::chrono::steady_clock::now();
    bool restoreHoldActive = false;
    bool rigResetHoldActive = false;
    const wchar_t* rigResetKind = nullptr;
    auto restoreHoldStarted = std::chrono::steady_clock::now();
    std::optional<interfayce::DesktopSurfaceHit> activeDesktopPointer;
    auto* chaperone = vr::VRChaperoneSetup();
    const auto restoreBaseline = [&](std::optional<vr::HmdMatrix34_t>& baseline, const char* reason) {
        if (baseline) {
            chaperone->SetWorkingStandingZeroPoseToRawTrackingPose(&*baseline);
            chaperone->HideWorkingSetPreview();
            std::cout << reason << " restored the session baseline\n";
            baseline.reset();
        }
    };
    auto lastPreviewLog = std::chrono::steady_clock::now();
    while (running && (!boundedCapture || std::chrono::steady_clock::now() < captureDeadline)) {
        if (shutdownEvent != nullptr && WaitForSingleObject(shutdownEvent, 0) == WAIT_OBJECT_0) {
            running = false;
            continue;
        }
        if (!wristAttached) {
            wristAttached = attachWristOverlay();
        }
        vr::VRActiveActionSet_t activeSet{};
        activeSet.ulActionSet = actionSet;
        activeSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
        activeSet.nPriority = 0;
        const auto updateError = vr::VRInput()->UpdateActionState(&activeSet, sizeof(activeSet), 1);

        vr::InputDigitalActionData_t leftDrag{};
        vr::InputDigitalActionData_t rightDrag{};
        vr::InputDigitalActionData_t rightUiClick{};
        const auto leftDataError = vr::VRInput()->GetDigitalActionData(
            leftDragAction, &leftDrag, sizeof(leftDrag), vr::k_ulInvalidInputValueHandle);
        const auto rightDataError = vr::VRInput()->GetDigitalActionData(
            rightDragAction, &rightDrag, sizeof(rightDrag), vr::k_ulInvalidInputValueHandle);
        vr::VRInput()->GetDigitalActionData(
            rightUiClickAction, &rightUiClick, sizeof(rightUiClick), vr::k_ulInvalidInputValueHandle);
        if (!printedInputDiagnostics) {
            std::array<vr::VRInputValueHandle_t, 16> leftOrigins{};
            std::array<vr::VRInputValueHandle_t, 16> rightOrigins{};
            const auto leftOriginError = vr::VRInput()->GetActionOrigins(
                actionSet, leftDragAction, leftOrigins.data(), static_cast<uint32_t>(leftOrigins.size()));
            const auto rightOriginError = vr::VRInput()->GetActionOrigins(
                actionSet, rightDragAction, rightOrigins.data(), static_cast<uint32_t>(rightOrigins.size()));
            std::cout << "drag action status: left_active=" << leftDrag.bActive
                      << " right_active=" << rightDrag.bActive
                      << " update=" << static_cast<int>(updateError)
                      << " left_data=" << static_cast<int>(leftDataError)
                      << " right_data=" << static_cast<int>(rightDataError)
                      << " left_origins=" << static_cast<int>(leftOriginError)
                      << " right_origins=" << static_cast<int>(rightOriginError)
                      << " left_origin_0=" << leftOrigins[0]
                      << " right_origin_0=" << rightOrigins[0] << '\n';
            printedInputDiagnostics = true;
        }
        if (leftDrag.bChanged || rightDrag.bChanged) {
            std::cout << "drag actions: left=" << leftDrag.bState
                      << " right=" << rightDrag.bState << '\n';
        }

        vr::VROverlayIntersectionResults_t panelHit{};
        bool restoreButtonHit = false;
        bool rigFullResetHit = false;
        bool rigMountResetHit = false;
        bool desktopNewSurfaceHit = false;
        bool desktopSurfaceListHit = false;
        bool desktopListBackHit = false;
        std::optional<size_t> desktopBringIndex;
        std::optional<size_t> desktopCloseIndex;
        std::optional<interfayce::DesktopSurfaceHit> desktopSurfaceHit;
        bool panelHitFound = false;
        float panelX = 0.0F;
        float panelY = 0.0F;
        if (const auto pointerRay = ReadRightPointerRay(system)) {
            vr::VROverlayIntersectionParams_t ray{};
            ray.eOrigin = vr::TrackingUniverseStanding;
            ray.vSource = {{pointerRay->source.x, pointerRay->source.y, pointerRay->source.z}};
            ray.vDirection = {{pointerRay->direction.x, pointerRay->direction.y, pointerRay->direction.z}};
            if (vr::VROverlay()->ComputeOverlayIntersection(wristOverlay, &ray, &panelHit)) {
                panelHitFound = true;
                const auto x = panelHit.vUVs.v[0] * 768.0F;
                const auto y = (1.0F - panelHit.vUVs.v[1]) * 384.0F;
                panelX = x;
                panelY = y;
                restoreButtonHit = x >= 42.0F && x <= 420.0F && y >= 276.0F && y <= 338.0F;
                rigFullResetHit = selectedDeck == 3 && x >= 42.0F && x <= 350.0F && y >= 320.0F && y <= 366.0F;
                rigMountResetHit = selectedDeck == 3 && x >= 414.0F && x <= 722.0F && y >= 320.0F && y <= 366.0F;
                if (selectedDeck == 1 && desktopPanel.showSurfaceList) {
                    desktopListBackHit = x >= 36.0F && x <= 92.0F && y >= 102.0F && y <= 154.0F;
                    if (y >= 166.0F && y < 352.0F) {
                        const auto row = static_cast<size_t>((y - 166.0F) / 62.0F);
                        if (row < desktopPanel.surfaces.size() && row < 3) {
                            if (x >= 530.0F && x <= 616.0F) desktopBringIndex = row;
                            if (x >= 630.0F && x <= 712.0F) desktopCloseIndex = row;
                        }
                    }
                } else {
                    desktopNewSurfaceHit = selectedDeck == 1 && x >= 120.0F && x <= 280.0F && y >= 252.0F && y <= 338.0F;
                    desktopSurfaceListHit = selectedDeck == 1 && x >= 488.0F && x <= 648.0F && y >= 252.0F && y <= 338.0F;
                }
            }
            if (!panelHitFound) desktopSurfaceHit = desktopSurfaces.HitTest(ray);
        }
        desktopSurfaces.SetHoveredHit(desktopSurfaceHit);
        if (panelHitFound) {
            vr::HmdMatrix34_t localCursor{};
            localCursor.m[0][0] = 1.0F;
            localCursor.m[1][1] = 1.0F;
            localCursor.m[2][2] = 1.0F;
            localCursor.m[0][3] = (panelHit.vUVs.v[0] - 0.5F) * 0.205F;
            localCursor.m[1][3] = (panelHit.vUVs.v[1] - 0.5F) * 0.1025F;
            localCursor.m[2][3] = 0.006F;
            const auto cursorTransform = MultiplyTransforms(wristTransform, localCursor);
            const auto leftController = system->GetTrackedDeviceIndexForControllerRole(
                vr::TrackedControllerRole_LeftHand);
            if (leftController != vr::k_unTrackedDeviceIndexInvalid) {
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
                    cursorOverlay, leftController, &cursorTransform);
                const bool actionable = restoreButtonHit || rigFullResetHit || rigMountResetHit
                    || desktopNewSurfaceHit || desktopSurfaceListHit
                    || desktopListBackHit || desktopBringIndex.has_value() || desktopCloseIndex.has_value();
                vr::VROverlay()->SetOverlayColor(cursorOverlay,
                    actionable ? 0.20F : 0.02F, actionable ? 1.0F : 0.85F, 1.0F);
                vr::VROverlay()->ShowOverlay(cursorOverlay);
            }
        } else {
            vr::VROverlay()->HideOverlay(cursorOverlay);
        }
        if (rightUiClick.bChanged && rightUiClick.bState && desktopSurfaceHit && !panelHitFound) {
            if (desktopSurfaceHit->captured) {
                if (desktopSurfaces.SendPointerEvent(*desktopSurfaceHit,
                        interfayce::DesktopPointerEvent::PrimaryDown)) {
                    activeDesktopPointer = desktopSurfaceHit;
                }
            } else if (desktopSurfaces.ActivateHit(*desktopSurfaceHit)) {
                if (desktopSurfaceHit->sourceIndex) {
                    desktopPanel.surfaces = desktopSurfaces.Summaries();
                    std::cout << "Desktop source assigned to surface " << desktopSurfaceHit->id << '\n';
                }
            } else if (desktopSurfaceHit->sourceIndex || desktopSurfaceHit->pageDelta != 0) {
                std::cerr << "Could not start selected desktop capture\n";
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && panelHitFound && panelY <= 82.0F) {
            const int requestedDeck = panelX < 155.0F ? 0 : panelX < 315.0F ? 1 : panelX < 520.0F ? 2 : 3;
            if (requestedDeck == 0) {
                musicLine = ReadSpotifyNowPlaying(projectRoot);
                RefreshSpotifyArt(projectRoot, musicArtPath);
            }
            if (requestedDeck == 1) {
                desktopPanel.showSurfaceList = false;
                desktopPanel.surfaces = desktopSurfaces.Summaries();
                desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
            }
            if (requestedDeck == 3) {
                rigLine = ReadControllerBatteryLine(system);
                const auto slimeStatus = ReadSlimeTrackerBatteries(projectRoot);
                rigSlots = slimeStatus.slots;
                mountReady = slimeStatus.mountReady;
            }
            if (requestedDeck != selectedDeck && renderer.Initialize(
                    system, requestedDeck, requestedDeck == 1 ? desktopLine : musicLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                selectedDeck = requestedDeck;
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && selectedDeck == 0
                   && panelY >= 272.0F && panelY <= 338.0F) {
            if (panelX >= 70.0F && panelX <= 210.0F) {
                LaunchSpotifyControl(projectRoot, L"previous");
            } else if (panelX >= 314.0F && panelX <= 454.0F) {
                LaunchSpotifyControl(projectRoot, L"toggle");
            } else if (panelX >= 558.0F && panelX <= 698.0F) {
                LaunchSpotifyControl(projectRoot, L"next");
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && desktopNewSurfaceHit) {
            const auto sources = desktopSourceManager.EnumerateSources();
            const auto surfaceId = desktopSurfaces.SpawnPicker(sources);
            if (surfaceId != 0) {
                desktopPanel.surfaces = desktopSurfaces.Summaries();
                desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
                std::cout << "Desktop picker surface " << surfaceId << " spawned with "
                          << sources.size() << " eligible sources\n";
                if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            } else {
                std::cerr << "Could not spawn desktop picker surface\n";
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && desktopSurfaceListHit) {
            desktopPanel.showSurfaceList = true;
            desktopPanel.surfaces = desktopSurfaces.Summaries();
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && desktopListBackHit) {
            desktopPanel.showSurfaceList = false;
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && desktopBringIndex) {
            desktopSurfaces.BringToMe(desktopPanel.surfaces[*desktopBringIndex].id);
        } else if (rightUiClick.bChanged && rightUiClick.bState && desktopCloseIndex) {
            desktopSurfaces.Close(desktopPanel.surfaces[*desktopCloseIndex].id);
            desktopPanel.surfaces = desktopSurfaces.Summaries();
            desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && restoreButtonHit && selectedDeck == 2) {
            restoreHoldActive = true;
            restoreHoldStarted = std::chrono::steady_clock::now();
        } else if (rightUiClick.bChanged && rightUiClick.bState && (rigFullResetHit || (rigMountResetHit && mountReady))) {
            rigResetHoldActive = true;
            rigResetKind = rigFullResetHit ? L"full" : L"mounting";
            restoreHoldStarted = std::chrono::steady_clock::now();
        }
        if (rightUiClick.bState && activeDesktopPointer && desktopSurfaceHit
            && desktopSurfaceHit->captured && desktopSurfaceHit->id == activeDesktopPointer->id) {
            activeDesktopPointer = desktopSurfaceHit;
            desktopSurfaces.SendPointerEvent(*activeDesktopPointer,
                interfayce::DesktopPointerEvent::Move);
        }
        if (rightUiClick.bChanged && !rightUiClick.bState && activeDesktopPointer) {
            desktopSurfaces.SendPointerEvent(*activeDesktopPointer,
                interfayce::DesktopPointerEvent::PrimaryUp);
            activeDesktopPointer.reset();
        }
        if (selectedDeck == 0 && std::chrono::steady_clock::now() >= nextMusicPoll) {
            nextMusicPoll = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            const auto updatedMusicLine = ReadSpotifyNowPlaying(projectRoot);
            if (updatedMusicLine != musicLine && renderer.Initialize(system, selectedDeck, updatedMusicLine)) {
                musicLine = updatedMusicLine;
                RefreshSpotifyArt(projectRoot, musicArtPath);
                renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring());
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (selectedDeck == 3 && std::chrono::steady_clock::now() >= nextRigPoll) {
            nextRigPoll = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            const auto updatedRigLine = ReadControllerBatteryLine(system);
            const auto slimeStatus = ReadSlimeTrackerBatteries(projectRoot);
            const auto updatedRigSlots = slimeStatus.slots;
            if ((updatedRigLine != rigLine || updatedRigSlots != rigSlots)
                && renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    updatedRigLine, updatedRigSlots, slimeStatus.mountReady)) {
                rigLine = updatedRigLine;
                rigSlots = updatedRigSlots;
                mountReady = slimeStatus.mountReady;
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (rightUiClick.bChanged && !rightUiClick.bState && restoreHoldActive) {
            const auto heldFor = std::chrono::steady_clock::now() - restoreHoldStarted;
            if (restoreButtonHit && heldFor >= std::chrono::milliseconds(750)) {
                restoreBaseline(sessionBaseline, "wrist restore control");
            } else {
                std::cout << "wrist restore cancelled: hold for 0.75 seconds\n";
            }
            restoreHoldActive = false;
        }
        if (rightUiClick.bChanged && !rightUiClick.bState && rigResetHoldActive) {
            const auto heldFor = std::chrono::steady_clock::now() - restoreHoldStarted;
            const bool stillOnControl = rigResetKind == std::wstring_view(L"full") ? rigFullResetHit : rigMountResetHit;
            if (stillOnControl && heldFor >= std::chrono::milliseconds(1000)) {
                LaunchSlimeReset(projectRoot, rigResetKind);
                std::cout << "SlimeVR " << (rigResetKind == std::wstring_view(L"full") ? "full" : "mounting") << " reset requested\n";
            }
            rigResetHoldActive = false;
            rigResetKind = nullptr;
        }

        const auto desiredHand = leftDrag.bState ? DragHand::Left
            : rightDrag.bState                ? DragHand::Right
                                                : DragHand::None;
        if ((dragPreview || temporaryDrag || sessionDrag) && desiredHand != activeDragHand) {
            if (activeDragHand != DragHand::None && desiredHand == DragHand::None) {
                if (temporaryDrag) {
                    restoreBaseline(temporaryBaseline, "temporary drag");
                }
                std::cout << (dragPreview ? "preview" : temporaryDrag ? "temporary" : "session")
                          << " drag released: x=" << previewOffset.x
                          << " y=" << previewOffset.y << " z=" << previewOffset.z << '\n';
            }
            activeDragHand = desiredHand;
            if (activeDragHand != DragHand::None) {
                if (!sessionDrag) {
                    previewOffset = {};
                }
                dragFaulted = false;
                lastDragPosition = ReadControllerPosition(system, activeDragHand);
                if (temporaryDrag) {
                    vr::HmdMatrix34_t baseline{};
                    if (!chaperone->GetWorkingStandingZeroPoseToRawTrackingPose(&baseline)) {
                        std::cerr << "temporary drag refused: no working standing baseline\n";
                        activeDragHand = DragHand::None;
                        lastDragPosition.reset();
                        continue;
                    }
                    temporaryBaseline = baseline;
                }
                if (sessionDrag && !sessionBaseline) {
                    vr::HmdMatrix34_t baseline{};
                    if (!chaperone->GetWorkingStandingZeroPoseToRawTrackingPose(&baseline)) {
                        std::cerr << "session drag refused: no working standing baseline\n";
                        activeDragHand = DragHand::None;
                        lastDragPosition.reset();
                        continue;
                    }
                    sessionBaseline = baseline;
                }
                std::cout << (dragPreview ? "preview" : temporaryDrag ? "temporary" : "session")
                          << " drag started on "
                          << (activeDragHand == DragHand::Left ? "left" : "right") << " hand\n";
            }
        }
        if ((dragPreview || temporaryDrag || sessionDrag)
            && activeDragHand != DragHand::None && !dragFaulted) {
            const auto currentPosition = ReadControllerPosition(system, activeDragHand);
            if (currentPosition && lastDragPosition) {
                previewOffset.x += currentPosition->x - lastDragPosition->x;
                previewOffset.y += currentPosition->y - lastDragPosition->y;
                previewOffset.z += currentPosition->z - lastDragPosition->z;
            }
            lastDragPosition = currentPosition;
            if ((temporaryDrag || sessionDrag) && (temporaryBaseline || sessionBaseline)) {
                constexpr float kMaximumTemporaryDragMeters = 2.0F;
                if (std::abs(previewOffset.x) > kMaximumTemporaryDragMeters
                    || std::abs(previewOffset.y) > kMaximumTemporaryDragMeters
                    || std::abs(previewOffset.z) > kMaximumTemporaryDragMeters) {
                    std::cerr << "session drag cancelled: implausible offset\n";
                    restoreBaseline(temporaryBaseline, "temporary drag");
                    restoreBaseline(sessionBaseline, "session drag");
                    dragFaulted = true;
                } else {
                    const auto& baseline = sessionDrag ? *sessionBaseline : *temporaryBaseline;
                    const auto previewOrigin = TranslateOrigin(baseline, previewOffset);
                    chaperone->SetWorkingStandingZeroPoseToRawTrackingPose(&previewOrigin);
                    chaperone->ShowWorkingSetPreview();
                }
            }
            if (std::chrono::steady_clock::now() - lastPreviewLog > std::chrono::milliseconds(250)) {
                std::cout << (dragPreview ? "preview" : temporaryDrag ? "temporary" : "session")
                          << " offset: x=" << previewOffset.x
                          << " y=" << previewOffset.y << " z=" << previewOffset.z << '\n';
                lastPreviewLog = std::chrono::steady_clock::now();
            }
        }

        vr::VREvent_t event{};
        while (system->PollNextEvent(&event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) {
                running = false;
            }
        }
        desktopSurfaces.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    restoreBaseline(temporaryBaseline, "temporary drag shutdown");
    restoreBaseline(sessionBaseline, "Interfayce shutdown");
    desktopSurfaces.Shutdown();
    vr::VROverlay()->DestroyOverlay(wristOverlay);
    vr::VROverlay()->DestroyOverlay(cursorOverlay);
    if (shutdownEvent != nullptr) CloseHandle(shutdownEvent);
    vr::VR_Shutdown();
    return 0;
}
