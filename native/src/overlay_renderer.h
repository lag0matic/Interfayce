#pragma once

#include <openvr.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <array>
#include <string>
#include <wrl/client.h>

namespace interfayce {

class OverlayRenderer {
public:
    bool Initialize(vr::IVRSystem* system, int deck = 2, const std::wstring& musicLine = L"",
                    const std::wstring& musicArtPath = L"", const std::wstring& rigLine = L"",
                    const std::array<std::wstring, 8>& rigSlots = {}, bool mountReady = false);
    vr::Texture_t Texture() const;

private:
    bool Render(int deck, const std::wstring& musicLine, const std::wstring& musicArtPath,
                const std::wstring& rigLine, const std::array<std::wstring, 8>& rigSlots,
                bool mountReady);

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
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> buttonBrush_;
    HANDLE sharedTextureHandle_{};
};

}  // namespace interfayce
