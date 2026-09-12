#include "private_window_manager.h"
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <setupapi.h>

namespace interfayce {
namespace {
constexpr wchar_t markerName[] = L"Interfayce.PrivateWindow.v1";
struct DpiScope {
    DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope() { if (old) SetThreadDpiAwarenessContext(old); }
};
struct Record {
    uint32_t magic{0x49565031};
    uint32_t size{sizeof(Record)};
    uint64_t window{}, cookie{};
    DWORD pid{};
    FILETIME created{};
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    RECT originalScreen{};
    wchar_t display[512]{};
};
std::filesystem::path Directory() {
    PWSTR local{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    auto result = std::filesystem::path(local) / L"Interfayce" / L"private-windows";
    CoTaskMemFree(local);
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return error ? std::filesystem::path{} : result;
}
std::wstring EventName(const std::filesystem::path& path, const wchar_t* suffix) {
    return L"Local\\Interfayce.Private." + path.stem().wstring() + suffix;
}
struct Lock {
    HANDLE handle{};
    bool acquired{};
    explicit Lock(const std::filesystem::path& path) {
        handle = CreateMutexW(nullptr, FALSE, EventName(path, L".lock").c_str());
        if (handle) { auto wait = WaitForSingleObject(handle, 2000); acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED; }
    }
    ~Lock() { if (acquired) ReleaseMutex(handle); if (handle) CloseHandle(handle); }
};
bool Read(const std::filesystem::path& path, Record& record) {
    std::ifstream file(path, std::ios::binary);
    return file.read(reinterpret_cast<char*>(&record), sizeof(record))
        && file.peek() == EOF && record.magic == 0x49565031 && record.size == sizeof(record)
        && record.placement.length == sizeof(WINDOWPLACEMENT)
        && record.display[511] == 0;
}
bool Write(const std::filesystem::path& path, const Record& record) {
    auto temporary = path; temporary += L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    const bool ok = WriteFile(file, &record, sizeof(record), &written, nullptr)
        && written == sizeof(record) && FlushFileBuffers(file);
    CloseHandle(file);
    return ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
bool ProcessCreated(DWORD pid, FILETIME& created) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    FILETIME exit{}, kernel{}, user{};
    bool ok = GetProcessTimes(process, &created, &exit, &kernel, &user) != FALSE;
    CloseHandle(process); return ok;
}
bool Matches(const Record& record) {
    HWND window = reinterpret_cast<HWND>(record.window);
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    FILETIME created{};
    return IsWindow(window) && pid == record.pid && ProcessCreated(pid, created)
        && CompareFileTime(&created, &record.created) == 0
        && reinterpret_cast<uint64_t>(GetPropW(window, markerName)) == record.cookie;
}
bool Inside(RECT rectangle, RECT work) {
    return rectangle.left >= work.left && rectangle.top >= work.top
        && rectangle.right <= work.right && rectangle.bottom <= work.bottom;
}
bool VisibleFrame(HWND window, RECT& rect) {
    // Maximized GetWindowRect includes invisible resize borders outside the
    // monitor. DWM bounds match the visible content used by window capture.
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))
        || GetWindowRect(window, &rect);
}
bool IsCherryAdapter(LUID luid) {
    DISPLAYCONFIG_ADAPTER_NAME adapter{};
    adapter.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME, sizeof(adapter), luid, 0};
    if (DisplayConfigGetDeviceInfo(&adapter.header)) return false;
    HDEVINFO devices = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (devices == INVALID_HANDLE_VALUE) return false;
    SP_DEVICE_INTERFACE_DATA interfaceData{sizeof(interfaceData)};
    bool found = false;
    if (SetupDiOpenDeviceInterfaceW(devices, adapter.adapterDevicePath, 0, &interfaceData)) {
        DWORD required{};
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0, &required, nullptr);
        if (required >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            std::vector<BYTE> buffer(required);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(*detail);
            SP_DEVINFO_DATA device{sizeof(device)};
            if (SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, required, nullptr, &device)) {
                wchar_t ids[2048]{};
                if (SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_HARDWAREID, nullptr,
                    reinterpret_cast<PBYTE>(ids), sizeof(ids), nullptr)) {
                    for (const wchar_t* id = ids; *id; id += wcslen(id) + 1) {
                        if (_wcsicmp(id, L"Root\\VirtualDisplayDriver") == 0) found = true;
                    }
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return found;
}
bool Parked(const Record& record) {
    if (!Matches(record)) return false;
    DpiScope dpi;
    RECT rect{};
    if (IsIconic(reinterpret_cast<HWND>(record.window)) || !VisibleFrame(reinterpret_cast<HWND>(record.window), rect)) return false;
    for (const auto& display : PrivateDisplays()) {
        if (display.candidate && display.identity == record.display && Inside(rect, display.work)) return true;
    }
    return false;
}
bool RemoveRecord(const std::filesystem::path& path) {
    return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}
bool Recover(const std::filesystem::path& path, bool show) {
    Lock lock(path); if (!lock.acquired) return false;
    Record record;
    if (!Read(path, record)) return !std::filesystem::exists(path);
    if (!Matches(record)) {
        // A recycled HWND or a new process must never inherit recovery ownership.
        return RemoveRecord(path);
    }
    DpiScope dpi;
    auto displays = PrivateDisplays();
    auto destination = std::find_if(displays.begin(), displays.end(), [](const auto& d) { return d.physical; });
    if (destination == displays.end()) return false;
    for (auto it = displays.begin(); it != displays.end(); ++it) {
        RECT overlap{};
        if (it->physical && IntersectRect(&overlap, &it->work, &record.originalScreen)) { destination = it; break; }
    }
    HWND window = reinterpret_cast<HWND>(record.window);
    // Async show avoids waiting indefinitely on another application's UI thread.
    ShowWindowAsync(window, SW_MINIMIZE);
    for (int attempt = 0; attempt < 20 && !IsIconic(window); ++attempt) Sleep(25);
    if (!IsIconic(window)) return false;
    auto screen = ClampPrivateRestoreRect(record.originalScreen, destination->work);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(destination->monitor, &info)) return false;
    auto placement = record.placement;
    placement.flags = WPF_ASYNCWINDOWPLACEMENT;
    placement.showCmd = SW_SHOWMINNOACTIVE;
    placement.rcNormalPosition = screen;
    if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
        OffsetRect(&placement.rcNormalPosition, info.rcMonitor.left - info.rcWork.left,
            info.rcMonitor.top - info.rcWork.top);
    }
    if (!SetWindowPlacement(window, &placement)) return false;
    bool verified = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        WINDOWPLACEMENT actual{sizeof(actual)};
        verified = GetWindowPlacement(window, &actual) && IsIconic(window)
            && EqualRect(&actual.rcNormalPosition, &placement.rcNormalPosition);
        if (verified) break;
        Sleep(25);
    }
    if (!verified) return false;
    if (show) {
        ShowWindowAsync(window, record.placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    }
    // Remove journal first: failed disk cleanup keeps enough state to retry.
    if (!RemoveRecord(path)) return false;
    RemovePropW(window, markerName);
    return true;
}
}

