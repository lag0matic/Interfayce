#include "desktop_surface_manager.h"

#include <algorithm>
#include <appmodel.h>
#include <cstring>
#include <cwctype>
#include <dwmapi.h>
#include <filesystem>
#include <shellapi.h>
#include <unordered_set>

namespace {

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

struct ProcessIdentity {
    std::wstring name;
    std::wstring path;
    std::wstring packageFamily;
};

ProcessIdentity ProcessForWindow(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0 || processId == GetCurrentProcessId()) return {};

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) return {};
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const bool found = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
    std::wstring packageFamily;
    UINT32 packageLength = 0;
    if (GetPackageFamilyName(process, &packageLength, nullptr) == ERROR_INSUFFICIENT_BUFFER
        && packageLength > 1) {
        packageFamily.resize(packageLength);
        if (GetPackageFamilyName(process, &packageLength, packageFamily.data()) == ERROR_SUCCESS) {
            if (!packageFamily.empty() && packageFamily.back() == L'\0') packageFamily.pop_back();
        } else {
            packageFamily.clear();
        }
    }
    CloseHandle(process);
    if (!found) return {};
    path.resize(length);
    return {std::filesystem::path(path).filename().wstring(), path, packageFamily};
}

bool IsAppIdTarget(const std::wstring& target) {
    return target.starts_with(L"aumid:") && target.find(L'!') != std::wstring::npos;
}

std::wstring PackageFamilyFromTarget(const std::wstring& target) {
    if (!IsAppIdTarget(target)) return {};
    const auto separator = target.find(L'!', 6);
    return separator == std::wstring::npos ? std::wstring{} : target.substr(6, separator - 6);
}

bool IsSafeAppId(const std::wstring& target) {
    if (!IsAppIdTarget(target)) return false;
    return std::all_of(target.begin() + 6, target.end(), [](wchar_t character) {
        return std::iswalnum(character) || character == L'.' || character == L'_'
            || character == L'-' || character == L'!';
    });
}

std::vector<uint8_t> IconPixelsForExecutable(const std::wstring& path) {
    if (path.empty()) return {};
    SHFILEINFOW fileInfo{};
    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &fileInfo, sizeof(fileInfo),
            SHGFI_ICON | SHGFI_SMALLICON) == 0 || fileInfo.hIcon == nullptr) return {};
    constexpr int size = 32;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HDC dc = CreateCompatibleDC(nullptr);
    const HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    std::vector<uint8_t> pixels;
    if (dc != nullptr && bitmap != nullptr && bits != nullptr) {
        const auto previous = SelectObject(dc, bitmap);
        std::memset(bits, 0, size * size * 4);
        DrawIconEx(dc, 0, 0, fileInfo.hIcon, size, size, 0, nullptr, DI_NORMAL);
        pixels.assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + size * size * 4);
        SelectObject(dc, previous);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (dc != nullptr) DeleteDC(dc);
    DestroyIcon(fileInfo.hIcon);
    return pixels;
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

std::optional<interfayce::DesktopSource> SourceForWindow(HWND window) {
    const auto length = GetWindowTextLengthW(window);
    if (length == 0) return std::nullopt;
    const auto process = ProcessForWindow(window);
    if (!IsUsefulApplicationWindow(window, process.name)) return std::nullopt;
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    title.resize(static_cast<size_t>(length));
    return interfayce::DesktopSource{interfayce::DesktopSource::Kind::Window,
        std::to_wstring(reinterpret_cast<uintptr_t>(window)), title, process.name,
        process.path, IconPixelsForExecutable(process.path), window, nullptr};
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
                (info.dwFlags & MONITORINFOF_PRIMARY) ? L"Primary display" : L"Display",
                {}, {}, nullptr, monitor});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&displays));
    return displays;
}

std::vector<DesktopSource> DesktopSurfaceManager::EnumerateWindows() const {
    std::vector<DesktopSource> windows;
    EnumWindows([](HWND window, LPARAM data) {
        auto& output = *reinterpret_cast<std::vector<DesktopSource>*>(data);
        if (auto source = SourceForWindow(window)) output.push_back(std::move(*source));
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

std::optional<DesktopSource> DesktopSurfaceManager::FindWindowForTarget(
        const std::wstring& target) const {
    if (target.empty()) return std::nullopt;
    const bool appId = IsAppIdTarget(target);
    const auto wanted = appId
        ? Lowercase(PackageFamilyFromTarget(target))
        : Lowercase(std::filesystem::path(target).lexically_normal().wstring());
    struct Context {
        const std::wstring* wanted{};
        bool appId{};
        std::optional<DesktopSource> source;
    } context{&wanted, appId};
    EnumWindows([](HWND window, LPARAM data) {
        auto& context = *reinterpret_cast<Context*>(data);
        const auto process = ProcessForWindow(window);
        const auto identity = context.appId
            ? Lowercase(process.packageFamily)
            : Lowercase(std::filesystem::path(process.path).lexically_normal().wstring());
        if (identity != *context.wanted) return TRUE;
        context.source = SourceForWindow(window);
        return context.source ? FALSE : TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.source;
}

bool DesktopSurfaceManager::LaunchTarget(const std::wstring& target) const {
    if (IsAppIdTarget(target)) {
        if (!IsSafeAppId(target)) return false;
        const auto shellTarget = L"shell:AppsFolder\\" + target.substr(6);
        return reinterpret_cast<INT_PTR>(ShellExecuteW(
            nullptr, L"open", shellTarget.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }
    const std::filesystem::path path(target);
    if (!path.is_absolute() || Lowercase(path.extension().wstring()) != L".exe"
        || !std::filesystem::is_regular_file(path)) return false;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto workingDirectory = path.parent_path().wstring();
    const bool launched = CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE,
        CREATE_DEFAULT_ERROR_MODE, nullptr, workingDirectory.c_str(), &startup, &process) != FALSE;
    if (!launched) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace interfayce
