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
    std::array<std::wstring, 3> favorites;
};

class OverlayRenderer {
public:
    bool Initialize(vr::IVRSystem* system, int deck = 2, const std::wstring& musicLine = L"",
                    const std::wstring& musicArtPath = L"", const std::wstring& rigLine = L"",
                    const std::array<std::wstring, 8>& rigSlots = {}, bool mountReady = false,
                    const DesktopPanelState& desktop = {});
    void SetPlayspaceAdjusted(bool adjusted);
    void SetPlayspaceHoldProgress(float progress);
    void SetSlimeAvailable(bool available);
    void SetMusicVoiceStatus(const std::wstring& status, bool active);
    void SetMusicPlaying(bool playing);
    void SetMusicBroadcastState(bool active, const std::wstring& status);
    void SetCommsStatus(const std::wstring& status, const std::wstring& transcript, bool active);
    void SetAssistantStatus(const std::wstring& status, const std::wstring& transcript,
                            const std::wstring& response, bool active);
    void SetCommsShortcuts(const std::array<std::wstring, 4>& labels);
    void SetTtsSettings(int volumePercent, bool muted);
    void SetBroadcastGainDb(int gainDb);
    void SetShutdownHoldProgress(float progress);
    void SetRigHoldProgress(float resetProgress, float mountProgress);
    void SetClockText(const std::wstring& text);
    void SetBatteryEstimate(const std::wstring& text, int lowestPercent);
    void SetPressFeedback(float x, float y, bool active);
    void SetRigBodyArtPath(const std::wstring& path);
    void SetPlayspaceResetArtPath(const std::wstring& path);
    void SetHoloGlyphAtlasPath(const std::wstring& path);
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
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyWrapFormat_;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> labelEllipsis_;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> titleEllipsis_;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> bodyEllipsis_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glassBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stripBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> structureBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> structureDimBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activeFillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> buttonBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> warningBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> criticalBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bodyFillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> scanFillBrush_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> holoGlassBrush_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> holoGlyphBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> holoSpecularBrush_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> rigBodyArt_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> playspaceResetArt_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> holoGlyphAtlas_;
    HANDLE sharedTextureHandle_{};
    bool playspaceAdjusted_{};
    float playspaceHoldProgress_{};
    bool slimeAvailable_{};
    std::wstring musicVoiceStatus_{L"VOICE READY"};
    bool musicVoiceActive_{};
    bool musicPlaying_{};
    bool musicBroadcastActive_{};
    std::wstring musicBroadcastStatus_{L"BROADCAST OFF"};
    std::wstring commsStatus_{L"IDLE"};
    std::wstring commsTranscript_;
    bool commsActive_{};
    std::wstring assistantStatus_{L"READY"};
    std::wstring assistantTranscript_;
    std::wstring assistantResponse_;
    bool assistantActive_{};
    std::array<std::wstring, 4> commsShortcutLabels_{};
    int ttsVolumePercent_{85};
    bool ttsMuted_{};
    int broadcastGainDb_{12};
    float shutdownHoldProgress_{};
    float rigResetHoldProgress_{};
    float rigMountHoldProgress_{};
    std::wstring clockText_;
    std::wstring batteryEstimateText_;
    int lowestBatteryPercent_{-1};
    D2D1_POINT_2F pressFeedbackCenter_{};
    bool pressFeedbackActive_{};
    std::wstring rigBodyArtPath_;
    std::wstring playspaceResetArtPath_;
    std::wstring holoGlyphAtlasPath_;
};

}  // namespace interfayce
