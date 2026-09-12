#include "private_window_manager.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = 1;
    if (argv && argc == 4 && wcscmp(argv[1], L"--watch") == 0) {
        result = interfayce::WatchPrivateWindow(argv[2], wcstoul(argv[3], nullptr, 10));
    } else {
        const bool quiet = argv && argc == 2 && wcscmp(argv[1], L"--quiet") == 0;
        result = interfayce::RecoverPrivateWindows(!quiet);
        if (result && !quiet) MessageBoxW(nullptr, L"Some VR windows could not be recovered yet. Connect a physical monitor and try Recover VR windows again.", L"Interfayce", MB_OK | MB_ICONINFORMATION);
    }
    if (argv) LocalFree(argv);
    return result;
}
