#include "tray_icon.h"

#include <shellapi.h>

#include <string>

#ifndef INTERFAYCE_VERSION
#define INTERFAYCE_VERSION "development"
#endif

namespace interfayce {
namespace {

constexpr wchar_t kWindowClass[] = L"InterfayceTrayWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kOpenSettings = 1001;
constexpr UINT kRestart = 1002;
constexpr UINT kExit = 1003;

}  // namespace

TrayIcon::~TrayIcon() {
    Shutdown();
}

bool TrayIcon::Initialize(HINSTANCE instance) {
    if (window_ != nullptr) return true;
    instance_ = instance;
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Interfayce",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (window_ == nullptr) return false;

    icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    ownsIcon_ = icon_ != nullptr;
    if (icon_ == nullptr) icon_ = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

    return AddNotificationIcon();
}

bool TrayIcon::AddNotificationIcon() {
    if (window_ == nullptr || icon_ == nullptr) return false;
    NOTIFYICONDATAW notification{};
    notification.cbSize = sizeof(notification);
    notification.hWnd = window_;
    notification.uID = 1;
    notification.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    notification.uCallbackMessage = kTrayMessage;
    notification.hIcon = icon_;
    const std::wstring tip = L"Interfayce " + std::wstring(
        INTERFAYCE_VERSION, INTERFAYCE_VERSION + std::char_traits<char>::length(INTERFAYCE_VERSION))
        + L" - Running";
    wcscpy_s(notification.szTip, tip.c_str());
    if (!Shell_NotifyIconW(NIM_ADD, &notification)) return false;
    notification.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &notification);
    return true;
}

std::optional<TrayAction> TrayIcon::Poll() {
    if (window_ == nullptr) return std::nullopt;
    MSG message{};
    while (PeekMessageW(&message, window_, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    auto action = pendingAction_;
    pendingAction_.reset();
    return action;
}

void TrayIcon::Shutdown() {
    if (window_ != nullptr) {
        NOTIFYICONDATAW notification{};
        notification.cbSize = sizeof(notification);
        notification.hWnd = window_;
        notification.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &notification);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (ownsIcon_ && icon_ != nullptr) DestroyIcon(icon_);
    icon_ = nullptr;
    ownsIcon_ = false;
    if (instance_ != nullptr) UnregisterClassW(kWindowClass, instance_);
    instance_ = nullptr;
}

LRESULT CALLBACK TrayIcon::WindowProcedure(HWND window, UINT message,
                                            WPARAM wParam, LPARAM lParam) {
    auto* tray = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        tray = static_cast<TrayIcon*>(create->lpCreateParams);
        tray->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tray));
    }
    return tray != nullptr
        ? tray->HandleMessage(message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayIcon::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        AddNotificationIcon();
        return 0;
    }
    if (message == kTrayMessage) {
        const UINT notification = LOWORD(lParam);
        if (notification == WM_CONTEXTMENU || notification == WM_RBUTTONUP) {
            ShowMenu();
        } else if (notification == WM_LBUTTONDBLCLK || notification == NIN_KEYSELECT) {
            pendingAction_ = TrayAction::OpenSettings;
        }
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void TrayIcon::ShowMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const std::string versionText = std::string("Interfayce ") + INTERFAYCE_VERSION;
    const std::wstring version(versionText.begin(), versionText.end());
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, version.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kOpenSettings, L"Open Settings");
    AppendMenuW(menu, MF_STRING, kRestart, L"Restart Interfayce");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExit, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        cursor.x, cursor.y, 0, window_, nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);

    if (command == kOpenSettings) pendingAction_ = TrayAction::OpenSettings;
    else if (command == kRestart) pendingAction_ = TrayAction::Restart;
    else if (command == kExit) pendingAction_ = TrayAction::Exit;
}

}  // namespace interfayce
