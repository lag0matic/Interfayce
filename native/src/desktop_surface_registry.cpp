#include "desktop_surface_registry.h"

#include <d2d1helper.h>
#include <dwmapi.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace {

constexpr UINT kPickerWidth = 1024;
constexpr UINT kPickerHeight = 640;

std::optional<size_t> SourceAtPickerCoordinates(const std::vector<interfayce::DesktopSource>& sources,
                                                 float x, float y, size_t applicationPage) {
    const bool displayColumn = x >= 34.0F && x <= 498.0F;
    const bool applicationColumn = x >= 526.0F && x <= 990.0F;
    if ((!displayColumn && !applicationColumn) || y < 148.0F) return std::nullopt;
    const auto row = static_cast<size_t>((y - 148.0F) / 86.0F);
    const auto rowTop = 148.0F + static_cast<float>(row) * 86.0F;
    if (row >= 5 || y > rowTop + 74.0F) return std::nullopt;
    size_t matchingRow = 0;
    const size_t wantedRow = applicationColumn ? applicationPage * 5 + row : row;
    for (size_t index = 0; index < sources.size(); ++index) {
        const bool kindMatches = displayColumn
            ? sources[index].kind == interfayce::DesktopSource::Kind::Display
            : sources[index].kind == interfayce::DesktopSource::Kind::Window;
        if (!kindMatches) continue;
        if (matchingRow++ == wantedRow) return index;
    }
    return std::nullopt;
}

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

std::optional<POINT> DesktopPointForHit(const interfayce::DesktopSource& source, float u, float v) {
    RECT bounds{};
    if (source.kind == interfayce::DesktopSource::Kind::Display) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (source.monitor == nullptr || !GetMonitorInfoW(source.monitor, &info)) return std::nullopt;
        bounds = info.rcMonitor;
    } else {
        if (source.window == nullptr || !IsWindow(source.window)) return std::nullopt;
        if (IsIconic(source.window)) ShowWindowAsync(source.window, SW_RESTORE);
        SetForegroundWindow(source.window);
        if (FAILED(DwmGetWindowAttribute(source.window, DWMWA_EXTENDED_FRAME_BOUNDS,
                &bounds, sizeof(bounds))) && !GetWindowRect(source.window, &bounds)) return std::nullopt;
    }
    const auto width = (std::max)(bounds.right - bounds.left, 1L);
    const auto height = (std::max)(bounds.bottom - bounds.top, 1L);
    return POINT{
        bounds.left + static_cast<LONG>(std::lround(std::clamp(u, 0.0F, 1.0F) * width)),
        bounds.top + static_cast<LONG>(std::lround((1.0F - std::clamp(v, 0.0F, 1.0F)) * height)),
    };
}

