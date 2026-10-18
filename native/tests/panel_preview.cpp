#include "overlay_renderer.h"
#include <wincodec.h>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <fstream>
#include "performance_monitor.h"

// Render the actual production panel without a headset or running overlay.
int wmain(int argc, wchar_t** argv) {
    if (argc < 3 || argc > 4) return 2;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    interfayce::OverlayRenderer renderer;
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const bool share = argc == 4 && std::wstring(argv[3]) == L"share";
    const auto readText = [&](const wchar_t* name) {
        std::ifstream file(output / name, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), {});
        const int count=MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring text(count, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), count);
        return text;
    };
    renderer.SetHoloGlyphAtlasPath((root / L"assets/ui/holo-glyph-atlas.png").wstring());
    renderer.SetRigBodyArtPath((root / L"assets/ui/rig-body-scanner.png").wstring());
    renderer.SetPlayspaceResetArtPath((root / L"assets/ui/playspace-reset.png").wstring());
    renderer.SetBatteryEstimate(L"4h 20m", 65);
    renderer.SetPerformance({L"32%", L"91%", L"12.0/16 GB", L"24.0/64 GB"});
    renderer.SetClockText(L"8:42 PM");
    renderer.SetSlimeAvailable(!(argc == 4 && std::wstring(argv[3]) == L"cetra-offline"));
    renderer.SetCetraBatteries({interfayce::CetraBattery{93, false}, {66, false}, {13, true}});
    if (argc == 4 && std::wstring(argv[3]) == L"cetra-disconnected") renderer.SetCetraBatteries({});
    renderer.SetMusicPlaying(true);
    if (argc == 4 && std::wstring(argv[3]) == L"youtube") {
        renderer.SetMusicBroadcastSource(2);
        renderer.SetMusicProvider(true);
    }
    renderer.SetAssistantStatus(L"CODEX / ANSWER", L"What should we try next?",
        L"We could find a quiet world, put on some music, and see where the evening goes.\n\n"
        L"I can keep our conversation going while you are in VR. Scroll here with the thumbstick to read longer replies.", false);
    renderer.SetServiceStatus(L"TTS\toffline\tSpeech server unreachable\nSTT\tbackup\tRemote unavailable; local recognition ready\nLLM\tgood\tAPI reachable\nSPOTIFY\tgood\tMedia session available");
    if (share) {
        interfayce::PerformanceMonitor performance;
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        renderer.SetPerformance(performance.Snapshot());
        renderer.SetBatteryEstimate(L"BAT --", -1);
        wchar_t clock[64]{};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, clock, 64);
        renderer.SetClockText(clock);
        renderer.SetCetraBatteries(interfayce::ReadCetraBatteries());
        renderer.SetServiceStatus(readText(L"services.txt"));
        const bool youtube=readText(L"provider.txt")==L"youtube";
        renderer.SetMusicProvider(youtube);
        renderer.SetMusicBroadcastSource(youtube ? 2 : 0);
        renderer.SetCommsStatus(L"READY", L"Let's find a quiet world and put some music on.", false);
        renderer.SetCommsShortcuts({L"Hello!", L"One moment", L"Thank you", L"Be right back"});
        renderer.SetAssistantStatus(L"CODEX / ANSWER", L"What should we try next?",
            L"A quiet world, some music, and good company sounds like a plan. I can stay here while you explore.", false);
    }
    std::filesystem::create_directories(output);
    for (int deck = 0; deck < 7; ++deck) {
        interfayce::DesktopPanelState desktop;
        if (deck == 1) {
            const std::wstring mode = argc == 4 ? argv[3] : L"";
            desktop.showSurfaceList = mode != L"blocking-home";
            desktop.blockGameInput = mode == L"blocking-home" || mode == L"blocking-list" || mode == L"blocking-unavailable";
            desktop.inputBlockingAvailable = mode != L"blocking-unavailable";
            desktop.firstSurface = 2;
            for (int index = 0; index < 7; ++index) {
                interfayce::DesktopSurfaceSummary surface{};
                surface.id = index + 1; surface.label = share ? std::wstring(std::array<const wchar_t*, 7>{L"Discord", L"Browser", L"YouTube Music", L"Spotify", L"Discord", L"Browser", L"Desktop"}[index]) : L"Test window " + std::to_wstring(index + 1);
                surface.privateEligible = true; surface.reusable = true;
                desktop.surfaces.push_back(surface);
            }
        }
        if (deck == 6 && argc == 4 && !share) {
            renderer.SetAssistantChoices({L"Allow once", L"Decline"}, false, "preview-decision");
            renderer.SetAssistantStatus(L"CODEX / AWAITING INPUT", L"Review this change",
                L"Review this file change:\n\nnotes.txt\n-old line\n+new line\n\n"
                L"This long decision must begin at the top, even when the previous conversation followed the bottom.", true);
        }
        const auto panelText = share ? (deck == 1 ? std::wstring(L"7 desktop surfaces") : readText(L"music.txt")) : L"Persevere / Gang of Youths";
        if (!renderer.Initialize(nullptr, deck, panelText, share ? (output / L"album.jpg").wstring() : L"", L"", {}, false, desktop)) return 3;
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
    if (share) return 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        renderer.SetPerformance({std::to_wstring(i) + L"%", L"91%", L"12.0/16 GB", L"24.0/64 GB"});
        if (!renderer.Initialize(nullptr, 0, L"Persevere / Gang of Youths")) return 8;
    }
    std::cout << "Average cached panel redraw/submission ms: "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 100 << '\n';
}
