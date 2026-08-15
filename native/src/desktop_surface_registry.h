#pragma once

#include "desktop_capture.h"
#include "desktop_surface_manager.h"

#include <openvr.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>

#include <cstdint>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace interfayce {

struct DesktopSurfaceSummary {
    uint64_t id{};
    std::wstring label;
    bool visible{};
    bool reusable{};
    bool locked{};
};

struct DesktopSurfaceHit {
    uint64_t id{};
    std::optional<size_t> sourceIndex;
    bool captured{};
    float distance{};
    int pageDelta{};
    float u{};
    float v{};
};

struct KeyboardSurfaceHit {
    uint64_t id{};
    std::optional<size_t> keyIndex;
    float distance{};
    float u{};
    float v{};
};

enum class DesktopPointerEvent { Move, PrimaryDown, PrimaryUp };
enum class DesktopGrabHand { Left, Right };

class DesktopPickerTexture {
public:
    bool Initialize(ID3D11Device* device, UINT width = 1024, UINT height = 640);
    bool Render(const std::vector<DesktopSource>& sources,
                std::optional<size_t> hoveredSource = std::nullopt, size_t applicationPage = 0);
    bool RenderKeyboard(const std::wstring& targetLabel, bool shifted, bool controlled = false,
                        bool altered = false,
                        std::optional<size_t> hoveredKey = std::nullopt,
                        std::optional<size_t> pressedKey = std::nullopt);
    vr::Texture_t Texture() const;

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> itemFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detailFormat_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surfaceBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cyanBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> violetBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> violetDimBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activeFillBrush_;
    std::vector<std::wstring> iconSourceIds_;
    std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap>> sourceIcons_;
    HANDLE sharedHandle_{};
};

class DesktopSurfaceRegistry {
public:
    bool Initialize(vr::IVRSystem* system, ID3D11Device* device);
    uint64_t SpawnPicker(const std::vector<DesktopSource>& sources);
    uint64_t SpawnKeyboard();
    std::optional<DesktopSurfaceHit> HitTest(const vr::VROverlayIntersectionParams_t& ray) const;
    std::optional<DesktopSurfaceHit> SurfaceAimHitTest(
        const vr::VROverlayIntersectionParams_t& ray,
        float edgeToleranceMeters = 0.008F) const;
    std::optional<KeyboardSurfaceHit> KeyboardHitTest(
        const vr::VROverlayIntersectionParams_t& ray,
        float edgeToleranceMeters = 0.008F) const;
    std::optional<uint64_t> FrameHitTest(const vr::VROverlayIntersectionParams_t& ray) const;
    std::optional<DesktopSource> SourceForHit(const DesktopSurfaceHit& hit) const;
    bool ActivateHit(const DesktopSurfaceHit& hit);
    bool AssignSource(uint64_t id, const DesktopSource& source);
    bool SendPointerEvent(const DesktopSurfaceHit& hit, DesktopPointerEvent event);
    bool SendScrollEvent(const DesktopSurfaceHit& hit, int32_t verticalDelta,
                         int32_t horizontalDelta);
    bool ActivateKeyboardHit(const KeyboardSurfaceHit& hit);
    std::optional<vr::HmdMatrix34_t> CursorTransform(const DesktopSurfaceHit& hit) const;
    std::optional<vr::HmdMatrix34_t> KeyboardCursorTransform(
        const KeyboardSurfaceHit& hit) const;
    void SetHoveredHit(const std::optional<DesktopSurfaceHit>& hit);
    void SetHoveredKeyboard(const std::optional<KeyboardSurfaceHit>& hit);
    void SetHoveredFrame(std::optional<uint64_t> id);
    void SetDeckVisible(bool visible);
    void Update();
    bool BringToMe(uint64_t id);
    bool BringAllToMe();
    bool ToggleLocked(uint64_t id);
    bool ReturnToPicker(uint64_t id, const std::vector<DesktopSource>& sources);
    bool Close(uint64_t id);
    bool BeginGrab(uint64_t id, DesktopGrabHand hand, const vr::HmdMatrix34_t& handTransform);
    bool UpdateGrab(DesktopGrabHand hand, const vr::HmdMatrix34_t& handTransform);
    void EndGrab(DesktopGrabHand hand);
    std::vector<DesktopSurfaceSummary> Summaries() const;
    void Shutdown();

private:
    struct Surface {
        uint64_t id{};
        std::string overlayKey;
        std::wstring label;
        vr::VROverlayHandle_t overlay{vr::k_ulOverlayHandleInvalid};
        std::array<vr::VROverlayHandle_t, 4> frameOverlays{
            vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid,
            vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid};
        std::array<vr::VROverlayHandle_t, 4> glowOverlays{
            vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid,
            vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid};
        std::unique_ptr<DesktopPickerTexture> texture;
        std::vector<std::unique_ptr<DesktopPickerTexture>> additionalPickerPages;
        std::unique_ptr<DesktopCapture> capture;
        std::vector<DesktopSource> sources;
        std::optional<size_t> assignedSource;
        std::optional<size_t> hoveredSource;
        size_t applicationPage{};
        bool keyboard{};
        bool keyboardShifted{};
        bool keyboardControlled{};
        bool keyboardAltered{};
        std::optional<size_t> hoveredKey;
        std::optional<size_t> pressedKey;
        std::chrono::steady_clock::time_point keyFlashUntil{};
        float aspectRatio{1.6F};
        float widthMeters{0.69F};
        vr::HmdMatrix34_t transform{};
        bool visible{true};
        bool locked{};
    };

    struct GrabState {
        uint64_t id{};
        vr::HmdMatrix34_t handToSurface{};
        vr::HmdMatrix34_t lastHandTransform{};
    };

    struct ScaleState {
        uint64_t id{};
        float initialSpan{};
        float initialWidth{};
        vr::HmdVector3_t initialMidpoint{};
        vr::HmdMatrix34_t initialSurfaceTransform{};
    };

    bool PlaceAtEyeLine(Surface& surface) const;
    bool PlaceRelativeToHmd(Surface& surface, float x, float y, float z) const;
    bool CreateFrameOverlays(Surface& surface) const;
    bool UpdateFrameOverlays(const Surface& surface) const;
    void DestroySurfaceOverlays(Surface& surface) const;
    void RememberFocusedSurface(uint64_t id);
    void ForgetFocusedSurface(uint64_t id);

    vr::IVRSystem* system_{};
    ID3D11Device* device_{};
    uint64_t nextId_{1};
    std::vector<Surface> surfaces_;
    std::array<std::optional<GrabState>, 2> activeGrabs_;
    std::optional<ScaleState> activeScale_;
    std::optional<uint64_t> focusedSurfaceId_;
    std::vector<uint64_t> focusHistory_;
    bool deckVisible_{true};
};

}  // namespace interfayce