bool InjectDesktopPointer(const POINT point, interfayce::DesktopPointerEvent event) {
    const auto virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const auto virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const auto virtualWidth = (std::max)(GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1, 1);
    const auto virtualHeight = (std::max)(GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1, 1);
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(std::lround(
        static_cast<double>(point.x - virtualLeft) * 65535.0 / virtualWidth));
    input.mi.dy = static_cast<LONG>(std::lround(
        static_cast<double>(point.y - virtualTop) * 65535.0 / virtualHeight));
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
    if (event == interfayce::DesktopPointerEvent::PrimaryDown) input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    if (event == interfayce::DesktopPointerEvent::PrimaryUp) input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
    return SendInput(1, &input, sizeof(input)) == 1;
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

bool DesktopPickerTexture::Render(const std::vector<DesktopSource>& sources,
                                  std::optional<size_t> hoveredSource, size_t applicationPage) {
    if (!context_) return false;
    const auto drawText = [&](std::wstring_view text, IDWriteTextFormat* format,
                              const D2D1_RECT_F& rectangle, ID2D1Brush* brush) {
        context_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rectangle, brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    const auto drawSource = [&](const DesktopSource& source, size_t sourceIndex,
                                float left, float top, float right) {
        const auto bounds = D2D1::RectF(left, top, right, top + 74.0F);
        context_->FillRectangle(bounds, surfaceBrush_.Get());
        context_->DrawRectangle(bounds, hoveredSource == sourceIndex
                ? (source.kind == DesktopSource::Kind::Display ? cyanBrush_.Get() : violetBrush_.Get())
                : mutedBrush_.Get(), hoveredSource == sourceIndex ? 3.0F : 1.0F);
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
    const auto applicationCount = static_cast<size_t>(std::count_if(sources.begin(), sources.end(),
        [](const auto& source) { return source.kind == DesktopSource::Kind::Window; }));
    const auto applicationPages = (std::max<size_t>)(1, (applicationCount + 4) / 5);
    const auto applicationHeader = L"APPLICATION   " + std::to_wstring(applicationPage + 1)
        + L"/" + std::to_wstring(applicationPages);
    drawText(applicationHeader, detailFormat_.Get(), D2D1::RectF(526.0F, 112.0F, 800.0F, 140.0F),
        violetBrush_.Get());

    size_t displayRow = 0;
    size_t applicationRow = 0;
    size_t seenApplications = 0;
    for (size_t index = 0; index < sources.size(); ++index) {
        const auto& source = sources[index];
        if (source.kind == DesktopSource::Kind::Display && displayRow < 5) {
            drawSource(source, index, 34.0F,
                148.0F + static_cast<float>(displayRow++) * 86.0F, 498.0F);
        } else if (source.kind == DesktopSource::Kind::Window
                   && seenApplications++ >= applicationPage * 5 && applicationRow < 5) {
            drawSource(source, index, 526.0F,
                148.0F + static_cast<float>(applicationRow++) * 86.0F, 990.0F);
        }
    }
    if (displayRow == 0) drawText(L"No displays available", itemFormat_.Get(),
        D2D1::RectF(34.0F, 164.0F, 498.0F, 202.0F), mutedBrush_.Get());
    if (applicationRow == 0) drawText(L"No eligible applications", itemFormat_.Get(),
        D2D1::RectF(526.0F, 164.0F, 990.0F, 202.0F), mutedBrush_.Get());
    if (applicationPages > 1) {
        context_->DrawLine(D2D1::Point2F(866, 595), D2D1::Point2F(846, 608), violetBrush_.Get(), 3.0F);
        context_->DrawLine(D2D1::Point2F(846, 608), D2D1::Point2F(866, 621), violetBrush_.Get(), 3.0F);
        context_->DrawLine(D2D1::Point2F(950, 595), D2D1::Point2F(970, 608), violetBrush_.Get(), 3.0F);
        context_->DrawLine(D2D1::Point2F(970, 608), D2D1::Point2F(950, 621), violetBrush_.Get(), 3.0F);
    }
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
    surface.sources = sources;
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

std::optional<DesktopSurfaceHit> DesktopSurfaceRegistry::HitTest(
        const vr::VROverlayIntersectionParams_t& ray) const {
    std::optional<DesktopSurfaceHit> nearest;
    for (const auto& surface : surfaces_) {
        if (!surface.visible) continue;
        vr::VROverlayIntersectionResults_t result{};
        if (!vr::VROverlay()->ComputeOverlayIntersection(surface.overlay, &ray, &result)) continue;
        DesktopSurfaceHit hit{};
        hit.id = surface.id;
        hit.captured = surface.capture != nullptr;
        hit.distance = result.fDistance;
        hit.u = result.vUVs.v[0];
        hit.v = result.vUVs.v[1];
        if (!hit.captured) {
            const auto x = result.vUVs.v[0] * static_cast<float>(kPickerWidth);
            const auto y = (1.0F - result.vUVs.v[1]) * static_cast<float>(kPickerHeight);
            hit.sourceIndex = SourceAtPickerCoordinates(surface.sources, x, y, surface.applicationPage);
            const auto applicationCount = static_cast<size_t>(std::count_if(surface.sources.begin(),
                surface.sources.end(), [](const auto& source) {
                    return source.kind == DesktopSource::Kind::Window;
                }));
            if (!hit.sourceIndex && applicationCount > 5 && y >= 580.0F && y <= 632.0F) {
                if (x >= 820.0F && x <= 900.0F) hit.pageDelta = -1;
                if (x >= 920.0F && x <= 1000.0F) hit.pageDelta = 1;
            }
        }
        if (!nearest || hit.distance < nearest->distance) nearest = hit;
    }
    return nearest;
}

bool DesktopSurfaceRegistry::ActivateHit(const DesktopSurfaceHit& hit) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&hit](const auto& surface) { return surface.id == hit.id; });
    if (found == surfaces_.end() || found->capture) return false;
    if (hit.pageDelta != 0) {
        const auto applicationCount = static_cast<size_t>(std::count_if(found->sources.begin(),
            found->sources.end(), [](const auto& source) {
                return source.kind == DesktopSource::Kind::Window;
            }));
        const auto pageCount = (std::max<size_t>)(1, (applicationCount + 4) / 5);
        const auto current = static_cast<int>(found->applicationPage);
        found->applicationPage = static_cast<size_t>((current + hit.pageDelta
            + static_cast<int>(pageCount)) % static_cast<int>(pageCount));
        found->hoveredSource.reset();
        return found->texture->Render(found->sources, std::nullopt, found->applicationPage);
    }
    if (!hit.sourceIndex || *hit.sourceIndex >= found->sources.size()) return false;
    auto capture = std::make_unique<DesktopCapture>();
    if (!capture->Start(device_, found->sources[*hit.sourceIndex])) return false;
    const auto texture = capture->Texture();
    if (vr::VROverlay()->SetOverlayTexture(found->overlay, &texture) != vr::VROverlayError_None) {
        capture->Stop();
        return false;
    }
    found->label = found->sources[*hit.sourceIndex].label;
    found->assignedSource = *hit.sourceIndex;
    found->hoveredSource.reset();
    found->capture = std::move(capture);
    return true;
}

bool DesktopSurfaceRegistry::SendPointerEvent(const DesktopSurfaceHit& hit,
                                              DesktopPointerEvent event) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&hit](const auto& surface) { return surface.id == hit.id; });
    if (found == surfaces_.end() || !found->capture || !found->assignedSource
        || *found->assignedSource >= found->sources.size()) return false;
    const auto point = DesktopPointForHit(found->sources[*found->assignedSource], hit.u, hit.v);
    return point && InjectDesktopPointer(*point, event);
}

