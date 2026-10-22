#include "keyboard_dictation.h"
#include "local_service_client.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <stdexcept>

// Production constructor links here but tests inject all effects: no real
// microphone, network requests, focus changes, or keystrokes.
namespace interfayce {
std::optional<std::string> LocalHttpRequest(std::string_view, std::string_view,
    std::chrono::milliseconds, std::string_view) { throw std::runtime_error("Unexpected HTTP"); }
}
void Check(bool condition) { if (!condition) throw std::runtime_error("Check failed"); }
void Pump(interfayce::KeyboardDictation& dictation) {
    for (int i = 0; i < 800 && dictation.Active(); ++i) {
        dictation.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(!dictation.Active());
}
int main() {
    try {
        for (int scenario = 0; scenario < 6; ++scenario) {
            HWND window = reinterpret_cast<HWND>(1), focus = reinterpret_cast<HWND>(2);
            int inserts = 0;
            std::atomic<int> cancels{}, stops{};
            interfayce::KeyboardDictation dictation({
                [&] { return std::pair{window, focus}; },
                [&](const std::string& text) { Check(text == "Discord reply"); ++inserts; return true; },
                [&](const std::string& path) -> std::optional<std::string> {
                    if (path.ends_with("start")) {
                        if (scenario == 4) return std::nullopt;
                        return std::string(32, 'a');
                    }
                    if (path.find("cancel/") != std::string::npos) { ++cancels; return "ok"; }
                    if (path.find("stop/") != std::string::npos) { ++stops; return "ok"; }
                    if (scenario == 5) return std::nullopt;
                    return "DONE\tDiscord reply";
                }
            });
            dictation.Toggle();
            if (scenario == 1) focus = reinterpret_cast<HWND>(3);
            if (scenario == 2) dictation.Cancel();
            if (scenario == 3) dictation.Toggle(); // Stop during start request.
            Pump(dictation);
            Check(inserts == ((scenario == 0 || scenario == 3) ? 1 : 0));
            Check(cancels == ((scenario == 1 || scenario == 2 || scenario == 5) ? 1 : 0));
            if (scenario == 3) Check(stops == 1);
            for (int i = 0; i < 10; ++i) dictation.Tick();
            Check(inserts <= 1);
            dictation.Shutdown();
        }
        std::cout << "Six keyboard dictation scenarios passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
