#include "overlay_renderer.h"
#include <wincodec.h>
#include <filesystem>
#include <iostream>

// Render the actual production panel without a headset or running overlay.
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    interfayce::OverlayRenderer renderer;
    const std::filesystem::path root(argv[1]), output(argv[2]);
    renderer.SetHoloGlyphAtlasPath((root / L"assets/ui/holo-glyph-atlas.png").wstring());
    renderer.SetRigBodyArtPath((root / L"assets/ui/rig-body-scanner.png").wstring());
    renderer.SetPlayspaceResetArtPath((root / L"assets/ui/playspace-reset.png").wstring());
    renderer.SetBatteryEstimate(L"4h 20m", 65);
    renderer.SetClockText(L"8:42 PM");
    renderer.SetSlimeAvailable(true);
    renderer.SetMusicPlaying(true);
    renderer.SetServiceStatus(L"TTS\toffline\tSpeech server unreachable\nSTT\tbackup\tRemote unavailable; local recognition ready\nLLM\tgood\tAPI reachable\nSPOTIFY\tgood\tMedia session available");
    std::filesystem::create_directories(output);
    for (int deck = 0; deck < 7; ++deck) {
        interfayce::DesktopPanelState desktop;
        if (deck == 1) {
            desktop.showSurfaceList = true;
            desktop.firstSurface = 2;
            for (int index = 0; index < 7; ++index) {
                interfayce::DesktopSurfaceSummary surface{};
                surface.id = index + 1; surface.label = L"Test window " + std::to_wstring(index + 1);
                surface.privateEligible = true; surface.reusable = true;
                desktop.surfaces.push_back(surface);
            }
        }
        if (!renderer.Initialize(nullptr, deck, L"Persevere / Gang of Youths", L"", L"", {}, false, desktop)) return 3;
        auto* device = renderer.Device();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture, staging;
        if (FAILED(device->OpenSharedResource(renderer.Texture().handle, IID_PPV_ARGS(&texture)))) return 4;
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return 5;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 6;
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Microsoft::WRL::ComPtr<IWICStream> stream;
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename((output / (L"deck-" + std::to_wstring(deck) + L".png")).c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(desc.Width, desc.Height);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
        if (SUCCEEDED(hr)) hr = frame->WritePixels(desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height, static_cast<BYTE*>(mapped.pData));
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
        context->Unmap(staging.Get(), 0);
        if (FAILED(hr)) return 7;
    }
    std::cout << "Rendered seven production panels.\n";
}
