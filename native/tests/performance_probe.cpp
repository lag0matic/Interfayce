#include "performance_monitor.h"
#include "panel_layout.h"
#include <windows.h>
#include <iostream>
#include <chrono>
unsigned long long Cpu() {
    FILETIME a{}, b{}, k{}, u{};
    GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u);
    return ((static_cast<unsigned long long>(k.dwHighDateTime) << 32) | k.dwLowDateTime)
         + ((static_cast<unsigned long long>(u.dwHighDateTime) << 32) | u.dwLowDateTime);
}
int main() {
    static_assert(interfayce::panel::ContentY(100) < 0);
    static_assert(interfayce::panel::ContentY(130) < 0);
    static_assert(interfayce::panel::ContentY(355) == 287);
    auto baseline = Cpu(); Sleep(5000);
    std::cout << "Idle CPU milliseconds / 5s: " << (Cpu() - baseline) / 10000.0 << '\n';
    interfayce::PerformanceMonitor monitor;
    Sleep(2000); // Exclude one-time driver initialization.
    baseline = Cpu();
    for (int i = 0; i < 10; ++i) {
        Sleep(1000);
        for (const auto& value : monitor.Snapshot()) std::wcout << value << L"  ";
        std::wcout << L'\n';
    }
    std::cout << "Monitor CPU milliseconds / 10s: " << (Cpu() - baseline) / 10000.0 << '\n';
    monitor.SetVisible(false);
    return 0;
}
