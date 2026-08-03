#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace interfayce {

struct DesktopSource {
    enum class Kind { Display, Window };
    Kind kind{};
    std::wstring id;
    std::wstring label;
    std::wstring detail;
    std::vector<uint8_t> iconBgra;
    HWND window{};
    HMONITOR monitor{};
};

// Inventory only for now. Capture and OpenVR surface ownership are deliberately
// separate so a lost window can be reposed without disturbing its capture.
class DesktopSurfaceManager {
public:
    std::vector<DesktopSource> EnumerateDisplays() const;
    std::vector<DesktopSource> EnumerateWindows() const;
    std::vector<DesktopSource> EnumerateSources() const;
};

}  // namespace interfayce
