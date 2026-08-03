#define NOMINMAX
#include "broadcast_controller.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace interfayce {
namespace {

constexpr wchar_t kBroadcastStopEventName[] = L"Local\\InterfayceBroadcastStop";

std::wstring WindowsError(const wchar_t* prefix) {
    const DWORD code = GetLastError();
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result(prefix);
    if (length > 0 && buffer != nullptr) {
        result += L": ";
        result.append(buffer, length);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
            result.pop_back();
        }
    }
    if (buffer != nullptr) LocalFree(buffer);
    return result;
}

} // namespace

BroadcastController::BroadcastController(std::filesystem::path enginePath)
    : enginePath_(std::move(enginePath)) {}

BroadcastController::~BroadcastController() {
    Stop();
    if (stopEvent_ != nullptr) CloseHandle(stopEvent_);
}

bool BroadcastController::Start(std::wstring& error) {
    if (Enabled()) return true;
    CloseProcessHandles();
    if (!std::filesystem::is_regular_file(enginePath_)) {
        error = L"InterfayceAudioEngine.exe is missing.";
        SetState(BroadcastState::Faulted, L"BROADCAST UNAVAILABLE");
        return false;
    }
    if (stopEvent_ == nullptr) {
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, kBroadcastStopEventName);
    }
    if (stopEvent_ == nullptr || !ResetEvent(stopEvent_)) {
        error = WindowsError(L"Could not prepare the broadcast stop signal");
        SetState(BroadcastState::Faulted, L"BROADCAST FAILED");
        return false;
    }

    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job_ == nullptr || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
            &limits, sizeof(limits))) {
        error = WindowsError(L"Could not create the broadcast safety job");
        CloseProcessHandles();
        SetState(BroadcastState::Faulted, L"BROADCAST FAILED");
        return false;
    }

    std::wstring command = L"\"" + enginePath_.wstring()
        + L"\" --broadcast-spotify 86400 --gain-db " + std::to_wstring(gainDb_);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
            enginePath_.parent_path().wstring().c_str(), &startup, &process)) {
        error = WindowsError(L"Could not launch the broadcast engine");
        CloseProcessHandles();
        SetState(BroadcastState::Faulted, L"BROADCAST FAILED");
        return false;
    }
    process_ = process.hProcess;
    if (!AssignProcessToJobObject(job_, process_)) {
        error = WindowsError(L"Could not contain the broadcast engine");
        TerminateProcess(process_, 1);
        CloseHandle(process.hThread);
        CloseProcessHandles();
        SetState(BroadcastState::Faulted, L"BROADCAST FAILED");
        return false;
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    startedAt_ = std::chrono::steady_clock::now();
    SetState(BroadcastState::Starting, L"BROADCAST STARTING");
    return true;
}

void BroadcastController::Stop() {
    if (process_ != nullptr) {
        if (stopEvent_ != nullptr) SetEvent(stopEvent_);
        if (WaitForSingleObject(process_, 1500) != WAIT_OBJECT_0) {
            TerminateProcess(process_, 0);
            WaitForSingleObject(process_, 500);
        }
    }
    CloseProcessHandles();
    SetState(BroadcastState::Off, L"BROADCAST OFF");
}

bool BroadcastController::Poll() {
    if (process_ == nullptr) return false;
    if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
        CloseProcessHandles();
        SetState(BroadcastState::Faulted, L"BROADCAST FAILED");
        return true;
    }
    if (state_ == BroadcastState::Starting
        && std::chrono::steady_clock::now() - startedAt_ >= std::chrono::milliseconds(350)) {
        SetState(BroadcastState::Active, L"BROADCAST LIVE");
        return true;
    }
    return false;
}

void BroadcastController::SetGainDb(float gainDb) {
    gainDb_ = (std::max)(0.0F, (std::min)(24.0F, gainDb));
}

BroadcastState BroadcastController::State() const { return state_; }

bool BroadcastController::Enabled() const {
    return state_ == BroadcastState::Starting || state_ == BroadcastState::Active;
}

const std::wstring& BroadcastController::StatusText() const { return status_; }

void BroadcastController::CloseProcessHandles() {
    if (process_ != nullptr) CloseHandle(process_);
    process_ = nullptr;
    if (job_ != nullptr) CloseHandle(job_);
    job_ = nullptr;
}

void BroadcastController::SetState(BroadcastState state, std::wstring status) {
    state_ = state;
    status_ = std::move(status);
}

} // namespace interfayce
