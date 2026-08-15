#pragma once

#include <Windows.h>

#include <optional>

namespace interfayce {

enum class TrayAction {
    OpenSettings,
    Restart,
    Exit,
};

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Initialize(HINSTANCE instance);
    std::optional<TrayAction> Poll();
    void Shutdown();

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void ShowMenu();
    bool AddNotificationIcon();

    HINSTANCE instance_{};
    HWND window_{};
    HICON icon_{};
    bool ownsIcon_{};
    UINT taskbarCreatedMessage_{};
    std::optional<TrayAction> pendingAction_;
};

}  // namespace interfayce
