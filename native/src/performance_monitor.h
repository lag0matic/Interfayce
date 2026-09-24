#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace interfayce {
// A single worker owns all driver calls. Rendering only reads cached text.
class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor();
    void SetVisible(bool visible);
    std::array<std::wstring, 4> Snapshot();
private:
    void Run();
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_{};
    bool visible_{true};
    std::array<std::wstring, 4> text_{L"--", L"--", L"--", L"--"};
    std::thread worker_;
};
}
