#include "overlay_renderer.h"

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>

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

    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.025F, 0.045F, 0.070F, 0.90F), &glassBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.045F, 0.075F, 0.105F, 0.96F), &stripBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.88F, 0.94F, 0.98F, 1.0F), &textBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.47F, 0.60F, 0.70F, 1.0F), &mutedTextBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.18F, 0.85F, 0.95F, 1.0F), &accentBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.09F, 0.17F, 0.23F, 0.96F), &buttonBrush_);

    return Render(deck, musicLine, musicArtPath, rigLine, rigSlots, mountReady, desktop);
}

bool OverlayRenderer::Render(int deck, const std::wstring& musicLine, const std::wstring& musicArtPath,
                             const std::wstring& rigLine, const std::array<std::wstring, 8>& rigSlots,
                             bool mountReady, const DesktopPanelState& desktop) {

    const auto drawText = [&](std::wstring_view text, IDWriteTextFormat* format, const D2D1_RECT_F rect,
                              ID2D1Brush* brush) {
        d2dContext_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rect, brush);
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
    d2dContext_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(6.0F, 6.0F, 762.0F, 378.0F), 18.0F, 18.0F), glassBrush_.Get());
    d2dContext_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(12.0F, 12.0F, 756.0F, 82.0F), 12.0F, 12.0F), stripBrush_.Get());
    drawText(L"MUSIC", labelFormat_.Get(), D2D1::RectF(36.0F, 35.0F, 145.0F, 70.0F), deck == 0 ? textBrush_.Get() : mutedTextBrush_.Get());
    drawText(L"DESKTOP", labelFormat_.Get(), D2D1::RectF(172.0F, 35.0F, 305.0F, 70.0F), deck == 1 ? textBrush_.Get() : mutedTextBrush_.Get());
    drawText(L"PLAYSPACE", labelFormat_.Get(), D2D1::RectF(332.0F, 35.0F, 500.0F, 70.0F), deck == 2 ? textBrush_.Get() : mutedTextBrush_.Get());
    drawText(L"RIG", labelFormat_.Get(), D2D1::RectF(595.0F, 35.0F, 670.0F, 70.0F), deck == 3 ? textBrush_.Get() : mutedTextBrush_.Get());
    d2dContext_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(deck == 0 ? 34.0F : deck == 1 ? 170.0F : deck == 2 ? 330.0F : 593.0F, 72.0F,
            deck == 0 ? 145.0F : deck == 1 ? 305.0F : deck == 2 ? 478.0F : 670.0F, 76.0F), 2.0F, 2.0F), accentBrush_.Get());

    drawText(deck == 0 ? L"MUSIC" : deck == 1 ? L"DESKTOP" : deck == 2 ? L"PLAYSPACE" : L"RIG", labelFormat_.Get(), D2D1::RectF(42.0F, 116.0F, 300.0F, 148.0F), accentBrush_.Get());
    drawText(deck == 0 ? (musicLine.empty() ? L"No active track" : musicLine) : deck == 1 ? musicLine : deck == 2 ? L"Session-safe movement" : (rigLine.empty() ? L"Controllers unavailable" : rigLine), titleFormat_.Get(), D2D1::RectF(42.0F, 154.0F, deck == 0 ? 540.0F : 690.0F, 202.0F), textBrush_.Get());
    if (albumArt) d2dContext_->DrawBitmap(albumArt.Get(), D2D1::RectF(570.0F, 108.0F, 720.0F, 258.0F));
    if (deck == 0) {
        for (const auto& rect : {D2D1::RectF(70, 272, 210, 338), D2D1::RectF(314, 272, 454, 338), D2D1::RectF(558, 272, 698, 338)}) {
            d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(rect, 10.0F, 10.0F), buttonBrush_.Get());
        }
        drawText(L"PREV", labelFormat_.Get(), D2D1::RectF(105, 292, 190, 324), textBrush_.Get());
        drawText(L"PLAY", labelFormat_.Get(), D2D1::RectF(351, 292, 435, 324), textBrush_.Get());
        drawText(L"NEXT", labelFormat_.Get(), D2D1::RectF(592, 292, 680, 324), textBrush_.Get());
    } else if (deck == 1) {
        if (desktop.showSurfaceList) {
            d2dContext_->DrawLine(D2D1::Point2F(48, 128), D2D1::Point2F(76, 110), accentBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(48, 128), D2D1::Point2F(76, 146), accentBrush_.Get(), 3.0F);
            const auto count = std::min<size_t>(desktop.surfaces.size(), 3);
            for (size_t index = 0; index < count; ++index) {
                const float top = 166.0F + static_cast<float>(index) * 62.0F;
                d2dContext_->FillRectangle(D2D1::RectF(42, top, 722, top + 50), buttonBrush_.Get());
                drawText(desktop.surfaces[index].label, bodyFormat_.Get(),
                    D2D1::RectF(58, top + 12, 500, top + 42), textBrush_.Get());
                // Crosshair icon brings a surface to eye line; X destroys only the VR surface.
                d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(570, top + 25), 13, 13),
                    accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(548, top + 25), D2D1::Point2F(592, top + 25),
                    accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(570, top + 3), D2D1::Point2F(570, top + 47),
                    accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(660, top + 14), D2D1::Point2F(682, top + 36),
                    mutedTextBrush_.Get(), 2.5F);
                d2dContext_->DrawLine(D2D1::Point2F(682, top + 14), D2D1::Point2F(660, top + 36),
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
    } else {
        drawText(L"Release leaves the session offset in place. Closing Interfayce restores your baseline.",
            bodyFormat_.Get(), D2D1::RectF(42.0F, 213.0F, 712.0F, 248.0F), mutedTextBrush_.Get());
        d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(42.0F, 276.0F, 420.0F, 338.0F), 10.0F, 10.0F), buttonBrush_.Get());
        drawText(L"HOLD  RESTORE SESSION BASELINE", labelFormat_.Get(),
            D2D1::RectF(66.0F, 294.0F, 400.0F, 326.0F), textBrush_.Get());
    }
    if (deck == 2) {
        drawText(L"SESSION ONLY / NO DISK COMMIT", labelFormat_.Get(),
            D2D1::RectF(450.0F, 296.0F, 730.0F, 326.0F), accentBrush_.Get());
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

}  // namespace interfayce
