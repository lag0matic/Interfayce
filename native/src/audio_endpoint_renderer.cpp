#define NOMINMAX
#include "audio_endpoint_renderer.h"

#include <Windows.h>
#include <AudioClient.h>
#include <propkeydef.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>

namespace interfayce {
namespace {

using Microsoft::WRL::ComPtr;

std::wstring Lowercase(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return text;
}

std::wstring HResultText(HRESULT result) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0,
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = length > 0 && buffer != nullptr ? buffer : L"Unknown error";
    if (buffer != nullptr) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    wchar_t code[16]{};
    swprintf_s(code, L"0x%08X", static_cast<unsigned int>(result));
    return message + L" (" + code + L")";
}

std::wstring DeviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) return {};
    PROPVARIANT value{};
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value))
        && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

ComPtr<IMMDevice> FindEndpoint(EDataFlow flow, const wchar_t* requiredName) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator)))) return {};
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices))) return {};
    UINT count = 0;
    devices->GetCount(&count);
    const auto required = Lowercase(requiredName == nullptr
        ? std::wstring{} : std::wstring(requiredName));
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(index, &device))) continue;
        if (Lowercase(DeviceName(device.Get())) == required) {
            return device;
        }
    }
    return {};
}

} // namespace

struct AudioEndpointRenderer::State {
    ComPtr<IAudioClient> audioClient;
    ComPtr<IAudioRenderClient> renderClient;
    HANDLE sampleReady{};
    UINT32 bufferFrames{};
    bool started{};
};

std::vector<RenderEndpointInfo> EnumerateRenderEndpoints() {
    std::vector<RenderEndpointInfo> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator)))) return result;
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
        return result;
    }
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(index, &device))) continue;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id))) continue;
        result.push_back({id, DeviceName(device.Get())});
        CoTaskMemFree(id);
    }
    return result;
}

AudioEndpointRenderer::AudioEndpointRenderer() : state_(std::make_unique<State>()) {}
AudioEndpointRenderer::~AudioEndpointRenderer() { Stop(); }

bool AudioEndpointRenderer::Start(const wchar_t* requiredName, std::wstring& error) {
    Stop();
    auto device = FindEndpoint(eRender, requiredName);
    if (!device) {
        error = L"The required broadcast endpoint is not installed or active: ";
        error += requiredName == nullptr ? L"(unnamed)" : requiredName;
        return false;
    }
    HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(state_->audioClient.ReleaseAndGetAddressOf()));
    if (FAILED(result)) {
        error = L"Could not activate the Interfayce feed: " + HResultText(result);
        return false;
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    result = state_->audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        0, 0, &format, nullptr);
    if (FAILED(result)) {
        error = L"The Interfayce feed rejected 48 kHz stereo PCM: " + HResultText(result);
        Stop();
        return false;
    }
    result = state_->audioClient->GetBufferSize(&state_->bufferFrames);
    if (SUCCEEDED(result)) {
        result = state_->audioClient->GetService(IID_PPV_ARGS(&state_->renderClient));
    }
    state_->sampleReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (SUCCEEDED(result) && state_->sampleReady != nullptr) {
        result = state_->audioClient->SetEventHandle(state_->sampleReady);
    }
    if (FAILED(result) || state_->sampleReady == nullptr) {
        error = L"Could not prepare the Interfayce feed buffer: "
            + HResultText(FAILED(result) ? result : HRESULT_FROM_WIN32(GetLastError()));
        Stop();
        return false;
    }
    result = state_->audioClient->Start();
    if (FAILED(result)) {
        error = L"Could not start the Interfayce feed: " + HResultText(result);
        Stop();
        return false;
    }
    state_->started = true;
    return true;
}

bool AudioEndpointRenderer::Write(const std::int16_t* stereoSamples,
        std::uint32_t frames, std::wstring& error) {
    if (!state_->started || stereoSamples == nullptr || frames == 0) return false;
    std::uint32_t written = 0;
    while (written < frames) {
        UINT32 padding = 0;
        HRESULT result = state_->audioClient->GetCurrentPadding(&padding);
        if (FAILED(result)) {
            error = L"Could not read Interfayce feed capacity: " + HResultText(result);
            return false;
        }
        const UINT32 available = state_->bufferFrames - padding;
        if (available == 0) {
            if (WaitForSingleObject(state_->sampleReady, 250) != WAIT_OBJECT_0) {
                error = L"Timed out waiting for the Interfayce feed.";
                return false;
            }
            continue;
        }
        const UINT32 run = std::min<std::uint32_t>(available, frames - written);
        BYTE* destination = nullptr;
        result = state_->renderClient->GetBuffer(run, &destination);
        if (FAILED(result)) {
            error = L"Could not acquire the Interfayce feed buffer: " + HResultText(result);
            return false;
        }
        CopyMemory(destination, stereoSamples + static_cast<std::size_t>(written) * 2,
            static_cast<std::size_t>(run) * 2 * sizeof(std::int16_t));
        result = state_->renderClient->ReleaseBuffer(run, 0);
        if (FAILED(result)) {
            error = L"Could not submit frames to the Interfayce feed: " + HResultText(result);
            return false;
        }
        written += run;
    }
    return true;
}

void AudioEndpointRenderer::Stop() {
    if (!state_) return;
    if (state_->started && state_->audioClient) state_->audioClient->Stop();
    state_->started = false;
    state_->renderClient.Reset();
    state_->audioClient.Reset();
    if (state_->sampleReady != nullptr) CloseHandle(state_->sampleReady);
    state_->sampleReady = nullptr;
    state_->bufferFrames = 0;
}

} // namespace interfayce
