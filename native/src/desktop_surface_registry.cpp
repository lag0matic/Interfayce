#include "desktop_surface_registry.h"

#include <d2d1helper.h>
#include <dwmapi.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace {

constexpr UINT kPickerWidth = 1024;
constexpr UINT kPickerHeight = 640;
constexpr UINT kKeyboardWidth = 1200;
constexpr UINT kKeyboardHeight = 440;
constexpr float kFrameAspectRatio = 128.0F;

struct KeyboardKeyDefinition {
    D2D1_RECT_F bounds{};
    std::wstring label;
    wchar_t character{};
    WORD virtualKey{};
    bool togglesShift{};
    bool togglesControl{};
    bool togglesAlt{};
};

std::vector<KeyboardKeyDefinition> KeyboardLayout(bool shifted) {
    std::vector<KeyboardKeyDefinition> keys;
    const auto addCharacters = [&](std::wstring_view normal, std::wstring_view upper,
                                   float x, float y, float width, float gap) {
        for (size_t index = 0; index < normal.size(); ++index) {
            const auto character = shifted ? upper[index] : normal[index];
            keys.push_back({D2D1::RectF(x, y, x + width, y + 58.0F),
                std::wstring(1, character), character});
            x += width + gap;
        }
    };
    keys.push_back({D2D1::RectF(24, 68, 94, 126), L"ESC", 0, VK_ESCAPE});
    addCharacters(L"1234567890-=", L"!@#$%^&*()_+", 104.0F, 68.0F, 66.0F, 6.0F);
    keys.push_back({D2D1::RectF(970, 68, 1176, 126), L"BACKSPACE", 0, VK_BACK});
    keys.push_back({D2D1::RectF(24, 136, 120, 194), L"TAB", 0, VK_TAB});
    addCharacters(L"qwertyuiop[]\\", L"QWERTYUIOP{}|", 130.0F, 136.0F, 70.0F, 6.0F);
    addCharacters(L"asdfghjkl;'", L"ASDFGHJKL:\"", 112.0F, 204.0F, 76.0F, 6.0F);
    keys.push_back({D2D1::RectF(1020, 204, 1176, 262), L"ENTER", 0, VK_RETURN});
    keys.push_back({D2D1::RectF(24, 272, 150, 330), L"SHIFT", 0, 0, true});
    addCharacters(L"zxcvbnm,./", L"ZXCVBNM<>?", 162.0F, 272.0F, 76.0F, 6.0F);
    keys.push_back({D2D1::RectF(994, 272, 1176, 330), L"SHIFT", 0, 0, true});
    keys.push_back({D2D1::RectF(24, 340, 114, 420), L"CTRL", 0, 0, false, true});
    keys.push_back({D2D1::RectF(126, 340, 216, 420), L"ALT", 0, 0, false, false, true});
    keys.push_back({D2D1::RectF(228, 340, 920, 420), L"SPACE", L' '});
    keys.push_back({D2D1::RectF(932, 340, 986, 420), L"LEFT", 0, VK_LEFT});
    keys.push_back({D2D1::RectF(994, 340, 1048, 420), L"DOWN", 0, VK_DOWN});
    keys.push_back({D2D1::RectF(1056, 340, 1110, 420), L"UP", 0, VK_UP});
    keys.push_back({D2D1::RectF(1118, 340, 1176, 420), L"RIGHT", 0, VK_RIGHT});
    return keys;
}

std::optional<size_t> KeyboardKeyAt(float x, float y, bool shifted) {
    const auto keys = KeyboardLayout(shifted);
    for (size_t index = 0; index < keys.size(); ++index) {
        const auto& bounds = keys[index].bounds;
        if (x >= bounds.left && x <= bounds.right && y >= bounds.top && y <= bounds.bottom) {
            return index;
        }
    }
    return std::nullopt;
}

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

vr::HmdMatrix34_t InverseRigid(const vr::HmdMatrix34_t& transform) {
    vr::HmdMatrix34_t inverse{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            inverse.m[row][column] = transform.m[column][row];
        }
        inverse.m[row][3] = -(inverse.m[row][0] * transform.m[0][3]
            + inverse.m[row][1] * transform.m[1][3]
            + inverse.m[row][2] * transform.m[2][3]);
    }
    return inverse;
}

