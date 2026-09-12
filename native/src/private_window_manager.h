#pragma once
#include <Windows.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace interfayce {
struct PrivateDisplay {
    HMONITOR monitor{};
    RECT work{};
    std::wstring identity;
    bool physical{};
    bool candidate{};
};
std::vector<PrivateDisplay> PrivateDisplays();
RECT ClampPrivateRestoreRect(RECT rectangle, RECT work);
// Shared by the overlay and the independent recovery executable.
int RecoverPrivateWindows(bool includeUntracked = false);
int WatchPrivateWindow(const std::filesystem::path& record, DWORD parent);

class PrivateWindowManager {
public:
    ~PrivateWindowManager();
    bool Park(HWND window, std::wstring& message);
    bool Return(HWND window, bool show);
    bool Contains(HWND window) const;
    bool AllowInput(HWND window);
    void Tick();
    void RecoverAll();
private:
    struct Entry { std::filesystem::path record; HANDLE process{}; HANDLE heartbeat{}; RECT work{}; };
    std::map<HWND, Entry> windows_;
    ULONGLONG nextTick_{};
};
}
