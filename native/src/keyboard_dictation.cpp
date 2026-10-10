#include "keyboard_dictation.h"
#include "local_service_client.h"
#include <vector>

namespace {
HWND FocusedControl(HWND window) {
    GUITHREADINFO info{sizeof(info)};
    return window && GetGUIThreadInfo(GetWindowThreadProcessId(window, nullptr), &info)
        ? info.hwndFocus : nullptr;
}
bool TypeText(const std::string& text) {
    if (text.empty()) return true;
    for (const auto key : {VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(key) & 0x8000) return false;
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (!count) return false;
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), wide.data(), count);
    std::vector<INPUT> inputs;
    for (auto character : wide) {
        if (character < L' ') character = L' '; // Never inject Enter/Tab commands.
        INPUT event{};
        event.type = INPUT_KEYBOARD;
        event.ki.wScan = character;
        event.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(event);
        event.ki.dwFlags |= KEYEVENTF_KEYUP;
        inputs.push_back(event);
    }
    return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
}
}
namespace interfayce {
KeyboardDictation::KeyboardDictation() : KeyboardDictation(Hooks{
    [] { const auto window = GetForegroundWindow();
         return std::pair{window, FocusedControl(window)}; },
    TypeText,
    [](const std::string& path) {
        return LocalHttpRequest("POST", path, std::chrono::seconds(2));
    }}) {}
void KeyboardDictation::Request(Operation operation) {
    operation_ = operation;
    const std::string name = operation == Operation::Start ? "start" :
        operation == Operation::Stop ? "stop" : operation == Operation::Cancel ? "cancel" : "status";
    const auto path = "/keyboard/dictation/" + name +
        (operation == Operation::Start ? "" : "/" + token_);
    request_ = std::async(std::launch::async, [path, call = hooks_.request] {
        return call(path);
    });
}
bool KeyboardDictation::SameFocus() const {
    return window_ && hooks_.focus() == std::pair{window_, focus_};
}
void KeyboardDictation::Toggle() {
    if (Active()) {
        stopRequested_ = true;
        label_ = L"PROCESSING...";
        return;
    }
    const auto focus = hooks_.focus();
    window_ = focus.first;
    focus_ = focus.second;
    if (!window_ || !focus_) { label_ = L"CLICK A TEXT FIELD FIRST"; return; }
    cancelled_ = false;
    stopRequested_ = false;
    label_ = L"STARTING MIC...";
    Request(Operation::Start);
}
void KeyboardDictation::Cancel() {
    if (!Active()) return;
    cancelled_ = true;
    label_ = L"DICTATION CANCELLED";
}
void KeyboardDictation::Tick() {
    if (Active() && !SameFocus()) Cancel();
    if (request_.valid()) {
        if (request_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const auto result = request_.get();
        if (!result) {
            // A failed poll/stop should still attempt cancellation of known sessions.
            if (!token_.empty() && operation_ != Operation::Cancel) {
                cancelled_ = true;
                label_ = L"VOICE SERVICE UNAVAILABLE";
                Request(Operation::Cancel);
                return;
            }
            token_.clear();
            label_ = L"VOICE BUSY / UNAVAILABLE";
            return;
        }
        if (operation_ == Operation::Start) {
            if (result->size() != 32) { label_ = L"VOICE BUSY / UNAVAILABLE"; return; }
            token_ = *result;
            if (!cancelled_ && !stopRequested_) label_ = L"LISTENING - TAP MIC TO FINISH";
        } else if (operation_ == Operation::Cancel) {
            token_.clear();
            return;
        } else if (operation_ == Operation::Poll && !cancelled_) {
            const auto tab = result->find('\t');
            const auto state = result->substr(0, tab);
            if (state == "DONE") {
                const auto text = tab == std::string::npos ? "" : result->substr(tab + 1);
                label_ = text.empty() ? L"NO SPEECH HEARD" :
                    SameFocus() && hooks_.insert(text) ? L"TEXT INSERTED" : L"COULD NOT INSERT TEXT";
                token_.clear();
                return;
            }
            if (state == "ERROR" || state == "CANCELLED") {
                token_.clear();
                label_ = L"DICTATION STOPPED - TRY AGAIN";
                return;
            }
            if (state == "PROCESSING") label_ = L"PROCESSING...";
        }
        nextPoll_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    }
    if (token_.empty()) return;
    if (cancelled_) Request(Operation::Cancel);
    else if (stopRequested_) {
        stopRequested_ = false;
        Request(Operation::Stop);
    } else if (std::chrono::steady_clock::now() >= nextPoll_) Request(Operation::Poll);
}
void KeyboardDictation::Shutdown() {
    Cancel();
    if (request_.valid()) {
        request_.wait();
        Tick();
    }
    if (request_.valid()) request_.wait();
    if (!token_.empty())
        hooks_.request("/keyboard/dictation/cancel/" + token_);
    token_.clear();
}
}
