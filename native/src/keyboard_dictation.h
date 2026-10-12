#pragma once
#include <windows.h>
#include <chrono>
#include <future>
#include <functional>
#include <utility>
#include <optional>
#include <string>

namespace interfayce {
// All state and input injection stay on the overlay thread; HTTP runs off-thread.
class KeyboardDictation {
public:
    struct Hooks {
        std::function<std::pair<HWND, HWND>()> focus;
        std::function<bool(const std::string&)> insert;
        std::function<std::optional<std::string>(const std::string&)> request;
    };
    KeyboardDictation();
    explicit KeyboardDictation(Hooks hooks) : hooks_(std::move(hooks)) {}
    void Toggle();
    void Tick();
    void Cancel();
    void Shutdown();
    bool Active() const { return !token_.empty() || request_.valid(); }
    const std::wstring& Label() const { return label_; }
private:
    enum class Operation { Start, Stop, Poll, Cancel };
    void Request(Operation operation);
    bool SameFocus() const;
    Hooks hooks_;
    std::string token_;
    std::future<std::optional<std::string>> request_;
    Operation operation_{};
    HWND window_{};
    HWND focus_{};
    bool cancelled_{};
    bool stopRequested_{};
    std::wstring label_;
    std::chrono::steady_clock::time_point nextPoll_{};
};
}
