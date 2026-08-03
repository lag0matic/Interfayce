#pragma once

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace interfayce {

enum class BroadcastState { Off, Starting, Active, Faulted };

class BroadcastController {
public:
    explicit BroadcastController(std::filesystem::path enginePath);
    ~BroadcastController();
    BroadcastController(const BroadcastController&) = delete;
    BroadcastController& operator=(const BroadcastController&) = delete;

    bool Start(std::wstring& error);
    void Stop();
    bool Poll();
    void SetGainDb(float gainDb);
    BroadcastState State() const;
    bool Enabled() const;
    const std::wstring& StatusText() const;

private:
    void CloseProcessHandles();
    void SetState(BroadcastState state, std::wstring status);

    std::filesystem::path enginePath_;
    HANDLE process_{};
    HANDLE job_{};
    HANDLE stopEvent_{};
    BroadcastState state_{BroadcastState::Off};
    std::wstring status_{L"BROADCAST OFF"};
    std::chrono::steady_clock::time_point startedAt_{};
    float gainDb_{12.0F};
};

} // namespace interfayce
