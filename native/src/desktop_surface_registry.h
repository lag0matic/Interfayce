#pragma once

#include "desktop_surface_manager.h"

#include <openvr.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace interfayce {

struct DesktopSurfaceSummary {
    uint64_t id{};
    std::wstring label;
    bool visible{};
};

class DesktopPickerTexture {
public:
    bool Initialize(ID3D11Device* device);
    bool Render(const std::vector<DesktopSource>& sources);
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
    bool BringToMe(uint64_t id);
    bool Close(uint64_t id);
    std::vector<DesktopSurfaceSummary> Summaries() const;
    void Shutdown();

private:
    struct Surface {
        uint64_t id{};
        std::string overlayKey;
        std::wstring label;
        vr::VROverlayHandle_t overlay{vr::k_ulOverlayHandleInvalid};
        std::unique_ptr<DesktopPickerTexture> texture;
        bool visible{true};
    };

    bool PlaceAtEyeLine(Surface& surface) const;

    vr::IVRSystem* system_{};
    ID3D11Device* device_{};
    uint64_t nextId_{1};
    std::vector<Surface> surfaces_;
};

}  // namespace interfayce
