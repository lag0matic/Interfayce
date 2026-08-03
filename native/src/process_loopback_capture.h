#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace interfayce {

struct ProcessLoopbackStats {
    std::uint32_t processId{};
    std::uint32_t sampleRate{};
    std::uint16_t channels{};
    std::uint16_t bitsPerSample{};
    std::uint64_t totalFrames{};
    std::uint64_t audibleFrames{};
    std::uint32_t discontinuities{};
    float peak{};
};

using ProcessLoopbackPcmSink = std::function<bool(
    const std::int16_t* stereoSamples, std::uint32_t frames, std::wstring& error)>;
using ProcessLoopbackStopPredicate = std::function<bool()>;

std::optional<std::uint32_t> FindProcessTreeRoot(const wchar_t* executableName);

std::optional<ProcessLoopbackStats> CaptureProcessLoopback(
    std::uint32_t processId,
    std::chrono::milliseconds duration,
    std::wstring& error,
    const ProcessLoopbackPcmSink& sink = {},
    const ProcessLoopbackStopPredicate& shouldStop = {});

} // namespace interfayce
