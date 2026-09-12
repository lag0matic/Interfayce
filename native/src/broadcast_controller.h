#pragma once

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace interfayce {

enum class BroadcastState { Off, Starting, Active, Faulted };
enum class BroadcastSource { Spotify, Chrome };

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
    void SetSource(BroadcastSource source);
    BroadcastSource Source() const;
    const wchar_t* SourceProcessName() const;
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
    BroadcastSource source_{BroadcastSource::Spotify};
};

} // namespace interfayce
