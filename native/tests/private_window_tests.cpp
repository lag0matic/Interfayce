// Include the recovery implementation to exercise journal and fault paths without
// exposing those internals as application APIs. Uses only a window owned by this test.
#include "private_window_manager.cpp"
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace interfayce;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--enable-display" || std::string(argv[1]) == "--enable-cherry")) {
        const bool cherry = std::string(argv[1]) == "--enable-cherry";
        DpiScope dpi;
        UINT32 pc{}, mc{};
        GetDisplayConfigBufferSizes(QDC_ALL_PATHS, &pc, &mc);
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
        auto code = QueryDisplayConfig(QDC_ALL_PATHS, &pc, paths.data(), &mc, modes.data(), nullptr);
        if (code) return code;
        paths.resize(pc); modes.resize(mc);
        std::vector<DISPLAYCONFIG_PATH_INFO> active;
        std::optional<DISPLAYCONFIG_PATH_INFO> virtualPath;
        for (const auto& p : paths) {
            if (p.flags & DISPLAYCONFIG_PATH_ACTIVE) { active.push_back(p); continue; }
            DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
            name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), p.targetInfo.adapterId, p.targetInfo.id};
            if (!virtualPath && !DisplayConfigGetDeviceInfo(&name.header) && p.targetInfo.targetAvailable
                && (cherry ? IsCherryAdapter(p.targetInfo.adapterId)
                    : std::wstring(name.monitorFriendlyDeviceName) == L"VDD by MTT")) virtualPath = p;
        }
        if (!virtualPath) return 2;
        DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{};
        preferred.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE, sizeof(preferred), virtualPath->targetInfo.adapterId, virtualPath->targetInfo.id};
        code = DisplayConfigGetDeviceInfo(&preferred.header);
        std::cout << "Preferred mode query=" << code << " size=" << preferred.width << "x" << preferred.height << std::endl;
        if (code) return code;
        DISPLAYCONFIG_MODE_INFO source{};
        source.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        source.id = virtualPath->sourceInfo.id; source.adapterId = virtualPath->sourceInfo.adapterId;
        source.sourceMode.width = preferred.width; source.sourceMode.height = preferred.height;
        source.sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
        source.sourceMode.position = {GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN), 0};
        virtualPath->sourceInfo.modeInfoIdx = static_cast<UINT32>(modes.size());
        modes.push_back(source);
        DISPLAYCONFIG_MODE_INFO target{};
        target.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        target.id = virtualPath->targetInfo.id; target.adapterId = virtualPath->targetInfo.adapterId;
        target.targetMode = preferred.targetMode;
        virtualPath->targetInfo.modeInfoIdx = static_cast<UINT32>(modes.size());
        modes.push_back(target);
        virtualPath->targetInfo.refreshRate = preferred.targetMode.targetVideoSignalInfo.vSyncFreq;
        virtualPath->targetInfo.scanLineOrdering = preferred.targetMode.targetVideoSignalInfo.scanLineOrdering;
        virtualPath->flags |= DISPLAYCONFIG_PATH_ACTIVE;
        active.push_back(*virtualPath);
        UINT32 flags = SDC_USE_SUPPLIED_DISPLAY_CONFIG;
        code = SetDisplayConfig(static_cast<UINT32>(active.size()), active.data(), static_cast<UINT32>(modes.size()), modes.data(), flags | SDC_VALIDATE);
        std::cout << "Extended topology validation=" << code << std::endl;
        if (!code) {
            code = SetDisplayConfig(static_cast<UINT32>(active.size()), active.data(), static_cast<UINT32>(modes.size()), modes.data(), flags | SDC_APPLY | SDC_SAVE_TO_DATABASE);
            std::cout << "Extended topology apply=" << code << std::endl;
            for (int n = 0; n < 3; ++n) {
                Sleep(1000);
                for (const auto& d : PrivateDisplays()) std::wcout << L"after apply candidate=" << d.candidate << L" " << d.identity << std::endl;
            }
        }
        return code;
    }
    if (argc > 1 && std::string(argv[1]) == "--displays") {
        UINT32 pc{}, mc{};
        GetDisplayConfigBufferSizes(QDC_ALL_PATHS, &pc, &mc);
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
        auto code = QueryDisplayConfig(QDC_ALL_PATHS, &pc, paths.data(), &mc, modes.data(), nullptr);
        std::wcout << L"Topology query=" << code << L" paths=" << pc << std::endl;
        if (!code) for (UINT32 i = 0; i < pc; ++i) {
            const auto& p = paths[i];
            if (!p.targetInfo.targetAvailable) continue;
            DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
            name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), p.targetInfo.adapterId, p.targetInfo.id};
            auto named = DisplayConfigGetDeviceInfo(&name.header);
            std::wcout << L"target=" << name.monitorFriendlyDeviceName << L" named=" << named
                << L" flags=" << p.flags << L" technology=" << p.targetInfo.outputTechnology
                << L" source=" << p.sourceInfo.id << L" targetId=" << p.targetInfo.id << std::endl;
        }
        for (const auto& display : PrivateDisplays()) {
            std::wcout << L"physical=" << display.physical << L" candidate=" << display.candidate
                << L" work=" << display.work.left << L"," << display.work.top << L"," << display.work.right
                << L"," << display.work.bottom << L" identity=" << display.identity << std::endl;
        }
        return 0;
    }
    std::promise<HWND> ready;
    auto future = ready.get_future();
    std::thread ui([&] {
        DpiScope dpi;
        WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"InterfayceRecoveryTest";
        RegisterClassW(&cls);
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"Interfayce harmless recovery test",
            WS_OVERLAPPEDWINDOW, 60, 60, 400, 300, nullptr, nullptr, cls.hInstance, nullptr);
        ShowWindow(window, SW_SHOWNOACTIVATE); ready.set_value(window);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        DestroyWindow(window);
    });
    HWND window = future.get();
    int result = 0;
    std::filesystem::path path;
    try {
        Check(window != nullptr, "test window creation");
        RECT clamped = ClampPrivateRestoreRect({-9000, -7000, -4000, -3000}, {-1920, 40, 0, 1080});
        Check(clamped.left == -1920 && clamped.top == 40 && clamped.right == 0 && clamped.bottom == 1080, "negative origin and oversize clamp");
        clamped = ClampPrivateRestoreRect({700, 700, 1000, 1000}, {0, 0, 800, 800});
        Check(clamped.left == 500 && clamped.top == 500, "edge clamp preserves size");
        auto displays = PrivateDisplays();
        Check(std::any_of(displays.begin(), displays.end(), [](const auto& d) { return d.physical; }), "physical recovery destination");
        Record record;
        record.window = reinterpret_cast<uint64_t>(window); record.pid = GetCurrentProcessId();
        record.cookie = GetTickCount64();
        Check(ProcessCreated(record.pid, record.created), "process identity");
        Check(GetWindowPlacement(window, &record.placement), "original placement");
        record.originalScreen = {60, 60, 460, 360};
        path = Directory() / (L"test-" + std::to_wstring(record.pid) + L".bin");
        SetPropW(window, markerName, reinterpret_cast<HANDLE>(record.cookie));
        Check(Write(path, record), "durable journal");
        Record loaded; Check(Read(path, loaded) && Matches(loaded), "journal roundtrip and identity");
        SetWindowPos(window, nullptr, -20000, -20000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        Sleep(100);
        Check(Recover(path, false), "off-screen window recovery");
        Check(IsIconic(window), "automatic recovery stays minimized");
        WINDOWPLACEMENT restored{sizeof(restored)}; GetWindowPlacement(window, &restored);
        Check(std::any_of(displays.begin(), displays.end(), [&](const auto& d) { return d.physical && Inside(restored.rcNormalPosition, d.work); }), "restore destination on physical display");
        Check(!std::filesystem::exists(path) && !GetPropW(window, markerName), "journal and marker released");
        Check(Recover(path, false), "repeated recovery is idempotent");
        // A matching HWND with the wrong process lifetime must never be moved.
        record.created.dwLowDateTime ^= 1;
        SetPropW(window, markerName, reinterpret_cast<HANDLE>(record.cookie));
        Check(Write(path, record) && Recover(path, true) && IsIconic(window), "stale identity ignored without showing window");
        RemovePropW(window, markerName);
        record.created.dwLowDateTime ^= 1;
        { std::ofstream corrupt(path, std::ios::binary); corrupt << "truncated"; }
        Check(!Recover(path, false) && std::filesystem::exists(path), "corrupt journal retained for diagnosis");
        RemoveRecord(path);

        // Normal windows store workspace coordinates (unlike the tool window
        // above). Exercise the production conversion and visible-return path.
        SetWindowLongPtrW(window, GWL_EXSTYLE, 0);
        SetPropW(window, markerName, reinterpret_cast<HANDLE>(record.cookie));
        Check(Write(path, record) && Recover(path, false), "normal window recovery");
        GetWindowPlacement(window, &restored);
        MONITORINFO monitor{sizeof(monitor)};
        Check(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor), "normal monitor info");
        RECT restoredScreen = restored.rcNormalPosition;
        OffsetRect(&restoredScreen, monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top);
        Check(Inside(restoredScreen, monitor.rcWork) && IsIconic(window), "normal workspace restore is reachable and minimized");
        SetPropW(window, markerName, reinterpret_cast<HANDLE>(record.cookie));
        record.placement.showCmd = SW_SHOWMAXIMIZED;
        Check(Write(path, record) && Recover(path, true), "explicit return requested");
        for (int n = 0; n < 20 && !IsZoomed(window); ++n) Sleep(25);
        Check(IsZoomed(window), "explicit return restores maximized state");

        // Exercise the actual separate helper: an absent destination must trigger
        // recovery even while its parent is alive and heartbeat is healthy.
        SetPropW(window, markerName, reinterpret_cast<HANDLE>(record.cookie));
        Check(Write(path, record), "watch journal");
        HANDLE beat = CreateEventW(nullptr, FALSE, FALSE, EventName(path, L".beat").c_str());
        HANDLE helperReady = CreateEventW(nullptr, TRUE, FALSE, EventName(path, L".ready").c_str());
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        auto helper = std::filesystem::path(executable).parent_path() / L"InterfayceWindowRecovery.exe";
        std::wstring command = L"\"" + helper.wstring() + L"\" --watch \"" + path.wstring() + L"\" " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION child{};
        Check(CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child), "separate recovery helper launch");
        CloseHandle(child.hThread);
        Check(WaitForSingleObject(helperReady, 3000) == WAIT_OBJECT_0, "watchdog readiness handshake");
        SetEvent(beat);
        Check(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0, "missing display recovery by watchdog");
        DWORD childExit{}; GetExitCodeProcess(child.hProcess, &childExit); CloseHandle(child.hProcess);
        Check(childExit == 0, "watchdog exit success");
        Check(IsIconic(window) && !std::filesystem::exists(path), "watchdog leaves reachable minimized window");
        CloseHandle(beat); CloseHandle(helperReady);
        if (argc > 1) {
            PrivateWindowManager manager;
            std::wstring message;
            Check(manager.Park(window, message), "live virtual display parking");
            Check(manager.AllowInput(window), "parked input allowed");
            Check(manager.Return(window, false) && IsIconic(window), "live private return minimized");
        }
        std::cout << "Private window tests passed: placement, identity, journal, missing-display watchdog" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl; result = 1;
    }
    if (!path.empty()) { RemoveRecord(path); RemovePropW(window, markerName); }
    PostThreadMessageW(GetWindowThreadProcessId(window, nullptr), WM_QUIT, 0, 0);
    ui.join(); return result;
}