size_t GrabIndex(interfayce::DesktopGrabHand hand) {
    return hand == interfayce::DesktopGrabHand::Left ? 0U : 1U;
}

vr::HmdVector3_t Translation(const vr::HmdMatrix34_t& transform) {
    return {{transform.m[0][3], transform.m[1][3], transform.m[2][3]}};
}

float Distance(const vr::HmdVector3_t& left, const vr::HmdVector3_t& right) {
    const auto x = right.v[0] - left.v[0];
    const auto y = right.v[1] - left.v[1];
    const auto z = right.v[2] - left.v[2];
    return std::sqrt(x * x + y * y + z * z);
}

vr::HmdVector3_t Midpoint(const vr::HmdVector3_t& left, const vr::HmdVector3_t& right) {
    return {{(left.v[0] + right.v[0]) * 0.5F, (left.v[1] + right.v[1]) * 0.5F,
        (left.v[2] + right.v[2]) * 0.5F}};
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

bool InjectDesktopScroll(const POINT point, int32_t verticalDelta, int32_t horizontalDelta) {
    if (!InjectDesktopPointer(point, interfayce::DesktopPointerEvent::Move)) return false;
    std::array<INPUT, 2> inputs{};
    UINT count = 0;
    if (verticalDelta != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[count].mi.mouseData = static_cast<DWORD>(verticalDelta);
        ++count;
    }
    if (horizontalDelta != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        inputs[count].mi.mouseData = static_cast<DWORD>(horizontalDelta);
        ++count;
    }
    return count == 0 || SendInput(count, inputs.data(), sizeof(INPUT)) == count;
}

bool InjectKeyboardKey(const KeyboardKeyDefinition& key, bool controlled, bool altered) {
    std::vector<INPUT> inputs;
    const auto addVirtual = [&inputs](WORD virtualKey, bool release) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = virtualKey;
        input.ki.dwFlags = release ? KEYEVENTF_KEYUP : 0;
        inputs.push_back(input);
    };
    if (controlled) addVirtual(VK_CONTROL, false);
    if (altered) addVirtual(VK_MENU, false);
    if (key.character != 0 && (controlled || altered)) {
        const auto mapped = VkKeyScanW(key.character);
        if (mapped == -1) return false;
        const auto virtualKey = static_cast<WORD>(mapped & 0xFF);
        addVirtual(virtualKey, false);
        addVirtual(virtualKey, true);
    } else if (key.character != 0) {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = key.character;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(down);
        down.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(down);
    } else if (key.virtualKey != 0) {
        addVirtual(key.virtualKey, false);
        addVirtual(key.virtualKey, true);
    } else {
        return false;
    }
    if (altered) addVirtual(VK_MENU, true);
    if (controlled) addVirtual(VK_CONTROL, true);
    const auto count = static_cast<UINT>(inputs.size());
    return SendInput(count, inputs.data(), sizeof(INPUT)) == count;
}

}  // namespace

