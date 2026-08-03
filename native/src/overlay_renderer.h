#pragma once

#include "desktop_surface_registry.h"

#include <openvr.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <array>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace interfayce {

struct DesktopPanelState {
    bool showSurfaceList{};
    std::vector<DesktopSurfaceSummary> surfaces;
};

class OverlayRenderer {
public:
    bool Initialize(vr::IVRSystem* system, int deck = 2, const std::wstring& musicLine = L"",
                    const std::wstring& musicArtPath = L"", const std::wstring& rigLine = L"",
                    const std::array<std::wstring, 8>& rigSlots = {}, bool mountReady = false,
                    const DesktopPanelState& desktop = {});
    void SetPlayspaceAdjusted(bool adjusted);
    void SetSlimeAvailable(bool available);
    void SetMusicVoiceStatus(const std::wstring& status, bool active);
    void SetCommsStatus(const std::wstring& status, const std::wstring& transcript, bool active);
    void SetTtsSettings(int volumePercent, bool muted);
    ID3D11Device* Device() const;
    vr::Texture_t Texture() const;

private:
    bool Render(int deck, const std::wstring& musicLine, const std::wstring& musicArtPath,
                const std::wstring& rigLine, const std::array<std::wstring, 8>& rigSlots,
                bool mountReady, const DesktopPanelState& desktop);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> panelTexture_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFormat_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glassBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stripBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> structureBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> structureDimBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activeFillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> buttonBrush_;
    HANDLE sharedTextureHandle_{};
    bool playspaceAdjusted_{};
    bool slimeAvailable_{};
    std::wstring musicVoiceStatus_{L"VOICE READY"};
    bool musicVoiceActive_{};
    std::wstring commsStatus_{L"IDLE"};
    std::wstring commsTranscript_;
    bool commsActive_{};
    int ttsVolumePercent_{85};
    bool ttsMuted_{};
};

}  // namespace interfayce