RECT ClampPrivateRestoreRect(RECT rect, RECT work) {
    LONG width = (std::max)(1L, (std::min)(rect.right - rect.left, work.right - work.left));
    LONG height = (std::max)(1L, (std::min)(rect.bottom - rect.top, work.bottom - work.top));
    LONG left = std::clamp(rect.left, work.left, work.right - width);
    LONG top = std::clamp(rect.top, work.top, work.bottom - height);
    return {left, top, left + width, top + height};
}

std::vector<PrivateDisplay> PrivateDisplays() {
    DpiScope dpi;
    std::vector<PrivateDisplay> result;
    UINT32 pathCount{}, modeCount{};
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return result;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return result;
    paths.resize(pathCount);
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id};
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) || DisplayConfigGetDeviceInfo(&target.header)) continue;
        struct Match { const wchar_t* device; HMONITOR monitor{}; MONITORINFOEXW info{}; } match{source.viewGdiDeviceName};
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data)->BOOL {
            auto& m = *reinterpret_cast<Match*>(data); MONITORINFOEXW info{}; info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) && wcscmp(info.szDevice, m.device) == 0) { m.monitor = monitor; m.info = info; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&match));
        if (!match.monitor) continue;
        bool cloned = std::count_if(paths.begin(), paths.end(), [&](const auto& p) {
            return p.sourceInfo.id == path.sourceInfo.id && p.sourceInfo.adapterId.HighPart == path.sourceInfo.adapterId.HighPart
                && p.sourceInfo.adapterId.LowPart == path.sourceInfo.adapterId.LowPart;
        }) != 1;
        const bool indirect = path.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED
            || path.targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL;
        std::wstring name = target.monitorFriendlyDeviceName;
        std::transform(name.begin(), name.end(), name.begin(), towlower);
        std::wstring identity = target.monitorDevicePath;
        std::transform(identity.begin(), identity.end(), identity.begin(), towlower);
        // The signed 25.7.23 MTT driver advertises HDMI, not INDIRECT_WIRED.
        // Identify that version by both its EDID identity and monitor name.
        const bool mtt = identity.find(L"display#mtt1337#") != std::wstring::npos
            && name.find(L"mtt") != std::wstring::npos;
        const bool known = mtt || IsCherryAdapter(path.targetInfo.adapterId)
            || (indirect && name.find(L"virtual display") != std::wstring::npos);
        const bool candidate = known && !cloned && !(match.info.dwFlags & MONITORINFOF_PRIMARY);
        const bool physical = !known && !indirect && path.targetInfo.outputTechnology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST;
        result.push_back({match.monitor, match.info.rcWork, target.monitorDevicePath, physical, candidate});
    }
    return result;
}