namespace interfayce {

bool DesktopPickerTexture::Initialize(ID3D11Device* device, UINT width, UINT height) {
    if (device == nullptr) return false;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
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

bool DesktopPickerTexture::RenderKeyboard(const std::wstring& targetLabel, bool shifted,
                                          bool controlled, bool altered,
                                          std::optional<size_t> hoveredKey) {
    if (!context_ || !target_) return false;
    const auto drawText = [&](std::wstring_view text, IDWriteTextFormat* format,
                              const D2D1_RECT_F rectangle, ID2D1Brush* brush) {
        context_->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rectangle, brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    context_->BeginDraw();
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    context_->FillRectangle(D2D1::RectF(4, 4, 1196, 436), panelBrush_.Get());
    context_->DrawRectangle(D2D1::RectF(4, 4, 1196, 436), cyanBrush_.Get(), 2.0F);
    drawText(L"KEYBOARD", titleFormat_.Get(), D2D1::RectF(24, 14, 260, 54), textBrush_.Get());
    const std::wstring targetLine = targetLabel.empty()
        ? L"Target: click a captured surface first" : L"Target: " + targetLabel;
    drawText(targetLine, detailFormat_.Get(), D2D1::RectF(280, 22, 1176, 54),
        targetLabel.empty() ? mutedBrush_.Get() : cyanBrush_.Get());
    context_->DrawLine(D2D1::Point2F(24, 60), D2D1::Point2F(1176, 60), violetBrush_.Get(), 2.0F);

    const auto keys = KeyboardLayout(shifted);
    for (size_t index = 0; index < keys.size(); ++index) {
        const auto& key = keys[index];
        const bool selected = hoveredKey && *hoveredKey == index;
        const bool modifierSelected = (key.togglesShift && shifted)
            || (key.togglesControl && controlled) || (key.togglesAlt && altered);
        context_->FillRoundedRectangle(D2D1::RoundedRect(key.bounds, 8, 8),
            selected || modifierSelected ? violetBrush_.Get() : surfaceBrush_.Get());
        context_->DrawRoundedRectangle(D2D1::RoundedRect(key.bounds, 8, 8),
            selected ? cyanBrush_.Get() : mutedBrush_.Get(), selected ? 3.0F : 1.0F);
        drawText(key.label, itemFormat_.Get(),
            D2D1::RectF(key.bounds.left + 8, key.bounds.top + 18,
                key.bounds.right - 8, key.bounds.bottom - 8), textBrush_.Get());
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

bool DesktopSurfaceRegistry::CreateFrameOverlays(Surface& surface) const {
    constexpr uint32_t kLongEdge = 128;
    for (size_t index = 0; index < surface.frameOverlays.size(); ++index) {
        const auto key = surface.overlayKey + ".frame." + std::to_string(index);
        vr::VROverlayHandle_t stale = vr::k_ulOverlayHandleInvalid;
        if (vr::VROverlay()->FindOverlay(key.c_str(), &stale) == vr::VROverlayError_None) {
            vr::VROverlay()->DestroyOverlay(stale);
        }
        if (vr::VROverlay()->CreateOverlay(key.c_str(), "Interfayce Desktop Frame",
                &surface.frameOverlays[index]) != vr::VROverlayError_None) return false;
        vr::VROverlay()->SetOverlayInputMethod(
            surface.frameOverlays[index], vr::VROverlayInputMethod_None);
        vr::VROverlay()->SetOverlaySortOrder(surface.frameOverlays[index], 21);
        vr::VROverlay()->SetOverlayAlpha(surface.frameOverlays[index], 0.78F);

        const bool horizontal = index < 2;
        const uint32_t width = horizontal ? kLongEdge : 1;
        const uint32_t height = horizontal ? 1 : kLongEdge;
        std::array<uint8_t, kLongEdge * 4> pixels{};
        for (uint32_t pixelIndex = 0; pixelIndex < kLongEdge; ++pixelIndex) {
            const auto pixel = pixelIndex * 4U;
            const bool violet = index >= 2;
            pixels[pixel + 0] = violet ? 128 : 40;
            pixels[pixel + 1] = violet ? 74 : 230;
            pixels[pixel + 2] = 255;
            pixels[pixel + 3] = 255;
        }
        if (vr::VROverlay()->SetOverlayRaw(surface.frameOverlays[index], pixels.data(),
                width, height, 4) != vr::VROverlayError_None) return false;
    }
    return true;
}

bool DesktopSurfaceRegistry::UpdateFrameOverlays(const Surface& surface) const {
    const auto surfaceHeight = surface.widthMeters / (std::max)(surface.aspectRatio, 0.1F);
    const auto horizontalWidth = surface.widthMeters + 0.014F;
    const auto horizontalHeight = horizontalWidth / kFrameAspectRatio;
    const auto verticalHeight = surfaceHeight;
    const auto verticalWidth = verticalHeight / kFrameAspectRatio;
    for (size_t index = 0; index < surface.frameOverlays.size(); ++index) {
        if (surface.frameOverlays[index] == vr::k_ulOverlayHandleInvalid) return false;
        vr::HmdMatrix34_t local{};
        local.m[0][0] = 1.0F;
        local.m[1][1] = 1.0F;
        local.m[2][2] = 1.0F;
        local.m[2][3] = 0.001F;
        if (index < 2) {
            local.m[1][3] = (index == 0 ? 1.0F : -1.0F)
                * (surfaceHeight * 0.5F + horizontalHeight * 0.5F);
            vr::VROverlay()->SetOverlayWidthInMeters(
                surface.frameOverlays[index], horizontalWidth);
        } else {
            local.m[0][3] = (index == 2 ? -1.0F : 1.0F)
                * (surface.widthMeters * 0.5F + verticalWidth * 0.5F);
            vr::VROverlay()->SetOverlayWidthInMeters(
                surface.frameOverlays[index], verticalWidth);
        }
        const auto transform = Multiply(surface.transform, local);
        if (vr::VROverlay()->SetOverlayTransformAbsolute(surface.frameOverlays[index],
                vr::TrackingUniverseStanding, &transform) != vr::VROverlayError_None) return false;
    }
    return true;
}

void DesktopSurfaceRegistry::DestroySurfaceOverlays(Surface& surface) const {
    for (auto& frame : surface.frameOverlays) {
        if (frame != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(frame);
        frame = vr::k_ulOverlayHandleInvalid;
    }
    if (surface.overlay != vr::k_ulOverlayHandleInvalid) {
        vr::VROverlay()->DestroyOverlay(surface.overlay);
        surface.overlay = vr::k_ulOverlayHandleInvalid;
    }
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
    vr::VROverlay()->SetOverlayWidthInMeters(surface.overlay, surface.widthMeters);
    vr::VROverlay()->SetOverlayInputMethod(surface.overlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(surface.overlay, 20);
    const auto texture = surface.texture->Texture();
    if (!CreateFrameOverlays(surface)
        || vr::VROverlay()->SetOverlayTexture(surface.overlay, &texture) != vr::VROverlayError_None
        || !PlaceAtEyeLine(surface)
        || vr::VROverlay()->ShowOverlay(surface.overlay) != vr::VROverlayError_None) {
        DestroySurfaceOverlays(surface);
        return 0;
    }
    for (const auto frame : surface.frameOverlays) vr::VROverlay()->ShowOverlay(frame);
    surfaces_.push_back(std::move(surface));
    return surfaces_.back().id;
}

uint64_t DesktopSurfaceRegistry::SpawnKeyboard() {
    const auto existing = std::find_if(surfaces_.begin(), surfaces_.end(),
        [](const auto& surface) { return surface.keyboard; });
    if (existing != surfaces_.end()) {
        BringToMe(existing->id);
        return existing->id;
    }
    if (!system_ || !device_) return 0;
    Surface surface{};
    surface.id = nextId_++;
    surface.overlayKey = "com.lag0matic.interfayce.keyboard";
    surface.label = L"Keyboard";
    surface.keyboard = true;
    surface.widthMeters = 0.96F;
    surface.aspectRatio = static_cast<float>(kKeyboardWidth) / kKeyboardHeight;
    surface.texture = std::make_unique<DesktopPickerTexture>();
    std::wstring targetLabel;
    if (focusedSurfaceId_) {
        const auto target = std::find_if(surfaces_.begin(), surfaces_.end(),
            [this](const auto& candidate) { return candidate.id == *focusedSurfaceId_; });
        if (target != surfaces_.end() && target->capture) targetLabel = target->label;
    }
    if (!surface.texture->Initialize(device_, kKeyboardWidth, kKeyboardHeight)
        || !surface.texture->RenderKeyboard(targetLabel, false)) return 0;
    vr::VROverlayHandle_t stale = vr::k_ulOverlayHandleInvalid;
    if (vr::VROverlay()->FindOverlay(surface.overlayKey.c_str(), &stale)
        == vr::VROverlayError_None) vr::VROverlay()->DestroyOverlay(stale);
    if (vr::VROverlay()->CreateOverlay(surface.overlayKey.c_str(), "Interfayce Keyboard",
            &surface.overlay) != vr::VROverlayError_None) return 0;
    vr::VROverlay()->SetOverlayWidthInMeters(surface.overlay, surface.widthMeters);
    vr::VROverlay()->SetOverlayInputMethod(surface.overlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(surface.overlay, 20);
    const auto texture = surface.texture->Texture();
    if (!CreateFrameOverlays(surface)
        || vr::VROverlay()->SetOverlayTexture(surface.overlay, &texture) != vr::VROverlayError_None
        || !PlaceAtEyeLine(surface)
        || vr::VROverlay()->ShowOverlay(surface.overlay) != vr::VROverlayError_None) {
        DestroySurfaceOverlays(surface);
        return 0;
    }
    for (const auto frame : surface.frameOverlays) vr::VROverlay()->ShowOverlay(frame);
    surfaces_.push_back(std::move(surface));
    return surfaces_.back().id;
}

std::optional<DesktopSurfaceHit> DesktopSurfaceRegistry::HitTest(
        const vr::VROverlayIntersectionParams_t& ray) const {
    std::optional<DesktopSurfaceHit> nearest;
    for (const auto& surface : surfaces_) {
        if (!surface.visible || surface.keyboard) continue;
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

std::optional<KeyboardSurfaceHit> DesktopSurfaceRegistry::KeyboardHitTest(
        const vr::VROverlayIntersectionParams_t& ray) const {
    std::optional<KeyboardSurfaceHit> nearest;
    for (const auto& surface : surfaces_) {
        if (!surface.visible || !surface.keyboard) continue;
        vr::VROverlayIntersectionResults_t result{};
        if (!vr::VROverlay()->ComputeOverlayIntersection(surface.overlay, &ray, &result)) continue;
        const auto x = result.vUVs.v[0] * static_cast<float>(kKeyboardWidth);
        const auto y = (1.0F - result.vUVs.v[1]) * static_cast<float>(kKeyboardHeight);
        const auto keyIndex = KeyboardKeyAt(x, y, surface.keyboardShifted);
        if (!keyIndex) continue;
        KeyboardSurfaceHit hit{surface.id, *keyIndex, result.fDistance,
            result.vUVs.v[0], result.vUVs.v[1]};
        if (!nearest || hit.distance < nearest->distance) nearest = hit;
    }
    return nearest;
}

std::optional<uint64_t> DesktopSurfaceRegistry::FrameHitTest(
        const vr::VROverlayIntersectionParams_t& ray) const {
    std::optional<uint64_t> nearestId;
    float nearestDistance = (std::numeric_limits<float>::max)();
    for (const auto& surface : surfaces_) {
        if (!surface.visible) continue;
        for (const auto frame : surface.frameOverlays) {
            vr::VROverlayIntersectionResults_t result{};
            if (frame != vr::k_ulOverlayHandleInvalid
                && vr::VROverlay()->ComputeOverlayIntersection(frame, &ray, &result)
                && result.fDistance < nearestDistance) {
                nearestDistance = result.fDistance;
                nearestId = surface.id;
            }
        }
    }
    return nearestId;
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
    found->aspectRatio = capture->AspectRatio();
    found->capture = std::move(capture);
    UpdateFrameOverlays(*found);
    return true;
}

bool DesktopSurfaceRegistry::SendPointerEvent(const DesktopSurfaceHit& hit,
                                              DesktopPointerEvent event) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&hit](const auto& surface) { return surface.id == hit.id; });
    if (found == surfaces_.end() || !found->capture || !found->assignedSource
        || *found->assignedSource >= found->sources.size()) return false;
    if (event == DesktopPointerEvent::PrimaryDown) {
        focusedSurfaceId_ = found->id;
        for (auto& surface : surfaces_) {
            if (surface.keyboard) {
                surface.texture->RenderKeyboard(
                    found->label, surface.keyboardShifted, surface.keyboardControlled,
                    surface.keyboardAltered, surface.hoveredKey);
            }
        }
    }
    const auto point = DesktopPointForHit(found->sources[*found->assignedSource], hit.u, hit.v);
    return point && InjectDesktopPointer(*point, event);
}

bool DesktopSurfaceRegistry::SendScrollEvent(const DesktopSurfaceHit& hit,
                                             int32_t verticalDelta,
                                             int32_t horizontalDelta) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&hit](const auto& surface) { return surface.id == hit.id; });
    if (found == surfaces_.end() || !found->capture || !found->assignedSource
        || *found->assignedSource >= found->sources.size()) return false;
    const auto point = DesktopPointForHit(found->sources[*found->assignedSource], hit.u, hit.v);
    return point && InjectDesktopScroll(*point, verticalDelta, horizontalDelta);
}

bool DesktopSurfaceRegistry::ActivateKeyboardHit(const KeyboardSurfaceHit& hit) {
    const auto keyboard = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&hit](const auto& surface) { return surface.id == hit.id && surface.keyboard; });
    if (keyboard == surfaces_.end()) return false;
    const auto keys = KeyboardLayout(keyboard->keyboardShifted);
    if (hit.keyIndex >= keys.size()) return false;
    const auto& key = keys[hit.keyIndex];
    if (key.togglesShift) {
        keyboard->keyboardShifted = !keyboard->keyboardShifted;
        keyboard->hoveredKey.reset();
    } else if (key.togglesControl) {
        keyboard->keyboardControlled = !keyboard->keyboardControlled;
        keyboard->hoveredKey.reset();
    } else if (key.togglesAlt) {
        keyboard->keyboardAltered = !keyboard->keyboardAltered;
        keyboard->hoveredKey.reset();
    } else {
        if (!focusedSurfaceId_) return false;
        const auto target = std::find_if(surfaces_.begin(), surfaces_.end(),
            [this](const auto& surface) { return surface.id == *focusedSurfaceId_; });
        if (target == surfaces_.end() || !target->capture || !target->assignedSource
            || *target->assignedSource >= target->sources.size()) return false;
        const auto& source = target->sources[*target->assignedSource];
        if (source.kind == DesktopSource::Kind::Window && source.window != nullptr) {
            if (IsIconic(source.window)) ShowWindowAsync(source.window, SW_RESTORE);
            SetForegroundWindow(source.window);
        }
        if (!InjectKeyboardKey(key, keyboard->keyboardControlled, keyboard->keyboardAltered)) {
            return false;
        }
        if (keyboard->keyboardShifted && key.character != 0) keyboard->keyboardShifted = false;
        keyboard->keyboardControlled = false;
        keyboard->keyboardAltered = false;
    }
    std::wstring targetLabel;
    if (focusedSurfaceId_) {
        const auto target = std::find_if(surfaces_.begin(), surfaces_.end(),
            [this](const auto& surface) { return surface.id == *focusedSurfaceId_; });
        if (target != surfaces_.end() && target->capture) targetLabel = target->label;
    }
    return keyboard->texture->RenderKeyboard(
        targetLabel, keyboard->keyboardShifted, keyboard->keyboardControlled,
        keyboard->keyboardAltered, keyboard->hoveredKey);
}

std::optional<vr::HmdMatrix34_t> DesktopSurfaceRegistry::CursorTransform(
        const DesktopSurfaceHit& hit) const {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&hit](const auto& surface) { return surface.id == hit.id; });
    if (found == surfaces_.end()) return std::nullopt;

