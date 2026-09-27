#include "performance_monitor.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace interfayce {
namespace {
// Stable NVML C ABI; dynamically loaded from the installed NVIDIA driver.
using Device = void*;
struct Utilization { unsigned gpu, memory; };
struct Memory { unsigned long long total, free, used; };
using Init = int(*)();
using Count = int(*)(unsigned*);
using Handle = int(*)(unsigned, Device*);
using GetMemory = int(*)(Device, Memory*);
using GetUtilization = int(*)(Device, Utilization*);
unsigned long long Ticks(FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
std::wstring MemoryText(unsigned long long used, unsigned long long total) {
    if (!total || used > total) return L"--";
    constexpr double gib = 1073741824.0;
    std::wostringstream s;
    s << std::fixed << std::setprecision(1) << used / gib << L"/" << std::setprecision(0) << total / gib << L" GB";
    return s.str();
}
}
PerformanceMonitor::PerformanceMonitor() : worker_([this] { Run(); }) {}
PerformanceMonitor::~PerformanceMonitor() {
    { std::lock_guard lock(mutex_); stop_ = true; }
    wake_.notify_one();
    worker_.join();
}
void PerformanceMonitor::SetVisible(bool visible) {
    bool changed;
    { std::lock_guard lock(mutex_); changed = visible_ != visible; visible_ = visible; }
    if (changed) wake_.notify_one();
}
std::array<std::wstring, 4> PerformanceMonitor::Snapshot() {
    std::lock_guard lock(mutex_); return text_;
}
void PerformanceMonitor::Run() {
    HMODULE library = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto init = library ? reinterpret_cast<Init>(GetProcAddress(library, "nvmlInit_v2")) : nullptr;
    auto shutdown = library ? reinterpret_cast<Init>(GetProcAddress(library, "nvmlShutdown")) : nullptr;
    auto count = library ? reinterpret_cast<Count>(GetProcAddress(library, "nvmlDeviceGetCount_v2")) : nullptr;
    auto handle = library ? reinterpret_cast<Handle>(GetProcAddress(library, "nvmlDeviceGetHandleByIndex_v2")) : nullptr;
    auto memory = library ? reinterpret_cast<GetMemory>(GetProcAddress(library, "nvmlDeviceGetMemoryInfo")) : nullptr;
    auto utilization = library ? reinterpret_cast<GetUtilization>(GetProcAddress(library, "nvmlDeviceGetUtilizationRates")) : nullptr;
    const bool initialized = init && shutdown && count && handle && memory && utilization && init() == 0;
    Device gpu{};
    if (initialized) {
        unsigned n = 0;
        unsigned long long largest = 0;
        if (count(&n) == 0) for (unsigned i = 0; i < n; ++i) {
            Device candidate{}; Memory info{};
            if (handle(i, &candidate) == 0 && memory(candidate, &info) == 0 && info.total > largest) {
                largest = info.total; gpu = candidate;
            }
        }
    }
    unsigned long long previousTotal = 0, previousIdle = 0;
    bool primed = false;
    for (;;) {
        std::array<std::wstring, 4> sample{L"--", L"--", L"--", L"--"};
        FILETIME idle{}, kernel{}, user{};
        if (GetSystemTimes(&idle, &kernel, &user)) {
            const auto total = Ticks(kernel) + Ticks(user), idleTicks = Ticks(idle);
            if (primed && total > previousTotal && idleTicks >= previousIdle) {
                const double busy = 1.0 - double(idleTicks - previousIdle) / double(total - previousTotal);
                sample[0] = std::to_wstring(static_cast<int>(std::clamp(busy * 100.0, 0.0, 100.0) + 0.5)) + L"%";
            }
            previousTotal = total; previousIdle = idleTicks; primed = true;
        }
        MEMORYSTATUSEX ram{sizeof(ram)};
        if (GlobalMemoryStatusEx(&ram)) sample[3] = MemoryText(ram.ullTotalPhys - ram.ullAvailPhys, ram.ullTotalPhys);
        if (gpu) {
            Utilization use{}; Memory info{};
            if (utilization(gpu, &use) == 0 && use.gpu <= 100) sample[1] = std::to_wstring(use.gpu) + L"%";
            if (memory(gpu, &info) == 0) sample[2] = MemoryText(info.used, info.total);
        }
        std::unique_lock lock(mutex_);
        text_ = std::move(sample);
        if (stop_) break;
        wake_.wait_for(lock, std::chrono::seconds(visible_ ? 1 : 5));
        if (stop_) break;
    }
    if (initialized) shutdown();
    if (library) FreeLibrary(library);
}
}