int RecoverPrivateWindows(bool includeUntracked) {
    auto directory = Directory(); if (directory.empty()) return 1;
    int failures{}; std::error_code error;
    for (const auto& item : std::filesystem::directory_iterator(directory, error)) {
        if (item.path().extension() == L".bin" && !Recover(item.path(), false)) ++failures;
    }
    if (error) return 1;
    if (includeUntracked) {
        // Explicit rescue also handles apps that reopened on the retained virtual
        // display after reboot, when their old HWND/process identity is gone.
        struct Context { std::vector<PrivateDisplay> displays; std::vector<HWND> windows; } context{PrivateDisplays()};
        EnumWindows([](HWND window, LPARAM data)->BOOL {
            auto& context = *reinterpret_cast<Context*>(data);
            if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || GetPropW(window, markerName)) return TRUE;
            DpiScope dpi;
            WINDOWPLACEMENT placement{sizeof(placement)};
            MONITORINFO info{sizeof(info)};
            if (!GetWindowPlacement(window, &placement) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) return TRUE;
            RECT rect = placement.rcNormalPosition;
            if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) OffsetRect(&rect,
                info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top);
            if (std::any_of(context.displays.begin(), context.displays.end(), [&](const auto& d) { return d.candidate && Inside(rect, d.work); })) context.windows.push_back(window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
        for (auto window : context.windows) {
            Record record; record.window = reinterpret_cast<uint64_t>(window);
            record.cookie = GetTickCount64() ^ record.window;
            if (!record.cookie) record.cookie = 1;
            GetWindowThreadProcessId(window, &record.pid);
            if (!ProcessCreated(record.pid, record.created) || !GetWindowPlacement(window, &record.placement)) { ++failures; continue; }
            record.originalScreen = record.placement.rcNormalPosition;
            auto path = directory / (L"rescue-" + std::to_wstring(record.cookie) + L".bin");
            Lock lock(path);
            if (!lock.acquired || !SetPropW(window, markerName, reinterpret_cast<HANDLE>(record.cookie))) { ++failures; continue; }
            if (!Write(path, record)) { RemovePropW(window, markerName); ++failures; continue; }
            if (!Recover(path, false)) ++failures;
        }
    }
    return failures;
}

int WatchPrivateWindow(const std::filesystem::path& path, DWORD parent) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parent);
    HANDLE beat = OpenEventW(SYNCHRONIZE, FALSE, EventName(path, L".beat").c_str());
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, EventName(path, L".ready").c_str());
    if (!process || !beat || !ready) return 2;
    SetEvent(ready); CloseHandle(ready);
    HANDLE handles[]{process, beat};
    ULONGLONG lastBeat = GetTickCount64();
    bool recovery = false;
    for (;;) {
        const auto wait = WaitForMultipleObjects(2, handles, FALSE, 500);
        if (wait == WAIT_OBJECT_0 + 1) lastBeat = GetTickCount64();
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED || GetTickCount64() - lastBeat > 15000) recovery = true;
        if (!std::filesystem::exists(path)) break;
        Record record;
        { Lock lock(path); if (!lock.acquired) continue; if (!Read(path, record)) { recovery = true; } }
        if (!Matches(record) || !Parked(record)) recovery = true;
        if (recovery && Recover(path, false)) break;
        if (recovery) Sleep(500); // Retain journal and retry if no physical monitor is available.
    }
    CloseHandle(beat); CloseHandle(process); return 0;
}

