#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace interfayce {

struct RenderEndpointInfo {
    std::wstring id;
    std::wstring name;
};

std::vector<RenderEndpointInfo> EnumerateRenderEndpoints();

class AudioEndpointRenderer {
public:
    AudioEndpointRenderer();
    ~AudioEndpointRenderer();
    AudioEndpointRenderer(const AudioEndpointRenderer&) = delete;
    AudioEndpointRenderer& operator=(const AudioEndpointRenderer&) = delete;

    bool Start(const wchar_t* requiredName, std::wstring& error);
    bool Write(const std::int16_t* stereoSamples, std::uint32_t frames,
               std::wstring& error);
    void Stop();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace interfayce
