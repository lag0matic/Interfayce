#include "process_loopback_capture.h"
#include "audio_endpoint_renderer.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

void ApplyBroadcastGain(const std::int16_t* input, std::uint32_t frames,
        double gainDb, std::vector<std::int16_t>& output) {
    const double multiplier = std::pow(10.0, gainDb / 20.0);
    constexpr double knee = 0.85;
    constexpr double headroom = 1.0 - knee;
    output.resize(static_cast<std::size_t>(frames) * 2);
    for (std::size_t index = 0; index < output.size(); ++index) {
        double value = static_cast<double>(input[index]) / 32768.0 * multiplier;
        const double magnitude = std::abs(value);
        if (magnitude > knee) {
            const double limited = knee + headroom
                * (1.0 - std::exp(-(magnitude - knee) / headroom));
            value = std::copysign(limited, value);
        }
        value = std::clamp(value, -1.0, 32767.0 / 32768.0);
        output[index] = static_cast<std::int16_t>(std::lround(value * 32768.0));
    }
}

int RunProbe(std::uint32_t processId, double seconds, const char* heading,
        const interfayce::ProcessLoopbackPcmSink& sink = {},
        const interfayce::ProcessLoopbackStopPredicate& shouldStop = {}) {
    std::wstring error;
    const auto stats = interfayce::CaptureProcessLoopback(
        processId,
        std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0)),
        error,
        sink,
        shouldStop);
    if (!stats) {
        std::wcerr << L"Process-loopback probe failed: " << error << L'\n';
        return 1;
    }
    const double audiblePercent = stats->totalFrames == 0 ? 0.0
        : 100.0 * static_cast<double>(stats->audibleFrames)
            / static_cast<double>(stats->totalFrames);
    std::cout << heading << '\n'
              << "process_id\t" << stats->processId << '\n'
              << "format\t" << stats->sampleRate << " Hz, "
              << stats->channels << " channels, " << stats->bitsPerSample << " bit\n"
              << "frames\t" << stats->totalFrames << '\n'
              << "audible_frames_percent\t" << audiblePercent << '\n'
              << "peak\t" << stats->peak << '\n'
              << "discontinuities\t" << stats->discontinuities << '\n';
    return stats->totalFrames > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        std::cerr << "Could not initialize COM for the audio engine.\n";
        return 1;
    }
    const auto uninitialize = [] { CoUninitialize(); };

    const bool spotifyProbe = argc > 1
        && std::string_view(argv[1]) == "--probe-spotify";
    const bool processProbe = argc > 1
        && std::string_view(argv[1]) == "--probe-process";
    const bool spotifyBroadcast = argc > 1
        && std::string_view(argv[1]) == "--broadcast-spotify";
    const bool listRenderEndpoints = argc > 1
        && std::string_view(argv[1]) == "--list-render-endpoints";
    if (listRenderEndpoints) {
        for (const auto& endpoint : interfayce::EnumerateRenderEndpoints()) {
            std::wcout << endpoint.name << L'\t' << endpoint.id << L'\n';
        }
        uninitialize();
        return 0;
    }
    if (!spotifyProbe && !processProbe && !spotifyBroadcast) {
        std::cout << "Interfayce Audio Engine\n"
                  << "  --probe-spotify [seconds]\n"
                  << "  --probe-process <process-id> [seconds]\n"
                  << "  --broadcast-spotify [seconds] [--gain-db 0-24]\n"
                  << "  --list-render-endpoints\n";
        uninitialize();
        return 0;
    }

    std::optional<std::uint32_t> targetProcess;
    int durationArgument = 2;
    const char* heading = "PROCESS_LOOPBACK";
    if (spotifyProbe || spotifyBroadcast) {
        targetProcess = interfayce::FindProcessTreeRoot(L"Spotify.exe");
        heading = spotifyBroadcast
            ? "SPOTIFY_INTERFAYCE_BROADCAST" : "SPOTIFY_PROCESS_LOOPBACK";
    } else if (argc > 2) {
        try {
            targetProcess = static_cast<std::uint32_t>(std::stoul(argv[2]));
            durationArgument = 3;
        } catch (...) {
            std::cerr << "Process ID must be an unsigned integer.\n";
            uninitialize();
            return 1;
        }
    }
    if (!targetProcess) {
        std::cerr << ((spotifyProbe || spotifyBroadcast)
            ? "Spotify is not running; no audio process tree is available.\n"
            : "Usage: --probe-process <process-id> [seconds]\n");
        uninitialize();
        return 1;
    }

    double seconds = spotifyBroadcast ? 86400.0 : 5.0;
    if (argc > durationArgument) {
        try {
            seconds = std::clamp(std::stod(argv[durationArgument]), 0.25,
                spotifyBroadcast ? 86400.0 : 30.0);
        } catch (...) {
            std::cerr << "Capture duration must be a number of seconds.\n";
            uninitialize();
            return 1;
        }
    }
    double broadcastGainDb = 12.0;
    if (spotifyBroadcast) {
        for (int index = durationArgument + 1; index < argc; ++index) {
            if (std::string_view(argv[index]) != "--gain-db" || index + 1 >= argc) {
                std::cerr << "Broadcast options must use --gain-db <0-24>.\n";
                uninitialize();
                return 1;
            }
            try {
                broadcastGainDb = std::clamp(std::stod(argv[++index]), 0.0, 24.0);
            } catch (...) {
                std::cerr << "Broadcast gain must be a number from 0 through 24 dB.\n";
                uninitialize();
                return 1;
            }
        }
    }
    int result = 0;
    if (spotifyBroadcast) {
        constexpr wchar_t kBroadcastStopEventName[] = L"Local\\InterfayceBroadcastStop";
        const HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, kBroadcastStopEventName);
        interfayce::AudioEndpointRenderer renderer;
        std::wstring error;
        if (!renderer.Start(L"CABLE Input (VB-Audio Virtual Cable)", error)) {
            std::wcerr << L"Broadcast did not start: " << error << L'\n';
            if (stopEvent != nullptr) CloseHandle(stopEvent);
            uninitialize();
            return 1;
        }
        std::vector<std::int16_t> boostedSamples;
        result = RunProbe(*targetProcess, seconds, heading,
            [&renderer, &boostedSamples, broadcastGainDb](
                        const std::int16_t* samples, std::uint32_t frames,
                        std::wstring& sinkError) {
                ApplyBroadcastGain(samples, frames, broadcastGainDb, boostedSamples);
                return renderer.Write(boostedSamples.data(), frames, sinkError);
            }, [stopEvent] {
                return stopEvent != nullptr
                    && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0;
            });
        renderer.Stop();
        if (stopEvent != nullptr) CloseHandle(stopEvent);
    } else {
        result = RunProbe(*targetProcess, seconds, heading);
    }
    uninitialize();
    return result;
}
