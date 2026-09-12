#pragma once
#include <windows.h>
#include <optional>

namespace interfayce {
class PhysicalDesktopCoordinates {
public:
    PhysicalDesktopCoordinates()
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalDesktopCoordinates() { if (previous_) SetThreadDpiAwarenessContext(previous_); }
    PhysicalDesktopCoordinates(const PhysicalDesktopCoordinates&) = delete;
    PhysicalDesktopCoordinates& operator=(const PhysicalDesktopCoordinates&) = delete;
private:
    DPI_AWARENESS_CONTEXT previous_{};
};

inline std::optional<POINT> PhysicalClientPoint(HWND window, POINT screen) {
    PhysicalDesktopCoordinates coordinates;
    if (!ScreenToClient(window, &screen)) return std::nullopt;
    return screen;
}
}
