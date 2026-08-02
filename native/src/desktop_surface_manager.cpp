#include "desktop_surface_manager.h"

#include <algorithm>

namespace interfayce {

std::vector<DesktopSource> DesktopSurfaceManager::EnumerateDisplays() const {
    std::vector<DesktopSource> displays;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        auto& output = *reinterpret_cast<std::vector<DesktopSource>*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            output.push_back({DesktopSource::Kind::Display, info.szDevice,
                std::wstring(L"Display ") + info.szDevice, nullptr});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&displays));
    return displays;
}

std::vector<DesktopSource> DesktopSurfaceManager::EnumerateWindows() const {
    std::vector<DesktopSource> windows;
    EnumWindows([](HWND window, LPARAM data) {
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
        const auto length = GetWindowTextLengthW(window);
        if (length == 0) return TRUE;
        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        title.resize(static_cast<size_t>(length));
        auto& output = *reinterpret_cast<std::vector<DesktopSource>*>(data);
        output.push_back({DesktopSource::Kind::Window,
            std::to_wstring(reinterpret_cast<uintptr_t>(window)), title, window});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    std::sort(windows.begin(), windows.end(), [](const auto& left, const auto& right) {
        return left.label < right.label;
    });
    return windows;
}

std::vector<DesktopSource> DesktopSurfaceManager::EnumerateSources() const {
    auto sources = EnumerateDisplays();
    auto windows = EnumerateWindows();
    sources.insert(sources.end(), windows.begin(), windows.end());
    return sources;
}

}  // namespace interfayce