    const auto surfaceWidth = found->widthMeters;
    const auto surfaceHeight = surfaceWidth / (std::max)(found->aspectRatio, 0.1F);
    vr::HmdMatrix34_t local{};
    local.m[0][0] = 1.0F;
    local.m[1][1] = 1.0F;
    local.m[2][2] = 1.0F;
    local.m[0][3] = (hit.u - 0.5F) * surfaceWidth;
    local.m[1][3] = (hit.v - 0.5F) * surfaceHeight;
    local.m[2][3] = 0.006F;
    return Multiply(found->transform, local);
}

std::optional<vr::HmdMatrix34_t> DesktopSurfaceRegistry::KeyboardCursorTransform(
        const KeyboardSurfaceHit& hit) const {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&hit](const auto& surface) { return surface.id == hit.id && surface.keyboard; });
    if (found == surfaces_.end()) return std::nullopt;
    const auto height = found->widthMeters / (std::max)(found->aspectRatio, 0.1F);
    vr::HmdMatrix34_t local{};
    local.m[0][0] = 1.0F;
    local.m[1][1] = 1.0F;
    local.m[2][2] = 1.0F;
    local.m[0][3] = (hit.u - 0.5F) * found->widthMeters;
    local.m[1][3] = (hit.v - 0.5F) * height;
    local.m[2][3] = 0.006F;
    return Multiply(found->transform, local);
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

