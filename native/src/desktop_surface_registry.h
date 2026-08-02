#pragma once

#include "desktop_capture.h"
#include "desktop_surface_manager.h"

#include <openvr.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>

#include <cstdint>
#include <array>
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

enum class DesktopPointerEvent { Move, PrimaryDown, PrimaryUp };

class DesktopPickerTexture {
public:
    bool Initialize(ID3D11Device* device);
    bool Render(const std::vector<DesktopSource>& sources,
                std::optional<size_t> hoveredSource = std::nullopt, size_t applicationPage = 0);
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
    HANDLE sharedHandle_{};
};

class DesktopSurfaceRegistry {
public:
    bool Initialize(vr::IVRSystem* system, ID3D11Device* device);
    uint64_t SpawnPicker(const std::vector<DesktopSource>& sources);
    std::optional<DesktopSurfaceHit> HitTest(const vr::VROverlayIntersectionParams_t& ray) const;
    std::optional<uint64_t> FrameHitTest(const vr::VROverlayIntersectionParams_t& ray) const;
    bool ActivateHit(const DesktopSurfaceHit& hit);
    bool SendPointerEvent(const DesktopSurfaceHit& hit, DesktopPointerEvent event);
    std::optional<vr::HmdMatrix34_t> CursorTransform(const DesktopSurfaceHit& hit) const;
    void SetHoveredHit(const std::optional<DesktopSurfaceHit>& hit);
    void SetHoveredFrame(std::optional<uint64_t> id);
    void Update();
    bool BringToMe(uint64_t id);
    bool Close(uint64_t id);
    bool BeginGrab(uint64_t id, const vr::HmdMatrix34_t& handTransform);
    bool UpdateGrab(const vr::HmdMatrix34_t& handTransform);
    void EndGrab();
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
        std::unique_ptr<DesktopPickerTexture> texture;
        std::unique_ptr<DesktopCapture> capture;
        std::vector<DesktopSource> sources;
        std::optional<size_t> assignedSource;
        std::optional<size_t> hoveredSource;
        size_t applicationPage{};
        float aspectRatio{1.6F};
        vr::HmdMatrix34_t transform{};
        bool visible{true};
    };

    struct GrabState {
        uint64_t id{};
        vr::HmdMatrix34_t handToSurface{};
    };

    bool PlaceAtEyeLine(Surface& surface) const;
    bool CreateFrameOverlays(Surface& surface) const;
    bool UpdateFrameOverlays(const Surface& surface) const;
    void DestroySurfaceOverlays(Surface& surface) const;

    vr::IVRSystem* system_{};
    ID3D11Device* device_{};
    uint64_t nextId_{1};
    std::vector<Surface> surfaces_;
    std::optional<GrabState> activeGrab_;
};

}  // namespace interfayce
