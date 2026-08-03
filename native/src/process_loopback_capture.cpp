#include "process_loopback_capture.h"

#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <AudioClient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace interfayce {
namespace {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::FtmBase;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

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

class ActivationHandler final : public RuntimeClass<
        RuntimeClassFlags<ClassicCom>, FtmBase,
        IActivateAudioInterfaceCompletionHandler> {
public:
    ActivationHandler() : completed_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivationHandler() override {
        if (completed_ != nullptr) CloseHandle(completed_);
    }

    HRESULT RuntimeClassInitialize() {
        return completed_ != nullptr ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> activated;
        HRESULT activationResult = E_UNEXPECTED;
        result_ = operation->GetActivateResult(&activationResult, &activated);
        if (SUCCEEDED(result_)) result_ = activationResult;
        if (SUCCEEDED(result_)) result_ = activated.As(&audioClient_);
        SetEvent(completed_);
        return S_OK;
    }

    HRESULT WaitForClient(ComPtr<IAudioClient>& audioClient) {
        const DWORD wait = WaitForSingleObject(completed_, 5000);
        if (wait != WAIT_OBJECT_0) {
            return wait == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                                        : HRESULT_FROM_WIN32(GetLastError());
        }
        if (SUCCEEDED(result_)) audioClient = audioClient_;
        return result_;
    }

private:
    HANDLE completed_{};
    HRESULT result_{E_PENDING};
    ComPtr<IAudioClient> audioClient_;
};

} // namespace

std::optional<std::uint32_t> FindProcessTreeRoot(const wchar_t* executableName) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;

    struct ProcessEntry {
        std::uint32_t id{};
        std::uint32_t parentId{};
    };
    std::vector<ProcessEntry> matches;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, executableName) == 0) {
                matches.push_back({entry.th32ProcessID, entry.th32ParentProcessID});
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (matches.empty()) return std::nullopt;

    std::unordered_set<std::uint32_t> matchingIds;
    for (const auto& process : matches) matchingIds.insert(process.id);
    for (const auto& process : matches) {
        if (!matchingIds.contains(process.parentId)) return process.id;
    }
    return matches.front().id;
}