void DesktopSurfaceRegistry::SetHoveredKeyboard(const std::optional<KeyboardSurfaceHit>& hit) {
    std::wstring targetLabel;
    if (focusedSurfaceId_) {
        const auto target = std::find_if(surfaces_.begin(), surfaces_.end(),
            [this](const auto& surface) { return surface.id == *focusedSurfaceId_; });
        if (target != surfaces_.end() && target->capture) targetLabel = target->label;
    }
    for (auto& surface : surfaces_) {
        if (!surface.keyboard) continue;
        const auto nextHover = hit && hit->id == surface.id
            ? std::optional<size_t>(hit->keyIndex) : std::nullopt;
        if (nextHover == surface.hoveredKey) continue;
        surface.hoveredKey = nextHover;
        surface.texture->RenderKeyboard(targetLabel, surface.keyboardShifted,
            surface.keyboardControlled, surface.keyboardAltered, surface.hoveredKey);
    }
}

void DesktopSurfaceRegistry::SetHoveredFrame(std::optional<uint64_t> id) {
    for (const auto& surface : surfaces_) {
        const bool highlighted = (id && *id == surface.id)
            || std::any_of(activeGrabs_.begin(), activeGrabs_.end(), [&surface](const auto& grab) {
                return grab && grab->id == surface.id;
            });
        for (const auto frame : surface.frameOverlays) {
            vr::VROverlay()->SetOverlayAlpha(frame, highlighted ? 1.0F : 0.78F);
        }
    }
}

