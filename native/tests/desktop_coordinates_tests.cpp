#include "desktop_coordinates.h"
#include <iostream>
#include <vector>

int main() {
    interfayce::PhysicalDesktopCoordinates physical;
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data)->BOOL {
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcWork);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    for (const auto& bounds : monitors) {
        // Hidden test windows: no desktop focus or user input is touched.
        const auto window = CreateWindowExW(0, L"STATIC", L"Interfayce coordinate test",
            WS_OVERLAPPEDWINDOW, bounds.left + 50, bounds.top + 50, 400, 300,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) return 1;
        POINT screen{73, 61};
        ClientToScreen(window, &screen);
        auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
        POINT unguarded = screen;
        ScreenToClient(window, &unguarded);
        const auto result = interfayce::PhysicalClientPoint(window, screen);
        const bool restored = AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_UNAWARE);
        SetThreadDpiAwarenessContext(previous);
        const auto dpi = GetDpiForWindow(window);
        DestroyWindow(window);
        if (!result || result->x != 73 || result->y != 61 || !restored) return 2;
        std::cout << "Monitor origin " << bounds.left << "," << bounds.top
                  << " DPI " << dpi << ": physical client mapping passed; unguarded=" << unguarded.x << "," << unguarded.y << "\n";
    }
    return monitors.empty() ? 3 : 0;
}
