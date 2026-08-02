#include "desktop_surface_registry.h"

#include <d2d1helper.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace {

constexpr UINT kPickerWidth = 1024;
constexpr UINT kPickerHeight = 640;

vr::HmdMatrix34_t Multiply(const vr::HmdMatrix34_t& left, const vr::HmdMatrix34_t& right) {
    vr::HmdMatrix34_t result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row][column] = left.m[row][0] * right.m[0][column]
                + left.m[row][1] * right.m[1][column]
                + left.m[row][2] * right.m[2][column];
        }
        result.m[row][3] = left.m[row][0] * right.m[0][3]
            + left.m[row][1] * right.m[1][3]
            + left.m[row][2] * right.m[2][3]
            + left.m[row][3];
    }
    return result;
}

}  // namespace

namespace interfayce {

bool DesktopPickerTexture::Initialize(ID3D11Device* device) {
    if (device == nullptr) return false;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = kPickerWidth;
    description.Height = kPickerHeight;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &texture_))) return false;

    Microsoft::WRL::ComPtr<IDXGIResource> sharedResource;
    if (FAILED(texture_.As(&sharedResource))
        || FAILED(sharedResource->GetSharedHandle(&sharedHandle_))) return false;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) return false;
    Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
            &options, reinterpret_cast<void**>(factory.GetAddressOf())))) return false;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
    if (FAILED(factory->CreateDevice(dxgiDevice.Get(), &d2dDevice))
        || FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_))) return false;

    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    if (FAILED(texture_.As(&surface))) return false;
    const auto properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(context_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target_))) return false;
    context_->SetTarget(target_.Get());

    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf())))) return false;
    if (FAILED(writeFactory->CreateTextFormat(L"Bahnschrift", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 32.0F, L"en-us", &titleFormat_))
        || FAILED(writeFactory->CreateTextFormat(L"Bahnschrift", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 22.0F, L"en-us", &itemFormat_))
        || FAILED(writeFactory->CreateTextFormat(L"Bahnschrift", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0F, L"en-us", &detailFormat_))) return false;

    context_->CreateSolidColorBrush(D2D1::ColorF(0.015F, 0.030F, 0.050F, 0.92F), &panelBrush_);
    context_->CreateSolidColorBrush(D2D1::ColorF(0.035F, 0.070F, 0.100F, 0.94F), &surfaceBrush_);
    context_->CreateSolidColorBrush(D2D1::ColorF(0.90F, 0.96F, 0.98F, 1.0F), &textBrush_);
    context_->CreateSolidColorBrush(D2D1::ColorF(0.46F, 0.64F, 0.72F, 1.0F), &mutedBrush_);
    context_->CreateSolidColorBrush(D2D1::ColorF(0.12F, 0.82F, 0.96F, 1.0F), &cyanBrush_);
    context_->CreateSolidColorBrush(D2D1::ColorF(0.48F, 0.28F, 0.96F, 1.0F), &violetBrush_);
    return true;
}

