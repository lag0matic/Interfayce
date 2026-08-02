#include "desktop_surface_manager.h"

#include <algorithm>
#include <cwctype>
#include <dwmapi.h>
#include <filesystem>
#include <unordered_set>

namespace {

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

std::wstring ProcessNameForWindow(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0 || processId == GetCurrentProcessId()) return {};

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) return {};
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const bool found = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
    CloseHandle(process);
    if (!found) return {};
    path.resize(length);
    return std::filesystem::path(path).filename().wstring();
}

bool IsUsefulApplicationWindow(HWND window, const std::wstring& processName) {
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
        return false;
    }
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        return false;
    }

    static const std::unordered_set<std::wstring> ignoredProcesses{
        L"applicationframehost.exe",
        L"explorer.exe",
        L"lockapp.exe",
        L"searchhost.exe",
        L"shellexperiencehost.exe",
        L"startmenuexperiencehost.exe",
        L"textinputhost.exe",
        L"widgets.exe",
    };
    return !processName.empty() && !ignoredProcesses.contains(Lowercase(processName));
}

}  // namespace

namespace interfayce {

std::vector<DesktopSource> DesktopSurfaceManager::EnumerateDisplays() const {
    std::vector<DesktopSource> displays;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        auto& output = *reinterpret_cast<std::vector<DesktopSource>*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            DISPLAY_DEVICEW device{};
            device.cb = sizeof(device);
            const bool hasFriendlyName = EnumDisplayDevicesW(info.szDevice, 0, &device, 0) != FALSE;
            const auto label = hasFriendlyName && device.DeviceString[0] != L'\0'
                ? std::wstring(device.DeviceString)
                : std::wstring(info.szDevice);
            output.push_back({DesktopSource::Kind::Display, info.szDevice, label,
                (info.dwFlags & MONITORINFOF_PRIMARY) ? L"Primary display" : L"Display", nullptr, monitor});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&displays));
    return displays;
}

std::vector<DesktopSource> DesktopSurfaceManager::EnumerateWindows() const {
    std::vector<DesktopSource> windows;
    EnumWindows([](HWND window, LPARAM data) {
        const auto length = GetWindowTextLengthW(window);
        if (length == 0) return TRUE;
        const auto processName = ProcessNameForWindow(window);
        if (!IsUsefulApplicationWindow(window, processName)) return TRUE;
        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        title.resize(static_cast<size_t>(length));
        auto& output = *reinterpret_cast<std::vector<DesktopSource>*>(data);
        output.push_back({DesktopSource::Kind::Window,
            std::to_wstring(reinterpret_cast<uintptr_t>(window)), title, processName, window, nullptr});
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