void DesktopSurfaceRegistry::Update() {
    for (auto& surface : surfaces_) {
        if (!surface.capture) continue;
        const auto result = surface.capture->Update();
        if (result == DesktopCapture::UpdateResult::TextureChanged) {
            const auto texture = surface.capture->Texture();
            vr::VROverlay()->SetOverlayTexture(surface.overlay, &texture);
            surface.aspectRatio = surface.capture->AspectRatio();
            UpdateFrameOverlays(surface);
        } else if (result == DesktopCapture::UpdateResult::Closed
                   || result == DesktopCapture::UpdateResult::Failed) {
            surface.capture->Stop();
            surface.capture.reset();
            if (focusedSurfaceId_ && *focusedSurfaceId_ == surface.id) focusedSurfaceId_.reset();
            surface.assignedSource.reset();
            surface.label = L"Choose source";
            surface.aspectRatio = static_cast<float>(kPickerWidth) / kPickerHeight;
            surface.texture->Render(surface.sources);
            const auto texture = surface.texture->Texture();
            vr::VROverlay()->SetOverlayTexture(surface.overlay, &texture);
            UpdateFrameOverlays(surface);
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
    offset.m[1][3] = surface.keyboard ? -0.28F : -0.04F;
    offset.m[2][3] = -1.05F;
    const auto transform = Multiply(hmd.mDeviceToAbsoluteTracking, offset);
    if (vr::VROverlay()->SetOverlayTransformAbsolute(surface.overlay,
            vr::TrackingUniverseStanding, &transform) != vr::VROverlayError_None) return false;
    surface.transform = transform;
    return UpdateFrameOverlays(surface);
}

bool DesktopSurfaceRegistry::BringToMe(uint64_t id) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [id](const auto& surface) { return surface.id == id; });
    if (found == surfaces_.end() || !PlaceAtEyeLine(*found)) return false;
    found->visible = vr::VROverlay()->ShowOverlay(found->overlay) == vr::VROverlayError_None;
    if (found->visible) {
        for (const auto frame : found->frameOverlays) vr::VROverlay()->ShowOverlay(frame);
    }
    return found->visible;
}