PrivateWindowManager::~PrivateWindowManager() { RecoverAll(); }
bool PrivateWindowManager::Contains(HWND window) const { return windows_.contains(window); }
bool PrivateWindowManager::Park(HWND window, std::wstring& message) {
    if (Contains(window)) return true;
    DpiScope dpi;
    auto displays = PrivateDisplays();
    if (std::none_of(displays.begin(), displays.end(), [](const auto& d) { return d.physical; })) {
        message = L"Connect a physical monitor before keeping windows in VR"; return false;
    }
    auto target = std::find_if(displays.begin(), displays.end(), [](const auto& d) { return d.candidate; });
    if (target == displays.end()) { message = L"Set up an extended virtual display first"; return false; }
    if (std::count_if(displays.begin(), displays.end(), [](const auto& d) { return d.candidate; }) != 1) {
        message = L"Use one virtual display for Keep in VR"; return false;
    }
    if (GetPropW(window, markerName)) { message = L"Recover this window before parking again"; return false; }
    Record record; record.window = reinterpret_cast<uint64_t>(window);
    record.cookie = GetTickCount64() ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 32);
    if (!record.cookie) record.cookie = 1;
    GetWindowThreadProcessId(window, &record.pid);
    if (!ProcessCreated(record.pid, record.created) || !GetWindowPlacement(window, &record.placement)) return false;
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) return false;
    record.originalScreen = record.placement.rcNormalPosition;
    if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
        OffsetRect(&record.originalScreen, info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top);
    }
    wcsncpy_s(record.display, target->identity.c_str(), _TRUNCATE);
    auto directory = Directory(); if (directory.empty()) return false;
    auto path = directory / (std::to_wstring(record.pid) + L"-" + std::to_wstring(record.cookie) + L".bin");
    Lock lock(path); if (!lock.acquired) return false;
    if (!SetPropW(window, markerName, reinterpret_cast<HANDLE>(record.cookie))) { message = L"Cannot control this window"; return false; }
    if (!Write(path, record)) { RemovePropW(window, markerName); return false; }
    HANDLE beat = CreateEventW(nullptr, FALSE, FALSE, EventName(path, L".beat").c_str());
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, EventName(path, L".ready").c_str());
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
    auto helper = std::filesystem::path(executable).parent_path() / L"InterfayceWindowRecovery.exe";
    std::wstring command = L"\"" + helper.wstring() + L"\" --watch \"" + path.wstring() + L"\" " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION child{};
    bool started = beat && ready && CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
    if (started) { CloseHandle(child.hThread); started = WaitForSingleObject(ready, 3000) == WAIT_OBJECT_0; }
    if (ready) CloseHandle(ready);
    if (!started) {
        if (beat) CloseHandle(beat); if (child.hProcess) CloseHandle(child.hProcess);
        RemoveRecord(path); RemovePropW(window, markerName);
        message = L"Window recovery helper could not start"; return false;
    }
    windows_[window] = {path, child.hProcess, beat, target->work};
    ShowWindowAsync(window, SW_MINIMIZE);
    for (int n = 0; n < 20 && !IsIconic(window); ++n) Sleep(25);
    auto placement = record.placement;
    placement.flags = WPF_ASYNCWINDOWPLACEMENT;
    placement.showCmd = SW_SHOWMINNOACTIVE;
    RECT desired{target->work.left + 20, target->work.top + 20, target->work.right - 20, target->work.bottom - 20};
    placement.rcNormalPosition = desired;
    MONITORINFO virtualInfo{sizeof(virtualInfo)}; GetMonitorInfoW(target->monitor, &virtualInfo);
    if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) OffsetRect(&placement.rcNormalPosition,
        virtualInfo.rcMonitor.left - virtualInfo.rcWork.left, virtualInfo.rcMonitor.top - virtualInfo.rcWork.top);
    bool moved = IsIconic(window) && SetWindowPlacement(window, &placement);
    if (moved) {
        // Wait for asynchronous placement before showing on the destination.
        moved = false;
        for (int n = 0; n < 20; ++n) {
            WINDOWPLACEMENT actual{sizeof(actual)};
            if (GetWindowPlacement(window, &actual) && EqualRect(&actual.rcNormalPosition, &placement.rcNormalPosition)) { moved = true; break; }
            Sleep(25);
        }
    }
    if (moved) {
        ShowWindowAsync(window, SW_SHOWNOACTIVATE);
        moved = false;
        for (int n = 0; n < 20; ++n) { if (!IsIconic(window) && Parked(record)) { moved = true; break; } Sleep(25); }
    }
    if (!moved) { Return(window, false); message = L"Could not park window; recovery requested"; return false; }
    SetEvent(beat); message = L"Window kept in VR"; return true;
}
bool PrivateWindowManager::Return(HWND window, bool show) {
    auto found = windows_.find(window); if (found == windows_.end()) return true;
    if (!Recover(found->second.record, show)) return false;
    CloseHandle(found->second.process); CloseHandle(found->second.heartbeat); windows_.erase(found); return true;
}
bool PrivateWindowManager::AllowInput(HWND window) {
    auto found = windows_.find(window); if (found == windows_.end()) return GetPropW(window, markerName) == nullptr;
    DpiScope dpi;
    RECT rect{};
    // The slower topology/journal check belongs in Tick, not in the 100 Hz pointer path.
    if (WaitForSingleObject(found->second.process, 0) == WAIT_TIMEOUT && !IsIconic(window)
        && VisibleFrame(window, rect) && Inside(rect, found->second.work)) return true;
    Return(window, false); return false;
}
void PrivateWindowManager::Tick() {
    if (GetTickCount64() < nextTick_) return;
    nextTick_ = GetTickCount64() + 500;
    std::vector<HWND> windows; for (auto& [window, entry] : windows_) { SetEvent(entry.heartbeat); windows.push_back(window); }
    for (auto window : windows) {
        auto found = windows_.find(window); if (found == windows_.end()) continue;
        Record record;
        if (!Read(found->second.record, record) || !Parked(record)) Return(window, false);
        else AllowInput(window);
    }
}
void PrivateWindowManager::RecoverAll() {
    std::vector<HWND> windows; for (const auto& [window, entry] : windows_) windows.push_back(window);
    for (auto window : windows) Return(window, false);
}
}