bool DesktopPickerTexture::Render(const std::vector<DesktopSource>& sources) {
    if (!context_) return false;
    const auto drawText = [&](std::wstring_view text, IDWriteTextFormat* format,
                              const D2D1_RECT_F& rectangle, ID2D1Brush* brush) {
        context_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rectangle, brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    const auto drawSource = [&](const DesktopSource& source, float left, float top, float right) {
        const auto bounds = D2D1::RectF(left, top, right, top + 74.0F);
        context_->FillRectangle(bounds, surfaceBrush_.Get());
        context_->DrawRectangle(bounds, mutedBrush_.Get(), 1.0F);
        const auto icon = D2D1::RectF(left + 18.0F, top + 20.0F, left + 54.0F, top + 45.0F);
        context_->DrawRectangle(icon, source.kind == DesktopSource::Kind::Display
                ? cyanBrush_.Get() : violetBrush_.Get(), 2.0F);
        if (source.kind == DesktopSource::Kind::Display) {
            context_->DrawLine(D2D1::Point2F(left + 28.0F, top + 51.0F),
                D2D1::Point2F(left + 44.0F, top + 51.0F), cyanBrush_.Get(), 2.0F);
        } else {
            context_->DrawLine(D2D1::Point2F(left + 18.0F, top + 27.0F),
                D2D1::Point2F(left + 54.0F, top + 27.0F), violetBrush_.Get(), 2.0F);
        }
        drawText(source.label, itemFormat_.Get(), D2D1::RectF(left + 72.0F, top + 13.0F,
            right - 14.0F, top + 43.0F), textBrush_.Get());
        drawText(source.detail, detailFormat_.Get(), D2D1::RectF(left + 72.0F, top + 44.0F,
            right - 14.0F, top + 68.0F), mutedBrush_.Get());
    };

    context_->BeginDraw();
    context_->Clear(D2D1::ColorF(0, 0));
    context_->FillRectangle(D2D1::RectF(4.0F, 4.0F, 1020.0F, 636.0F), panelBrush_.Get());
    context_->DrawRectangle(D2D1::RectF(4.0F, 4.0F, 1020.0F, 636.0F), cyanBrush_.Get(), 1.5F);
    context_->DrawLine(D2D1::Point2F(4.0F, 92.0F), D2D1::Point2F(1020.0F, 92.0F),
        violetBrush_.Get(), 2.0F);
    drawText(L"Choose a source", titleFormat_.Get(), D2D1::RectF(34.0F, 27.0F, 600.0F, 75.0F),
        textBrush_.Get());
    drawText(L"DISPLAY", detailFormat_.Get(), D2D1::RectF(34.0F, 112.0F, 300.0F, 140.0F),
        cyanBrush_.Get());
    drawText(L"APPLICATION", detailFormat_.Get(), D2D1::RectF(526.0F, 112.0F, 800.0F, 140.0F),
        violetBrush_.Get());

    size_t displayRow = 0;
    size_t applicationRow = 0;
    for (const auto& source : sources) {
        if (source.kind == DesktopSource::Kind::Display && displayRow < 5) {
            drawSource(source, 34.0F, 148.0F + static_cast<float>(displayRow++) * 86.0F, 498.0F);
        } else if (source.kind == DesktopSource::Kind::Window && applicationRow < 5) {
            drawSource(source, 526.0F, 148.0F + static_cast<float>(applicationRow++) * 86.0F, 990.0F);
        }
    }
    if (displayRow == 0) drawText(L"No displays available", itemFormat_.Get(),
        D2D1::RectF(34.0F, 164.0F, 498.0F, 202.0F), mutedBrush_.Get());
    if (applicationRow == 0) drawText(L"No eligible applications", itemFormat_.Get(),
        D2D1::RectF(526.0F, 164.0F, 990.0F, 202.0F), mutedBrush_.Get());
    return SUCCEEDED(context_->EndDraw());
}

vr::Texture_t DesktopPickerTexture::Texture() const {
    vr::Texture_t texture{};
    texture.handle = sharedHandle_;
    texture.eType = vr::TextureType_DXGISharedHandle;
    texture.eColorSpace = vr::ColorSpace_Auto;
    return texture;
}

bool DesktopSurfaceRegistry::Initialize(vr::IVRSystem* system, ID3D11Device* device) {
    system_ = system;
    device_ = device;
    return system_ != nullptr && device_ != nullptr;
}

uint64_t DesktopSurfaceRegistry::SpawnPicker(const std::vector<DesktopSource>& sources) {
    if (!system_ || !device_) return 0;
    Surface surface{};
    surface.id = nextId_++;
    surface.overlayKey = "com.lag0matic.interfayce.desktop." + std::to_string(surface.id);
    surface.label = L"Choose source";
    surface.texture = std::make_unique<DesktopPickerTexture>();
    if (!surface.texture->Initialize(device_) || !surface.texture->Render(sources)) return 0;
    vr::VROverlayHandle_t staleOverlay = vr::k_ulOverlayHandleInvalid;
    if (vr::VROverlay()->FindOverlay(surface.overlayKey.c_str(), &staleOverlay)
        == vr::VROverlayError_None) {
        vr::VROverlay()->DestroyOverlay(staleOverlay);
    }
    if (vr::VROverlay()->CreateOverlay(surface.overlayKey.c_str(), "Interfayce Desktop",
            &surface.overlay) != vr::VROverlayError_None) return 0;
    vr::VROverlay()->SetOverlayWidthInMeters(surface.overlay, 0.92F);
    vr::VROverlay()->SetOverlayInputMethod(surface.overlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(surface.overlay, 20);
    const auto texture = surface.texture->Texture();
    if (vr::VROverlay()->SetOverlayTexture(surface.overlay, &texture) != vr::VROverlayError_None
        || !PlaceAtEyeLine(surface)
        || vr::VROverlay()->ShowOverlay(surface.overlay) != vr::VROverlayError_None) {
        vr::VROverlay()->DestroyOverlay(surface.overlay);
        return 0;
    }
    surfaces_.push_back(std::move(surface));
    return surfaces_.back().id;
}

bool DesktopSurfaceRegistry::PlaceAtEyeLine(Surface& surface) const {
    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
    system_->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0F, poses.data(),
        static_cast<uint32_t>(poses.size()));
    const auto& hmd = poses[vr::k_unTrackedDeviceIndex_Hmd];
    if (!hmd.bPoseIsValid) return false;
    vr::HmdMatrix34_t offset{};
    offset.m[0][0] = 1.0F;
    offset.m[1][1] = 1.0F;
    offset.m[2][2] = 1.0F;
    offset.m[1][3] = -0.04F;
    offset.m[2][3] = -1.05F;
    const auto transform = Multiply(hmd.mDeviceToAbsoluteTracking, offset);
    return vr::VROverlay()->SetOverlayTransformAbsolute(surface.overlay,
        vr::TrackingUniverseStanding, &transform) == vr::VROverlayError_None;
}

bool DesktopSurfaceRegistry::BringToMe(uint64_t id) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [id](const auto& surface) { return surface.id == id; });
    if (found == surfaces_.end() || !PlaceAtEyeLine(*found)) return false;
    found->visible = vr::VROverlay()->ShowOverlay(found->overlay) == vr::VROverlayError_None;
    return found->visible;
}

bool DesktopSurfaceRegistry::Close(uint64_t id) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [id](const auto& surface) { return surface.id == id; });
    if (found == surfaces_.end()) return false;
    vr::VROverlay()->DestroyOverlay(found->overlay);
    surfaces_.erase(found);
    return true;
}

std::vector<DesktopSurfaceSummary> DesktopSurfaceRegistry::Summaries() const {
    std::vector<DesktopSurfaceSummary> summaries;
    summaries.reserve(surfaces_.size());
    for (const auto& surface : surfaces_) {
        summaries.push_back({surface.id, surface.label, surface.visible});
    }
    return summaries;
}

void DesktopSurfaceRegistry::Shutdown() {
    for (auto& surface : surfaces_) {
        if (surface.overlay != vr::k_ulOverlayHandleInvalid) {
            vr::VROverlay()->DestroyOverlay(surface.overlay);
        }
    }
    surfaces_.clear();
}

}  // namespace interfayce