bool DesktopSurfaceRegistry::Close(uint64_t id) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [id](const auto& surface) { return surface.id == id; });
    if (found == surfaces_.end()) return false;
    for (auto& grab : activeGrabs_) {
        if (grab && grab->id == id) grab.reset();
    }
    if (activeScale_ && activeScale_->id == id) activeScale_.reset();
    if (focusedSurfaceId_ && *focusedSurfaceId_ == id) {
        focusedSurfaceId_.reset();
        for (auto& surface : surfaces_) {
            if (surface.keyboard && surface.id != id) {
                surface.texture->RenderKeyboard(L"", surface.keyboardShifted,
                    surface.keyboardControlled, surface.keyboardAltered, surface.hoveredKey);
            }
        }
    }
    DestroySurfaceOverlays(*found);
    surfaces_.erase(found);
    return true;
}

bool DesktopSurfaceRegistry::BeginGrab(uint64_t id, DesktopGrabHand hand,
                                       const vr::HmdMatrix34_t& handTransform) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [id](const auto& surface) { return surface.id == id; });
    if (found == surfaces_.end()) return false;
    const auto index = GrabIndex(hand);
    activeGrabs_[index] = GrabState{
        id, Multiply(InverseRigid(handTransform), found->transform), handTransform};
    const auto otherIndex = 1U - index;
    if (activeGrabs_[otherIndex] && activeGrabs_[otherIndex]->id == id) {
        const auto leftPosition = Translation(activeGrabs_[0]->lastHandTransform);
        const auto rightPosition = Translation(activeGrabs_[1]->lastHandTransform);
        const auto span = Distance(leftPosition, rightPosition);
        if (span >= 0.05F) {
            activeScale_ = ScaleState{id, span, found->widthMeters,
                Midpoint(leftPosition, rightPosition), found->transform};
        }
    }
    return true;
}

