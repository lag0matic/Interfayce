#include "overlay_renderer.h"

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace interfayce {

bool OverlayRenderer::Initialize(vr::IVRSystem* system, int deck, const std::wstring& musicLine,
                                 const std::wstring& musicArtPath, const std::wstring& rigLine,
                                 const std::array<std::wstring, 8>& rigSlots, bool mountReady,
                                 const DesktopPanelState& desktop) {
    if (device_) {
        return Render(deck, musicLine, musicArtPath, rigLine, rigSlots, mountReady, desktop);
    }
    constexpr UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel{};
    uint64_t compositorAdapterLuid = 0;
    system->GetOutputDevice(&compositorAdapterLuid, vr::TextureType_DirectX);

    Microsoft::WRL::ComPtr<IDXGIFactory1> dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> compositorAdapter;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)))) {
        return false;
    }
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        if (dxgiFactory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(candidate->GetDesc1(&description))) {
            const auto candidateLuid = (static_cast<uint64_t>(static_cast<uint32_t>(
                                           description.AdapterLuid.HighPart))
                                        << 32U)
                | description.AdapterLuid.LowPart;
            if (candidateLuid != compositorAdapterLuid) {
                continue;
            }
            compositorAdapter = candidate;
            break;
        }
    }
    if (!compositorAdapter
        || FAILED(D3D11CreateDevice(
            compositorAdapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            creationFlags,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device_,
            &featureLevel,
            &context_))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = 768;
    description.Height = 384;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &panelTexture_))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIResource> sharedResource;
    if (FAILED(panelTexture_.As(&sharedResource))
        || FAILED(sharedResource->GetSharedHandle(&sharedTextureHandle_))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory;
    D2D1_FACTORY_OPTIONS factoryOptions{};
    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1),
            &factoryOptions,
            reinterpret_cast<void**>(d2dFactory.GetAddressOf())))) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
    if (FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice))) {
        return false;
    }
    if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    if (FAILED(panelTexture_.As(&surface))) {
        return false;
    }
    const auto bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0F,
        96.0F);
    if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProperties, &targetBitmap_))) {
        return false;
    }
    d2dContext_->SetTarget(targetBitmap_.Get());

    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory;
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf())))) {
        return false;
    }
    if (FAILED(writeFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 18.0F, L"en-us", &labelFormat_))
        || FAILED(writeFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 32.0F, L"en-us", &titleFormat_))
        || FAILED(writeFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 20.0F, L"en-us", &bodyFormat_))) {
        return false;
    }

    // Orbital Utility: ultraviolet owns structure; cyan is reserved for live/action state.
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.012F, 0.020F, 0.038F, 0.92F), &glassBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.026F, 0.035F, 0.070F, 0.97F), &stripBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.90F, 0.94F, 0.98F, 1.0F), &textBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.48F, 0.55F, 0.68F, 1.0F), &mutedTextBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.15F, 0.87F, 0.95F, 1.0F), &accentBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.46F, 0.32F, 0.91F, 1.0F), &structureBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.30F, 0.22F, 0.57F, 0.78F), &structureDimBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.04F, 0.28F, 0.34F, 0.96F), &activeFillBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.035F, 0.055F, 0.105F, 0.96F), &buttonBrush_);

    return Render(deck, musicLine, musicArtPath, rigLine, rigSlots, mountReady, desktop);
}

