#include "overlay_renderer.h"

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>

namespace interfayce {

enum class HoloGlyph : UINT32 {
    Music, Comms, Desktop, Playspace, Rig, Settings,
    Previous, Play, Pause, Next, Broadcast, VoiceMic,
    NewSurface, Keyboard, SurfaceStack, Back, BringAll, ReturnPicker,
    Lock, Unlock, BringView, Close, Favorite,
    CommsMic, ClearChat, PlayspaceRestore, RigReset, RigMount, DesktopSettings,
    VolumeDown, VolumeUp, Speaker, Mute, BroadcastGainDown, BroadcastGainUp,
    Shutdown, Copy, Paste,
    Assistant,
};

constexpr UINT32 kHoloAtlasColumns = 8;
constexpr float kHoloAtlasCell = 72.0F;

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
            DWRITE_FONT_STRETCH_NORMAL, 20.0F, L"en-us", &bodyFormat_))
        || FAILED(writeFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 20.0F, L"en-us", &bodyWrapFormat_))) {
        return false;
    }
    labelFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    bodyFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    bodyWrapFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    const DWRITE_TRIMMING characterTrim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    if (FAILED(writeFactory->CreateEllipsisTrimmingSign(labelFormat_.Get(), &labelEllipsis_))
        || FAILED(writeFactory->CreateEllipsisTrimmingSign(titleFormat_.Get(), &titleEllipsis_))
        || FAILED(writeFactory->CreateEllipsisTrimmingSign(bodyFormat_.Get(), &bodyEllipsis_))) {
        return false;
    }
    labelFormat_->SetTrimming(&characterTrim, labelEllipsis_.Get());
    titleFormat_->SetTrimming(&characterTrim, titleEllipsis_.Get());
    bodyFormat_->SetTrimming(&characterTrim, bodyEllipsis_.Get());

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
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(1.0F, 0.64F, 0.16F, 1.0F), &warningBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(1.0F, 0.20F, 0.32F, 1.0F), &criticalBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.34F, 0.18F, 0.78F, 0.34F), &bodyFillBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.12F, 0.82F, 0.94F, 0.14F), &scanFillBrush_);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0.88F, 0.91F, 1.0F, 0.72F),
        &holoSpecularBrush_);
    {
        const D2D1_GRADIENT_STOP stops[]{
            {0.0F, D2D1::ColorF(0.32F, 0.23F, 0.65F, 0.94F)},
            {0.18F, D2D1::ColorF(0.10F, 0.08F, 0.24F, 0.98F)},
            {0.68F, D2D1::ColorF(0.025F, 0.036F, 0.085F, 0.99F)},
            {1.0F, D2D1::ColorF(0.025F, 0.20F, 0.27F, 0.98F)},
        };
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
        if (SUCCEEDED(d2dContext_->CreateGradientStopCollection(
                stops, 4, &collection))) {
            d2dContext_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), collection.Get(), &holoGlassBrush_);
        }
    }
    {
        const D2D1_GRADIENT_STOP stops[]{
            {0.0F, D2D1::ColorF(0.78F, 0.66F, 1.0F, 1.0F)},
            {0.42F, D2D1::ColorF(0.50F, 0.34F, 0.96F, 1.0F)},
            {1.0F, D2D1::ColorF(0.28F, 0.15F, 0.67F, 1.0F)},
        };
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
        if (SUCCEEDED(d2dContext_->CreateGradientStopCollection(
                stops, 3, &collection))) {
            d2dContext_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), collection.Get(), &holoGlyphBrush_);
        }
    }

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
    Microsoft::WRL::ComPtr<ID2D1Factory> geometryFactory;
    d2dContext_->GetFactory(&geometryFactory);
    const auto makePolygon = [&](std::initializer_list<D2D1_POINT_2F> points) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
        if (!geometryFactory || points.size() < 3
            || FAILED(geometryFactory->CreatePathGeometry(&geometry))) {
            return geometry;
        }
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geometry->Open(&sink))) return Microsoft::WRL::ComPtr<ID2D1PathGeometry>{};
        auto point = points.begin();
        sink->BeginFigure(*point, D2D1_FIGURE_BEGIN_FILLED);
        for (++point; point != points.end(); ++point) sink->AddLine(*point);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return Microsoft::WRL::ComPtr<ID2D1PathGeometry>{};
        return geometry;
    };
    const auto fillPolygon = [&](std::initializer_list<D2D1_POINT_2F> points, ID2D1Brush* brush) {
        const auto geometry = makePolygon(points);
        if (geometry) d2dContext_->FillGeometry(geometry.Get(), brush);
    };
    const auto drawArc = [&](D2D1_POINT_2F center, float radius, float startAngle,
                             float endAngle, ID2D1Brush* brush, float stroke) {
        if (!geometryFactory) return;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geometryFactory->CreatePathGeometry(&geometry))
            || FAILED(geometry->Open(&sink))) return;
        const auto start = D2D1::Point2F(center.x + std::cos(startAngle) * radius,
            center.y + std::sin(startAngle) * radius);
        const auto end = D2D1::Point2F(center.x + std::cos(endAngle) * radius,
            center.y + std::sin(endAngle) * radius);
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(radius, radius), 0.0F,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        if (SUCCEEDED(sink->Close())) d2dContext_->DrawGeometry(geometry.Get(), brush, stroke);
    };
    const auto facetGeometry = [&](D2D1_POINT_2F center, float halfWidth, float halfHeight,
                                   float inset = 0.0F) {
        const float left = center.x - halfWidth + inset;
        const float right = center.x + halfWidth - inset;
        const float top = center.y - halfHeight + inset;
        const float bottom = center.y + halfHeight - inset;
        const float cut = (std::min)(halfWidth, halfHeight) * 0.30F;
        return makePolygon({
            D2D1::Point2F(left, top + cut), D2D1::Point2F(left + cut, top),
            D2D1::Point2F(right - cut, top), D2D1::Point2F(right, top + cut),
            D2D1::Point2F(right, bottom - cut * 0.62F),
            D2D1::Point2F(right - cut * 0.62F, bottom),
            D2D1::Point2F(left + cut * 0.62F, bottom),
            D2D1::Point2F(left, bottom - cut * 0.62F)});
    };
    const auto drawHoloButton = [&](D2D1_POINT_2F center, float halfWidth, float halfHeight,
                                    bool active = false, bool enabled = true,
                                    bool destructive = false) {
        const auto outer = facetGeometry(center, halfWidth, halfHeight);
        const auto inner = facetGeometry(center, halfWidth, halfHeight, 4.0F);
        if (!outer || !inner) return;
        if (holoGlassBrush_) {
            holoGlassBrush_->SetStartPoint(D2D1::Point2F(center.x, center.y - halfHeight));
            holoGlassBrush_->SetEndPoint(D2D1::Point2F(center.x, center.y + halfHeight));
        }
        if (holoGlyphBrush_) {
            holoGlyphBrush_->SetStartPoint(D2D1::Point2F(center.x, center.y - halfHeight * 0.66F));
            holoGlyphBrush_->SetEndPoint(D2D1::Point2F(center.x, center.y + halfHeight * 0.66F));
        }
        auto* fill = holoGlassBrush_ ? static_cast<ID2D1Brush*>(holoGlassBrush_.Get())
            : static_cast<ID2D1Brush*>(buttonBrush_.Get());
        auto* frame = !enabled ? structureDimBrush_.Get()
            : destructive ? criticalBrush_.Get()
            : active ? accentBrush_.Get() : structureBrush_.Get();
        d2dContext_->FillGeometry(outer.Get(), fill);
        if (active) d2dContext_->FillGeometry(inner.Get(), activeFillBrush_.Get());
        d2dContext_->FillGeometry(inner.Get(), scanFillBrush_.Get());
        d2dContext_->DrawGeometry(outer.Get(), frame, active ? 2.2F : 1.4F);
        d2dContext_->DrawGeometry(inner.Get(), structureDimBrush_.Get(), 0.8F);
        if (enabled && holoSpecularBrush_) {
            const float cut = (std::min)(halfWidth, halfHeight) * 0.30F;
            d2dContext_->DrawLine(D2D1::Point2F(center.x - halfWidth + cut, center.y - halfHeight),
                D2D1::Point2F(center.x + halfWidth * 0.10F, center.y - halfHeight),
                holoSpecularBrush_.Get(), 1.2F);
            d2dContext_->DrawLine(D2D1::Point2F(center.x - halfWidth,
                    center.y - halfHeight + cut),
                D2D1::Point2F(center.x - halfWidth + cut, center.y - halfHeight),
                holoSpecularBrush_.Get(), 1.0F);
        }
        const float projectorY = center.y + halfHeight - 4.0F;
        d2dContext_->DrawLine(D2D1::Point2F(center.x - halfWidth * 0.34F, projectorY),
            D2D1::Point2F(center.x + halfWidth * 0.34F, projectorY),
            enabled ? accentBrush_.Get() : structureDimBrush_.Get(), active ? 2.5F : 1.2F);
        d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x, projectorY),
            active ? 3.2F : 2.2F, active ? 3.2F : 2.2F),
            enabled ? accentBrush_.Get() : structureDimBrush_.Get());
    };
    const auto drawHoloAsset = [&](HoloGlyph glyph, D2D1_POINT_2F center,
                                   float halfWidth, float halfHeight,
                                   bool active = false, bool enabled = true) {
        if (!holoGlyphAtlas_) return false;
        const auto index = static_cast<UINT32>(glyph);
        const auto column = index % kHoloAtlasColumns;
        const auto row = index / kHoloAtlasColumns;
        const auto source = D2D1::RectF(column * kHoloAtlasCell, row * kHoloAtlasCell,
            (column + 1) * kHoloAtlasCell, (row + 1) * kHoloAtlasCell);
        const auto destination = D2D1::RectF(center.x - halfWidth, center.y - halfHeight,
            center.x + halfWidth, center.y + halfHeight);
        d2dContext_->DrawBitmap(holoGlyphAtlas_.Get(), destination, enabled ? 1.0F : 0.34F,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
        if (active) {
            const auto outline = facetGeometry(center, halfWidth * 0.92F, halfHeight * 0.92F);
            if (outline) d2dContext_->DrawGeometry(outline.Get(), accentBrush_.Get(), 2.0F);
            d2dContext_->DrawLine(D2D1::Point2F(center.x - halfWidth * 0.32F,
                    center.y + halfHeight * 0.76F),
                D2D1::Point2F(center.x + halfWidth * 0.32F,
                    center.y + halfHeight * 0.76F), accentBrush_.Get(), 2.4F);
        }
        return true;
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
    if (deck == 3 && !rigBodyArt_ && !rigBodyArtPath_.empty()) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wicFactory)))
            && SUCCEEDED(wicFactory->CreateDecoderFromFilename(rigBodyArtPath_.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))
            && SUCCEEDED(decoder->GetFrame(0, &frame))
            && SUCCEEDED(wicFactory->CreateFormatConverter(&converter))
            && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            d2dContext_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &rigBodyArt_);
        }
    }
    if (deck == 2 && !playspaceResetArt_ && !playspaceResetArtPath_.empty()) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wicFactory)))
            && SUCCEEDED(wicFactory->CreateDecoderFromFilename(playspaceResetArtPath_.c_str(),
                nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))
            && SUCCEEDED(decoder->GetFrame(0, &frame))
            && SUCCEEDED(wicFactory->CreateFormatConverter(&converter))
            && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            d2dContext_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &playspaceResetArt_);
        }
    }
    if (!holoGlyphAtlas_ && !holoGlyphAtlasPath_.empty()) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wicFactory)))
            && SUCCEEDED(wicFactory->CreateDecoderFromFilename(holoGlyphAtlasPath_.c_str(),
                nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))
            && SUCCEEDED(decoder->GetFrame(0, &frame))
            && SUCCEEDED(wicFactory->CreateFormatConverter(&converter))
            && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            d2dContext_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &holoGlyphAtlas_);
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

    const std::array<D2D1_RECT_F, 6> tabs{
        D2D1::RectF(24, 22, 112, 76), D2D1::RectF(118, 22, 206, 76),
        D2D1::RectF(212, 22, 300, 76), D2D1::RectF(306, 22, 394, 76),
        D2D1::RectF(400, 22, 488, 76), D2D1::RectF(494, 22, 582, 76)};
    const std::array<const wchar_t*, 6> tabLabels{
        L"MUSIC", L"COMMS", L"ASK", L"DESK", L"SPACE", L"RIG"};
    const std::array<HoloGlyph, 6> tabGlyphs{
        HoloGlyph::Music, HoloGlyph::Comms, HoloGlyph::Assistant,
        HoloGlyph::Desktop, HoloGlyph::Playspace, HoloGlyph::Rig};
    const std::array<int, 6> tabDecks{0, 5, 6, 1, 2, 3};
    for (size_t index = 0; index < tabs.size(); ++index) {
        const bool selected = deck == tabDecks[index];
        if (selected) {
            d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(tabs[index], 9, 9), activeFillBrush_.Get());
            d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(tabs[index], 9, 9), accentBrush_.Get(), 2.0F);
        } else {
            d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(tabs[index], 9, 9),
                structureDimBrush_.Get(), 1.0F);
        }
        if (holoGlyphAtlas_) {
            drawHoloAsset(tabGlyphs[index],
                D2D1::Point2F(tabs[index].left + 15, (tabs[index].top + tabs[index].bottom) * 0.5F),
                15, 15, selected);
            drawText(tabLabels[index], labelFormat_.Get(),
                D2D1::RectF(tabs[index].left + 31, tabs[index].top + 15,
                    tabs[index].right - 5, tabs[index].bottom - 6),
                selected ? textBrush_.Get() : mutedTextBrush_.Get());
        } else {
            drawText(tabLabels[index], labelFormat_.Get(),
                D2D1::RectF(tabs[index].left + 12, tabs[index].top + 15,
                    tabs[index].right - 8, tabs[index].bottom - 6),
                selected ? textBrush_.Get() : mutedTextBrush_.Get());
        }
    }

    // Compact orbital gear; it opens settings without spending another text tab.
    const auto gearBrush = deck == 4 ? accentBrush_.Get() : mutedTextBrush_.Get();
    if (lowestBatteryPercent_ >= 0) {
        auto* batteryBrush = lowestBatteryPercent_ <= 10 ? criticalBrush_.Get()
            : lowestBatteryPercent_ <= 20 ? warningBrush_.Get() : structureBrush_.Get();
        d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(600, 49), 4, 4), batteryBrush);
        drawText(std::to_wstring(lowestBatteryPercent_) + L"%", labelFormat_.Get(),
            D2D1::RectF(609, 36, 664, 66), batteryBrush);
    }
    const auto gearCenter = D2D1::Point2F(730, 49);
    if (holoGlyphAtlas_) {
        drawHoloAsset(HoloGlyph::Settings, gearCenter, 20, 20, deck == 4);
    } else {
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
    }

    if (deck == 0) {
        drawText(musicLine.empty() ? L"No active track" : musicLine,
            titleFormat_.Get(), D2D1::RectF(42.0F, 116.0F,
                500.0F, 170.0F), textBrush_.Get());
    }
    if (albumArt) d2dContext_->DrawBitmap(albumArt.Get(), D2D1::RectF(570.0F, 108.0F, 720.0F, 258.0F));
    if (deck == 0) {
        // Orbital transport controls: no text boxes, only clear geometric controls.
        const std::array<D2D1_POINT_2F, 3> centers{
            D2D1::Point2F(140, 287), D2D1::Point2F(384, 287), D2D1::Point2F(628, 287)};
        const auto broadcastCenter = D2D1::Point2F(520, 145);
        const auto micCenter = D2D1::Point2F(520, 225);
        if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::Previous, centers[0], 39, 39);
            drawHoloAsset(musicPlaying_ ? HoloGlyph::Pause : HoloGlyph::Play,
                centers[1], 48, 48, true);
            drawHoloAsset(HoloGlyph::Next, centers[2], 39, 39);
            drawHoloAsset(HoloGlyph::Broadcast, broadcastCenter, 31, 31,
                musicBroadcastActive_);
            drawHoloAsset(HoloGlyph::VoiceMic, micCenter, 31, 31, musicVoiceActive_);
        } else {
        for (size_t index = 0; index < centers.size(); ++index) {
            const float extent = index == 1 ? 46.0F : 37.0F;
            drawHoloButton(centers[index], extent, extent, index == 1);
        }
        auto* mediaGlyphBrush = holoGlyphBrush_
            ? static_cast<ID2D1Brush*>(holoGlyphBrush_.Get())
            : static_cast<ID2D1Brush*>(structureBrush_.Get());
        // Previous: |<
        d2dContext_->FillRectangle(D2D1::RectF(121, 270, 127, 304), mediaGlyphBrush);
        fillPolygon({D2D1::Point2F(151, 271), D2D1::Point2F(128, 287),
            D2D1::Point2F(151, 303)}, mediaGlyphBrush);
        // The center glyph describes the action: pause while playing, play while paused.
        if (musicPlaying_) {
            d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(370, 265, 379, 309), 3, 3), structureBrush_.Get());
            d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(389, 265, 398, 309), 3, 3), structureBrush_.Get());
        } else {
            fillPolygon({D2D1::Point2F(370, 263), D2D1::Point2F(408, 287),
                D2D1::Point2F(370, 311)}, mediaGlyphBrush);
        }
        // Next: >|
        d2dContext_->FillRectangle(D2D1::RectF(641, 270, 647, 304), mediaGlyphBrush);
        fillPolygon({D2D1::Point2F(617, 271), D2D1::Point2F(640, 287),
            D2D1::Point2F(617, 303)}, mediaGlyphBrush);
        // Broadcast gate: a compact orbital transmitter above the voice mic.
        drawHoloButton(broadcastCenter, 30, 30, musicBroadcastActive_);
        const auto broadcastBrush = musicBroadcastActive_
            ? static_cast<ID2D1Brush*>(accentBrush_.Get()) : mediaGlyphBrush;
        d2dContext_->FillEllipse(D2D1::Ellipse(broadcastCenter, 4, 4), broadcastBrush);
        d2dContext_->DrawEllipse(D2D1::Ellipse(broadcastCenter, 11, 11),
            broadcastBrush, 2.0F);
        d2dContext_->DrawEllipse(D2D1::Ellipse(broadcastCenter, 19, 19),
            broadcastBrush, 1.5F);
        // Voice command microphone, kept separate from the three transport controls.
        drawHoloButton(micCenter, 30, 30, musicVoiceActive_);
        d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(512, 209, 528, 229), 8, 8),
            musicVoiceActive_ ? static_cast<ID2D1Brush*>(accentBrush_.Get()) : mediaGlyphBrush);
        auto* voiceGlyphBrush = musicVoiceActive_
            ? static_cast<ID2D1Brush*>(accentBrush_.Get()) : mediaGlyphBrush;
        d2dContext_->DrawLine(D2D1::Point2F(508, 223), D2D1::Point2F(508, 228),
            voiceGlyphBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(508, 228), D2D1::Point2F(513, 233),
            voiceGlyphBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(513, 233), D2D1::Point2F(527, 233),
            voiceGlyphBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(527, 233), D2D1::Point2F(532, 228),
            voiceGlyphBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(532, 228), D2D1::Point2F(532, 223),
            voiceGlyphBrush, 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(520, 234), D2D1::Point2F(520, 240),
            voiceGlyphBrush, 2.5F);
        }
        d2dContext_->DrawLine(D2D1::Point2F(42, 343), D2D1::Point2F(726, 343),
            structureDimBrush_.Get(), 1.0F);
        drawText(musicVoiceStatus_, labelFormat_.Get(), D2D1::RectF(48, 350, 430, 374),
            musicVoiceActive_ ? accentBrush_.Get() : mutedTextBrush_.Get());
        drawText(musicBroadcastStatus_, labelFormat_.Get(), D2D1::RectF(450, 350, 606, 374),
            musicBroadcastActive_ ? accentBrush_.Get() : mutedTextBrush_.Get());
    } else if (deck == 1) {
        if (desktop.showSurfaceList) {
            const auto bringCenter = D2D1::Point2F(684, 128);
            if (holoGlyphAtlas_) {
                drawHoloAsset(HoloGlyph::Back, D2D1::Point2F(62, 128), 30, 30);
                drawHoloAsset(HoloGlyph::BringAll, bringCenter, 30, 30);
            } else {
            d2dContext_->DrawLine(D2D1::Point2F(48, 128), D2D1::Point2F(76, 110), accentBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(48, 128), D2D1::Point2F(76, 146), accentBrush_.Get(), 3.0F);
            // Bring-all shares the stacked-surface motif and adds inward recall chevrons.
            drawHoloButton(bringCenter, 29, 29);
            for (int layer = 0; layer < 3; ++layer) {
                const float inset = static_cast<float>(layer) * 4.0F;
                d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(
                    D2D1::RectF(669 + inset, 116 - inset, 694 + inset, 135 - inset), 3, 3),
                    layer == 2 ? accentBrush_.Get() : structureDimBrush_.Get(), 1.4F);
            }
            d2dContext_->DrawLine(D2D1::Point2F(657, 128), D2D1::Point2F(665, 122), accentBrush_.Get(), 2.0F);
            d2dContext_->DrawLine(D2D1::Point2F(657, 128), D2D1::Point2F(665, 134), accentBrush_.Get(), 2.0F);
            d2dContext_->DrawLine(D2D1::Point2F(711, 128), D2D1::Point2F(703, 122), accentBrush_.Get(), 2.0F);
            d2dContext_->DrawLine(D2D1::Point2F(711, 128), D2D1::Point2F(703, 134), accentBrush_.Get(), 2.0F);
            }
            const auto count = std::min<size_t>(desktop.surfaces.size(), 3);
            for (size_t index = 0; index < count; ++index) {
                const float top = 166.0F + static_cast<float>(index) * 62.0F;
                d2dContext_->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(42, top, 722, top + 50), 10, 10), buttonBrush_.Get());
                drawText(desktop.surfaces[index].label, bodyFormat_.Get(),
                    D2D1::RectF(58, top + 12, 410, top + 42), textBrush_.Get());
                for (float centerX : {450.0F, 520.0F, 590.0F, 674.0F}) {
                    if (!holoGlyphAtlas_) {
                        drawHoloButton(D2D1::Point2F(centerX, top + 25), 21, 21);
                    }
                }
                if (holoGlyphAtlas_) {
                    drawHoloAsset(HoloGlyph::ReturnPicker, D2D1::Point2F(450, top + 25),
                        22, 22, false, desktop.surfaces[index].reusable);
                    drawHoloAsset(desktop.surfaces[index].locked ? HoloGlyph::Lock : HoloGlyph::Unlock,
                        D2D1::Point2F(520, top + 25), 22, 22, desktop.surfaces[index].locked);
                    drawHoloAsset(HoloGlyph::BringView, D2D1::Point2F(590, top + 25), 22, 22);
                    drawHoloAsset(HoloGlyph::Close, D2D1::Point2F(674, top + 25), 22, 22);
                } else {
                // A solid window slab ejecting left reads as "return to picker" even in grayscale.
                const auto reuseBrush = desktop.surfaces[index].reusable
                    ? structureBrush_.Get() : structureDimBrush_.Get();
                d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
                    D2D1::RectF(442, top + 13, 462, top + 37), 3, 3), reuseBrush);
                fillPolygon({D2D1::Point2F(452, top + 18), D2D1::Point2F(440, top + 25),
                    D2D1::Point2F(452, top + 32)}, buttonBrush_.Get());
                d2dContext_->FillRectangle(D2D1::RectF(449, top + 22, 460, top + 28),
                    buttonBrush_.Get());
                // A closed/open padlock gates only movement and two-hand scaling.
                const float lockX = 520.0F;
                const float lockY = top + 25.0F;
                auto* lockBrush = desktop.surfaces[index].locked
                    ? accentBrush_.Get() : structureBrush_.Get();
                d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
                    D2D1::RectF(lockX - 12, lockY - 2, lockX + 12, lockY + 14), 3, 3),
                    lockBrush);
                if (desktop.surfaces[index].locked) {
                    drawArc(D2D1::Point2F(lockX, lockY - 2), 8.0F, 3.14159265F,
                        6.2831853F, lockBrush, 5.0F);
                } else {
                    d2dContext_->DrawLine(D2D1::Point2F(lockX - 8, lockY - 2),
                        D2D1::Point2F(lockX - 8, lockY - 9), lockBrush, 5.0F);
                    d2dContext_->DrawLine(D2D1::Point2F(lockX - 8, lockY - 9),
                        D2D1::Point2F(lockX + 4, lockY - 9), lockBrush, 5.0F);
                    d2dContext_->DrawLine(D2D1::Point2F(lockX + 4, lockY - 9),
                        D2D1::Point2F(lockX + 10, lockY - 5), lockBrush, 5.0F);
                }
                d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lockX, lockY + 5),
                    2.2F, 3.5F), buttonBrush_.Get());
                // A solid acquisition reticle brings a surface to eye line.
                const auto focusCenter = D2D1::Point2F(590, top + 25);
                d2dContext_->FillEllipse(D2D1::Ellipse(focusCenter, 8, 8), structureBrush_.Get());
                d2dContext_->FillEllipse(D2D1::Ellipse(focusCenter, 3, 3), accentBrush_.Get());
                for (const auto& line : std::array<std::pair<D2D1_POINT_2F, D2D1_POINT_2F>, 8>{{
                         {{574, top + 19}, {574, top + 12}}, {{574, top + 12}, {581, top + 12}},
                         {{606, top + 19}, {606, top + 12}}, {{606, top + 12}, {599, top + 12}},
                         {{574, top + 31}, {574, top + 38}}, {{574, top + 38}, {581, top + 38}},
                         {{606, top + 31}, {606, top + 38}}, {{606, top + 38}, {599, top + 38}},
                     }}) {
                    d2dContext_->DrawLine(line.first, line.second, structureBrush_.Get(), 3.0F);
                }
                // Broad filled X destroys only the VR surface.
                fillPolygon({D2D1::Point2F(663, top + 14), D2D1::Point2F(674, top + 22),
                    D2D1::Point2F(685, top + 14), D2D1::Point2F(688, top + 18),
                    D2D1::Point2F(678, top + 25), D2D1::Point2F(688, top + 33),
                    D2D1::Point2F(685, top + 37), D2D1::Point2F(674, top + 29),
                    D2D1::Point2F(663, top + 37), D2D1::Point2F(660, top + 33),
                    D2D1::Point2F(670, top + 25), D2D1::Point2F(660, top + 18)},
                    structureBrush_.Get());
                }
            }
            if (desktop.surfaces.empty()) {
                drawText(L"No open surfaces", bodyFormat_.Get(), D2D1::RectF(42, 196, 500, 232),
                    mutedTextBrush_.Get());
            }
        } else {
            constexpr std::array<float, 3> favoriteLeft{70.0F, 304.0F, 538.0F};
            for (size_t index = 0; index < desktop.favorites.size(); ++index) {
                if (desktop.favorites[index].empty()) continue;
                const auto bounds = D2D1::RectF(
                    favoriteLeft[index], 132.0F, favoriteLeft[index] + 160.0F, 200.0F);
                d2dContext_->FillRoundedRectangle(
                    D2D1::RoundedRect(bounds, 10, 10), buttonBrush_.Get());
                d2dContext_->DrawRoundedRectangle(
                    D2D1::RoundedRect(bounds, 10, 10), structureBrush_.Get(), 1.5F);
                // Compact launch aperture; the label carries application identity.
                const auto center = D2D1::Point2F(favoriteLeft[index] + 28.0F, 166.0F);
                if (holoGlyphAtlas_) {
                    drawHoloAsset(HoloGlyph::Favorite, center, 24, 24);
                } else {
                d2dContext_->DrawEllipse(D2D1::Ellipse(center, 12, 12), accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(center.x - 5, center.y + 5),
                    D2D1::Point2F(center.x + 7, center.y - 7), accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(center.x + 1, center.y - 7),
                    D2D1::Point2F(center.x + 7, center.y - 7), accentBrush_.Get(), 2.0F);
                d2dContext_->DrawLine(D2D1::Point2F(center.x + 7, center.y - 7),
                    D2D1::Point2F(center.x + 7, center.y - 1), accentBrush_.Get(), 2.0F);
                }
                drawText(desktop.favorites[index], labelFormat_.Get(),
                    D2D1::RectF(favoriteLeft[index] + 50.0F, 150.0F,
                        favoriteLeft[index] + 150.0F, 184.0F), textBrush_.Get());
            }
            const std::array<D2D1_POINT_2F, 3> centers{
                D2D1::Point2F(150, 278),
                D2D1::Point2F(384, 278),
                D2D1::Point2F(618, 278),
            };
            if (holoGlyphAtlas_) {
                drawHoloAsset(HoloGlyph::NewSurface, centers[0], 51, 51);
                drawHoloAsset(HoloGlyph::Keyboard, centers[1], 51, 51);
                drawHoloAsset(HoloGlyph::SurfaceStack, centers[2], 51, 51);
            } else {
            for (const auto center : centers) {
                drawHoloButton(center, 49, 49);
            }
            // New surface: plus. Keyboard: key grid. Current surfaces: stacked windows.
            d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(126, 255, 174, 300), 4, 4), structureBrush_.Get());
            d2dContext_->FillRectangle(D2D1::RectF(146, 263, 154, 292), buttonBrush_.Get());
            d2dContext_->FillRectangle(D2D1::RectF(136, 273, 164, 282), buttonBrush_.Get());
            fillPolygon({D2D1::Point2F(347, 270), D2D1::Point2F(355, 260),
                D2D1::Point2F(413, 260), D2D1::Point2F(421, 270),
                D2D1::Point2F(416, 295), D2D1::Point2F(352, 295)}, structureBrush_.Get());
            for (int row = 0; row < 2; ++row) {
                for (int column = 0; column < 5; ++column) {
                    d2dContext_->FillRectangle(D2D1::RectF(357.0F + column * 11.0F,
                        269.0F + row * 9.0F, 364.0F + column * 11.0F,
                        274.0F + row * 9.0F), buttonBrush_.Get());
                }
            }
            // Current surfaces: a compact stack plus a live count badge.
            for (int layer = 0; layer < 3; ++layer) {
                const float inset = static_cast<float>(layer) * 5.0F;
                d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(
                    D2D1::RectF(592 + inset, 267 - inset, 630 + inset, 290 - inset), 4, 4),
                    layer == 2 ? accentBrush_.Get() : structureDimBrush_.Get(), 1.8F);
            }
            }
            const auto countText = std::to_wstring(desktop.surfaces.size());
            d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(642, 291), 12, 12), activeFillBrush_.Get());
            d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(642, 291), 12, 12), accentBrush_.Get(), 1.5F);
            labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            drawText(countText, labelFormat_.Get(), D2D1::RectF(630, 279, 654, 303), textBrush_.Get());
            labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        d2dContext_->DrawLine(D2D1::Point2F(42, 343), D2D1::Point2F(726, 343),
            structureDimBrush_.Get(), 1.0F);
        drawText(musicLine, labelFormat_.Get(), D2D1::RectF(48, 350, 606, 374),
            mutedTextBrush_.Get());
    } else if (deck == 3) {
        if (!slimeAvailable_) {
            d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(92, 242), 9, 9),
                structureDimBrush_.Get(), 2.0F);
            drawText(L"SLIMEVR OFFLINE FOR THIS SESSION", bodyFormat_.Get(),
                D2D1::RectF(122, 224, 650, 262), mutedTextBrush_.Get());
        } else {
        // Orbital diagnostic figure: location carries the label, leaving only battery values.
        const auto center = D2D1::Point2F(384, 226);
        d2dContext_->DrawEllipse(D2D1::Ellipse(center, 105, 105), structureDimBrush_.Get(), 1.0F);
        d2dContext_->DrawEllipse(D2D1::Ellipse(center, 88, 88), structureDimBrush_.Get(), 0.8F);
        if (rigBodyArt_) {
            d2dContext_->DrawBitmap(rigBodyArt_.Get(), D2D1::RectF(319, 98, 449, 351), 0.92F,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, D2D1::RectF(160, 100, 864, 1470));
        } else {
        d2dContext_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(384, 242), 111, 12), scanFillBrush_.Get());
        d2dContext_->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(384, 242), 111, 12), accentBrush_.Get(), 1.0F);

        Microsoft::WRL::ComPtr<ID2D1Factory> rigFactory;
        d2dContext_->GetFactory(&rigFactory);
        const auto fillShell = [&](const auto& points) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(rigFactory->CreatePathGeometry(&geometry))
                || FAILED(geometry->Open(&sink))) return;
            sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if (SUCCEEDED(sink->Close())) {
                d2dContext_->FillGeometry(geometry.Get(), bodyFillBrush_.Get());
            }
        };
        // Faceted cranial shell and visor, closer to a diagnostic scan than a head icon.
        const std::array<D2D1_POINT_2F, 8> head{
            D2D1::Point2F(384, 125), D2D1::Point2F(399, 135),
            D2D1::Point2F(402, 153), D2D1::Point2F(394, 171),
            D2D1::Point2F(384, 177), D2D1::Point2F(374, 171),
            D2D1::Point2F(366, 153), D2D1::Point2F(369, 135)};
        fillShell(head);
        for (size_t index = 0; index < head.size(); ++index) {
            d2dContext_->DrawLine(head[index], head[(index + 1) % head.size()],
                structureBrush_.Get(), 1.8F);
        }
        d2dContext_->DrawLine(D2D1::Point2F(369, 147), D2D1::Point2F(399, 147),
            accentBrush_.Get(), 1.2F);
        d2dContext_->DrawLine(D2D1::Point2F(375, 158), D2D1::Point2F(393, 158),
            structureDimBrush_.Get(), 1.0F);

        // Tapered double contours give the limbs a scanned-volume silhouette.
        const auto limb = [&](D2D1_POINT_2F a, D2D1_POINT_2F b,
                              float startWidth, float endWidth) {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            const float nx = -dy / length;
            const float ny = dx / length;
            const auto aLeft = D2D1::Point2F(a.x + nx * startWidth, a.y + ny * startWidth);
            const auto aRight = D2D1::Point2F(a.x - nx * startWidth, a.y - ny * startWidth);
            const auto bLeft = D2D1::Point2F(b.x + nx * endWidth, b.y + ny * endWidth);
            const auto bRight = D2D1::Point2F(b.x - nx * endWidth, b.y - ny * endWidth);
            const std::array<D2D1_POINT_2F, 4> shell{aLeft, bLeft, bRight, aRight};
            fillShell(shell);
            d2dContext_->DrawLine(aLeft, bLeft, structureBrush_.Get(), 1.6F);
            d2dContext_->DrawLine(aRight, bRight, structureBrush_.Get(), 1.6F);
            d2dContext_->DrawLine(aLeft, aRight, structureDimBrush_.Get(), 1.0F);
            d2dContext_->DrawLine(bLeft, bRight, structureDimBrush_.Get(), 1.0F);
            d2dContext_->DrawLine(a, b, structureDimBrush_.Get(), 0.8F);
        };
        // Armored torso shell, inner core and segmented pelvis.
        const std::array<D2D1_POINT_2F, 8> torso{
            D2D1::Point2F(350, 184), D2D1::Point2F(375, 177),
            D2D1::Point2F(393, 177), D2D1::Point2F(418, 184),
            D2D1::Point2F(407, 231), D2D1::Point2F(397, 249),
            D2D1::Point2F(371, 249), D2D1::Point2F(361, 231)};
        fillShell(torso);
        for (size_t index = 0; index < torso.size(); ++index) {
            d2dContext_->DrawLine(torso[index], torso[(index + 1) % torso.size()],
                structureBrush_.Get(), 1.8F);
        }
        d2dContext_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(384, 210), 20, 32),
            structureDimBrush_.Get(), 1.1F);
        d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(384, 210), 9, 25),
            scanFillBrush_.Get());
        d2dContext_->DrawLine(D2D1::Point2F(384, 178), D2D1::Point2F(384, 249),
            structureDimBrush_.Get(), 0.9F);
        for (float y : {194.0F, 207.0F, 220.0F, 233.0F}) {
            const float halfWidth = 20.0F - (y - 194.0F) * 0.25F;
            d2dContext_->DrawLine(D2D1::Point2F(384 - halfWidth, y),
                D2D1::Point2F(384 + halfWidth, y), structureDimBrush_.Get(), 0.8F);
        }
        limb(D2D1::Point2F(351, 188), D2D1::Point2F(319, 219), 7.0F, 5.0F);
        limb(D2D1::Point2F(319, 219), D2D1::Point2F(302, 258), 5.0F, 3.5F);
        limb(D2D1::Point2F(417, 188), D2D1::Point2F(449, 219), 7.0F, 5.0F);
        limb(D2D1::Point2F(449, 219), D2D1::Point2F(466, 258), 5.0F, 3.5F);
        limb(D2D1::Point2F(373, 247), D2D1::Point2F(363, 286), 8.0F, 6.0F);
        limb(D2D1::Point2F(363, 286), D2D1::Point2F(358, 331), 6.0F, 4.0F);
        limb(D2D1::Point2F(395, 247), D2D1::Point2F(405, 286), 8.0F, 6.0F);
        limb(D2D1::Point2F(405, 286), D2D1::Point2F(410, 331), 6.0F, 4.0F);
        }

        const std::array<D2D1_POINT_2F, 8> nodes{
            D2D1::Point2F(347, 190), D2D1::Point2F(421, 190),
            D2D1::Point2F(384, 166), D2D1::Point2F(384, 226),
            D2D1::Point2F(369, 250), D2D1::Point2F(399, 250),
            D2D1::Point2F(374, 330), D2D1::Point2F(394, 330),
        };
        const auto batteryBrush = [&](const std::wstring& value) -> ID2D1Brush* {
            const int percent = value.empty() ? -1 : _wtoi(value.c_str());
            if (percent >= 0 && percent <= 10) return criticalBrush_.Get();
            if (percent <= 20 && percent >= 0) return warningBrush_.Get();
            return value.empty() ? structureDimBrush_.Get() : accentBrush_.Get();
        };
        for (size_t index = 0; index < nodes.size(); ++index) {
            auto* brush = batteryBrush(rigSlots[index]);
            d2dContext_->FillEllipse(D2D1::Ellipse(nodes[index], 5.5F, 5.5F), brush);
            d2dContext_->DrawEllipse(D2D1::Ellipse(nodes[index], 9.5F, 9.5F), brush, 1.0F);
            if (index == 2 || index == 3) continue;
            const float textLeft = index == 0 || index == 4 || index == 6
                ? nodes[index].x - 49.0F : nodes[index].x + 11.0F;
            drawText(rigSlots[index].empty() ? L"--" : rigSlots[index], labelFormat_.Get(),
                D2D1::RectF(textLeft, nodes[index].y - 12, textLeft + 45, nodes[index].y + 15), brush);
        }
        const auto drawBatteryLeader = [&](size_t index, D2D1_POINT_2F elbow,
                                           D2D1_POINT_2F terminal, bool textOnRight) {
            auto* brush = batteryBrush(rigSlots[index]);
            d2dContext_->DrawLine(nodes[index], elbow, brush, 1.2F);
            d2dContext_->DrawLine(elbow, terminal, brush, 1.2F);
            d2dContext_->DrawEllipse(D2D1::Ellipse(terminal, 3.0F, 3.0F), brush, 1.2F);
            const float left = textOnRight ? terminal.x + 8.0F : terminal.x - 52.0F;
            drawText(rigSlots[index].empty() ? L"--" : rigSlots[index], labelFormat_.Get(),
                D2D1::RectF(left, terminal.y - 12, left + 45, terminal.y + 15), brush);
        };
        drawBatteryLeader(2, D2D1::Point2F(402, 154), D2D1::Point2F(466, 154), true);
        drawBatteryLeader(3, D2D1::Point2F(366, 238), D2D1::Point2F(302, 238), false);
        const auto controllerValue = [&](bool right) {
            const auto divider = rigLine.find(L"    ");
            const auto field = right
                ? (divider == std::wstring::npos ? std::wstring{} : rigLine.substr(divider + 4))
                : rigLine.substr(0, divider);
            const auto firstDigit = field.find_first_of(L"0123456789");
            const auto percent = field.find(L'%', firstDigit);
            return firstDigit == std::wstring::npos || percent == std::wstring::npos
                ? std::wstring{} : field.substr(firstDigit, percent - firstDigit + 1);
        };
        const std::array<D2D1_POINT_2F, 2> hands{
            D2D1::Point2F(337, 228), D2D1::Point2F(431, 228)};
        for (size_t index = 0; index < hands.size(); ++index) {
            const auto value = controllerValue(index == 1);
            auto* brush = batteryBrush(value);
            d2dContext_->FillEllipse(D2D1::Ellipse(hands[index], 5.5F, 5.5F), brush);
            d2dContext_->DrawEllipse(D2D1::Ellipse(hands[index], 9.5F, 9.5F), brush, 1.0F);
            const float textLeft = index == 0 ? hands[index].x - 50.0F : hands[index].x + 12.0F;
            drawText(value.empty() ? L"--" : value, labelFormat_.Get(),
                D2D1::RectF(textLeft, hands[index].y - 12, textLeft + 45, hands[index].y + 15), brush);
        }
        const auto drawRigHoldControl = [&](D2D1_POINT_2F center, wchar_t glyph,
                                             HoloGlyph holoGlyph, float progress, bool enabled) {
            const bool armed = enabled && progress > 0.0F;
            if (holoGlyphAtlas_) {
                drawHoloAsset(holoGlyph, center, 29, 29, armed, enabled);
            } else {
                drawHoloButton(center, 27, 27, armed, enabled);
            }
            constexpr int segments = 12;
            const int lit = enabled ? static_cast<int>(
                std::ceil(progress * static_cast<float>(segments))) : 0;
            for (int index = 0; index < segments; ++index) {
                const float angle = -1.5707963F
                    + static_cast<float>(index) * 6.2831853F / segments;
                const auto point = D2D1::Point2F(center.x + std::cos(angle) * 32.0F,
                    center.y + std::sin(angle) * 32.0F);
                d2dContext_->FillEllipse(D2D1::Ellipse(point, 2.25F, 2.25F),
                    index < lit ? accentBrush_.Get() : structureDimBrush_.Get());
            }
            if (!holoGlyphAtlas_) {
                const wchar_t text[]{glyph, L'\0'};
                titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                drawText(text, titleFormat_.Get(),
                    D2D1::RectF(center.x - 24, center.y - 24, center.x + 24, center.y + 24),
                    enabled ? accentBrush_.Get() : mutedTextBrush_.Get());
                titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }
        };
        drawRigHoldControl(D2D1::Point2F(118, 274), L'R', HoloGlyph::RigReset,
            rigResetHoldProgress_, true);
        drawRigHoldControl(D2D1::Point2F(650, 274), L'M', HoloGlyph::RigMount,
            rigMountHoldProgress_, mountReady);

        const auto drawLegProfile = [&](D2D1_POINT_2F center, wchar_t glyph,
                                        const wchar_t* length,
                                        RigLegProfile profile) {
            const bool active = rigLegProfile_ == profile;
            const bool pending = rigLegPending_ == profile;
            drawHoloButton(center, 27, 27, active || pending, true);
            if (active) {
                d2dContext_->DrawEllipse(D2D1::Ellipse(center, 31, 31), accentBrush_.Get(), 2.0F);
            }
            if (pending) {
                constexpr int segments = 8;
                for (int index = 0; index < segments; ++index) {
                    const float angle = -1.5707963F
                        + static_cast<float>(index) * 6.2831853F / segments;
                    const auto point = D2D1::Point2F(center.x + std::cos(angle) * 34.0F,
                        center.y + std::sin(angle) * 34.0F);
                    d2dContext_->FillEllipse(D2D1::Ellipse(point, 2.2F, 2.2F),
                        accentBrush_.Get());
                }
            }
            const wchar_t text[]{glyph, L'\0'};
            titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            drawText(text, titleFormat_.Get(),
                D2D1::RectF(center.x - 24, center.y - 24, center.x + 24, center.y + 24),
                active || pending ? accentBrush_.Get() : textBrush_.Get());
            labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            drawText(length, labelFormat_.Get(),
                D2D1::RectF(center.x - 48, center.y + 34, center.x + 48, center.y + 58),
                active ? accentBrush_.Get() : mutedTextBrush_.Get());
            titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        };
        drawLegProfile(D2D1::Point2F(118, 145), L'C', L"90.1 CM", RigLegProfile::Config);
        drawLegProfile(D2D1::Point2F(650, 145), L'P', L"100 CM", RigLegProfile::Play);

        if (rigLegProfile_ == RigLegProfile::Custom || rigLegProfile_ == RigLegProfile::Error) {
            labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            drawText(rigLegProfile_ == RigLegProfile::Custom ? L"CUSTOM LEG PROFILE" : L"PROFILE ERROR",
                labelFormat_.Get(), D2D1::RectF(274, 344, 494, 372),
                rigLegProfile_ == RigLegProfile::Error ? warningBrush_.Get() : mutedTextBrush_.Get());
            labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        }
    } else if (deck == 2) {
        const auto stateBrush = playspaceAdjusted_ ? accentBrush_.Get() : structureBrush_.Get();
        const auto resetCenter = D2D1::Point2F(199, 250);
        drawHoloButton(resetCenter, 96, 96, playspaceAdjusted_);
        if (playspaceResetArt_) {
            d2dContext_->DrawBitmap(playspaceResetArt_.Get(),
                D2D1::RectF(115, 166, 283, 334), playspaceAdjusted_ ? 0.92F : 0.42F,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        constexpr int restoreSegments = 12;
        const int restoreLit = playspaceAdjusted_ ? static_cast<int>(
            std::ceil(playspaceHoldProgress_ * restoreSegments)) : 0;
        for (int index = 0; index < restoreSegments; ++index) {
            const float angle = -1.5707963F
                + static_cast<float>(index) * 6.2831853F / restoreSegments;
            const auto point = D2D1::Point2F(
                resetCenter.x + std::cos(angle) * 106.0F,
                resetCenter.y + std::sin(angle) * 106.0F);
            d2dContext_->FillEllipse(D2D1::Ellipse(point, 2.8F, 2.8F),
                index < restoreLit ? accentBrush_.Get() : structureDimBrush_.Get());
        }

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
        drawText(commsTranscript_.empty() ? L"Voice transcription will appear here"
                : commsTranscript_, bodyWrapFormat_.Get(), D2D1::RectF(42, 116, 726, 190),
            commsTranscript_.empty() ? mutedTextBrush_.Get() : textBrush_.Get());

        const std::array<D2D1_RECT_F, 4> shortcutButtons{
            D2D1::RectF(42, 198, 198, 236), D2D1::RectF(218, 198, 374, 236),
            D2D1::RectF(394, 198, 550, 236), D2D1::RectF(570, 198, 726, 236)};
        labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        for (size_t index = 0; index < shortcutButtons.size(); ++index) {
            const bool configured = !commsShortcutLabels_[index].empty();
            d2dContext_->FillRoundedRectangle(
                D2D1::RoundedRect(shortcutButtons[index], 10, 10), buttonBrush_.Get());
            d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(shortcutButtons[index], 10, 10),
                configured ? structureBrush_.Get() : structureDimBrush_.Get(), configured ? 1.5F : 0.8F);
            drawText(configured ? commsShortcutLabels_[index] : L"\u00b7\u00b7\u00b7",
                labelFormat_.Get(), shortcutButtons[index],
                configured ? textBrush_.Get() : structureDimBrush_.Get());
        }
        labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        const auto micCenter = D2D1::Point2F(270, 285);
        const auto clearCenter = D2D1::Point2F(560, 285);
        if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::CommsMic, micCenter, 47, 47, commsActive_);
            drawHoloAsset(HoloGlyph::ClearChat, clearCenter, 41, 41);
        } else {
        drawHoloButton(micCenter, 45, 45, commsActive_);
        d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(257, 261, 283, 291), 13, 13),
            stateBrush);
        d2dContext_->DrawLine(D2D1::Point2F(250, 282), D2D1::Point2F(250, 290),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(250, 290), D2D1::Point2F(258, 298),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(258, 298), D2D1::Point2F(282, 298),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(282, 298), D2D1::Point2F(290, 290),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(290, 290), D2D1::Point2F(290, 282),
            stateBrush, 3.5F);
        d2dContext_->DrawLine(D2D1::Point2F(270, 299), D2D1::Point2F(270, 311),
            stateBrush, 3.5F);

        drawHoloButton(clearCenter, 39, 39);
        // Empty chatbox pulse: an erased speech cell, kept icon-only.
        d2dContext_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(540, 270, 580, 300), 6, 6),
            structureBrush_.Get());
        fillPolygon({D2D1::Point2F(550, 300), D2D1::Point2F(546, 309),
            D2D1::Point2F(560, 300)}, structureBrush_.Get());
        d2dContext_->DrawLine(D2D1::Point2F(539, 308), D2D1::Point2F(581, 267),
            buttonBrush_.Get(), 6.0F);
        }

        d2dContext_->DrawLine(D2D1::Point2F(42, 343), D2D1::Point2F(726, 343),
            structureDimBrush_.Get(), 1.0F);
        d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(49, 355), 5, 5), stateBrush);
        drawText(commsStatus_, labelFormat_.Get(), D2D1::RectF(64, 344, 606, 374),
            commsActive_ ? accentBrush_.Get() : mutedTextBrush_.Get());
    } else if (deck == 6) {
        drawText(assistantTranscript_.empty() ? L"Tap the core and ask anything"
                : (L"YOU  /  " + assistantTranscript_), labelFormat_.Get(),
            D2D1::RectF(42, 108, 726, 142),
            assistantTranscript_.empty() ? mutedTextBrush_.Get() : structureBrush_.Get());
        drawText(assistantResponse_.empty()
                ? L"Weather, calculations, current information, or a second opinion."
                : assistantResponse_, bodyWrapFormat_.Get(), D2D1::RectF(42, 148, 726, 238),
            assistantResponse_.empty() ? mutedTextBrush_.Get() : textBrush_.Get());

        const auto micCenter = D2D1::Point2F(220, 286);
        const auto cancelCenter = D2D1::Point2F(384, 286);
        const auto clearCenter = D2D1::Point2F(548, 286);
        if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::Assistant, micCenter, 49, 49, assistantActive_);
            drawHoloAsset(HoloGlyph::Close, cancelCenter, 38, 38, assistantActive_);
            drawHoloAsset(HoloGlyph::ClearChat, clearCenter, 38, 38);
        }
        d2dContext_->DrawLine(D2D1::Point2F(42, 343), D2D1::Point2F(726, 343),
            structureDimBrush_.Get(), 1.0F);
        auto* assistantStateBrush = assistantActive_ ? accentBrush_.Get() : structureBrush_.Get();
        d2dContext_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(49, 355), 5, 5), assistantStateBrush);
        drawText(assistantStatus_, labelFormat_.Get(), D2D1::RectF(64, 344, 606, 374),
            assistantActive_ ? accentBrush_.Get() : mutedTextBrush_.Get());
    } else {
        drawText(L"VOICE OUTPUT", labelFormat_.Get(), D2D1::RectF(42, 108, 300, 140),
            accentBrush_.Get());
        drawText(ttsMuted_ ? L"MUTED" : (std::to_wstring(ttsVolumePercent_) + L"%"),
            titleFormat_.Get(), D2D1::RectF(42, 150, 280, 202),
            ttsMuted_ ? mutedTextBrush_.Get() : textBrush_.Get());

        // Desktop settings launcher: monitor frame with an outward utility arrow.
        const auto desktopSettingsCenter = D2D1::Point2F(690, 132);
        if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::DesktopSettings, desktopSettingsCenter, 33, 33);
        } else {
        drawHoloButton(desktopSettingsCenter, 31, 31);
        d2dContext_->DrawRectangle(D2D1::RectF(675, 120, 700, 137), accentBrush_.Get(), 2.0F);
        d2dContext_->DrawLine(D2D1::Point2F(682, 143), D2D1::Point2F(694, 143),
            accentBrush_.Get(), 2.0F);
        d2dContext_->DrawLine(D2D1::Point2F(688, 137), D2D1::Point2F(688, 143),
            accentBrush_.Get(), 2.0F);
        d2dContext_->DrawLine(D2D1::Point2F(696, 116), D2D1::Point2F(706, 106),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(699, 106), D2D1::Point2F(706, 106),
            accentBrush_.Get(), 2.5F);
        d2dContext_->DrawLine(D2D1::Point2F(706, 106), D2D1::Point2F(706, 113),
            accentBrush_.Get(), 2.5F);
        }

        // Thin segmented level meter remains readable without becoming a luminous bar.
        for (int index = 0; index < 10; ++index) {
            const float left = 286.0F + static_cast<float>(index) * 39.0F;
            d2dContext_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(left, 164, left + 27, 181), 4, 4),
                !ttsMuted_ && index * 10 < ttsVolumePercent_
                    ? accentBrush_.Get() : structureDimBrush_.Get());
        }

        drawText(L"BROADCAST BOOST", labelFormat_.Get(), D2D1::RectF(42, 210, 280, 242),
            mutedTextBrush_.Get());
        drawText(L"+" + std::to_wstring(broadcastGainDb_) + L" dB",
            titleFormat_.Get(), D2D1::RectF(278, 201, 470, 248), textBrush_.Get());
        const std::array<D2D1_POINT_2F, 2> gainCenters{
            D2D1::Point2F(540, 220), D2D1::Point2F(650, 220)};
        if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::BroadcastGainDown, gainCenters[0], 33, 33);
            drawHoloAsset(HoloGlyph::BroadcastGainUp, gainCenters[1], 33, 33);
        } else {
            for (const auto center : gainCenters) {
                drawHoloButton(center, 31, 31);
                const auto emitter = D2D1::Point2F(center.x - 7, center.y - 4);
                d2dContext_->FillEllipse(D2D1::Ellipse(emitter, 3.5F, 3.5F), structureBrush_.Get());
                drawArc(emitter, 9.0F, -0.90F, 0.90F, structureBrush_.Get(), 2.7F);
                drawArc(emitter, 9.0F, 2.24F, 4.04F, structureBrush_.Get(), 2.7F);
                drawArc(emitter, 15.0F, -0.82F, 0.82F, structureBrush_.Get(), 2.2F);
                drawArc(emitter, 15.0F, 2.32F, 3.96F, structureBrush_.Get(), 2.2F);
                fillPolygon({D2D1::Point2F(emitter.x, emitter.y + 2),
                    D2D1::Point2F(emitter.x - 8, emitter.y + 20),
                    D2D1::Point2F(emitter.x + 8, emitter.y + 20)}, structureBrush_.Get());
            }
            d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(552, 230), 9, 9),
                structureBrush_.Get());
            d2dContext_->DrawLine(D2D1::Point2F(547, 230), D2D1::Point2F(557, 230),
                buttonBrush_.Get(), 3.0F);
            d2dContext_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(662, 230), 9, 9),
                structureBrush_.Get());
            d2dContext_->DrawLine(D2D1::Point2F(657, 230), D2D1::Point2F(667, 230),
                buttonBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(662, 225),
                D2D1::Point2F(662, 235), buttonBrush_.Get(), 3.0F);
        }

        const std::array<D2D1_POINT_2F, 3> centers{
            D2D1::Point2F(176, 300), D2D1::Point2F(384, 300), D2D1::Point2F(592, 300)};
        if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::VolumeDown, centers[0], 49, 49);
            drawHoloAsset(ttsMuted_ ? HoloGlyph::Mute : HoloGlyph::Speaker,
                centers[1], 49, 49, ttsMuted_);
            drawHoloAsset(HoloGlyph::VolumeUp, centers[2], 49, 49);
        } else {
        for (size_t index = 0; index < centers.size(); ++index) {
            drawHoloButton(centers[index], 47, 47, index == 1 && ttsMuted_);
        }
        // Minus and plus.
        d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(156, 295, 196, 305), 4, 4), structureBrush_.Get());
        d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(572, 295, 612, 305), 4, 4), structureBrush_.Get());
        d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(587, 280, 597, 320), 4, 4), structureBrush_.Get());
        // Speaker/mute glyph.
        fillPolygon({D2D1::Point2F(362, 291), D2D1::Point2F(374, 291),
            D2D1::Point2F(391, 278), D2D1::Point2F(391, 322),
            D2D1::Point2F(374, 309), D2D1::Point2F(362, 309)}, structureBrush_.Get());
        if (ttsMuted_) {
            d2dContext_->DrawLine(D2D1::Point2F(399, 287), D2D1::Point2F(417, 313),
                accentBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(417, 287), D2D1::Point2F(399, 313),
                accentBrush_.Get(), 3.0F);
        } else {
            d2dContext_->DrawLine(D2D1::Point2F(400, 289), D2D1::Point2F(408, 296),
                structureBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(408, 296), D2D1::Point2F(408, 304),
                structureBrush_.Get(), 3.0F);
            d2dContext_->DrawLine(D2D1::Point2F(408, 304), D2D1::Point2F(400, 311),
                structureBrush_.Get(), 3.0F);
        }
        }

        // Graceful shutdown: a deliberately separate power control with a
        // segmented confirmation ring that fills over the required hold.
        const auto shutdownCenter = D2D1::Point2F(704, 300);
        const bool shutdownArmed = shutdownHoldProgress_ > 0.0F;
        if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::Shutdown, shutdownCenter, 35, 35, shutdownArmed);
        } else {
            drawHoloButton(shutdownCenter, 33, 33, shutdownArmed, true, true);
        }
        constexpr int shutdownSegments = 12;
        const int litSegments = static_cast<int>(
            std::ceil(shutdownHoldProgress_ * static_cast<float>(shutdownSegments)));
        for (int index = 0; index < shutdownSegments; ++index) {
            const float angle = -1.5707963F
                + static_cast<float>(index) * 6.2831853F / shutdownSegments;
            const auto point = D2D1::Point2F(
                shutdownCenter.x + std::cos(angle) * 39.0F,
                shutdownCenter.y + std::sin(angle) * 39.0F);
            d2dContext_->FillEllipse(D2D1::Ellipse(point, 2.5F, 2.5F),
                index < litSegments ? criticalBrush_.Get() : structureDimBrush_.Get());
        }
        if (!holoGlyphAtlas_) {
            d2dContext_->DrawLine(D2D1::Point2F(704, 279), D2D1::Point2F(704, 299),
                criticalBrush_.Get(), 3.5F);
            d2dContext_->DrawEllipse(D2D1::Ellipse(shutdownCenter, 17, 17),
                criticalBrush_.Get(), 3.5F);
        }
    }
    // The clock owns one predictable footer cell on every deck. Keeping it
    // outside the header prevents battery and Settings from squeezing it.
    if (!clockText_.empty()) {
        d2dContext_->DrawLine(D2D1::Point2F(618, 348), D2D1::Point2F(618, 371),
            structureDimBrush_.Get(), 1.0F);
        d2dContext_->DrawLine(D2D1::Point2F(628, 343), D2D1::Point2F(726, 343),
            structureDimBrush_.Get(), 1.0F);
        labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        drawText(clockText_, labelFormat_.Get(), D2D1::RectF(626, 344, 724, 374),
            textBrush_.Get());
        labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    if (pressFeedbackActive_) {
        d2dContext_->FillEllipse(D2D1::Ellipse(pressFeedbackCenter_, 15, 15), scanFillBrush_.Get());
        d2dContext_->DrawEllipse(D2D1::Ellipse(pressFeedbackCenter_, 21, 21), accentBrush_.Get(), 1.5F);
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

void OverlayRenderer::SetPlayspaceHoldProgress(float progress) {
    playspaceHoldProgress_ = (std::max)(0.0F, (std::min)(1.0F, progress));
}

void OverlayRenderer::SetSlimeAvailable(bool available) {
    slimeAvailable_ = available;
}

void OverlayRenderer::SetMusicVoiceStatus(const std::wstring& status, bool active) {
    musicVoiceStatus_ = status;
    musicVoiceActive_ = active;
}

void OverlayRenderer::SetMusicPlaying(bool playing) {
    musicPlaying_ = playing;
}

void OverlayRenderer::SetMusicBroadcastState(bool active, const std::wstring& status) {
    musicBroadcastActive_ = active;
    musicBroadcastStatus_ = status;
}

void OverlayRenderer::SetCommsStatus(const std::wstring& status,
                                     const std::wstring& transcript, bool active) {
    commsStatus_ = status;
    commsTranscript_ = transcript;
    commsActive_ = active;
}

void OverlayRenderer::SetAssistantStatus(const std::wstring& status,
                                         const std::wstring& transcript,
                                         const std::wstring& response, bool active) {
    assistantStatus_ = status;
    assistantTranscript_ = transcript;
    assistantResponse_ = response;
    assistantActive_ = active;
}

void OverlayRenderer::SetCommsShortcuts(const std::array<std::wstring, 4>& labels) {
    commsShortcutLabels_ = labels;
}

void OverlayRenderer::SetTtsSettings(int volumePercent, bool muted) {
    ttsVolumePercent_ = (std::max)(0, (std::min)(100, volumePercent));
    ttsMuted_ = muted;
}

void OverlayRenderer::SetBroadcastGainDb(int gainDb) {
    broadcastGainDb_ = (std::max)(0, (std::min)(24, gainDb));
}

void OverlayRenderer::SetShutdownHoldProgress(float progress) {
    shutdownHoldProgress_ = (std::max)(0.0F, (std::min)(1.0F, progress));
}

void OverlayRenderer::SetRigHoldProgress(float resetProgress, float mountProgress) {
    rigResetHoldProgress_ = (std::max)(0.0F, (std::min)(1.0F, resetProgress));
    rigMountHoldProgress_ = (std::max)(0.0F, (std::min)(1.0F, mountProgress));
}

void OverlayRenderer::SetRigLegProfile(RigLegProfile active, RigLegProfile pending) {
    rigLegProfile_ = active;
    rigLegPending_ = pending;
}

void OverlayRenderer::SetClockText(const std::wstring& text) {
    clockText_ = text;
}

void OverlayRenderer::SetLowestBattery(int percent) {
    lowestBatteryPercent_ = percent < 0 ? -1 : (std::min)(100, percent);
}

void OverlayRenderer::SetPressFeedback(float x, float y, bool active) {
    pressFeedbackCenter_ = D2D1::Point2F(x, y);
    pressFeedbackActive_ = active;
}

void OverlayRenderer::SetRigBodyArtPath(const std::wstring& path) {
    if (rigBodyArtPath_ == path) return;
    rigBodyArtPath_ = path;
    rigBodyArt_.Reset();
}

void OverlayRenderer::SetPlayspaceResetArtPath(const std::wstring& path) {
    if (playspaceResetArtPath_ == path) return;
    playspaceResetArtPath_ = path;
    playspaceResetArt_.Reset();
}

void OverlayRenderer::SetHoloGlyphAtlasPath(const std::wstring& path) {
    if (holoGlyphAtlasPath_ == path) return;
    holoGlyphAtlasPath_ = path;
    holoGlyphAtlas_.Reset();
}

}  // namespace interfayce