std::optional<ProcessLoopbackStats> CaptureProcessLoopback(
        std::uint32_t processId, std::chrono::milliseconds duration,
        std::wstring& error, const ProcessLoopbackPcmSink& sink,
        const ProcessLoopbackStopPredicate& shouldStop) {
    if (processId == 0 || duration.count() <= 0) {
        error = L"A process ID and positive capture duration are required.";
        return std::nullopt;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS activation{};
    activation.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activation.ProcessLoopbackParams.TargetProcessId = processId;
    activation.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT parameters{};
    parameters.vt = VT_BLOB;
    parameters.blob.cbSize = sizeof(activation);
    parameters.blob.pBlobData = reinterpret_cast<BYTE*>(&activation);

    auto handler = Microsoft::WRL::Make<ActivationHandler>();
    if (!handler) {
        error = L"Could not allocate the WASAPI activation handler.";
        return std::nullopt;
    }
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT result = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
        &parameters, handler.Get(), &operation);
    if (FAILED(result)) {
        error = L"Process-loopback activation failed: " + HResultText(result);
        return std::nullopt;
    }

    ComPtr<IAudioClient> audioClient;
    result = handler->WaitForClient(audioClient);
    if (FAILED(result) || !audioClient) {
        error = L"Process-loopback activation did not complete: " + HResultText(result);
        return std::nullopt;
    }

    WAVEFORMATEX captureFormat{};
    captureFormat.wFormatTag = WAVE_FORMAT_PCM;
    captureFormat.nChannels = 2;
    captureFormat.nSamplesPerSec = 48000;
    captureFormat.wBitsPerSample = 16;
    captureFormat.nBlockAlign = captureFormat.nChannels
        * captureFormat.wBitsPerSample / 8;
    captureFormat.nAvgBytesPerSec = captureFormat.nSamplesPerSec
        * captureFormat.nBlockAlign;

    constexpr DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK
        | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
        | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
        | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    result = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &captureFormat, nullptr);
    if (FAILED(result)) {
        error = L"WASAPI could not initialize the capture stream: " + HResultText(result);
        return std::nullopt;
    }

    ComPtr<IAudioCaptureClient> captureClient;
    result = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(result)) {
        error = L"WASAPI did not expose its capture client: " + HResultText(result);
        return std::nullopt;
    }
    const HANDLE sampleReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (sampleReady == nullptr) {
        error = L"Could not create the capture notification event.";
        return std::nullopt;
    }
    result = audioClient->SetEventHandle(sampleReady);
    if (FAILED(result)) {
        CloseHandle(sampleReady);
        error = L"WASAPI rejected the capture notification event: " + HResultText(result);
        return std::nullopt;
    }
    result = audioClient->Start();
    if (FAILED(result)) {
        CloseHandle(sampleReady);
        error = L"WASAPI could not start process capture: " + HResultText(result);
        return std::nullopt;
    }

    ProcessLoopbackStats stats{};
    stats.processId = processId;
    stats.sampleRate = captureFormat.nSamplesPerSec;
    stats.channels = captureFormat.nChannels;
    stats.bitsPerSample = captureFormat.wBitsPerSample;
    const auto deadline = std::chrono::steady_clock::now() + duration;
    bool captureFailed = false;
    std::vector<std::int16_t> silence;
    while (std::chrono::steady_clock::now() < deadline && !captureFailed
           && !(shouldStop && shouldStop())) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const DWORD waitMs = static_cast<DWORD>(std::clamp<long long>(remaining.count(), 1, 250));
        const DWORD wait = WaitForSingleObject(sampleReady, waitMs);
        if (wait == WAIT_TIMEOUT) continue;
        if (wait != WAIT_OBJECT_0) {
            error = L"Waiting for Spotify audio failed.";
            captureFailed = true;
            break;
        }
        UINT32 packetFrames = 0;
        result = captureClient->GetNextPacketSize(&packetFrames);
        while (SUCCEEDED(result) && packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD packetFlags = 0;
            result = captureClient->GetBuffer(
                &data, &frames, &packetFlags, nullptr, nullptr);
            if (FAILED(result)) break;
            stats.totalFrames += frames;
            if ((packetFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
                ++stats.discontinuities;
            }
            if ((packetFlags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != nullptr) {
                const auto* samples = reinterpret_cast<const std::int16_t*>(data);
                const std::size_t sampleCount = static_cast<std::size_t>(frames)
                    * captureFormat.nChannels;
                std::int32_t packetPeak = 0;
                for (std::size_t index = 0; index < sampleCount; ++index) {
                    packetPeak = std::max(packetPeak,
                        std::abs(static_cast<std::int32_t>(samples[index])));
                }
                stats.peak = std::max(stats.peak,
                    static_cast<float>(packetPeak) / 32768.0F);
                if (packetPeak > 16) stats.audibleFrames += frames;
                if (sink && !sink(samples, frames, error)) captureFailed = true;
            } else if (sink) {
                silence.assign(static_cast<std::size_t>(frames)
                    * captureFormat.nChannels, 0);
                if (!sink(silence.data(), frames, error)) captureFailed = true;
            }
            const HRESULT releaseResult = captureClient->ReleaseBuffer(frames);
            if (FAILED(releaseResult)) {
                result = releaseResult;
                break;
            }
            result = captureClient->GetNextPacketSize(&packetFrames);
            if (captureFailed) break;
        }
        if (FAILED(result)) {
            error = L"Reading Spotify audio failed: " + HResultText(result);
            captureFailed = true;
        }
    }

    audioClient->Stop();
    CloseHandle(sampleReady);
    if (captureFailed) return std::nullopt;
    return stats;
}

} // namespace interfayce
