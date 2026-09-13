#include "overlay_renderer.h"
#include "assistant_panel_layout.h"
#include <d2d1helper.h>
#include <algorithm>
#include <string_view>

namespace interfayce {
void OverlayRenderer::DrawAssistantPanel(const GlyphPainter& drawHoloAsset) {
    const auto drawText = [&](std::wstring_view text, IDWriteTextFormat* format,
                              D2D1_RECT_F rect, ID2D1Brush* brush) {
        d2dContext_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rect, brush);
    };
        drawText(assistantTranscript_.empty() ? L"Tap the core and ask anything"
                : (L"YOU  /  " + assistantTranscript_), labelFormat_.Get(),
            D2D1::RectF(42, 108, 726, 142),
            assistantTranscript_.empty() ? mutedTextBrush_.Get() : structureBrush_.Get());
        const auto answer = assistantResponse_.empty()
            ? std::wstring(L"Ask a question. Use the thumbstick over this text to scroll replies.") : assistantResponse_;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> answerLayout;
        if (!assistantWriteFactory_) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(assistantWriteFactory_.GetAddressOf()));
        if (assistantWriteFactory_ && SUCCEEDED(assistantWriteFactory_->CreateTextLayout(answer.c_str(), static_cast<UINT32>(answer.size()),
                bodyWrapFormat_.Get(), 680, 100000, &answerLayout))) {
            DWRITE_TEXT_METRICS metrics{};
            answerLayout->GetMetrics(&metrics);
            assistantMaximum_ = (std::max)(0.0F, metrics.height - 90.0F);
            assistantScroll_ = assistantFollow_ ? assistantMaximum_ : (std::min)(assistantScroll_, assistantMaximum_);
            d2dContext_->PushAxisAlignedClip(D2D1::RectF(42, 148, 726, 238), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            d2dContext_->DrawTextLayout(D2D1::Point2F(42, 148 - assistantScroll_), answerLayout.Get(), textBrush_.Get());
            d2dContext_->PopAxisAlignedClip();
        }

        const auto micCenter = D2D1::Point2F(220, 286);
        const auto cancelCenter = D2D1::Point2F(384, 286);
        const auto clearCenter = D2D1::Point2F(548, 286);
        if (!assistantChoices_.empty()) {
            for (size_t i = 0; i < assistantChoices_.size() && i < 3; ++i) {
                const auto bounds = assistant_panel::Choice(i);
                const float left = bounds.left;
                d2dContext_->DrawRectangle(D2D1::RectF(bounds.left, bounds.top, bounds.right, bounds.bottom), structureBrush_.Get(), 2);
                drawText(assistantChoices_[i], labelFormat_.Get(), D2D1::RectF(left + 8, 274, left + 146, 308), textBrush_.Get());
            }
        } else if (holoGlyphAtlas_) {
            drawHoloAsset(HoloGlyph::Assistant, micCenter, 49, 49, assistantActive_, true);
            drawHoloAsset(HoloGlyph::Close, cancelCenter, 38, 38, assistantActive_, true);
            drawHoloAsset(HoloGlyph::ClearChat, clearCenter, 38, 38, false, true);
        }
        drawText(L"STOP", labelFormat_.Get(), D2D1::RectF(42, 275, 122, 312), structureBrush_.Get());
        drawText(assistantDictate_ ? L"ANSWER" : L"SWITCH", labelFormat_.Get(), D2D1::RectF(640, 275, 726, 312), structureBrush_.Get());
        d2dContext_->DrawLine(D2D1::Point2F(42, 343), D2D1::Point2F(726, 343),
            structureDimBrush_.Get(), 1.0F);
        auto* assistantStateBrush = assistantActive_ ? accentBrush_.Get() : structureBrush_.Get();
        d2dContext_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(49, 355), 5, 5), assistantStateBrush);
        drawText(assistantStatus_, labelFormat_.Get(), D2D1::RectF(64, 344, 606, 374),
            assistantActive_ ? accentBrush_.Get() : mutedTextBrush_.Get());
}

void OverlayRenderer::SetAssistantChoices(const std::vector<std::wstring>& choices,
        bool dictate, const std::string& token) {
    if (token != assistantDecisionToken_) {
        if (assistantDecisionToken_.empty() && !token.empty()) {
            assistantChatScroll_ = assistantScroll_;
            assistantChatFollow_ = assistantFollow_;
        }
        assistantScroll_ = token.empty() ? assistantChatScroll_ : 0;
        assistantFollow_ = token.empty() ? assistantChatFollow_ : false;
        assistantDecisionToken_ = token;
    }
    assistantChoices_ = choices;
    assistantDictate_ = dictate;
}

void OverlayRenderer::SetAssistantStatus(const std::wstring& status,
                                         const std::wstring& transcript,
                                         const std::wstring& response, bool active) {
    assistantStatus_ = status;
    if (transcript != assistantTranscript_ && assistantDecisionToken_.empty()) { assistantScroll_ = 0; assistantFollow_ = true; }
    assistantTranscript_ = transcript;
    assistantResponse_ = response;
    assistantActive_ = active;
}
} // namespace interfayce