bool DesktopSurfaceRegistry::UpdateGrab(DesktopGrabHand hand,
                                        const vr::HmdMatrix34_t& handTransform) {
    const auto index = GrabIndex(hand);
    if (!activeGrabs_[index]) return false;
    activeGrabs_[index]->lastHandTransform = handTransform;
    if (activeScale_) {
        if (!activeGrabs_[0] || !activeGrabs_[1]
            || activeGrabs_[0]->id != activeScale_->id
            || activeGrabs_[1]->id != activeScale_->id) {
            activeScale_.reset();
        } else {
            const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
                [this](const auto& surface) { return surface.id == activeScale_->id; });
            if (found == surfaces_.end()) return false;
            const auto leftPosition = Translation(activeGrabs_[0]->lastHandTransform);
            const auto rightPosition = Translation(activeGrabs_[1]->lastHandTransform);
            const auto midpoint = Midpoint(leftPosition, rightPosition);
            const auto span = Distance(leftPosition, rightPosition);
            found->widthMeters = std::clamp(
                activeScale_->initialWidth * span / activeScale_->initialSpan, 0.30F, 2.40F);
            found->transform = activeScale_->initialSurfaceTransform;
            for (int axis = 0; axis < 3; ++axis) {
                found->transform.m[axis][3] += midpoint.v[axis] - activeScale_->initialMidpoint.v[axis];
            }
            vr::VROverlay()->SetOverlayWidthInMeters(found->overlay, found->widthMeters);
            if (vr::VROverlay()->SetOverlayTransformAbsolute(found->overlay,
                    vr::TrackingUniverseStanding, &found->transform) != vr::VROverlayError_None) {
                return false;
            }
            return UpdateFrameOverlays(*found);
        }
    }
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [this, index](const auto& surface) { return surface.id == activeGrabs_[index]->id; });
    if (found == surfaces_.end()) {
        activeGrabs_[index].reset();
        return false;
    }
    found->transform = Multiply(handTransform, activeGrabs_[index]->handToSurface);
    if (vr::VROverlay()->SetOverlayTransformAbsolute(found->overlay,
            vr::TrackingUniverseStanding, &found->transform) != vr::VROverlayError_None) return false;
    return UpdateFrameOverlays(*found);
}

void DesktopSurfaceRegistry::EndGrab(DesktopGrabHand hand) {
    const auto index = GrabIndex(hand);
    const auto releasedId = activeGrabs_[index] ? activeGrabs_[index]->id : 0;
    activeGrabs_[index].reset();
    if (!activeScale_ || activeScale_->id != releasedId) return;
    activeScale_.reset();
    const auto otherIndex = 1U - index;
    if (!activeGrabs_[otherIndex]) return;
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(),
        [this, otherIndex](const auto& surface) {
            return surface.id == activeGrabs_[otherIndex]->id;
        });
    if (found != surfaces_.end()) {
        activeGrabs_[otherIndex]->handToSurface = Multiply(
            InverseRigid(activeGrabs_[otherIndex]->lastHandTransform), found->transform);
    }
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
    for (auto& grab : activeGrabs_) grab.reset();
    activeScale_.reset();
    focusedSurfaceId_.reset();
    for (auto& surface : surfaces_) {
        DestroySurfaceOverlays(surface);
    }
    surfaces_.clear();
}

}  // namespace interfayce