bool OverlayRenderer::Render(int deck, const std::wstring& musicLine, const std::wstring& musicArtPath,
                             const std::wstring& rigLine, const std::array<std::wstring, 8>& rigSlots,
                             bool mountReady, const DesktopPanelState& desktop) {

    const auto drawText = [&](std::wstring_view text, IDWriteTextFormat* format, const D2D1_RECT_F rect,
                              ID2D1Brush* brush) {
        d2dContext_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rect, brush);
    };
    const auto drawCornerFrame = [&](const D2D1_RECT_F rect, ID2D1Brush* brush, float stroke = 2.0F) {
        constexpr float corner = 16.0F;
        constexpr float segment = 36.0F;
        d2dContext_->DrawLine(D2D1::Point2F(rect.left + corner, rect.top),
            D2D1::Point2F(rect.left + corner + segment, rect.top), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.left, rect.top + corner),
            D2D1::Point2F(rect.left, rect.top + corner + segment), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.left, rect.top + corner),
            D2D1::Point2F(rect.left + corner, rect.top), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.right - corner - segment, rect.top),
            D2D1::Point2F(rect.right - corner, rect.top), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.right, rect.top + corner),
            D2D1::Point2F(rect.right, rect.top + corner + segment), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.right - corner, rect.top),
            D2D1::Point2F(rect.right, rect.top + corner), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.left, rect.bottom - corner - segment),
            D2D1::Point2F(rect.left, rect.bottom - corner), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.left, rect.bottom - corner),
            D2D1::Point2F(rect.left + corner, rect.bottom), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.left + corner, rect.bottom),
            D2D1::Point2F(rect.left + corner + segment, rect.bottom), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.right, rect.bottom - corner - segment),
            D2D1::Point2F(rect.right, rect.bottom - corner), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.right, rect.bottom - corner),
            D2D1::Point2F(rect.right - corner, rect.bottom), brush, stroke);
        d2dContext_->DrawLine(D2D1::Point2F(rect.right - corner - segment, rect.bottom),
            D2D1::Point2F(rect.right - corner, rect.bottom), brush, stroke);
    };

    Microsoft::WRL::ComPtr<ID2D1Bitmap> albumArt;
    if (deck == 0 && !musicArtPath.empty()) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wicFactory)))
            && SUCCEEDED(wicFactory->CreateDecoderFromFilename(musicArtPath.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &decoder))
            && SUCCEEDED(decoder->GetFrame(0, &frame))
            && SUCCEEDED(wicFactory->CreateFormatConverter(&converter))
            && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            d2dContext_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &albumArt);
        }
    }

    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
    const auto panelBounds = D2D1::RectF(6.0F, 6.0F, 762.0F, 378.0F);
    d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(panelBounds, 16.0F, 16.0F), glassBrush_.Get());
    d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(panelBounds, 16.0F, 16.0F),
        structureDimBrush_.Get(), 1.0F);
    drawCornerFrame(panelBounds, structureBrush_.Get(), 2.0F);
    d2dContext_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(16.0F, 16.0F, 752.0F, 82.0F), 10.0F, 10.0F), stripBrush_.Get());

    const std::array<D2D1_RECT_F, 5> tabs{
        D2D1::RectF(24, 22, 126, 76), D2D1::RectF(132, 22, 250, 76),
        D2D1::RectF(256, 22, 356, 76), D2D1::RectF(362, 22, 482, 76),
        D2D1::RectF(488, 22, 610, 76)};
    const std::array<const wchar_t*, 5> tabLabels{
        L"\u266b MUSIC", L"\u25c9 COMMS", L"\u25a3 DESK", L"\u25ce SPACE", L"\u25c7 RIG"};
    const std::array<int, 5> tabDecks{0, 5, 1, 2, 3};
    for (size_t index = 0; index < tabs.size(); ++index) {
        const bool selected = deck == tabDecks[index];
        if (selected) {
            d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(tabs[index], 9, 9), activeFillBrush_.Get());
            d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(tabs[index], 9, 9), accentBrush_.Get(), 2.0F);
        } else {
            d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(tabs[index], 9, 9),
                structureDimBrush_.Get(), 1.0F);
        }
        drawText(tabLabels[index], labelFormat_.Get(),
            D2D1::RectF(tabs[index].left + 12, tabs[index].top + 15,
                tabs[index].right - 8, tabs[index].bottom - 6),
            selected ? textBrush_.Get() : mutedTextBrush_.Get());
    }

    // Compact orbital gear; it opens settings without spending another text tab.
    const auto gearBrush = deck == 4 ? accentBrush_.Get() : mutedTextBrush_.Get();
    const auto gearCenter = D2D1::Point2F(699, 49);
    d2dContext_->DrawEllipse(D2D1::Ellipse(gearCenter, 11, 11), gearBrush, 2.5F);
    d2dContext_->DrawEllipse(D2D1::Ellipse(gearCenter, 4, 4), gearBrush, 2.0F);
    for (int index = 0; index < 8; ++index) {
        const float angle = static_cast<float>(index) * 3.14159265F / 4.0F;
        d2dContext_->DrawLine(
            D2D1::Point2F(gearCenter.x + std::cos(angle) * 13.0F,
                gearCenter.y + std::sin(angle) * 13.0F),
            D2D1::Point2F(gearCenter.x + std::cos(angle) * 18.0F,
                gearCenter.y + std::sin(angle) * 18.0F), gearBrush, 3.0F);
    }

    if (deck != 2 && deck != 4 && deck != 5) {
        drawText(deck == 0 ? L"MUSIC" : deck == 1 ? L"DESKTOP" : L"RIG",
            labelFormat_.Get(), D2D1::RectF(42.0F, 106.0F, 300.0F, 138.0F),
            accentBrush_.Get());
    }
    if (deck != 2 && deck != 4 && deck != 5) {
        drawText(deck == 0 ? (musicLine.empty() ? L"No active track" : musicLine)
                : deck == 1 ? musicLine
                : (rigLine.empty() ? L"Controllers unavailable" : rigLine),
            titleFormat_.Get(), D2D1::RectF(42.0F, 154.0F,
                deck == 0 ? 540.0F : 690.0F, 202.0F), textBrush_.Get());
    }
    if (albumArt) d2dContext_->DrawBitmap(albumArt.Get(), D2D1::RectF(570.0F, 108.0F, 720.0F, 258.0F));
    if (deck == 0) {
        // A restrained activity meter now; real per-process levels can replace this data later.
        for (int index = 0; index < 24; ++index) {
            const float left = 44.0F + static_cast<float>(index) * 14.0F;
            const float height = musicLine.empty() ? 3.0F
                : 5.0F + static_cast<float>((index * 7 + 3) % 6) * 2.0F;
            d2dContext_->FillRectangle(D2D1::RectF(left, 236.0F - height, left + 8.0F, 236.0F),
                index < 18 && !musicLine.empty() ? accentBrush_.Get() : structureDimBrush_.Get());
        }
        drawText(L"PLAYBACK", labelFormat_.Get(), D2D1::RectF(394, 211, 535, 242), mutedTextBrush_.Get());
        // Orbital transport controls: no text boxes, only clear geometric controls.
        const std::array<D2D1_POINT_2F, 3> centers{
            D2D1::Point2F(140, 287), D2D1::Point2F(384, 287), D2D1::Point2F(628, 287)};
        for (size_t index = 0; index < centers.size(); ++index) {
            const float radius = index == 1 ? 42.0F : 32.0F;
            d2dContext_->FillEllipse(D2D1::Ellipse(centers[index], radius, radius),
                index == 1 ? activeFillBrush_.Get() : buttonBrush_.Get());
            d2dContext_->DrawEllipse(D2D1::Ellipse(centers[index], radius, radius),
                index == 1 ? accentBrush_.Get() : structureBrush_.Get(), index == 1 ? 2.5F : 1.5F);
            d2dContext_->DrawEllipse(D2D1::Ellipse(centers[index], radius + 7.0F, radius + 7.0F),
                structureDimBrush_.Get(), 1.0F);
        }
        // Previous.
        d2dContext_->DrawLine(D2D1::Point2F(149, 272), D2D1::Point2F(149, 302), accentBrush_.Get(), 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(146, 287), D2D1::Point2F(124, 273), accentBrush_.Get(), 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(146, 287), D2D1::Point2F(124, 301), accentBrush_.Get(), 3.5F);
        // Play.
        d2dContext_->DrawLine(D2D1::Point2F(371, 265), D2D1::Point2F(371, 309), accentBrush_.Get(), 4.0F);
        d2dContext_->DrawLine(D2D1::Point2F(371, 265), D2D1::Point2F(407, 287), accentBrush_.Get(), 4.0F);
        d2dContext_->DrawLine(D2D1::Point2F(407, 287), D2D1::Point2F(371, 309), accentBrush_.Get(), 4.0F);
        // Next.
        d2dContext_->DrawLine(D2D1::Point2F(619, 272), D2D1::Point2F(619, 302), accentBrush_.Get(), 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(622, 287), D2D1::Point2F(644, 273), accentBrush_.Get(), 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(622, 287), D2D1::Point2F(644, 301), accentBrush_.Get(), 3.5F);
        // Voice command microphone, kept separate from the three transport controls.
        const auto micCenter = D2D1::Point2F(520, 225);
        d2dContext_->FillEllipse(D2D1::Ellipse(micCenter, 27, 27),
            musicVoiceActive_ ? activeFillBrush_.Get() : buttonBrush_.Get());
        d2dContext_->DrawEllipse(D2D1::Ellipse(micCenter, 27, 27),
            musicVoiceActive_ ? accentBrush_.Get() : structureBrush_.Get(),
            musicVoiceActive_ ? 2.5F : 1.5F);
        d2dContext_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(512, 209, 528, 229), 8, 8),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(508, 223), D2D1::Point2F(508, 228),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(508, 228), D2D1::Point2F(513, 233),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(513, 233), D2D1::Point2F(527, 233),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(527, 233), D2D1::Point2F(532, 228),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(532, 228), D2D1::Point2F(532, 223),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(520, 234), D2D1::Point2F(520, 240),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(42, 343), D2D1::Point2F(726, 343),
            structureDimBrush_.Get(), 1.0F);
        drawText(musicVoiceStatus_, labelFormat_.Get(), D2D1::RectF(48, 350, 720, 374),
            musicVoiceActive_ ? accentBrush_.Get() : mutedTextBrush_.Get());
    } else if (deck == 1) {
        if (desktop.showSurfaceList) {
            d2dContext_->DrawLine(D2D1::Point2F(48, 128), D2D1::Point2F(76, 110), accentBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(48, 128), D2D1::Point2F(76, 146), accentBrush_.Get(), 3.0F);
            const auto count = std::min<size_t>(desktop.surfaces.size(), 3);
            for (size_t index = 0; index < count; ++index) {
                const float top = 166.0F + static_cast<float>(index) * 62.0F;
                d2dContext_->FillRectangle(D2D1::RectF(42, top, 722, top + 50), buttonBrush_.Get());
                drawText(desktop.surfaces[index].label, bodyFormat_.Get(),
                    D2D1::RectF(58, top + 12, 480, top + 42), textBrush_.Get());
                // Circular arrow returns captured content to the source picker.
                const auto reuseBrush = desktop.surfaces[index].reusable
                    ? accentBrush_.Get() : structureDimBrush_.Get();
                d2dContext_->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(529, top + 25), 14, 14), reuseBrush, 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(519, top + 11),
                    D2D1::Point2F(510, top + 10), reuseBrush, 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(519, top + 11),
                    D2D1::Point2F(517, top + 20), reuseBrush, 2.0F);
                // Crosshair brings a surface to eye line; X destroys only the VR surface.
                d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(599, top + 25), 13, 13),
                    accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(579, top + 25), D2D1::Point2F(619, top + 25),
                    accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(599, top + 5), D2D1::Point2F(599, top + 45),
                    accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(663, top + 14), D2D1::Point2F(685, top + 36),
                    mutedTextBrush_.Get(), 2.5F);
                d2dContext_->DrawLine(D2D1::Point2F(685, top + 14), D2D1::Point2F(663, top + 36),
                    mutedTextBrush_.Get(), 2.5F);
            }
            if (desktop.surfaces.empty()) {
                drawText(L"No open surfaces", bodyFormat_.Get(), D2D1::RectF(42, 196, 500, 232),
                    mutedTextBrush_.Get());
            }
        } else {
            const std::array<D2D1_RECT_F, 3> buttons{
                D2D1::RectF(70, 252, 230, 338),
                D2D1::RectF(304, 252, 464, 338),
                D2D1::RectF(538, 252, 698, 338),
            };
            for (const auto& rectangle : buttons) {
                d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(rectangle, 8, 8), buttonBrush_.Get());
            }
            // New surface: plus. Keyboard: key grid. Current surfaces: stacked windows.
            d2dContext_->DrawLine(D2D1::Point2F(150, 276), D2D1::Point2F(150, 314), accentBrush_.Get(), 4.0F);
            d2dContext_->DrawLine(D2D1::Point2F(131, 295), D2D1::Point2F(169, 295), accentBrush_.Get(), 4.0F);
            d2dContext_->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(338, 276, 430, 315), 4, 4), accentBrush_.Get(), 2.0F);
            for (int row = 0; row < 2; ++row) {
                for (int column = 0; column < 5; ++column) {
                    d2dContext_->FillRectangle(D2D1::RectF(348.0F + column * 15.0F,
                        284.0F + row * 11.0F, 357.0F + column * 15.0F,
                        291.0F + row * 11.0F), textBrush_.Get());
                }
            }
            d2dContext_->DrawRectangle(D2D1::RectF(588, 282, 648, 314), textBrush_.Get(), 2.0F);
            d2dContext_->DrawRectangle(D2D1::RectF(596, 274, 656, 306), mutedTextBrush_.Get(), 2.0F);
        }
    } else if (deck == 3) {
        if (!slimeAvailable_) {
            d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(92, 242), 9, 9),
                structureDimBrush_.Get(), 2.0F);
            drawText(L"SLIMEVR OFFLINE FOR THIS SESSION", bodyFormat_.Get(),
                D2D1::RectF(122, 224, 650, 262), mutedTextBrush_.Get());
        } else {
        constexpr const wchar_t* slots[] = {
            L"L ELB", L"R ELB", L"CHEST", L"HIP",
            L"L THI", L"R THI", L"L FOOT", L"R FOOT",
        };
        for (int index = 0; index < 8; ++index) {
            const auto column = index % 4;
            const auto row = index / 4;
            const float left = 42.0F + column * 174.0F;
            const float top = 212.0F + row * 48.0F;
            d2dContext_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(left, top, left + 150.0F, top + 38.0F), 8.0F, 8.0F),
                buttonBrush_.Get());
            drawText(slots[index], labelFormat_.Get(), D2D1::RectF(left + 14.0F, top + 12.0F,
                left + 132.0F, top + 33.0F), mutedTextBrush_.Get());
            drawText(rigSlots[index].empty() ? L"--" : rigSlots[index], labelFormat_.Get(),
                D2D1::RectF(left + 105.0F, top + 8.0F, left + 148.0F, top + 33.0F), textBrush_.Get());
        }
        d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(42, 320, 350, 366), 8, 8), buttonBrush_.Get());
        d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(414, 320, 722, 366), 8, 8), buttonBrush_.Get());
        drawText(L"HOLD  FULL RESET", labelFormat_.Get(), D2D1::RectF(70, 330, 330, 358), textBrush_.Get());
        drawText(mountReady ? L"HOLD  BODY MOUNT" : L"BODY MOUNT / WAIT", labelFormat_.Get(),
            D2D1::RectF(438, 330, 700, 358), mountReady ? textBrush_.Get() : mutedTextBrush_.Get());
        }
    } else if (deck == 2) {
        const auto stateBrush = playspaceAdjusted_ ? accentBrush_.Get() : structureBrush_.Get();
        const auto controlBounds = D2D1::RectF(68, 154, 330, 346);
        d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(controlBounds, 18, 18),
            playspaceAdjusted_ ? activeFillBrush_.Get() : buttonBrush_.Get());
        d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(controlBounds, 18, 18),
            playspaceAdjusted_ ? accentBrush_.Get() : structureDimBrush_.Get(),
            playspaceAdjusted_ ? 2.0F : 1.0F);

        // Abstract origin-reset mark: zero-point diamond over an orbital floor datum.
        d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(199, 285), 88, 23),
            stateBrush, 2.5F);
        d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(199, 244), 75, 75),
            structureDimBrush_.Get(), 1.5F);
        d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(199, 244), 58, 58),
            structureDimBrush_.Get(), 1.0F);
        d2dContext_->DrawLine(D2D1::Point2F(199, 178), D2D1::Point2F(199, 215),
            stateBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(199, 257), D2D1::Point2F(199, 285),
            stateBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(166, 236), D2D1::Point2F(178, 236),
            stateBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(220, 236), D2D1::Point2F(232, 236),
            stateBrush, 2.5F);
        // Central default-origin diamond.
        d2dContext_->DrawLine(D2D1::Point2F(199, 215), D2D1::Point2F(220, 236),
            stateBrush, 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(220, 236), D2D1::Point2F(199, 257),
            stateBrush, 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(199, 257), D2D1::Point2F(178, 236),
            stateBrush, 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(178, 236), D2D1::Point2F(199, 215),
            stateBrush, 3.0F);
        d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(199, 236), 5, 5), stateBrush);
        // Reset arrow head rides the outer orbit without adding another label.
        d2dContext_->DrawLine(D2D1::Point2F(257, 194), D2D1::Point2F(277, 197),
            stateBrush, 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(277, 197), D2D1::Point2F(270, 216),
            stateBrush, 3.0F);

        drawText(L"SESSION", labelFormat_.Get(), D2D1::RectF(390, 178, 680, 210),
            mutedTextBrush_.Get());
        drawText(playspaceAdjusted_ ? L"ADJUSTED" : L"BASELINE", titleFormat_.Get(),
            D2D1::RectF(390, 214, 710, 266),
            playspaceAdjusted_ ? accentBrush_.Get() : textBrush_.Get());
        d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(405, 298), 7, 7),
            stateBrush);
        drawText(playspaceAdjusted_ ? L"HOLD ICON TO RESTORE" : L"NO SESSION OFFSET",
            labelFormat_.Get(), D2D1::RectF(426, 285, 720, 326), mutedTextBrush_.Get());
    } else if (deck == 5) {
        const auto stateBrush = commsActive_ ? accentBrush_.Get() : structureBrush_.Get();
        drawText(L"COMMS", labelFormat_.Get(), D2D1::RectF(42, 108, 260, 140),
            accentBrush_.Get());
        drawText(commsTranscript_.empty() ? L"Voice transcription will appear here"
                : commsTranscript_, bodyFormat_.Get(), D2D1::RectF(42, 150, 726, 202),
            commsTranscript_.empty() ? mutedTextBrush_.Get() : textBrush_.Get());

        const auto micCenter = D2D1::Point2F(270, 280);
        d2dContext_->FillEllipse(D2D1::Ellipse(micCenter, 58, 58),
            commsActive_ ? activeFillBrush_.Get() : buttonBrush_.Get());
        d2dContext_->DrawEllipse(D2D1::Ellipse(micCenter, 58, 58), stateBrush,
            commsActive_ ? 3.0F : 2.0F);
        d2dContext_->DrawEllipse(D2D1::Ellipse(micCenter, 67, 67),
            structureDimBrush_.Get(), 1.0F);
        d2dContext_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(253, 246, 287, 286), 17, 17),
            stateBrush, 4.0F);
        d2dContext_->DrawLine(D2D1::Point2F(244, 274), D2D1::Point2F(244, 286),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(244, 286), D2D1::Point2F(255, 297),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(255, 297), D2D1::Point2F(285, 297),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(285, 297), D2D1::Point2F(296, 286),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(296, 286), D2D1::Point2F(296, 274),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(270, 298), D2D1::Point2F(270, 313),
            stateBrush, 3.5F);

        const auto clearCenter = D2D1::Point2F(560, 280);
        d2dContext_->FillEllipse(D2D1::Ellipse(clearCenter, 45, 45), buttonBrush_.Get());
        d2dContext_->DrawEllipse(D2D1::Ellipse(clearCenter, 45, 45), structureBrush_.Get(), 2.0F);
        d2dContext_->DrawEllipse(D2D1::Ellipse(clearCenter, 53, 53), structureDimBrush_.Get(), 1.0F);
        // Empty chatbox pulse: an erased speech cell, kept icon-only.
        d2dContext_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(538, 258, 582, 290), 7, 7),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(548, 290), D2D1::Point2F(542, 301),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(542, 301), D2D1::Point2F(558, 291),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(536, 304), D2D1::Point2F(584, 256),
            accentBrush_.Get(), 3.5F);

        d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(49, 355), 5, 5), stateBrush);
        drawText(commsStatus_, labelFormat_.Get(), D2D1::RectF(64, 344, 724, 374),
            commsActive_ ? accentBrush_.Get() : mutedTextBrush_.Get());
    } else {
        drawText(L"VOICE OUTPUT", labelFormat_.Get(), D2D1::RectF(42, 108, 300, 140),
            accentBrush_.Get());
        drawText(ttsMuted_ ? L"MUTED" : (std::to_wstring(ttsVolumePercent_) + L"%"),
            titleFormat_.Get(), D2D1::RectF(42, 150, 280, 202),
            ttsMuted_ ? mutedTextBrush_.Get() : textBrush_.Get());

        // Thin segmented level meter remains readable without becoming a luminous bar.
        for (int index = 0; index < 10; ++index) {
            const float left = 286.0F + static_cast<float>(index) * 39.0F;
            d2dContext_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(left, 164, left + 27, 181), 4, 4),
                !ttsMuted_ && index * 10 < ttsVolumePercent_
                    ? accentBrush_.Get() : structureDimBrush_.Get());
        }

        const std::array<D2D1_POINT_2F, 3> centers{
            D2D1::Point2F(176, 300), D2D1::Point2F(384, 300), D2D1::Point2F(592, 300)};
        for (size_t index = 0; index < centers.size(); ++index) {
            d2dContext_->FillEllipse(D2D1::Ellipse(centers[index], 40, 40),
                index == 1 && ttsMuted_ ? activeFillBrush_.Get() : buttonBrush_.Get());
            d2dContext_->DrawEllipse(D2D1::Ellipse(centers[index], 40, 40),
                index == 1 && ttsMuted_ ? accentBrush_.Get() : structureBrush_.Get(), 2.0F);
            d2dContext_->DrawEllipse(D2D1::Ellipse(centers[index], 47, 47),
                structureDimBrush_.Get(), 1.0F);
        }
        // Minus and plus.
        d2dContext_->DrawLine(D2D1::Point2F(158, 300), D2D1::Point2F(194, 300),
            accentBrush_.Get(), 4.0F);
        d2dContext_->DrawLine(D2D1::Point2F(574, 300), D2D1::Point2F(610, 300),
            accentBrush_.Get(), 4.0F);
        d2dContext_->DrawLine(D2D1::Point2F(592, 282), D2D1::Point2F(592, 318),
            accentBrush_.Get(), 4.0F);
        // Speaker/mute glyph.
        d2dContext_->DrawLine(D2D1::Point2F(365, 292), D2D1::Point2F(375, 292),
            accentBrush_.Get(), 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(375, 292), D2D1::Point2F(389, 281),
            accentBrush_.Get(), 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(389, 281), D2D1::Point2F(389, 319),
            accentBrush_.Get(), 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(389, 319), D2D1::Point2F(375, 308),
            accentBrush_.Get(), 3.0F);
        d2dContext_->DrawLine(D2D1::Point2F(375, 308), D2D1::Point2F(365, 308),
            accentBrush_.Get(), 3.0F);
        if (ttsMuted_) {
            d2dContext_->DrawLine(D2D1::Point2F(399, 287), D2D1::Point2F(417, 313),
                accentBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(417, 287), D2D1::Point2F(399, 313),
                accentBrush_.Get(), 3.0F);
        } else {
            d2dContext_->DrawLine(D2D1::Point2F(400, 289), D2D1::Point2F(408, 296),
                accentBrush_.Get(), 2.5F);
            d2dContext_->DrawLine(D2D1::Point2F(408, 296), D2D1::Point2F(408, 304),
                accentBrush_.Get(), 2.5F);
            d2dContext_->DrawLine(D2D1::Point2F(408, 304), D2D1::Point2F(400, 311),
                accentBrush_.Get(), 2.5F);
        }
    }
    if (FAILED(d2dContext_->EndDraw())) {
        return false;
    }
    context_->Flush();
    return true;
}

vr::Texture_t OverlayRenderer::Texture() const {
    return {
        sharedTextureHandle_,
        vr::TextureType_DXGISharedHandle,
        vr::ColorSpace_Auto,
    };
}

ID3D11Device* OverlayRenderer::Device() const {
    return device_.Get();
}

void OverlayRenderer::SetPlayspaceAdjusted(bool adjusted) {
    playspaceAdjusted_ = adjusted;
}

void OverlayRenderer::SetSlimeAvailable(bool available) {
    slimeAvailable_ = available;
}

void OverlayRenderer::SetMusicVoiceStatus(const std::wstring& status, bool active) {
    musicVoiceStatus_ = status;
    musicVoiceActive_ = active;
}

void OverlayRenderer::SetCommsStatus(const std::wstring& status,
                                     const std::wstring& transcript, bool active) {
    commsStatus_ = status;
    commsTranscript_ = transcript;
    commsActive_ = active;
}

void OverlayRenderer::SetTtsSettings(int volumePercent, bool muted) {
    ttsVolumePercent_ = (std::max)(0, (std::min)(100, volumePercent));
    ttsMuted_ = muted;
}

}  // namespace interfayce