void DesktopSurfaceRegistry::SetHoveredHit(const std::optional<DesktopSurfaceHit>& hit) {
    for (auto& surface : surfaces_) {
        if (surface.capture) continue;
        const auto nextHover = hit && hit->id == surface.id ? hit->sourceIndex : std::nullopt;
        if (nextHover == surface.hoveredSource) continue;
        surface.hoveredSource = nextHover;
        surface.texture->Render(surface.sources, surface.hoveredSource, surface.applicationPage);
    }
}

void DesktopSurfaceRegistry::Update() {
    for (auto& surface : surfaces_) {
        if (!surface.capture) continue;
        const auto result = surface.capture->Update();
        if (result == DesktopCapture::UpdateResult::TextureChanged) {
            const auto texture = surface.capture->Texture();
            vr::VROverlay()->SetOverlayTexture(surface.overlay, &texture);
        } else if (result == DesktopCapture::UpdateResult::Closed
                   || result == DesktopCapture::UpdateResult::Failed) {
            surface.capture->Stop();
            surface.capture.reset();
            surface.assignedSource.reset();
            surface.label = L"Choose source";
            surface.texture->Render(surface.sources);
            const auto texture = surface.texture->Texture();
            vr::VROverlay()->SetOverlayTexture(surface.overlay, &texture);
        }
    }
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
