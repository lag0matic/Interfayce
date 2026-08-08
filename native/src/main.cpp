#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <TlHelp32.h>
#include <wincrypt.h>
#include <dwmapi.h>
#include <openvr.h>

#include "overlay_renderer.h"
#include "broadcast_controller.h"
#include "desktop_surface_manager.h"
#include "desktop_surface_registry.h"
#include "tray_icon.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <string_view>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr char kAppKey[] = "com.lag0matic.interfayce";
constexpr char kActionSetPath[] = "/actions/interfayce";
constexpr char kLeftDragActionPath[] = "/actions/interfayce/in/left_drag";
constexpr char kRightDragActionPath[] = "/actions/interfayce/in/right_drag";
constexpr char kLeftUiClickActionPath[] = "/actions/interfayce/in/left_ui_click";
constexpr char kRightUiClickActionPath[] = "/actions/interfayce/in/right_ui_click";
constexpr char kLeftSurfaceGrabActionPath[] = "/actions/interfayce/in/left_surface_grab";
constexpr char kRightSurfaceGrabActionPath[] = "/actions/interfayce/in/right_surface_grab";
constexpr char kRightSurfaceScrollActionPath[] = "/actions/interfayce/in/right_surface_scroll";
constexpr char kLeftHapticActionPath[] = "/actions/interfayce/out/left_haptic";
constexpr char kRightHapticActionPath[] = "/actions/interfayce/out/right_haptic";
constexpr char kWristOverlayKey[] = "com.lag0matic.interfayce.wrist.panel";
constexpr char kCursorOverlayKey[] = "com.lag0matic.interfayce.wrist.cursor";
constexpr char kLeftCursorOverlayKey[] = "com.lag0matic.interfayce.keyboard.left_cursor";
constexpr wchar_t kShutdownEventName[] = L"Local\\InterfayceOverlayShutdown";
constexpr uint16_t kVoiceServicePort = 43817;

enum class DragHand { None, Left, Right };

struct Vector3 {
    float x{};
    float y{};
    float z{};
};

std::filesystem::path ExecutableDirectory(char* executablePath) {
    return std::filesystem::absolute(executablePath).parent_path();
}

std::filesystem::path ProjectRoot(const std::filesystem::path& executableDirectory) {
    // A staged/package build may live inside the source tree. Prefer its complete
    // colocated runtime so bundled tools do not accidentally resolve against the
    // surrounding development checkout.
    if (std::filesystem::exists(executableDirectory / "runtime" / "node.exe")
        && std::filesystem::exists(executableDirectory / "tools" / "slimevr_probe.cjs")) {
        return executableDirectory;
    }
    const auto developmentRoot = executableDirectory.parent_path().parent_path().parent_path();
    return std::filesystem::exists(developmentRoot / "src" / "interfayce")
        ? developmentRoot : executableDirectory;
}

std::filesystem::path UserCacheFile(std::wstring_view filename) {
    wchar_t localAppData[32768]{};
    const auto length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    auto directory = length > 0 && length < std::size(localAppData)
        ? std::filesystem::path(localAppData) / "Interfayce" / "cache"
        : std::filesystem::temp_directory_path() / "Interfayce";
    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);
    return directory / filename;
}

std::optional<std::string> LocalServiceToken() {
    wchar_t localAppData[32768]{};
    const auto length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0 || length >= std::size(localAppData)) return std::nullopt;
    const auto path = std::filesystem::path(localAppData) / "Interfayce" / "secure"
        / "local-service-token.dpapi";
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    const std::vector<BYTE> protectedBytes(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (protectedBytes.empty() || protectedBytes.size() > MAXDWORD) return std::nullopt;
    static constexpr BYTE entropyBytes[] = "Interfayce secure settings v1";
    DATA_BLOB protectedBlob{static_cast<DWORD>(protectedBytes.size()),
        const_cast<BYTE*>(protectedBytes.data())};
    DATA_BLOB entropyBlob{static_cast<DWORD>(sizeof(entropyBytes) - 1),
        const_cast<BYTE*>(entropyBytes)};
    DATA_BLOB clearBlob{};
    if (!CryptUnprotectData(&protectedBlob, nullptr, &entropyBlob, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &clearBlob)) {
        return std::nullopt;
    }
    std::string token(reinterpret_cast<char*>(clearBlob.pbData), clearBlob.cbData);
    LocalFree(clearBlob.pbData);
    return token.empty() ? std::nullopt : std::optional<std::string>{std::move(token)};
}

std::wstring LocalClockText() {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);
    wchar_t formatted[64]{};
    const int length = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS,
        &localTime, nullptr, formatted, static_cast<int>(std::size(formatted)));
    if (length <= 1) return L"";
    std::wstring result(formatted, static_cast<size_t>(length - 1));
    const auto suffix = result.find(L' ');
    if (suffix != std::wstring::npos) result.erase(suffix);
    return result;
}

std::filesystem::path NodeExecutable(const std::filesystem::path& projectRoot) {
    const auto bundled = projectRoot / "runtime" / "node.exe";
    return std::filesystem::exists(bundled) ? bundled : std::filesystem::path(L"node.exe");
}

bool SlimeAdapterAvailable(const std::filesystem::path& projectRoot) {
    const auto script = projectRoot / "tools" / "slimevr_probe.cjs";
    const auto bundledProtocol = projectRoot / "tools" / "vendor" / "solarxr-protocol";
    wchar_t sourceRoot[32768]{};
    const auto sourceLength = GetEnvironmentVariableW(
        L"SLIMEVR_SERVER_SOURCE", sourceRoot, static_cast<DWORD>(std::size(sourceRoot)));
    const bool protocolAvailable = std::filesystem::exists(bundledProtocol)
        || (sourceLength > 0 && sourceLength < std::size(sourceRoot)
            && std::filesystem::exists(
                std::filesystem::path(sourceRoot) / "solarxr-protocol"));
    wchar_t resolvedNode[MAX_PATH]{};
    const bool nodeAvailable = std::filesystem::exists(projectRoot / "runtime" / "node.exe")
        || SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, resolvedNode, nullptr) > 0;
    return std::filesystem::exists(script) && protocolAvailable && nodeAvailable;
}

bool IsProcessRunning(std::wstring_view executableName) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, std::wstring(executableName).c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool IsLocalTcpPortOpen(uint16_t port, std::chrono::milliseconds timeout) {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return false;
    const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    u_long nonBlocking = 1;
    ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    bool connected = connect(socketHandle, reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == 0;
    if (!connected && WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(socketHandle, &writable);
        timeval wait{};
        wait.tv_sec = static_cast<long>(timeout.count() / 1000);
        wait.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
        if (select(0, nullptr, &writable, nullptr, &wait) > 0) {
            int socketError = 0;
            int length = sizeof(socketError);
            connected = getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                reinterpret_cast<char*>(&socketError), &length) == 0 && socketError == 0;
        }
    }
    closesocket(socketHandle);
    WSACleanup();
    return connected;
}

std::optional<std::string> LocalHttpRequest(std::string_view method, std::string_view path,
                                            std::chrono::milliseconds timeout,
                                            std::string_view body = {}) {
    const auto serviceToken = LocalServiceToken();
    if (!serviceToken) return std::nullopt;
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return std::nullopt;
    const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) {
        WSACleanup();
        return std::nullopt;
    }
    const DWORD timeoutMs = static_cast<DWORD>(timeout.count());
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(kVoiceServicePort);
    u_long nonBlocking = 1;
    ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
    bool connected = connect(socketHandle, reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == 0;
    if (!connected) {
        const int connectError = WSAGetLastError();
        if (connectError == WSAEWOULDBLOCK || connectError == WSAEINPROGRESS
            || connectError == WSAEINVAL) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(socketHandle, &writable);
            timeval wait{};
            // Localhost should complete immediately. Never let service traffic
            // stall the latency-sensitive OpenVR input loop.
            wait.tv_usec = 50000;
            if (select(0, nullptr, &writable, nullptr, &wait) > 0) {
                int socketError = 0;
                int length = sizeof(socketError);
                connected = getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                    reinterpret_cast<char*>(&socketError), &length) == 0
                    && socketError == 0;
            }
        }
    }
    nonBlocking = 0;
    ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
    if (!connected) {
        closesocket(socketHandle);
        WSACleanup();
        return std::nullopt;
    }
    const std::string request = std::string(method) + " " + std::string(path)
        + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(kVoiceServicePort)
        + "\r\nX-Interfayce-Token: " + *serviceToken
        + "\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
    size_t sent = 0;
    while (sent < request.size()) {
        const auto amount = send(socketHandle, request.data() + sent,
            static_cast<int>(request.size() - sent), 0);
        if (amount <= 0) {
            closesocket(socketHandle);
            WSACleanup();
            return std::nullopt;
        }
        sent += static_cast<size_t>(amount);
    }
    std::string response;
    std::array<char, 2048> buffer{};
    while (true) {
        const auto amount = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (amount == 0) break;
        if (amount < 0) {
            closesocket(socketHandle);
            WSACleanup();
            return std::nullopt;
        }
        response.append(buffer.data(), static_cast<size_t>(amount));
    }
    closesocket(socketHandle);
    WSACleanup();
    if (response.find(" 200 ") == std::string::npos) return std::nullopt;
    const auto bodyStart = response.find("\r\n\r\n");
    return bodyStart == std::string::npos
        ? std::optional<std::string>{} : response.substr(bodyStart + 4);
}

bool VoiceServiceAvailable() {
    const auto response = LocalHttpRequest("GET", "/health", std::chrono::milliseconds(150));
    return response && *response == "ready";
}

bool LaunchVoiceService(const std::filesystem::path& executableDirectory,
                        const std::filesystem::path& projectRoot) {
    const auto packagedService = executableDirectory / "service" / "InterfayceService.exe";
    if (std::filesystem::exists(packagedService)) {
        std::wstring command = L"\"" + packagedService.wstring()
            + L"\" voice-service --warm";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, executableDirectory.wstring().c_str(), &startup, &process)) return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }
    const auto sourceDirectory = (projectRoot / "src").wstring();
    std::wstring command = L"cmd.exe /d /s /c \"set \"PYTHONPATH=" + sourceDirectory
        + L"\" && python -m interfayce voice-service --warm\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, projectRoot.wstring().c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool LaunchDesktopSettings(const std::filesystem::path& executableDirectory,
                           const std::filesystem::path& projectRoot) {
    const auto packagedService = executableDirectory / "service" / "InterfayceService.exe";
    if (std::filesystem::exists(packagedService)) {
        std::wstring command = L"\"" + packagedService.wstring() + L"\" settings";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
        startup.wShowWindow = SW_SHOWNORMAL;
        PROCESS_INFORMATION process{};
        const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
            0, nullptr, executableDirectory.wstring().c_str(), &startup, &process) != FALSE;
        if (!launched) return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }
    const auto sourceDirectory = (projectRoot / "src").wstring();
    wchar_t previousPythonPath[32768]{};
    const auto previousLength = GetEnvironmentVariableW(
        L"PYTHONPATH", previousPythonPath, static_cast<DWORD>(std::size(previousPythonPath)));
    SetEnvironmentVariableW(L"PYTHONPATH", sourceDirectory.c_str());
    std::wstring command = L"pythonw.exe -m interfayce settings";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
    startup.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
        0, nullptr, projectRoot.wstring().c_str(), &startup, &process) != FALSE;
    if (previousLength > 0 && previousLength < std::size(previousPythonPath)) {
        SetEnvironmentVariableW(L"PYTHONPATH", previousPythonPath);
    } else {
        SetEnvironmentVariableW(L"PYTHONPATH", nullptr);
    }
    if (!launched) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool RelaunchInterfayce(const std::filesystem::path& executable,
                        const std::filesystem::path& workingDirectory) {
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(executable.wstring().c_str(), command.data(),
        nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
        workingDirectory.wstring().c_str(), &startup, &process) != FALSE;
    if (!launched) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), length);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring RequestMusicVoiceCommand() {
    const auto response = LocalHttpRequest("POST", "/listen/music", std::chrono::seconds(30));
    return response ? Utf8ToWide(*response) : L"ERROR\t\tVoice service did not respond.";
}

std::wstring VoiceStatusFromResponse(const std::wstring& response) {
    const auto firstTab = response.find(L'\t');
    const auto secondTab = firstTab == std::wstring::npos
        ? std::wstring::npos : response.find(L'\t', firstTab + 1);
    if (firstTab != std::wstring::npos && secondTab != std::wstring::npos
        && response.substr(0, firstTab) != L"OK") {
        auto transcript = response.substr(firstTab + 1, secondTab - firstTab - 1);
        if (!transcript.empty()) {
            if (transcript.size() > 26) transcript = transcript.substr(0, 25) + L"\u2026";
            return L"HEARD: " + transcript;
        }
    }
    if (secondTab != std::wstring::npos && secondTab + 1 < response.size()) {
        return response.substr(secondTab + 1);
    }
    return response.empty() ? L"VOICE ERROR" : response;
}

struct CommsState {
    std::wstring status{L"IDLE"};
    std::wstring transcript;
    bool active{};
};

struct AssistantPanelState {
    std::wstring status{L"READY"};
    std::wstring transcript;
    std::wstring response;
    bool active{};
};

std::optional<AssistantPanelState> ParseAssistantState(const std::string& response) {
    if (response.empty()) return std::nullopt;
    AssistantPanelState state;
    const auto first = response.find('\t');
    const auto second = first == std::string::npos
        ? std::string::npos : response.find('\t', first + 1);
    state.status = Utf8ToWide(response.substr(0, first));
    if (first != std::string::npos) {
        state.transcript = Utf8ToWide(response.substr(
            first + 1, second == std::string::npos ? std::string::npos : second - first - 1));
    }
    if (second != std::string::npos) state.response = Utf8ToWide(response.substr(second + 1));
    state.active = state.status == L"LISTENING" || state.status == L"THINKING"
        || state.status == L"SEARCHING" || state.status == L"READING"
        || state.status == L"CHECKING" || state.status == L"RESPONDING"
        || state.status == L"STOPPING";
    return state;
}

std::optional<AssistantPanelState> RequestAssistantState(
        std::string_view method, std::string_view path,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(750)) {
    const auto response = LocalHttpRequest(method, path, timeout);
    return response ? ParseAssistantState(*response) : std::nullopt;
}

std::optional<CommsState> ParseCommsState(const std::string& response) {
    if (response.empty()) return std::nullopt;
    const auto separator = response.find('\t');
    CommsState state;
    state.status = Utf8ToWide(response.substr(0, separator));
    if (separator != std::string::npos) {
        state.transcript = Utf8ToWide(response.substr(separator + 1));
    }
    state.active = state.status == L"LISTENING" || state.status == L"SENT"
        || state.status == L"STOPPING";
    return state;
}

std::optional<CommsState> RequestCommsState(std::string_view method, std::string_view path) {
    const auto response = LocalHttpRequest(method, path, std::chrono::milliseconds(750));
    return response ? ParseCommsState(*response) : std::nullopt;
}

std::optional<std::array<std::wstring, 4>> RequestCommsShortcutLabels() {
    const auto response = LocalHttpRequest(
        "GET", "/comms/shortcuts", std::chrono::milliseconds(750));
    if (!response) return std::nullopt;
    std::array<std::wstring, 4> labels;
    size_t start = 0;
    for (size_t index = 0; index < labels.size(); ++index) {
        const auto end = response->find('\t', start);
        labels[index] = Utf8ToWide(response->substr(
            start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return labels;
}

struct DesktopFavorite {
    std::wstring label;
    std::wstring executable;
    bool operator==(const DesktopFavorite&) const = default;
};

struct PendingFavoriteCapture {
    uint64_t surfaceId{};
    std::wstring executable;
    std::chrono::steady_clock::time_point nextPoll;
    std::chrono::steady_clock::time_point deadline;
};

std::optional<std::array<DesktopFavorite, 3>> RequestDesktopFavorites() {
    const auto response = LocalHttpRequest(
        "GET", "/desktop/favorites", std::chrono::milliseconds(750));
    if (!response) return std::nullopt;
    std::array<DesktopFavorite, 3> favorites;
    size_t start = 0;
    for (size_t index = 0; index < favorites.size() && start <= response->size(); ++index) {
        const auto end = response->find('\n', start);
        const auto line = response->substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        const auto separator = line.find('\t');
        if (separator != std::string::npos) {
            favorites[index].label = Utf8ToWide(line.substr(0, separator));
            favorites[index].executable = Utf8ToWide(line.substr(separator + 1));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return favorites;
}

void RecordDesktopRecent(const interfayce::DesktopSource& source) {
    if (source.kind != interfayce::DesktopSource::Kind::Window
        || source.executablePath.empty()) return;
    auto label = std::filesystem::path(source.executablePath).stem().wstring();
    if (label.empty()) label = source.detail;
    if (label.empty()) label = source.label;
    if (!label.empty()) label.front() = static_cast<wchar_t>(std::towupper(label.front()));
    const auto body = WideToUtf8(label) + "\t" + WideToUtf8(source.executablePath);
    std::thread([body] {
        static_cast<void>(LocalHttpRequest(
            "POST", "/desktop/recent", std::chrono::milliseconds(250), body));
    }).detach();
}

void LaunchSpotifyControl(const std::filesystem::path& projectRoot, const wchar_t* operation) {
    static_cast<void>(projectRoot);
    std::string operationPath;
    for (const auto* character = operation; *character; ++character) {
        operationPath.push_back(static_cast<char>(*character));
    }
    LocalHttpRequest("POST", "/music/control/" + operationPath,
        std::chrono::milliseconds(750));
}

void RefreshSpotifyArt(const std::filesystem::path& projectRoot, const std::filesystem::path& outputPath) {
    static_cast<void>(projectRoot);
    const auto art = LocalHttpRequest("GET", "/music/art", std::chrono::seconds(3));
    if (art && !art->empty()) {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(art->data(), static_cast<std::streamsize>(art->size()));
    }
}

struct MusicPlaybackState {
    std::wstring line;
    bool playing{};
};

MusicPlaybackState ReadSpotifyNowPlaying(const std::filesystem::path& projectRoot) {
    static_cast<void>(projectRoot);
    const auto response = LocalHttpRequest("GET", "/music/current", std::chrono::seconds(2));
    if (!response || response->empty()) return {};
    const auto statusEnd = response->find('\t');
    const auto artistEnd = statusEnd == std::string::npos
        ? std::string::npos : response->find('\t', statusEnd + 1);
    if (statusEnd == std::string::npos || artistEnd == std::string::npos) {
        return {Utf8ToWide(*response), false};
    }
    const auto artist = response->substr(statusEnd + 1, artistEnd - statusEnd - 1);
    const auto title = response->substr(artistEnd + 1);
    return {Utf8ToWide(artist + " - " + title), response->substr(0, statusEnd) == "PLAYING"};
}

struct TtsSettingsState {
    int volumePercent{85};
    bool muted{};
    float hapticStrength{0.22F};
    float broadcastGainDb{12.0F};
    bool wristRight{};
    float wristOffsetX{};
    float wristOffsetY{};
    float wristOffsetZ{};
    float wristPitch{};
    float wristYaw{};
    float wristRoll{};
    float playspaceTravelLimitMeters{10.0F};
};

std::optional<TtsSettingsState> ParseTtsSettings(const std::string& response) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= response.size()) {
        const auto end = response.find('\t', start);
        fields.push_back(response.substr(start,
            end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (fields.size() < 2) return std::nullopt;
    try {
        TtsSettingsState state;
        state.volumePercent = std::clamp(std::stoi(fields[0]), 0, 100);
        state.muted = fields[1] == "1";
        if (fields.size() > 3) state.hapticStrength = std::clamp(std::stof(fields[3]), 0.0F, 1.0F);
        if (fields.size() > 4) state.broadcastGainDb = std::clamp(std::stof(fields[4]), 0.0F, 24.0F);
        if (fields.size() > 5) state.wristRight = fields[5] == "right";
        if (fields.size() > 6) state.wristOffsetX = std::clamp(std::stof(fields[6]), -0.10F, 0.10F);
        if (fields.size() > 7) state.wristOffsetY = std::clamp(std::stof(fields[7]), -0.10F, 0.10F);
        if (fields.size() > 8) state.wristOffsetZ = std::clamp(std::stof(fields[8]), -0.10F, 0.10F);
        if (fields.size() > 9) state.wristPitch = std::clamp(std::stof(fields[9]), -45.0F, 45.0F);
        if (fields.size() > 10) state.wristYaw = std::clamp(std::stof(fields[10]), -45.0F, 45.0F);
        if (fields.size() > 11) state.wristRoll = std::clamp(std::stof(fields[11]), -45.0F, 45.0F);
        if (fields.size() > 12) state.playspaceTravelLimitMeters =
            std::clamp(std::stof(fields[12]), 1.0F, 50.0F);
        return state;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<TtsSettingsState> ReadTtsSettings() {
    const auto response = LocalHttpRequest("GET", "/settings", std::chrono::milliseconds(750));
    return response ? ParseTtsSettings(*response) : std::nullopt;
}

std::optional<TtsSettingsState> ChangeTtsSetting(std::string_view path) {
    const auto response = LocalHttpRequest("POST", path, std::chrono::milliseconds(750));
    return response ? ParseTtsSettings(*response) : std::nullopt;
}

std::wstring ReadControllerBatteryLine(vr::IVRSystem* system) {
    const auto batteryText = [&](vr::ETrackedControllerRole role, const wchar_t* label) {
        const auto index = system->GetTrackedDeviceIndexForControllerRole(role);
        if (index == vr::k_unTrackedDeviceIndexInvalid || !system->IsTrackedDeviceConnected(index)) {
            return std::wstring(label) + L" --";
        }
        const auto percent = static_cast<int>(std::lround(
            system->GetFloatTrackedDeviceProperty(index, vr::Prop_DeviceBatteryPercentage_Float) * 100.0F));
        return std::wstring(label) + L" " + std::to_wstring(percent) + L"%";
    };
    return batteryText(vr::TrackedControllerRole_LeftHand, L"L") + L"    "
        + batteryText(vr::TrackedControllerRole_RightHand, L"R");
}

int ReadControllerBatteryPercent(vr::IVRSystem* system, vr::ETrackedControllerRole role) {
    const auto index = system->GetTrackedDeviceIndexForControllerRole(role);
    if (index == vr::k_unTrackedDeviceIndexInvalid || !system->IsTrackedDeviceConnected(index)) {
        return -1;
    }
    return static_cast<int>(std::lround(system->GetFloatTrackedDeviceProperty(
        index, vr::Prop_DeviceBatteryPercentage_Float) * 100.0F));
}

std::wstring DesktopSurfaceLine(size_t count) {
    if (count == 0) return L"No open surfaces";
    return std::to_wstring(count) + (count == 1 ? L" open surface" : L" open surfaces");
}

struct SlimeRigStatus {
    std::array<std::wstring, 8> slots{};
    bool mountReady{};
    bool received{};
};

std::optional<std::string> RunHiddenAndCapture(std::wstring command,
                                               const std::filesystem::path& workingDirectory,
                                               std::chrono::milliseconds timeout) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return std::nullopt;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    const HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(writePipe);
        CloseHandle(readPipe);
        return std::nullopt;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullInput;
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, workingDirectory.wstring().c_str(), &startup, &process) != FALSE;
    CloseHandle(writePipe);
    CloseHandle(nullInput);
    if (!launched) {
        CloseHandle(readPipe);
        return std::nullopt;
    }
    CloseHandle(process.hThread);

    const DWORD waitResult = WaitForSingleObject(process.hProcess,
        static_cast<DWORD>(timeout.count()));
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
    }
    std::string output;
    std::array<char, 512> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()),
            &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer.data(), bytesRead);
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    return waitResult == WAIT_OBJECT_0 && exitCode == 0
        ? std::optional<std::string>{std::move(output)} : std::nullopt;
}

SlimeRigStatus ReadSlimeTrackerBatteries(const std::filesystem::path& projectRoot) {
    SlimeRigStatus status;
    std::wstring command = L"\"" + NodeExecutable(projectRoot).wstring() + L"\" \""
        + (projectRoot / "tools" / "slimevr_probe.cjs").wstring() + L"\" --summary";
    if (const auto captured = RunHiddenAndCapture(
            std::move(command), projectRoot, std::chrono::milliseconds(6500))) {
        status.received = true;
        const auto newline = captured->find_first_of("\r\n");
        const std::string line = captured->substr(0, newline);
        size_t start = 0;
        for (size_t index = 0; index < status.slots.size() && start <= line.size(); ++index) {
            const auto end = line.find('\t', start);
            const auto value = line.substr(
                start, end == std::string::npos ? std::string::npos : end - start);
            status.slots[index] = Utf8ToWide(value);
            if (end == std::string::npos) {
                start = line.size() + 1;
                break;
            }
            start = end + 1;
        }
        if (start <= line.size()) {
            status.mountReady = line.substr(start).starts_with("MOUNT_OK");
        }
    }
    return status;
}

void LaunchSlimeReset(const std::filesystem::path& projectRoot, const wchar_t* kind) {
    std::wstring command = L"\"" + NodeExecutable(projectRoot).wstring() + L"\" \""
        + (projectRoot / "tools" / "slimevr_reset.cjs").wstring() + L"\" " + kind;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, projectRoot.wstring().c_str(), &startup, &process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
}

interfayce::RigLegProfile RunSlimeLegProfile(
        const std::filesystem::path& projectRoot, const wchar_t* action) {
    std::wstring command = L"\"" + NodeExecutable(projectRoot).wstring() + L"\" \""
        + (projectRoot / "tools" / "slimevr_leg_profile.cjs").wstring() + L"\" " + action;
    const auto captured = RunHiddenAndCapture(
        std::move(command), projectRoot, std::chrono::milliseconds(7000));
    if (!captured) return interfayce::RigLegProfile::Error;
    const auto newline = captured->find_first_of("\r\n");
    const auto line = captured->substr(0, newline);
    if (line.starts_with("CONFIG\t")) return interfayce::RigLegProfile::Config;
    if (line.starts_with("PLAY\t")) return interfayce::RigLegProfile::Play;
    if (line.starts_with("CUSTOM\t")) return interfayce::RigLegProfile::Custom;
    return interfayce::RigLegProfile::Error;
}

std::optional<Vector3> ReadControllerPosition(vr::IVRSystem* system, DragHand hand) {
    const auto role = hand == DragHand::Left ? vr::TrackedControllerRole_LeftHand
                                             : vr::TrackedControllerRole_RightHand;
    const auto index = system->GetTrackedDeviceIndexForControllerRole(role);
    if (index == vr::k_unTrackedDeviceIndexInvalid) {
        return std::nullopt;
    }
    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding, 0.0F, poses.data(), static_cast<uint32_t>(poses.size()));
    const auto& pose = poses[index];
    if (!pose.bDeviceIsConnected || !pose.bPoseIsValid
        || pose.eTrackingResult != vr::TrackingResult_Running_OK) {
        return std::nullopt;
    }
    const auto& matrix = pose.mDeviceToAbsoluteTracking;
    return Vector3{matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
}

std::optional<vr::HmdMatrix34_t> ReadControllerPose(vr::IVRSystem* system, DragHand hand) {
    const auto role = hand == DragHand::Left ? vr::TrackedControllerRole_LeftHand
                                             : vr::TrackedControllerRole_RightHand;
    const auto index = system->GetTrackedDeviceIndexForControllerRole(role);
    if (index == vr::k_unTrackedDeviceIndexInvalid) {
        return std::nullopt;
    }
    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding, 0.0F, poses.data(), static_cast<uint32_t>(poses.size()));
    const auto& pose = poses[index];
    if (!pose.bDeviceIsConnected || !pose.bPoseIsValid
        || pose.eTrackingResult != vr::TrackingResult_Running_OK) {
        return std::nullopt;
    }
    return pose.mDeviceToAbsoluteTracking;
}

vr::HmdMatrix34_t TranslateOrigin(const vr::HmdMatrix34_t& baseline, const Vector3 offset) {
    auto translated = baseline;
    for (int row = 0; row < 3; ++row) {
        translated.m[row][3] += baseline.m[row][0] * offset.x
            + baseline.m[row][1] * offset.y + baseline.m[row][2] * offset.z;
    }
    return translated;
}

// The panel is authored in its own flat XY plane. This rotates that plane
// into the inner-wrist "watch check" orientation relative to a left Index
// controller, then offsets it above the wrist rather than the back hand.
vr::HmdMatrix34_t InnerLeftWristTransform() {
    constexpr float kDiagonal = 0.70710678F;
    vr::HmdMatrix34_t transform{};
    transform.m[1][0] = -kDiagonal;
    transform.m[2][0] = kDiagonal;
    transform.m[1][1] = -kDiagonal;
    transform.m[2][1] = -kDiagonal;
    transform.m[0][2] = 1.0F;
    transform.m[0][3] = 0.005F;
    transform.m[1][3] = 0.071F;
    transform.m[2][3] = 0.189F;
    return transform;
}

vr::HmdMatrix34_t InnerRightWristTransform() {
    constexpr float kDiagonal = 0.70710678F;
    vr::HmdMatrix34_t transform{};
    transform.m[1][0] = kDiagonal;
    transform.m[2][0] = -kDiagonal;
    transform.m[1][1] = -kDiagonal;
    transform.m[2][1] = -kDiagonal;
    transform.m[0][2] = -1.0F;
    transform.m[0][3] = -0.005F;
    transform.m[1][3] = 0.071F;
    transform.m[2][3] = 0.189F;
    return transform;
}

vr::HmdMatrix34_t MultiplyTransforms(const vr::HmdMatrix34_t& left, const vr::HmdMatrix34_t& right) {
    vr::HmdMatrix34_t result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row][column] = left.m[row][0] * right.m[0][column]
                + left.m[row][1] * right.m[1][column] + left.m[row][2] * right.m[2][column];
        }
        result.m[row][3] = left.m[row][0] * right.m[0][3]
            + left.m[row][1] * right.m[1][3] + left.m[row][2] * right.m[2][3] + left.m[row][3];
    }
    return result;
}

vr::HmdMatrix34_t ConfiguredWristTransform(const TtsSettingsState& settings) {
    constexpr float kDegreesToRadians = 0.01745329252F;
    const float pitch = settings.wristPitch * kDegreesToRadians;
    const float yaw = settings.wristYaw * kDegreesToRadians;
    const float roll = settings.wristRoll * kDegreesToRadians;
    const float cx = std::cos(pitch);
    const float sx = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cz = std::cos(roll);
    const float sz = std::sin(roll);

    vr::HmdMatrix34_t rotateX{};
    rotateX.m[0][0] = 1.0F;
    rotateX.m[1][1] = cx;
    rotateX.m[1][2] = -sx;
    rotateX.m[2][1] = sx;
    rotateX.m[2][2] = cx;
    vr::HmdMatrix34_t rotateY{};
    rotateY.m[0][0] = cy;
    rotateY.m[0][2] = sy;
    rotateY.m[1][1] = 1.0F;
    rotateY.m[2][0] = -sy;
    rotateY.m[2][2] = cy;
    vr::HmdMatrix34_t rotateZ{};
    rotateZ.m[0][0] = cz;
    rotateZ.m[0][1] = -sz;
    rotateZ.m[1][0] = sz;
    rotateZ.m[1][1] = cz;
    rotateZ.m[2][2] = 1.0F;
    auto adjustment = MultiplyTransforms(rotateZ, MultiplyTransforms(rotateY, rotateX));
    adjustment.m[0][3] = settings.wristOffsetX;
    adjustment.m[1][3] = settings.wristOffsetY;
    adjustment.m[2][3] = settings.wristOffsetZ;
    return MultiplyTransforms(
        settings.wristRight ? InnerRightWristTransform() : InnerLeftWristTransform(), adjustment);
}

vr::ETrackedControllerRole WristControllerRole(const TtsSettingsState& settings) {
    return settings.wristRight
        ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand;
}

bool WristPlacementChanged(const TtsSettingsState& left, const TtsSettingsState& right) {
    return left.wristRight != right.wristRight
        || left.wristOffsetX != right.wristOffsetX
        || left.wristOffsetY != right.wristOffsetY
        || left.wristOffsetZ != right.wristOffsetZ
        || left.wristPitch != right.wristPitch
        || left.wristYaw != right.wristYaw
        || left.wristRoll != right.wristRoll;
}

struct WristPresentation {
    float gaze{};
    float facing{};
    float distance{};
};

std::optional<WristPresentation> ReadWristPresentation(
        vr::IVRSystem* system, const vr::HmdMatrix34_t& wristTransform,
        vr::ETrackedControllerRole wristRole) {
    const auto wristController = system->GetTrackedDeviceIndexForControllerRole(wristRole);
    if (wristController == vr::k_unTrackedDeviceIndexInvalid) return std::nullopt;
    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding, 0.0F, poses.data(), static_cast<uint32_t>(poses.size()));
    const auto& hmd = poses[vr::k_unTrackedDeviceIndex_Hmd];
    const auto& hand = poses[wristController];
    if (!hmd.bPoseIsValid || !hand.bPoseIsValid
        || hmd.eTrackingResult != vr::TrackingResult_Running_OK
        || hand.eTrackingResult != vr::TrackingResult_Running_OK) {
        return std::nullopt;
    }
    const auto panel = MultiplyTransforms(hand.mDeviceToAbsoluteTracking, wristTransform);
    const Vector3 hmdPosition{hmd.mDeviceToAbsoluteTracking.m[0][3],
        hmd.mDeviceToAbsoluteTracking.m[1][3], hmd.mDeviceToAbsoluteTracking.m[2][3]};
    const Vector3 panelPosition{panel.m[0][3], panel.m[1][3], panel.m[2][3]};
    Vector3 hmdToPanel{panelPosition.x - hmdPosition.x,
        panelPosition.y - hmdPosition.y, panelPosition.z - hmdPosition.z};
    const float distance = std::sqrt(hmdToPanel.x * hmdToPanel.x
        + hmdToPanel.y * hmdToPanel.y + hmdToPanel.z * hmdToPanel.z);
    if (distance < 0.001F) return std::nullopt;
    hmdToPanel.x /= distance;
    hmdToPanel.y /= distance;
    hmdToPanel.z /= distance;
    const Vector3 hmdForward{-hmd.mDeviceToAbsoluteTracking.m[0][2],
        -hmd.mDeviceToAbsoluteTracking.m[1][2], -hmd.mDeviceToAbsoluteTracking.m[2][2]};
    const Vector3 panelNormal{panel.m[0][2], panel.m[1][2], panel.m[2][2]};
    const float gaze = hmdForward.x * hmdToPanel.x + hmdForward.y * hmdToPanel.y
        + hmdForward.z * hmdToPanel.z;
    const float facing = std::abs(panelNormal.x * hmdToPanel.x
        + panelNormal.y * hmdToPanel.y + panelNormal.z * hmdToPanel.z);
    return WristPresentation{gaze, facing, distance};
}

struct PointerRay {
    Vector3 source;
    Vector3 direction;
};

float ApplyDeadzone(float value, float deadzone = 0.18F) {
    const auto magnitude = std::abs(value);
    if (magnitude <= deadzone) return 0.0F;
    const auto scaled = (magnitude - deadzone) / (1.0F - deadzone);
    return std::copysign((std::min)(scaled, 1.0F), value);
}

std::optional<PointerRay> ReadPointerRay(vr::IVRSystem* system, DragHand hand) {
    const auto role = hand == DragHand::Left
        ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
    const auto device = system->GetTrackedDeviceIndexForControllerRole(role);
    const auto devicePose = ReadControllerPose(system, hand);
    if (device == vr::k_unTrackedDeviceIndexInvalid || !devicePose) {
        return std::nullopt;
    }
    char modelName[256]{};
    system->GetStringTrackedDeviceProperty(
        device, vr::Prop_RenderModelName_String, modelName, sizeof(modelName));
    vr::VRInputValueHandle_t devicePath = vr::k_ulInvalidInputValueHandle;
    vr::VRInput()->GetInputSourceHandle(
        hand == DragHand::Left ? "/user/hand/left" : "/user/hand/right", &devicePath);
    vr::RenderModel_ControllerMode_State_t controllerMode{};
    vr::RenderModel_ComponentState_t tipState{};
    if (modelName[0] != '\0' && devicePath != vr::k_ulInvalidInputValueHandle
        && vr::VRRenderModels()->GetComponentStateForDevicePath(
            modelName, "tip", devicePath, &controllerMode, &tipState)) {
        const auto tipPose = MultiplyTransforms(*devicePose, tipState.mTrackingToComponentLocal);
        return PointerRay{
            {tipPose.m[0][3], tipPose.m[1][3], tipPose.m[2][3]},
            {-tipPose.m[0][2], -tipPose.m[1][2], -tipPose.m[2][2]},
        };
    }
    constexpr float kFallbackOffsetMeters = 0.060F;
    return PointerRay{
        {devicePose->m[0][3] + devicePose->m[0][1] * kFallbackOffsetMeters,
         devicePose->m[1][3] + devicePose->m[1][1] * kFallbackOffsetMeters,
         devicePose->m[2][3] + devicePose->m[2][1] * kFallbackOffsetMeters},
        {devicePose->m[0][1], devicePose->m[1][1], devicePose->m[2][1]},
    };
}

vr::HmdMatrix34_t HeadsetCalibrationTransform() {
    vr::HmdMatrix34_t transform{};
    transform.m[0][0] = 1.0F;
    transform.m[1][1] = 1.0F;
    transform.m[2][2] = 1.0F;
    transform.m[2][3] = -0.65F;
    return transform;
}

void DestroyStaleOverlay(const char* key) {
    vr::VROverlayHandle_t staleOverlay = vr::k_ulOverlayHandleInvalid;
    if (vr::VROverlay()->FindOverlay(key, &staleOverlay) == vr::VROverlayError_None) {
        vr::VROverlay()->DestroyOverlay(staleOverlay);
    }
}
}

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool versionRequested = argc > 1 && std::string_view(argv[1]) == "--version";
    if (versionRequested) {
        std::cout << "Interfayce " << INTERFAYCE_VERSION << '\n';
        CoUninitialize();
        return 0;
    }
    const bool requestShutdown = argc > 1 && std::string_view(argv[1]) == "--shutdown";
    if (requestShutdown) {
        const HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, kShutdownEventName);
        if (event == nullptr) {
            std::cerr << "No running Interfayce host accepted the shutdown request.\n";
            CoUninitialize();
            return 1;
        }
        SetEvent(event);
        CloseHandle(event);
        CoUninitialize();
        return 0;
    }
    const bool desktopSourcesProbe = argc > 1 && std::string_view(argv[1]) == "--desktop-sources";
    if (desktopSourcesProbe) {
        interfayce::DesktopSurfaceManager manager;
        for (const auto& source : manager.EnumerateSources()) {
            std::wcout << (source.kind == interfayce::DesktopSource::Kind::Display
                    ? L"DISPLAY\t" : L"APPLICATION\t")
                       << source.label << L'\t' << source.detail << L'\n';
        }
        CoUninitialize();
        return 0;
    }
    const bool serviceStatus = argc > 1 && std::string_view(argv[1]) == "--service-status";
    if (serviceStatus) {
        const auto root = ProjectRoot(ExecutableDirectory(argv[0]));
        const bool slimeAvailable = SlimeAdapterAvailable(root)
            && IsLocalTcpPortOpen(21110, std::chrono::milliseconds(150));
        const bool spotifyAvailable = IsProcessRunning(L"Spotify.exe");
        std::cout << "SLIMEVR\t" << (slimeAvailable ? "available" : "offline")
                  << "\t127.0.0.1:21110\n"
                  << "SPOTIFY\t" << (spotifyAvailable ? "running" : "offline")
                  << "\tSpotify.exe\n";
        CoUninitialize();
        return 0;
    }
    const bool slimeSummary = argc > 1 && std::string_view(argv[1]) == "--slime-summary";
    if (slimeSummary) {
        const auto root = ProjectRoot(ExecutableDirectory(argv[0]));
        const auto status = ReadSlimeTrackerBatteries(root);
        for (size_t index = 0; index < status.slots.size(); ++index) {
            if (index > 0) std::wcout << L'\t';
            std::wcout << (status.slots[index].empty() ? L"--" : status.slots[index]);
        }
        std::wcout << L'\t' << (status.mountReady ? L"MOUNT_OK" : L"MOUNT_WAIT") << L'\n';
        CoUninitialize();
        return 0;
    }
    const bool broadcastControllerProbe = argc > 1
        && std::string_view(argv[1]) == "--broadcast-controller-probe";
    if (broadcastControllerProbe) {
        interfayce::BroadcastController controller(
            ExecutableDirectory(argv[0]) / "InterfayceAudioEngine.exe");
        std::wstring error;
        if (!controller.Start(error)) {
            std::wcerr << L"Broadcast controller probe failed to start: " << error << L'\n';
            CoUninitialize();
            return 1;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline
               && controller.State() == interfayce::BroadcastState::Starting) {
            controller.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        const bool becameActive = controller.State() == interfayce::BroadcastState::Active;
        std::wcout << L"broadcast_start\t" << controller.StatusText() << L'\n';
        controller.Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const bool engineLingering = IsProcessRunning(L"InterfayceAudioEngine.exe");
        std::wcout << L"broadcast_stop\t" << controller.StatusText()
                   << L"\tengine_lingering=" << engineLingering << L'\n';
        CoUninitialize();
        return becameActive && !engineLingering ? 0 : 1;
    }
    const bool desktopCaptureProbe = argc > 1 && std::string_view(argv[1]) == "--desktop-capture-probe";
    if (desktopCaptureProbe) {
        Microsoft::WRL::ComPtr<ID3D11Device> captureDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> captureContext;
        D3D_FEATURE_LEVEL featureLevel{};
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &captureDevice, &featureLevel, &captureContext))) {
            std::cerr << "Desktop capture probe could not create a D3D11 device.\n";
            CoUninitialize();
            return 1;
        }
        interfayce::DesktopSurfaceManager manager;
        const auto displays = manager.EnumerateDisplays();
        interfayce::DesktopCapture capture;
        if (displays.empty() || !capture.Start(captureDevice.Get(), displays.front())) {
            std::cerr << "Desktop capture probe could not start Windows Graphics Capture.\n";
            CoUninitialize();
            return 1;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool receivedFrame = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto result = capture.Update();
            if (result == interfayce::DesktopCapture::UpdateResult::FrameCopied) {
                receivedFrame = true;
                break;
            }
            if (result == interfayce::DesktopCapture::UpdateResult::Closed
                || result == interfayce::DesktopCapture::UpdateResult::Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        capture.Stop();
        std::cout << "Desktop capture probe " << (receivedFrame ? "received a GPU frame" : "timed out") << ".\n";
        CoUninitialize();
        return receivedFrame ? 0 : 1;
    }
    const bool windowGeometryProbe = argc > 2
        && std::string_view(argv[1]) == "--window-geometry-probe";
    if (windowGeometryProbe) {
        Microsoft::WRL::ComPtr<ID3D11Device> captureDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> captureContext;
        D3D_FEATURE_LEVEL featureLevel{};
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &captureDevice, &featureLevel, &captureContext))) {
            CoUninitialize();
            return 1;
        }
        const auto query = Utf8ToWide(argv[2]);
        const auto windows = interfayce::DesktopSurfaceManager{}.EnumerateWindows();
        const auto source = std::find_if(windows.begin(), windows.end(), [&](const auto& item) {
            return item.label.find(query) != std::wstring::npos;
        });
        if (source == windows.end()) {
            std::wcerr << L"No captured window matched: " << query << L'\n';
            CoUninitialize();
            return 1;
        }
        interfayce::DesktopCapture capture;
        if (!capture.Start(captureDevice.Get(), *source)) {
            CoUninitialize();
            return 1;
        }
        RECT windowBounds{};
        RECT frameBounds{};
        RECT clientBounds{};
        GetWindowRect(source->window, &windowBounds);
        DwmGetWindowAttribute(source->window, DWMWA_EXTENDED_FRAME_BOUNDS,
            &frameBounds, sizeof(frameBounds));
        GetClientRect(source->window, &clientBounds);
        POINT clientOrigin{clientBounds.left, clientBounds.top};
        ClientToScreen(source->window, &clientOrigin);
        std::wcout << source->label << L"\nWGC=" << capture.Width() << L'x' << capture.Height()
            << L"\nWINDOW=" << windowBounds.left << L',' << windowBounds.top << L' '
            << windowBounds.right - windowBounds.left << L'x' << windowBounds.bottom - windowBounds.top
            << L"\nDWM=" << frameBounds.left << L',' << frameBounds.top << L' '
            << frameBounds.right - frameBounds.left << L'x' << frameBounds.bottom - frameBounds.top
            << L"\nCLIENT=" << clientOrigin.x << L',' << clientOrigin.y << L' '
            << clientBounds.right - clientBounds.left << L'x' << clientBounds.bottom - clientBounds.top
            << L"\nDPI=" << GetDpiForWindow(source->window) << L'\n';
        capture.Stop();
        CoUninitialize();
        return 0;
    }
    const bool probeOnly = argc > 1 && std::string_view(argv[1]) == "--probe";
    const bool overlayProbe = argc > 1 && std::string_view(argv[1]) == "--overlay-probe";
    const bool inputCapture = argc > 1 && std::string_view(argv[1]) == "--input-capture";
    const bool dragPreview = argc > 1 && std::string_view(argv[1]) == "--drag-preview";
    const bool temporaryDrag = argc > 1 && std::string_view(argv[1]) == "--temporary-drag";
    const bool sessionDrag = argc == 1
        || (argc > 1 && std::string_view(argv[1]) == "--session-drag");
    const bool showBindings = argc > 1 && std::string_view(argv[1]) == "--show-bindings";
    const bool rawPanel = argc > 1 && std::string_view(argv[1]) == "--raw-panel";
    const bool headsetPanel = rawPanel
        || (argc > 1 && std::string_view(argv[1]) == "--headset-panel");
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&initError, vr::VRApplication_Overlay);
    if (initError != vr::VRInitError_None || system == nullptr) {
        std::cerr << "SteamVR overlay initialization failed: "
                  << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << '\n';
        return 1;
    }

    const auto directory = ExecutableDirectory(argv[0]);
    const auto projectRoot = ProjectRoot(directory);
    const auto musicArtPath = UserCacheFile(L"spotify-art.jpg");
    const auto actionManifest = directory / "assets" / "steamvr" / "actions.json";
    bool voiceServiceAvailable = VoiceServiceAvailable();
    if (!voiceServiceAvailable) LaunchVoiceService(directory, projectRoot);
    bool slimeAvailable = SlimeAdapterAvailable(projectRoot)
        && IsLocalTcpPortOpen(21110, std::chrono::milliseconds(150));
    bool spotifyAvailable = IsProcessRunning(L"Spotify.exe");
    const std::wstring initialMusicLine = spotifyAvailable
        ? L"Loading Spotify..." : L"Spotify is not running";
    std::cout << "startup capabilities: SlimeVR=" << (slimeAvailable ? "available" : "offline")
              << " Spotify=" << (spotifyAvailable ? "running" : "offline") << '\n';
    vr::VRApplications()->IdentifyApplication(GetCurrentProcessId(), kAppKey);
    const auto actionError = vr::VRInput()->SetActionManifestPath(actionManifest.string().c_str());
    if (actionError != vr::VRInputError_None) {
        std::cerr << "Could not load Interfayce actions: " << static_cast<int>(actionError) << '\n';
    }

    vr::VRActionSetHandle_t actionSet = vr::k_ulInvalidActionSetHandle;
    const auto actionSetError = vr::VRInput()->GetActionSetHandle(kActionSetPath, &actionSet);
    if (actionSetError != vr::VRInputError_None) {
        std::cerr << "Could not acquire Interfayce action set: "
                  << static_cast<int>(actionSetError) << '\n';
    }

    vr::VRActionHandle_t leftDragAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t rightDragAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t leftUiClickAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t rightUiClickAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t leftSurfaceGrabAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t rightSurfaceGrabAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t rightSurfaceScrollAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t leftHapticAction = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t rightHapticAction = vr::k_ulInvalidActionHandle;
    const auto leftHandleError = vr::VRInput()->GetActionHandle(kLeftDragActionPath, &leftDragAction);
    const auto rightHandleError = vr::VRInput()->GetActionHandle(kRightDragActionPath, &rightDragAction);
    const auto leftUiClickError = vr::VRInput()->GetActionHandle(
        kLeftUiClickActionPath, &leftUiClickAction);
    const auto rightUiClickError = vr::VRInput()->GetActionHandle(
        kRightUiClickActionPath, &rightUiClickAction);
    const auto leftSurfaceGrabError = vr::VRInput()->GetActionHandle(
        kLeftSurfaceGrabActionPath, &leftSurfaceGrabAction);
    const auto rightSurfaceGrabError = vr::VRInput()->GetActionHandle(
        kRightSurfaceGrabActionPath, &rightSurfaceGrabAction);
    const auto rightSurfaceScrollError = vr::VRInput()->GetActionHandle(
        kRightSurfaceScrollActionPath, &rightSurfaceScrollAction);
    const auto leftHapticError = vr::VRInput()->GetActionHandle(
        kLeftHapticActionPath, &leftHapticAction);
    const auto rightHapticError = vr::VRInput()->GetActionHandle(
        kRightHapticActionPath, &rightHapticAction);

    interfayce::OverlayRenderer renderer;
    renderer.SetRigBodyArtPath((directory / "assets" / "ui" / "rig-body-scanner.png").wstring());
    renderer.SetPlayspaceResetArtPath(
        (directory / "assets" / "ui" / "playspace-reset.png").wstring());
    renderer.SetHoloGlyphAtlasPath(
        (directory / "assets" / "ui" / "holo-glyph-atlas.png").wstring());
    std::wstring clockText = LocalClockText();
    renderer.SetClockText(clockText);
    interfayce::BroadcastController broadcast(
        directory / "InterfayceAudioEngine.exe");
    renderer.SetSlimeAvailable(slimeAvailable);
    renderer.SetMusicVoiceStatus(
        voiceServiceAvailable ? L"VOICE READY" : L"VOICE WARMING", false);
    renderer.SetMusicBroadcastState(false, broadcast.StatusText());
    renderer.SetCommsStatus(voiceServiceAvailable ? L"IDLE" : L"VOICE WARMING", L"", false);
    renderer.SetAssistantStatus(
        voiceServiceAvailable ? L"READY" : L"VOICE WARMING", L"", L"", false);
    TtsSettingsState ttsSettings;
    if (voiceServiceAvailable) {
        if (const auto loaded = ReadTtsSettings()) ttsSettings = *loaded;
    }
    renderer.SetTtsSettings(ttsSettings.volumePercent, ttsSettings.muted);
    renderer.SetBroadcastGainDb(static_cast<int>(std::lround(ttsSettings.broadcastGainDb)));
    broadcast.SetGainDb(ttsSettings.broadcastGainDb);
    if (!rawPanel && !renderer.Initialize(system, 0, initialMusicLine,
            spotifyAvailable ? musicArtPath.wstring() : L"")) {
        std::cerr << "Could not initialize the Interfayce D3D11 panel texture.\n";
        vr::VR_Shutdown();
        return 1;
    }

    if (probeOnly) {
        std::cout << "Interfayce native overlay probe completed.\n";
        vr::VR_Shutdown();
        return actionError == vr::VRInputError_None && actionSetError == vr::VRInputError_None
                && leftHandleError == vr::VRInputError_None && rightHandleError == vr::VRInputError_None
                && leftUiClickError == vr::VRInputError_None
                && rightUiClickError == vr::VRInputError_None
                && leftSurfaceGrabError == vr::VRInputError_None
                && rightSurfaceGrabError == vr::VRInputError_None
                && rightSurfaceScrollError == vr::VRInputError_None
                && leftHapticError == vr::VRInputError_None
                && rightHapticError == vr::VRInputError_None
            ? 0
            : 1;
    }

    // A forced process stop can leave an overlay registered until SteamVR's
    // cleanup cycle. Reclaim only Interfayce's own keys before recreating it.
    DestroyStaleOverlay(kWristOverlayKey);
    DestroyStaleOverlay(kCursorOverlayKey);
    DestroyStaleOverlay(kLeftCursorOverlayKey);

    vr::VROverlayHandle_t wristOverlay = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t cursorOverlay = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t leftCursorOverlay = vr::k_ulOverlayHandleInvalid;
    const auto panelTexture = renderer.Texture();

    const auto wristError = vr::VROverlay()->CreateOverlay(
        kWristOverlayKey, "Interfayce Wrist", &wristOverlay);
    if (wristError != vr::VROverlayError_None) {
        std::cerr << "Could not create Interfayce wrist overlay: "
                  << static_cast<int>(wristError) << '\n';
        vr::VR_Shutdown();
        return 1;
    }
    vr::VROverlay()->SetOverlayWidthInMeters(
        wristOverlay, rawPanel ? 0.80F : headsetPanel ? 0.55F : 0.205F);
    // SteamVR's built-in overlay mouse only exists in the dashboard. Interfayce
    // uses its own action-driven controller ray so the panel works in-world.
    vr::VROverlay()->SetOverlayInputMethod(wristOverlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(wristOverlay, 10);
    vr::VROverlay()->SetOverlayAlpha(wristOverlay, rawPanel || headsetPanel ? 1.0F : 0.0F);
    if (vr::VROverlay()->CreateOverlay(kCursorOverlayKey, "Interfayce wrist cursor", &cursorOverlay)
        != vr::VROverlayError_None) {
        std::cerr << "Could not create Interfayce wrist cursor overlay.\n";
        vr::VROverlay()->DestroyOverlay(wristOverlay);
        vr::VR_Shutdown();
        return 1;
    }
    vr::VROverlay()->SetOverlayWidthInMeters(cursorOverlay, 0.0035F);
    vr::VROverlay()->SetOverlayInputMethod(cursorOverlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(cursorOverlay, 11);
    std::vector<uint8_t> cursorPixels(32U * 32U * 4U);
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const auto pixel = (y * 32U + x) * 4U;
            const auto dx = static_cast<int>(x) - 16;
            const auto dy = static_cast<int>(y) - 16;
            const bool inside = dx * dx + dy * dy <= 180;
            cursorPixels[pixel + 0] = 40;
            cursorPixels[pixel + 1] = 230;
            cursorPixels[pixel + 2] = 255;
            cursorPixels[pixel + 3] = inside ? 255 : 0;
        }
    }
    vr::VROverlay()->SetOverlayRaw(cursorOverlay, cursorPixels.data(), 32, 32, 4);
    if (vr::VROverlay()->CreateOverlay(kLeftCursorOverlayKey,
            "Interfayce left keyboard cursor", &leftCursorOverlay) != vr::VROverlayError_None) {
        std::cerr << "Could not create Interfayce left keyboard cursor overlay.\n";
        vr::VROverlay()->DestroyOverlay(cursorOverlay);
        vr::VROverlay()->DestroyOverlay(wristOverlay);
        vr::VR_Shutdown();
        return 1;
    }
    vr::VROverlay()->SetOverlayWidthInMeters(leftCursorOverlay, 0.0055F);
    vr::VROverlay()->SetOverlayInputMethod(leftCursorOverlay, vr::VROverlayInputMethod_None);
    vr::VROverlay()->SetOverlaySortOrder(leftCursorOverlay, 30);
    std::vector<uint8_t> leftCursorPixels = cursorPixels;
    for (size_t pixel = 0; pixel < leftCursorPixels.size(); pixel += 4) {
        leftCursorPixels[pixel + 0] = 188;
        leftCursorPixels[pixel + 1] = 82;
        leftCursorPixels[pixel + 2] = 255;
    }
    vr::VROverlay()->SetOverlayRaw(
        leftCursorOverlay, leftCursorPixels.data(), 32, 32, 4);
    std::vector<uint8_t> rawVisibilityCard;
    if (rawPanel) {
        constexpr uint32_t kCardSize = 512;
        rawVisibilityCard.resize(kCardSize * kCardSize * 4U);
        for (uint32_t y = 0; y < kCardSize; ++y) {
            for (uint32_t x = 0; x < kCardSize; ++x) {
                const auto pixel = (y * kCardSize + x) * 4U;
                const bool cross = std::abs(static_cast<int>(x) - static_cast<int>(kCardSize / 2)) < 18
                    || std::abs(static_cast<int>(y) - static_cast<int>(kCardSize / 2)) < 18;
                rawVisibilityCard[pixel + 0] = cross ? 255 : 235;
                rawVisibilityCard[pixel + 1] = cross ? 255 : 0;
                rawVisibilityCard[pixel + 2] = cross ? 255 : 0;
                rawVisibilityCard[pixel + 3] = 255;
            }
        }
        const auto rawError = vr::VROverlay()->SetOverlayRaw(
            wristOverlay, rawVisibilityCard.data(), kCardSize, kCardSize, 4);
        if (rawError != vr::VROverlayError_None) {
            std::cerr << "Raw visibility card upload failed: "
                      << vr::VROverlay()->GetOverlayErrorNameFromEnum(rawError) << '\n';
        }
    } else {
        const auto wristTextureError = vr::VROverlay()->SetOverlayTexture(wristOverlay, &panelTexture);
        if (wristTextureError != vr::VROverlayError_None) {
            std::cerr << "Wrist texture upload failed: "
                      << vr::VROverlay()->GetOverlayErrorNameFromEnum(wristTextureError) << '\n';
        }
    }
    auto wristTransform = headsetPanel
        ? HeadsetCalibrationTransform() : ConfiguredWristTransform(ttsSettings);
    bool wristAttached = false;
    const auto attachWristOverlay = [&]() {
        const auto device = headsetPanel ? vr::k_unTrackedDeviceIndex_Hmd
            : system->GetTrackedDeviceIndexForControllerRole(WristControllerRole(ttsSettings));
        if (device == vr::k_unTrackedDeviceIndexInvalid) {
            return false;
        }
        const auto attachError = vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
            wristOverlay, device, &wristTransform);
        if (attachError != vr::VROverlayError_None) {
            std::cerr << "Could not attach Interfayce wrist panel to the configured controller: "
                      << static_cast<int>(attachError) << '\n';
            return false;
        }
        const auto showError = vr::VROverlay()->ShowOverlay(wristOverlay);
        if (showError != vr::VROverlayError_None) {
            std::cerr << "Wrist show failed: "
                      << vr::VROverlay()->GetOverlayErrorNameFromEnum(showError) << '\n';
            return false;
        }
        std::cout << "Interfayce wrist panel attached to "
                  << (headsetPanel ? "headset calibration view"
                      : ttsSettings.wristRight ? "right controller" : "left controller") << ".\n";
        return true;
    };
    wristAttached = attachWristOverlay();

    if (showBindings) {
        vr::VRActiveActionSet_t bindingSet{};
        bindingSet.ulActionSet = actionSet;
        const auto bindingError = vr::VRInput()->ShowBindingsForActionSet(
            &bindingSet, sizeof(bindingSet), 1, vr::k_ulInvalidInputValueHandle);
        if (bindingError != vr::VRInputError_None) {
            std::cerr << "Could not show Interfayce bindings: " << static_cast<int>(bindingError) << '\n';
        }
    }

    if (overlayProbe) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        vr::VROverlay()->DestroyOverlay(wristOverlay);
        vr::VROverlay()->DestroyOverlay(cursorOverlay);
        vr::VROverlay()->DestroyOverlay(leftCursorOverlay);
        std::cout << "Interfayce tracked-overlay probe completed.\n";
        vr::VR_Shutdown();
        return 0;
    }

    interfayce::DesktopSurfaceManager desktopSourceManager;
    interfayce::DesktopSurfaceRegistry desktopSurfaces;
    if (!rawPanel && !desktopSurfaces.Initialize(system, renderer.Device())) {
        std::cerr << "Could not initialize desktop surface registry.\n";
    }
    desktopSurfaces.SetDeckVisible(false);

    std::cout << "Interfayce overlay host is running.\n";
    const HANDLE shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, kShutdownEventName);
    interfayce::TrayIcon tray;
    if (argc == 1 && !tray.Initialize(GetModuleHandleW(nullptr))) {
        std::cerr << "Could not create the Interfayce notification icon.\n";
    }
    bool running = true;
    bool restartRequested = false;
    const bool boundedCapture = inputCapture || dragPreview || temporaryDrag
        || (argc > 1 && std::string_view(argv[1]) == "--session-drag");
    const auto captureDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds((temporaryDrag || sessionDrag) ? 120 : 30);
    bool printedInputDiagnostics = false;
    DragHand activeDragHand = DragHand::None;
    std::optional<Vector3> lastDragPosition;
    Vector3 previewOffset{};
    std::optional<vr::HmdMatrix34_t> temporaryBaseline;
    std::optional<vr::HmdMatrix34_t> sessionBaseline;
    bool playspaceAdjusted = false;
    bool dragFaulted = false;
    int selectedDeck = 0;
    std::wstring musicLine = initialMusicLine;
    bool musicPlaying = false;
    std::wstring desktopLine = DesktopSurfaceLine(0);
    interfayce::DesktopPanelState desktopPanel;
    std::array<DesktopFavorite, 3> desktopFavorites;
    std::wstring rigLine;
    std::array<std::wstring, 8> rigSlots;
    bool mountReady = false;
    auto rigLegProfile = interfayce::RigLegProfile::Unknown;
    std::future<interfayce::RigLegProfile> rigLegProfileTask;
    if (slimeAvailable) {
        renderer.SetRigLegProfile(
            interfayce::RigLegProfile::Unknown, interfayce::RigLegProfile::Config);
        rigLegProfileTask = std::async(std::launch::async,
            [projectRoot] { return RunSlimeLegProfile(projectRoot, L"config"); });
    }
    auto nextMusicPoll = std::chrono::steady_clock::now();
    std::future<MusicPlaybackState> musicPoll;
    std::future<std::wstring> musicVoiceCommand;
    auto nextVoiceHealthPoll = std::chrono::steady_clock::now();
    CommsState commsState;
    std::array<std::wstring, 4> commsShortcuts;
    AssistantPanelState assistantState;
    std::future<std::optional<AssistantPanelState>> assistantCommand;
    auto nextAssistantPoll = std::chrono::steady_clock::now();
    auto nextCommsPoll = std::chrono::steady_clock::now();
    auto nextCommsShortcutPoll = std::chrono::steady_clock::now();
    auto nextRuntimeSettingsPoll = std::chrono::steady_clock::now();
    auto nextDesktopFavoritePoll = std::chrono::steady_clock::now();
    std::vector<PendingFavoriteCapture> pendingFavoriteCaptures;
    auto nextRigPoll = std::chrono::steady_clock::now();
    std::future<SlimeRigStatus> slimeBatteryPoll;
    std::future<std::optional<std::string>> batteryStatusPost;
    auto nextControllerBatteryPoll = std::chrono::steady_clock::now();
    std::array<int, 2> controllerBatteries{-1, -1};
    auto nextClockPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    bool restoreHoldActive = false;
    int restoreHoldSegment = 0;
    bool rigResetHoldActive = false;
    bool rigResetHoldCompleted = false;
    bool shutdownHoldActive = false;
    const wchar_t* rigResetKind = nullptr;
    auto restoreHoldStarted = std::chrono::steady_clock::now();
    auto rigResetHoldStarted = std::chrono::steady_clock::now();
    auto shutdownHoldStarted = std::chrono::steady_clock::now();
    auto pressFeedbackUntil = std::chrono::steady_clock::time_point{};
    bool pressFeedbackActive = false;
    int rigResetHoldSegment = 0;
    int shutdownHoldSegment = 0;
    std::optional<interfayce::DesktopSurfaceHit> activeDesktopPointer;
    std::optional<uint64_t> activeScrollSurface;
    double verticalScrollRemainder = 0.0;
    double horizontalScrollRemainder = 0.0;
    auto lastScrollUpdate = std::chrono::steady_clock::now();
    float wristAlpha = rawPanel || headsetPanel ? 1.0F : 0.0F;
    bool wristPresented = rawPanel || headsetPanel;
    auto lastWristFadeUpdate = std::chrono::steady_clock::now();
    auto* chaperone = vr::VRChaperoneSetup();
    const auto restoreBaseline = [&](std::optional<vr::HmdMatrix34_t>& baseline, const char* reason) {
        if (baseline) {
            chaperone->SetWorkingStandingZeroPoseToRawTrackingPose(&*baseline);
            chaperone->HideWorkingSetPreview();
            std::cout << reason << " restored the session baseline\n";
            baseline.reset();
        }
    };
    auto lastPreviewLog = std::chrono::steady_clock::now();
    while (running && (!boundedCapture || std::chrono::steady_clock::now() < captureDeadline)) {
        if (const auto trayAction = tray.Poll()) {
            if (*trayAction == interfayce::TrayAction::OpenSettings) {
                if (!LaunchDesktopSettings(directory, projectRoot)) {
                    std::cerr << "Could not launch the desktop settings window.\n";
                }
            } else {
                restartRequested = *trayAction == interfayce::TrayAction::Restart;
                running = false;
            }
        }
        if (shutdownEvent != nullptr && WaitForSingleObject(shutdownEvent, 0) == WAIT_OBJECT_0) {
            running = false;
            continue;
        }
        if (!wristAttached) {
            wristAttached = attachWristOverlay();
        }
        if (std::chrono::steady_clock::now() >= nextClockPoll) {
            nextClockPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            const auto updatedClock = LocalClockText();
            if (updatedClock != clockText) {
                clockText = updatedClock;
                renderer.SetClockText(clockText);
                if (!rawPanel && renderer.Initialize(system, selectedDeck, musicLine,
                        musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        }
        if (pressFeedbackActive && std::chrono::steady_clock::now() >= pressFeedbackUntil) {
            pressFeedbackActive = false;
            renderer.SetPressFeedback(0.0F, 0.0F, false);
            if (!rawPanel && renderer.Initialize(system, selectedDeck, musicLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        const auto wristFadeNow = std::chrono::steady_clock::now();
        const float wristFadeSeconds = static_cast<float>((std::min)(
            std::chrono::duration<double>(wristFadeNow - lastWristFadeUpdate).count(), 0.05));
        lastWristFadeUpdate = wristFadeNow;
        if (!rawPanel && !headsetPanel) {
            if (const auto presentation = ReadWristPresentation(
                    system, wristTransform, WristControllerRole(ttsSettings))) {
                if (wristPresented) {
                    wristPresented = presentation->gaze >= 0.68F
                        && presentation->facing >= 0.28F && presentation->distance <= 1.20F;
                } else {
                    wristPresented = presentation->gaze >= 0.80F
                        && presentation->facing >= 0.48F && presentation->distance <= 1.00F;
                }
            } else {
                wristPresented = false;
            }
            const float targetAlpha = wristPresented ? 1.0F : 0.0F;
            const float fadeDuration = wristPresented ? 0.16F : 0.30F;
            const float fadeStep = wristFadeSeconds / fadeDuration;
            wristAlpha = targetAlpha > wristAlpha
                ? (std::min)(targetAlpha, wristAlpha + fadeStep)
                : (std::max)(targetAlpha, wristAlpha - fadeStep);
            vr::VROverlay()->SetOverlayAlpha(wristOverlay, wristAlpha);
        }
        vr::VRActiveActionSet_t activeSet{};
        activeSet.ulActionSet = actionSet;
        activeSet.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
        activeSet.nPriority = 0;
        const auto updateError = vr::VRInput()->UpdateActionState(&activeSet, sizeof(activeSet), 1);

        vr::InputDigitalActionData_t leftDrag{};
        vr::InputDigitalActionData_t rightDrag{};
        vr::InputDigitalActionData_t leftUiClick{};
        vr::InputDigitalActionData_t rightUiClick{};
        vr::InputDigitalActionData_t leftSurfaceGrab{};
        vr::InputDigitalActionData_t rightSurfaceGrab{};
        vr::InputAnalogActionData_t rightSurfaceScroll{};
        const auto leftDataError = vr::VRInput()->GetDigitalActionData(
            leftDragAction, &leftDrag, sizeof(leftDrag), vr::k_ulInvalidInputValueHandle);
        const auto rightDataError = vr::VRInput()->GetDigitalActionData(
            rightDragAction, &rightDrag, sizeof(rightDrag), vr::k_ulInvalidInputValueHandle);
        vr::VRInput()->GetDigitalActionData(
            leftUiClickAction, &leftUiClick, sizeof(leftUiClick), vr::k_ulInvalidInputValueHandle);
        vr::VRInput()->GetDigitalActionData(
            rightUiClickAction, &rightUiClick, sizeof(rightUiClick), vr::k_ulInvalidInputValueHandle);
        vr::VRInput()->GetDigitalActionData(leftSurfaceGrabAction, &leftSurfaceGrab,
            sizeof(leftSurfaceGrab), vr::k_ulInvalidInputValueHandle);
        vr::VRInput()->GetDigitalActionData(rightSurfaceGrabAction, &rightSurfaceGrab,
            sizeof(rightSurfaceGrab), vr::k_ulInvalidInputValueHandle);
        vr::VRInput()->GetAnalogActionData(rightSurfaceScrollAction, &rightSurfaceScroll,
            sizeof(rightSurfaceScroll), vr::k_ulInvalidInputValueHandle);
        const auto& wristUiClick = ttsSettings.wristRight ? leftUiClick : rightUiClick;
        if (!printedInputDiagnostics) {
            std::array<vr::VRInputValueHandle_t, 16> leftOrigins{};
            std::array<vr::VRInputValueHandle_t, 16> rightOrigins{};
            const auto leftOriginError = vr::VRInput()->GetActionOrigins(
                actionSet, leftDragAction, leftOrigins.data(), static_cast<uint32_t>(leftOrigins.size()));
            const auto rightOriginError = vr::VRInput()->GetActionOrigins(
                actionSet, rightDragAction, rightOrigins.data(), static_cast<uint32_t>(rightOrigins.size()));
            std::cout << "drag action status: left_active=" << leftDrag.bActive
                      << " right_active=" << rightDrag.bActive
                      << " update=" << static_cast<int>(updateError)
                      << " left_data=" << static_cast<int>(leftDataError)
                      << " right_data=" << static_cast<int>(rightDataError)
                      << " left_origins=" << static_cast<int>(leftOriginError)
                      << " right_origins=" << static_cast<int>(rightOriginError)
                      << " left_origin_0=" << leftOrigins[0]
                      << " right_origin_0=" << rightOrigins[0] << '\n';
            printedInputDiagnostics = true;
        }
        if (leftDrag.bChanged || rightDrag.bChanged) {
            std::cout << "drag actions: left=" << leftDrag.bState
                      << " right=" << rightDrag.bState << '\n';
        }

        vr::VROverlayIntersectionResults_t panelHit{};
        bool restoreButtonHit = false;
        bool rigFullResetHit = false;
        bool rigMountResetHit = false;
        bool rigConfigProfileHit = false;
        bool rigPlayProfileHit = false;
        bool desktopNewSurfaceHit = false;
        bool desktopKeyboardSpawnHit = false;
        bool desktopSurfaceListHit = false;
        std::optional<size_t> desktopFavoriteHit;
        bool desktopListBackHit = false;
        bool desktopBringAllHit = false;
        bool musicMicHit = false;
        bool musicBroadcastHit = false;
        bool musicPreviousHit = false;
        bool musicToggleHit = false;
        bool musicNextHit = false;
        bool commsMicHit = false;
        bool commsClearHit = false;
        bool assistantMicHit = false;
        bool assistantCancelHit = false;
        bool assistantClearHit = false;
        std::optional<size_t> commsShortcutHit;
        bool ttsVolumeDownHit = false;
        bool ttsMuteHit = false;
        bool ttsVolumeUpHit = false;
        bool broadcastGainDownHit = false;
        bool broadcastGainUpHit = false;
        bool desktopSettingsHit = false;
        bool shutdownButtonHit = false;
        std::optional<size_t> desktopBringIndex;
        std::optional<size_t> desktopLockIndex;
        std::optional<size_t> desktopReuseIndex;
        std::optional<size_t> desktopCloseIndex;
        std::optional<interfayce::DesktopSurfaceHit> desktopSurfaceHit;
        std::optional<interfayce::KeyboardSurfaceHit> keyboardSurfaceHit;
        std::optional<interfayce::DesktopSurfaceHit> leftDesktopSurfaceHit;
        std::optional<interfayce::KeyboardSurfaceHit> leftKeyboardSurfaceHit;
        std::optional<uint64_t> leftDesktopFrameHit;
        std::optional<uint64_t> desktopFrameHit;
        bool panelHitFound = false;
        float panelX = 0.0F;
        float panelY = 0.0F;
        const auto panelPointerRay = ReadPointerRay(
            system, ttsSettings.wristRight ? DragHand::Left : DragHand::Right);
        if (panelPointerRay) {
            vr::VROverlayIntersectionParams_t ray{};
            ray.eOrigin = vr::TrackingUniverseStanding;
            ray.vSource = {{panelPointerRay->source.x, panelPointerRay->source.y,
                panelPointerRay->source.z}};
            ray.vDirection = {{panelPointerRay->direction.x, panelPointerRay->direction.y,
                panelPointerRay->direction.z}};
            if (wristAlpha >= 0.30F
                && vr::VROverlay()->ComputeOverlayIntersection(wristOverlay, &ray, &panelHit)) {
                panelHitFound = true;
                const auto x = panelHit.vUVs.v[0] * 768.0F;
                const auto y = (1.0F - panelHit.vUVs.v[1]) * 384.0F;
                panelX = x;
                panelY = y;
                const auto circleHit = [&](float centerX, float centerY, float radius) {
                    const float dx = x - centerX;
                    const float dy = y - centerY;
                    return dx * dx + dy * dy <= radius * radius;
                };
                const auto restoreX = x - 199.0F;
                const auto restoreY = y - 250.0F;
                restoreButtonHit = selectedDeck == 2 && playspaceAdjusted
                    && restoreX * restoreX + restoreY * restoreY <= 98.0F * 98.0F;
                musicMicHit = selectedDeck == 0 && circleHit(520, 225, 35);
                musicBroadcastHit = selectedDeck == 0 && circleHit(520, 145, 35);
                musicPreviousHit = selectedDeck == 0 && spotifyAvailable && circleHit(140, 287, 39);
                musicToggleHit = selectedDeck == 0 && spotifyAvailable && circleHit(384, 287, 49);
                musicNextHit = selectedDeck == 0 && spotifyAvailable && circleHit(628, 287, 39);
                commsClearHit = selectedDeck == 5 && circleHit(560, 285, 40);
                if (selectedDeck == 5 && y >= 198.0F && y <= 236.0F) {
                    constexpr std::array<float, 4> shortcutLeft{42, 218, 394, 570};
                    for (size_t index = 0; index < shortcutLeft.size(); ++index) {
                        if (!commsShortcuts[index].empty() && x >= shortcutLeft[index]
                            && x <= shortcutLeft[index] + 156.0F) {
                            commsShortcutHit = index;
                            break;
                        }
                    }
                }
                commsMicHit = selectedDeck == 5 && circleHit(270, 285, 46);
                assistantMicHit = selectedDeck == 6 && circleHit(220, 286, 49);
                assistantCancelHit = selectedDeck == 6 && circleHit(384, 286, 40);
                assistantClearHit = selectedDeck == 6 && circleHit(548, 286, 40);
                ttsVolumeDownHit = selectedDeck == 4 && circleHit(176, 300, 49);
                ttsMuteHit = selectedDeck == 4 && circleHit(384, 300, 49);
                ttsVolumeUpHit = selectedDeck == 4 && circleHit(592, 300, 49);
                broadcastGainDownHit = selectedDeck == 4 && circleHit(540, 220, 36);
                broadcastGainUpHit = selectedDeck == 4 && circleHit(650, 220, 36);
                desktopSettingsHit = selectedDeck == 4 && circleHit(690, 132, 37);
                shutdownButtonHit = selectedDeck == 4 && circleHit(704, 300, 41);
                rigFullResetHit = slimeAvailable && selectedDeck == 3 && circleHit(118, 274, 36);
                rigMountResetHit = slimeAvailable && selectedDeck == 3 && circleHit(650, 274, 36);
                rigConfigProfileHit = slimeAvailable && selectedDeck == 3 && circleHit(118, 145, 36);
                rigPlayProfileHit = slimeAvailable && selectedDeck == 3 && circleHit(650, 145, 36);
                if (selectedDeck == 1 && desktopPanel.showSurfaceList) {
                    desktopListBackHit = x >= 36.0F && x <= 92.0F && y >= 102.0F && y <= 154.0F;
                    desktopBringAllHit = !desktopPanel.surfaces.empty() && circleHit(684, 128, 31);
                    if (y >= 166.0F && y < 352.0F) {
                        const auto row = static_cast<size_t>((y - 166.0F) / 62.0F);
                        if (row < desktopPanel.surfaces.size() && row < 3) {
                            const float rowCenterY = 166.0F + static_cast<float>(row) * 62.0F + 25.0F;
                            if (circleHit(450, rowCenterY, 23)
                                && desktopPanel.surfaces[row].reusable) desktopReuseIndex = row;
                            if (circleHit(520, rowCenterY, 23)) desktopLockIndex = row;
                            if (circleHit(590, rowCenterY, 23)) desktopBringIndex = row;
                            if (circleHit(674, rowCenterY, 23)) desktopCloseIndex = row;
                        }
                    }
                } else {
                    if (selectedDeck == 1 && y >= 132.0F && y <= 200.0F) {
                        constexpr std::array<float, 3> favoriteLeft{70, 304, 538};
                        for (size_t index = 0; index < favoriteLeft.size(); ++index) {
                            if (!desktopFavorites[index].label.empty()
                                && x >= favoriteLeft[index]
                                && x <= favoriteLeft[index] + 160.0F) {
                                desktopFavoriteHit = index;
                                break;
                            }
                        }
                    }
                    const auto deskY = y - 278.0F;
                    const auto newX = x - 150.0F;
                    const auto keyboardX = x - 384.0F;
                    const auto listX = x - 618.0F;
                    desktopNewSurfaceHit = selectedDeck == 1
                        && newX * newX + deskY * deskY <= 48.0F * 48.0F;
                    desktopKeyboardSpawnHit = selectedDeck == 1
                        && keyboardX * keyboardX + deskY * deskY <= 48.0F * 48.0F;
                    desktopSurfaceListHit = selectedDeck == 1
                        && listX * listX + deskY * deskY <= 48.0F * 48.0F;
                }
            }
        }
        const auto rightPointerRay = ReadPointerRay(system, DragHand::Right);
        if (rightPointerRay && !panelHitFound) {
            vr::VROverlayIntersectionParams_t rightRay{};
            rightRay.eOrigin = vr::TrackingUniverseStanding;
            rightRay.vSource = {{rightPointerRay->source.x, rightPointerRay->source.y,
                rightPointerRay->source.z}};
            rightRay.vDirection = {{rightPointerRay->direction.x, rightPointerRay->direction.y,
                rightPointerRay->direction.z}};
            desktopSurfaceHit = desktopSurfaces.HitTest(rightRay);
            keyboardSurfaceHit = desktopSurfaces.KeyboardHitTest(rightRay);
            if (desktopSurfaceHit && keyboardSurfaceHit) {
                if (keyboardSurfaceHit->distance < desktopSurfaceHit->distance) {
                    desktopSurfaceHit.reset();
                } else {
                    keyboardSurfaceHit.reset();
                }
            }
            desktopFrameHit = desktopSurfaces.FrameHitTest(rightRay);
        }
        const auto leftPointerRay = ReadPointerRay(system, DragHand::Left);
        if (leftPointerRay && !panelHitFound) {
            vr::VROverlayIntersectionParams_t leftRay{};
            leftRay.eOrigin = vr::TrackingUniverseStanding;
            leftRay.vSource = {{leftPointerRay->source.x, leftPointerRay->source.y,
                leftPointerRay->source.z}};
            leftRay.vDirection = {{leftPointerRay->direction.x, leftPointerRay->direction.y,
                leftPointerRay->direction.z}};
            leftDesktopFrameHit = desktopSurfaces.FrameHitTest(leftRay);
            leftDesktopSurfaceHit = desktopSurfaces.SurfaceAimHitTest(leftRay);
            leftKeyboardSurfaceHit = desktopSurfaces.KeyboardHitTest(leftRay);
            if (leftDesktopSurfaceHit && leftKeyboardSurfaceHit) {
                if (leftKeyboardSurfaceHit->distance < leftDesktopSurfaceHit->distance) {
                    leftDesktopSurfaceHit.reset();
                } else {
                    leftKeyboardSurfaceHit.reset();
                }
            }
        }
        desktopSurfaces.SetHoveredHit(desktopSurfaceHit);
        desktopSurfaces.SetHoveredKeyboard(
            keyboardSurfaceHit ? keyboardSurfaceHit : leftKeyboardSurfaceHit);
        const std::optional<uint64_t> rightAimSurface = keyboardSurfaceHit
            ? std::optional<uint64_t>{keyboardSurfaceHit->id}
            : desktopSurfaceHit ? std::optional<uint64_t>{desktopSurfaceHit->id}
            : desktopFrameHit;
        const std::optional<uint64_t> leftAimSurface = leftKeyboardSurfaceHit
            ? std::optional<uint64_t>{leftKeyboardSurfaceHit->id}
            : leftDesktopSurfaceHit ? std::optional<uint64_t>{leftDesktopSurfaceHit->id}
            : leftDesktopFrameHit;
        const auto rightGrabTarget = rightAimSurface;
        const auto leftGrabTarget = leftAimSurface;
        std::optional<uint64_t> highlightedSurface;
        if (keyboardSurfaceHit) highlightedSurface = keyboardSurfaceHit->id;
        else if (leftKeyboardSurfaceHit) highlightedSurface = leftKeyboardSurfaceHit->id;
        else if (desktopSurfaceHit) highlightedSurface = desktopSurfaceHit->id;
        else if (leftDesktopSurfaceHit) highlightedSurface = leftDesktopSurfaceHit->id;
        else highlightedSurface = desktopFrameHit ? desktopFrameHit : leftDesktopFrameHit;
        desktopSurfaces.SetHoveredFrame(highlightedSurface);
        const bool panelActionable = (panelHitFound && panelY <= 82.0F)
            || restoreButtonHit || rigFullResetHit
            || (rigMountResetHit && mountReady)
            || rigConfigProfileHit || rigPlayProfileHit
            || desktopNewSurfaceHit || desktopKeyboardSpawnHit || desktopSurfaceListHit
            || desktopFavoriteHit || desktopListBackHit || desktopBringAllHit
            || desktopBringIndex.has_value() || desktopLockIndex.has_value()
            || desktopReuseIndex.has_value() || desktopCloseIndex.has_value()
            || musicMicHit || musicBroadcastHit || musicPreviousHit || musicToggleHit || musicNextHit
            || commsMicHit || commsClearHit || commsShortcutHit.has_value()
            || assistantMicHit || assistantCancelHit || assistantClearHit
            || ttsVolumeDownHit || ttsMuteHit || ttsVolumeUpHit || desktopSettingsHit
            || broadcastGainDownHit || broadcastGainUpHit || shutdownButtonHit;
        if (panelHitFound) {
            vr::VROverlay()->SetOverlayWidthInMeters(cursorOverlay, 0.0035F);
            vr::VROverlay()->SetOverlaySortOrder(cursorOverlay, 11);
            vr::HmdMatrix34_t localCursor{};
            localCursor.m[0][0] = 1.0F;
            localCursor.m[1][1] = 1.0F;
            localCursor.m[2][2] = 1.0F;
            localCursor.m[0][3] = (panelHit.vUVs.v[0] - 0.5F) * 0.205F;
            localCursor.m[1][3] = (panelHit.vUVs.v[1] - 0.5F) * 0.1025F;
            localCursor.m[2][3] = 0.006F;
            const auto cursorTransform = MultiplyTransforms(wristTransform, localCursor);
            const auto wristController = system->GetTrackedDeviceIndexForControllerRole(
                WristControllerRole(ttsSettings));
            if (wristController != vr::k_unTrackedDeviceIndexInvalid) {
                vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(
                    cursorOverlay, wristController, &cursorTransform);
                vr::VROverlay()->SetOverlayColor(cursorOverlay,
                    panelActionable ? 0.20F : 0.02F, panelActionable ? 1.0F : 0.85F, 1.0F);
                vr::VROverlay()->ShowOverlay(cursorOverlay);
            }
        } else if (keyboardSurfaceHit) {
            if (const auto cursorTransform = desktopSurfaces.KeyboardCursorTransform(
                    *keyboardSurfaceHit)) {
                vr::VROverlay()->SetOverlayWidthInMeters(cursorOverlay, 0.0055F);
                vr::VROverlay()->SetOverlaySortOrder(cursorOverlay, 30);
                vr::VROverlay()->SetOverlayTransformAbsolute(cursorOverlay,
                    vr::TrackingUniverseStanding, &*cursorTransform);
                vr::VROverlay()->SetOverlayColor(cursorOverlay, 0.20F, 1.0F, 1.0F);
                vr::VROverlay()->ShowOverlay(cursorOverlay);
            } else {
                vr::VROverlay()->HideOverlay(cursorOverlay);
            }
        } else if (desktopSurfaceHit) {
            if (const auto cursorTransform = desktopSurfaces.CursorTransform(*desktopSurfaceHit)) {
                vr::VROverlay()->SetOverlayWidthInMeters(cursorOverlay, 0.0055F);
                vr::VROverlay()->SetOverlaySortOrder(cursorOverlay, 30);
                vr::VROverlay()->SetOverlayTransformAbsolute(cursorOverlay,
                    vr::TrackingUniverseStanding, &*cursorTransform);
                const bool actionable = desktopSurfaceHit->captured
                    || desktopSurfaceHit->sourceIndex.has_value()
                    || desktopSurfaceHit->pageDelta != 0;
                vr::VROverlay()->SetOverlayColor(cursorOverlay,
                    actionable ? 0.20F : 0.02F, actionable ? 1.0F : 0.85F, 1.0F);
                vr::VROverlay()->ShowOverlay(cursorOverlay);
            } else {
                vr::VROverlay()->HideOverlay(cursorOverlay);
            }
        } else {
            vr::VROverlay()->HideOverlay(cursorOverlay);
        }
        if (leftKeyboardSurfaceHit) {
            if (const auto cursorTransform = desktopSurfaces.KeyboardCursorTransform(
                    *leftKeyboardSurfaceHit)) {
                vr::VROverlay()->SetOverlayWidthInMeters(leftCursorOverlay, 0.0055F);
                vr::VROverlay()->SetOverlayTransformAbsolute(leftCursorOverlay,
                    vr::TrackingUniverseStanding, &*cursorTransform);
                vr::VROverlay()->ShowOverlay(leftCursorOverlay);
            } else {
                vr::VROverlay()->HideOverlay(leftCursorOverlay);
            }
        } else if (leftDesktopSurfaceHit) {
            if (const auto cursorTransform = desktopSurfaces.CursorTransform(
                    *leftDesktopSurfaceHit)) {
                vr::VROverlay()->SetOverlayWidthInMeters(leftCursorOverlay, 0.0055F);
                vr::VROverlay()->SetOverlayTransformAbsolute(leftCursorOverlay,
                    vr::TrackingUniverseStanding, &*cursorTransform);
                vr::VROverlay()->ShowOverlay(leftCursorOverlay);
            } else {
                vr::VROverlay()->HideOverlay(leftCursorOverlay);
            }
        } else {
            vr::VROverlay()->HideOverlay(leftCursorOverlay);
        }
        if (leftSurfaceGrab.bChanged && leftSurfaceGrab.bState && leftGrabTarget) {
            if (const auto handPose = ReadControllerPose(system, DragHand::Left)) {
                if (desktopSurfaces.BeginGrab(*leftGrabTarget,
                        interfayce::DesktopGrabHand::Left, *handPose)) {
                    std::cout << "Desktop surface " << *leftGrabTarget
                              << " grabbed with left hand.\n";
                }
            }
        }
        if (rightSurfaceGrab.bChanged && rightSurfaceGrab.bState && rightGrabTarget) {
            if (const auto handPose = ReadControllerPose(system, DragHand::Right)) {
                if (desktopSurfaces.BeginGrab(*rightGrabTarget,
                        interfayce::DesktopGrabHand::Right, *handPose)) {
                    std::cout << "Desktop surface " << *rightGrabTarget
                              << " grabbed with right hand.\n";
                }
            }
        }
        if (leftSurfaceGrab.bState) {
            if (const auto handPose = ReadControllerPose(system, DragHand::Left)) {
                desktopSurfaces.UpdateGrab(interfayce::DesktopGrabHand::Left, *handPose);
            }
        }
        if (rightSurfaceGrab.bState) {
            if (const auto handPose = ReadControllerPose(system, DragHand::Right)) {
                desktopSurfaces.UpdateGrab(interfayce::DesktopGrabHand::Right, *handPose);
            }
        }
        if (leftSurfaceGrab.bChanged && !leftSurfaceGrab.bState) {
            desktopSurfaces.EndGrab(interfayce::DesktopGrabHand::Left);
        }
        if (rightSurfaceGrab.bChanged && !rightSurfaceGrab.bState) {
            desktopSurfaces.EndGrab(interfayce::DesktopGrabHand::Right);
        }
        if (wristUiClick.bChanged && wristUiClick.bState && panelHitFound && panelActionable) {
            pressFeedbackActive = true;
            pressFeedbackUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(130);
            renderer.SetPressFeedback(panelX, panelY, true);
            const auto feedbackHaptic = ttsSettings.wristRight ? leftHapticAction : rightHapticAction;
            vr::VRInput()->TriggerHapticVibrationAction(feedbackHaptic, 0.0F, 0.025F, 105.0F,
                ttsSettings.hapticStrength * 0.65F, vr::k_ulInvalidInputValueHandle);
            if (!rawPanel && renderer.Initialize(system, selectedDeck, musicLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (leftUiClick.bChanged && leftUiClick.bState && leftKeyboardSurfaceHit && !panelHitFound) {
            if (desktopSurfaces.ActivateKeyboardHit(*leftKeyboardSurfaceHit)) {
                vr::VRInput()->TriggerHapticVibrationAction(
                    leftHapticAction, 0.0F, 0.035F, 115.0F, ttsSettings.hapticStrength,
                    vr::k_ulInvalidInputValueHandle);
            }
        }
        if (rightUiClick.bChanged && rightUiClick.bState && keyboardSurfaceHit && !panelHitFound) {
            if (desktopSurfaces.ActivateKeyboardHit(*keyboardSurfaceHit)) {
                vr::VRInput()->TriggerHapticVibrationAction(
                    rightHapticAction, 0.0F, 0.035F, 115.0F, ttsSettings.hapticStrength,
                    vr::k_ulInvalidInputValueHandle);
            }
        } else if (rightUiClick.bChanged && rightUiClick.bState && desktopSurfaceHit && !panelHitFound) {
            if (desktopSurfaceHit->captured) {
                if (desktopSurfaces.SendPointerEvent(*desktopSurfaceHit,
                        interfayce::DesktopPointerEvent::PrimaryDown)) {
                    activeDesktopPointer = desktopSurfaceHit;
                }
            } else {
                const auto selectedSource = desktopSurfaces.SourceForHit(*desktopSurfaceHit);
                if (desktopSurfaces.ActivateHit(*desktopSurfaceHit)) {
                    if (selectedSource) RecordDesktopRecent(*selectedSource);
                    if (desktopSurfaceHit->sourceIndex) {
                        desktopPanel.surfaces = desktopSurfaces.Summaries();
                        std::cout << "Desktop source assigned to surface "
                                  << desktopSurfaceHit->id << '\n';
                    }
                } else if (desktopSurfaceHit->sourceIndex || desktopSurfaceHit->pageDelta != 0) {
                    std::cerr << "Could not start selected desktop capture\n";
                }
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && panelHitFound && panelY <= 82.0F) {
            const int requestedDeck = panelX < 115.0F ? 0 : panelX < 209.0F ? 5
                : panelX < 303.0F ? 6 : panelX < 397.0F ? 1
                : panelX < 491.0F ? 2 : panelX < 585.0F ? 3 : 4;
            if (requestedDeck == 0) {
                spotifyAvailable = IsProcessRunning(L"Spotify.exe");
                if (spotifyAvailable) {
                    if (musicLine.empty() || musicLine == L"Spotify is not running") {
                        musicLine = L"Loading Spotify...";
                    }
                    nextMusicPoll = std::chrono::steady_clock::now();
                } else {
                    musicLine = L"Spotify is not running";
                }
            }
            if (requestedDeck == 1) {
                desktopPanel.showSurfaceList = false;
                desktopPanel.surfaces = desktopSurfaces.Summaries();
                if (const auto favorites = RequestDesktopFavorites()) {
                    desktopFavorites = *favorites;
                    for (size_t index = 0; index < favorites->size(); ++index) {
                        desktopPanel.favorites[index] = (*favorites)[index].label;
                    }
                }
                desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
            }
            if (requestedDeck == 3) {
                rigLine = ReadControllerBatteryLine(system);
            }
            if (requestedDeck == 4) {
                if (const auto loaded = ReadTtsSettings()) ttsSettings = *loaded;
                renderer.SetTtsSettings(ttsSettings.volumePercent, ttsSettings.muted);
                renderer.SetBroadcastGainDb(
                    static_cast<int>(std::lround(ttsSettings.broadcastGainDb)));
            }
            if (requestedDeck == 5) {
                if (const auto labels = RequestCommsShortcutLabels()) {
                    commsShortcuts = *labels;
                    renderer.SetCommsShortcuts(commsShortcuts);
                }
                if (const auto current = RequestCommsState("GET", "/comms/status")) {
                    commsState = *current;
                    renderer.SetCommsStatus(
                        commsState.status, commsState.transcript, commsState.active);
                }
                nextCommsPoll = std::chrono::steady_clock::now();
                nextCommsShortcutPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
            if (requestedDeck == 6) {
                if (const auto current = RequestAssistantState("GET", "/assistant/status")) {
                    assistantState = *current;
                    renderer.SetAssistantStatus(assistantState.status, assistantState.transcript,
                        assistantState.response, assistantState.active);
                }
                nextAssistantPoll = std::chrono::steady_clock::now();
            }
            if (requestedDeck != selectedDeck && renderer.Initialize(
                    system, requestedDeck, requestedDeck == 1 ? desktopLine : musicLine,
                    requestedDeck == 0 && !spotifyAvailable ? L"" : musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                selectedDeck = requestedDeck;
                desktopSurfaces.SetDeckVisible(selectedDeck == 1);
                if (selectedDeck != 1) {
                    vr::VROverlay()->HideOverlay(cursorOverlay);
                    vr::VROverlay()->HideOverlay(leftCursorOverlay);
                }
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && musicBroadcastHit) {
            if (broadcast.Enabled()) {
                broadcast.Stop();
                renderer.SetMusicBroadcastState(false, broadcast.StatusText());
            } else if (!IsProcessRunning(L"Spotify.exe")) {
                renderer.SetMusicBroadcastState(false, L"SPOTIFY OFFLINE");
            } else {
                std::wstring error;
                if (!broadcast.Start(error)) {
                    std::wcerr << L"Broadcast start failed: " << error << L'\n';
                }
                renderer.SetMusicBroadcastState(
                    broadcast.Enabled(), broadcast.StatusText());
            }
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && musicMicHit) {
            if (!musicVoiceCommand.valid()) {
                voiceServiceAvailable = VoiceServiceAvailable();
                if (!voiceServiceAvailable) {
                    LaunchVoiceService(directory, projectRoot);
                    renderer.SetMusicVoiceStatus(L"VOICE WARMING", false);
                } else {
                    renderer.SetMusicVoiceStatus(L"LISTENING...", true);
                    musicVoiceCommand = std::async(
                        std::launch::async, [] { return RequestMusicVoiceCommand(); });
                }
                if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && commsShortcutHit) {
            const auto path = "/comms/shortcut/" + std::to_string(*commsShortcutHit);
            if (const auto changed = RequestCommsState("POST", path)) {
                commsState = *changed;
            } else {
                commsState = {L"COMMS ERROR", L"Shortcut service did not respond", false};
            }
            renderer.SetCommsStatus(
                commsState.status, commsState.transcript, commsState.active);
            nextCommsPoll = std::chrono::steady_clock::now();
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState
                   && (commsMicHit || commsClearHit)) {
            voiceServiceAvailable = VoiceServiceAvailable();
            if (!voiceServiceAvailable) {
                LaunchVoiceService(directory, projectRoot);
                commsState = {L"VOICE WARMING", L"", false};
            } else if (const auto changed = RequestCommsState(
                    "POST", commsMicHit ? "/comms/toggle" : "/comms/clear")) {
                commsState = *changed;
            } else {
                commsState = {L"COMMS ERROR", L"Voice service did not respond", false};
            }
            renderer.SetCommsStatus(
                commsState.status, commsState.transcript, commsState.active);
            nextCommsPoll = std::chrono::steady_clock::now();
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState
                   && (assistantMicHit || assistantCancelHit || assistantClearHit)) {
            voiceServiceAvailable = VoiceServiceAvailable();
            if (!voiceServiceAvailable) {
                LaunchVoiceService(directory, projectRoot);
                assistantState = {L"VOICE WARMING", L"", L"", false};
            } else if (assistantMicHit && !assistantCommand.valid()) {
                assistantState = {L"LISTENING", L"", L"", true};
                assistantCommand = std::async(std::launch::async, [] {
                    return RequestAssistantState("POST", "/listen/assistant",
                        std::chrono::seconds(60));
                });
            } else if (assistantCancelHit && assistantCommand.valid()) {
                if (const auto changed = RequestAssistantState("POST", "/assistant/cancel")) {
                    assistantState = *changed;
                }
            } else if (assistantClearHit && !assistantCommand.valid()) {
                if (const auto changed = RequestAssistantState("POST", "/assistant/clear")) {
                    assistantState = *changed;
                }
            }
            renderer.SetAssistantStatus(assistantState.status, assistantState.transcript,
                assistantState.response, assistantState.active);
            nextAssistantPoll = std::chrono::steady_clock::now();
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState
                   && (broadcastGainDownHit || broadcastGainUpHit)) {
            const auto path = broadcastGainDownHit
                ? "/settings/broadcast/gain/down" : "/settings/broadcast/gain/up";
            if (const auto changed = ChangeTtsSetting(path)) {
                const bool restartBroadcast = broadcast.Enabled();
                if (restartBroadcast) broadcast.Stop();
                ttsSettings = *changed;
                broadcast.SetGainDb(ttsSettings.broadcastGainDb);
                renderer.SetBroadcastGainDb(
                    static_cast<int>(std::lround(ttsSettings.broadcastGainDb)));
                if (restartBroadcast) {
                    std::wstring error;
                    if (!broadcast.Start(error)) {
                        std::wcerr << L"Broadcast gain restart failed: " << error << L'\n';
                    }
                }
                renderer.SetMusicBroadcastState(
                    broadcast.Enabled(), broadcast.StatusText());
                if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState
                   && (ttsVolumeDownHit || ttsMuteHit || ttsVolumeUpHit)) {
            const auto path = ttsVolumeDownHit ? "/settings/tts/volume/down"
                : ttsVolumeUpHit ? "/settings/tts/volume/up"
                : "/settings/tts/mute/toggle";
            if (const auto changed = ChangeTtsSetting(path)) {
                ttsSettings = *changed;
                renderer.SetTtsSettings(ttsSettings.volumePercent, ttsSettings.muted);
                if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopSettingsHit) {
            if (!LaunchDesktopSettings(directory, projectRoot)) {
                std::cerr << "Could not launch the desktop settings window.\n";
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && shutdownButtonHit) {
            shutdownHoldActive = true;
            shutdownHoldStarted = std::chrono::steady_clock::now();
            shutdownHoldSegment = 1;
            renderer.SetShutdownHoldProgress(1.0F / 12.0F);
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState
                   && (musicPreviousHit || musicToggleHit || musicNextHit)) {
            if (musicPreviousHit) {
                LaunchSpotifyControl(projectRoot, L"previous");
            } else if (musicToggleHit) {
                LaunchSpotifyControl(projectRoot, L"toggle");
            } else if (musicNextHit) {
                LaunchSpotifyControl(projectRoot, L"next");
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopFavoriteHit) {
            const auto& favorite = desktopFavorites[*desktopFavoriteHit];
            const auto sources = desktopSourceManager.EnumerateSources();
            const auto surfaceId = desktopSurfaces.SpawnPicker(sources);
            if (surfaceId != 0) {
                if (const auto runningWindow = desktopSourceManager.FindWindowForTarget(
                        favorite.executable)) {
                    if (desktopSurfaces.AssignSource(surfaceId, *runningWindow)) {
                        RecordDesktopRecent(*runningWindow);
                    }
                } else if (desktopSourceManager.LaunchTarget(favorite.executable)) {
                    const auto now = std::chrono::steady_clock::now();
                    pendingFavoriteCaptures.push_back({
                        surfaceId, favorite.executable, now + std::chrono::milliseconds(150),
                        now + std::chrono::seconds(10)});
                }
                desktopPanel.surfaces = desktopSurfaces.Summaries();
                desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
                if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopNewSurfaceHit) {
            const auto sources = desktopSourceManager.EnumerateSources();
            const auto surfaceId = desktopSurfaces.SpawnPicker(sources);
            if (surfaceId != 0) {
                desktopPanel.surfaces = desktopSurfaces.Summaries();
                desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
                std::cout << "Desktop picker surface " << surfaceId << " spawned with "
                          << sources.size() << " eligible sources\n";
                if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            } else {
                std::cerr << "Could not spawn desktop picker surface\n";
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopKeyboardSpawnHit) {
            const auto keyboardId = desktopSurfaces.SpawnKeyboard();
            if (keyboardId != 0) {
                desktopPanel.surfaces = desktopSurfaces.Summaries();
                desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
                std::cout << "Keyboard surface " << keyboardId << " is available.\n";
                if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            } else {
                std::cerr << "Could not spawn keyboard surface\n";
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopSurfaceListHit) {
            desktopPanel.showSurfaceList = true;
            desktopPanel.surfaces = desktopSurfaces.Summaries();
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopListBackHit) {
            desktopPanel.showSurfaceList = false;
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopBringAllHit) {
            desktopSurfaces.BringAllToMe();
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopLockIndex) {
            desktopSurfaces.ToggleLocked(desktopPanel.surfaces[*desktopLockIndex].id);
            desktopPanel.surfaces = desktopSurfaces.Summaries();
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopBringIndex) {
            desktopSurfaces.BringToMe(desktopPanel.surfaces[*desktopBringIndex].id);
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopReuseIndex) {
            const auto sources = desktopSourceManager.EnumerateSources();
            desktopSurfaces.ReturnToPicker(
                desktopPanel.surfaces[*desktopReuseIndex].id, sources);
            desktopPanel.surfaces = desktopSurfaces.Summaries();
            desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && desktopCloseIndex) {
            desktopSurfaces.Close(desktopPanel.surfaces[*desktopCloseIndex].id);
            desktopPanel.surfaces = desktopSurfaces.Summaries();
            desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
            if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && restoreButtonHit && selectedDeck == 2) {
            restoreHoldActive = true;
            restoreHoldStarted = std::chrono::steady_clock::now();
            restoreHoldSegment = 1;
            renderer.SetPlayspaceHoldProgress(1.0F / 12.0F);
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState
                && (rigConfigProfileHit || rigPlayProfileHit) && !rigLegProfileTask.valid()) {
            const auto requested = rigConfigProfileHit
                ? interfayce::RigLegProfile::Config : interfayce::RigLegProfile::Play;
            const wchar_t* action = rigConfigProfileHit ? L"config" : L"play";
            renderer.SetRigLegProfile(rigLegProfile, requested);
            rigLegProfileTask = std::async(std::launch::async,
                [projectRoot, action] { return RunSlimeLegProfile(projectRoot, action); });
            std::cout << "SlimeVR leg profile change requested\n";
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        } else if (wristUiClick.bChanged && wristUiClick.bState && (rigFullResetHit || (rigMountResetHit && mountReady))) {
            rigResetHoldActive = true;
            rigResetHoldCompleted = false;
            rigResetKind = rigFullResetHit ? L"full" : L"mounting";
            rigResetHoldStarted = std::chrono::steady_clock::now();
            rigResetHoldSegment = 1;
            renderer.SetRigHoldProgress(
                rigFullResetHit ? 1.0F / 12.0F : 0.0F,
                rigMountResetHit ? 1.0F / 12.0F : 0.0F);
            if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                    rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (rightUiClick.bState && activeDesktopPointer && desktopSurfaceHit
            && desktopSurfaceHit->captured && desktopSurfaceHit->id == activeDesktopPointer->id) {
            activeDesktopPointer = desktopSurfaceHit;
            desktopSurfaces.SendPointerEvent(*activeDesktopPointer,
                interfayce::DesktopPointerEvent::Move);
        }
        if (rightUiClick.bChanged && !rightUiClick.bState && activeDesktopPointer) {
            desktopSurfaces.SendPointerEvent(*activeDesktopPointer,
                interfayce::DesktopPointerEvent::PrimaryUp);
            activeDesktopPointer.reset();
        }
        const auto scrollNow = std::chrono::steady_clock::now();
        const auto scrollSeconds = (std::min)(
            std::chrono::duration<double>(scrollNow - lastScrollUpdate).count(), 0.05);
        lastScrollUpdate = scrollNow;
        if (desktopSurfaceHit && desktopSurfaceHit->captured && rightSurfaceScroll.bActive
            && !leftSurfaceGrab.bState && !rightSurfaceGrab.bState && !rightUiClick.bState) {
            if (!activeScrollSurface || *activeScrollSurface != desktopSurfaceHit->id) {
                activeScrollSurface = desktopSurfaceHit->id;
                verticalScrollRemainder = 0.0;
                horizontalScrollRemainder = 0.0;
            }
            constexpr double kWheelUnitsPerSecond = 900.0;
            verticalScrollRemainder += ApplyDeadzone(rightSurfaceScroll.y)
                * kWheelUnitsPerSecond * scrollSeconds;
            horizontalScrollRemainder += ApplyDeadzone(rightSurfaceScroll.x)
                * kWheelUnitsPerSecond * scrollSeconds;
            const auto verticalDelta = static_cast<int32_t>(verticalScrollRemainder);
            const auto horizontalDelta = static_cast<int32_t>(horizontalScrollRemainder);
            verticalScrollRemainder -= verticalDelta;
            horizontalScrollRemainder -= horizontalDelta;
            if (verticalDelta != 0 || horizontalDelta != 0) {
                desktopSurfaces.SendScrollEvent(
                    *desktopSurfaceHit, verticalDelta, horizontalDelta);
            }
        } else {
            activeScrollSurface.reset();
            verticalScrollRemainder = 0.0;
            horizontalScrollRemainder = 0.0;
        }
        if (broadcast.Poll()) {
            renderer.SetMusicBroadcastState(
                broadcast.Enabled(), broadcast.StatusText());
            if (selectedDeck == 0 && renderer.Initialize(system, selectedDeck,
                    musicLine, musicArtPath.wstring(), rigLine, rigSlots,
                    mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (rigLegProfileTask.valid()
            && rigLegProfileTask.wait_for(std::chrono::milliseconds(0))
                == std::future_status::ready) {
            rigLegProfile = rigLegProfileTask.get();
            renderer.SetRigLegProfile(rigLegProfile);
            std::cout << "SlimeVR leg profile result: "
                      << (rigLegProfile == interfayce::RigLegProfile::Config ? "CONFIG"
                          : rigLegProfile == interfayce::RigLegProfile::Play ? "PLAY"
                          : rigLegProfile == interfayce::RigLegProfile::Custom ? "CUSTOM"
                          : "ERROR") << '\n';
            if (selectedDeck == 3 && renderer.Initialize(system, selectedDeck, musicLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (selectedDeck == 0) {
            const auto musicNow = std::chrono::steady_clock::now();
            if (musicVoiceCommand.valid()
                && musicVoiceCommand.wait_for(std::chrono::milliseconds(0))
                    == std::future_status::ready) {
                const auto response = musicVoiceCommand.get();
                std::wcout << L"music voice response: " << response << L'\n';
                renderer.SetMusicVoiceStatus(VoiceStatusFromResponse(response), false);
                nextMusicPoll = musicNow;
                if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
            if (!voiceServiceAvailable && musicNow >= nextVoiceHealthPoll) {
                nextVoiceHealthPoll = musicNow + std::chrono::seconds(1);
                voiceServiceAvailable = VoiceServiceAvailable();
                if (voiceServiceAvailable) {
                    renderer.SetMusicVoiceStatus(L"VOICE READY", false);
                    if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                            rigLine, rigSlots, mountReady, desktopPanel)) {
                        const auto updatedTexture = renderer.Texture();
                        vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                    }
                }
            }
            if (musicPoll.valid()
                && musicPoll.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                const auto updatedMusic = musicPoll.get();
                nextMusicPoll = musicNow + std::chrono::seconds(2);
                if (updatedMusic.line != musicLine || updatedMusic.playing != musicPlaying) {
                    const bool trackChanged = updatedMusic.line != musicLine;
                    musicLine = updatedMusic.line;
                    musicPlaying = updatedMusic.playing;
                    renderer.SetMusicPlaying(musicPlaying);
                    if (trackChanged) RefreshSpotifyArt(projectRoot, musicArtPath);
                    renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel);
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
            if (!musicPoll.valid() && musicNow >= nextMusicPoll) {
                const bool spotifyRunningNow = IsProcessRunning(L"Spotify.exe");
                if (!spotifyRunningNow) {
                    spotifyAvailable = false;
                    musicPlaying = false;
                    renderer.SetMusicPlaying(false);
                    nextMusicPoll = musicNow + std::chrono::seconds(2);
                    const std::wstring unavailable = L"Spotify is not running";
                    if (musicLine != unavailable && renderer.Initialize(system, selectedDeck,
                            unavailable, L"", rigLine, rigSlots, mountReady, desktopPanel)) {
                        musicLine = unavailable;
                        const auto updatedTexture = renderer.Texture();
                        vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                    }
                } else {
                    spotifyAvailable = true;
                    musicPoll = std::async(std::launch::async,
                        [projectRoot] { return ReadSpotifyNowPlaying(projectRoot); });
                }
            }
        }
        if (selectedDeck == 5 && std::chrono::steady_clock::now() >= nextCommsPoll) {
            nextCommsPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            if (const auto current = RequestCommsState("GET", "/comms/status")) {
                if (current->status != commsState.status
                    || current->transcript != commsState.transcript) {
                    commsState = *current;
                    renderer.SetCommsStatus(
                        commsState.status, commsState.transcript, commsState.active);
                    if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                            rigLine, rigSlots, mountReady, desktopPanel)) {
                        const auto updatedTexture = renderer.Texture();
                        vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                    }
                }
            }
        }
        if (selectedDeck == 5
            && std::chrono::steady_clock::now() >= nextCommsShortcutPoll) {
            nextCommsShortcutPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            if (const auto labels = RequestCommsShortcutLabels(); labels && *labels != commsShortcuts) {
                commsShortcuts = *labels;
                renderer.SetCommsShortcuts(commsShortcuts);
                if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        }
        if (assistantCommand.valid()
            && assistantCommand.wait_for(std::chrono::milliseconds(0))
                == std::future_status::ready) {
            if (const auto completed = assistantCommand.get()) assistantState = *completed;
            else assistantState = {L"ERROR", assistantState.transcript,
                L"Voice service did not respond", false};
            renderer.SetAssistantStatus(assistantState.status, assistantState.transcript,
                assistantState.response, assistantState.active);
            if (selectedDeck == 6 && renderer.Initialize(system, selectedDeck, musicLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (selectedDeck == 6
            && std::chrono::steady_clock::now() >= nextAssistantPoll) {
            nextAssistantPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            if (const auto current = RequestAssistantState("GET", "/assistant/status")) {
                if (current->status != assistantState.status
                    || current->transcript != assistantState.transcript
                    || current->response != assistantState.response) {
                    assistantState = *current;
                    renderer.SetAssistantStatus(assistantState.status, assistantState.transcript,
                        assistantState.response, assistantState.active);
                    if (renderer.Initialize(system, selectedDeck, musicLine,
                            musicArtPath.wstring(), rigLine, rigSlots,
                            mountReady, desktopPanel)) {
                        const auto updatedTexture = renderer.Texture();
                        vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                    }
                }
            }
        }
        bool favoriteCaptureChanged = false;
        const auto favoriteNow = std::chrono::steady_clock::now();
        for (auto pending = pendingFavoriteCaptures.begin();
             pending != pendingFavoriteCaptures.end();) {
            if (favoriteNow >= pending->deadline) {
                pending = pendingFavoriteCaptures.erase(pending);
                favoriteCaptureChanged = true;
                continue;
            }
            if (favoriteNow < pending->nextPoll) {
                ++pending;
                continue;
            }
            pending->nextPoll = favoriteNow + std::chrono::milliseconds(150);
            if (const auto source = desktopSourceManager.FindWindowForTarget(
                    pending->executable)) {
                if (desktopSurfaces.AssignSource(pending->surfaceId, *source)) {
                    RecordDesktopRecent(*source);
                }
                pending = pendingFavoriteCaptures.erase(pending);
                favoriteCaptureChanged = true;
            } else {
                ++pending;
            }
        }
        if (favoriteCaptureChanged) {
            desktopPanel.surfaces = desktopSurfaces.Summaries();
            desktopLine = DesktopSurfaceLine(desktopPanel.surfaces.size());
            if (selectedDeck == 1 && renderer.Initialize(system, selectedDeck, desktopLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (selectedDeck == 1
            && std::chrono::steady_clock::now() >= nextDesktopFavoritePoll) {
            nextDesktopFavoritePoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            if (const auto favorites = RequestDesktopFavorites();
                    favorites && *favorites != desktopFavorites) {
                desktopFavorites = *favorites;
                for (size_t index = 0; index < favorites->size(); ++index) {
                    desktopPanel.favorites[index] = (*favorites)[index].label;
                }
                if (renderer.Initialize(system, selectedDeck, desktopLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        }
        if (std::chrono::steady_clock::now() >= nextRuntimeSettingsPoll) {
            nextRuntimeSettingsPoll = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            if (const auto loaded = ReadTtsSettings()) {
                const bool wristDisplayChanged = loaded->volumePercent != ttsSettings.volumePercent
                    || loaded->muted != ttsSettings.muted
                    || loaded->broadcastGainDb != ttsSettings.broadcastGainDb;
                const bool wristPlacementChanged = WristPlacementChanged(*loaded, ttsSettings);
                ttsSettings = *loaded;
                broadcast.SetGainDb(ttsSettings.broadcastGainDb);
                if (wristPlacementChanged && !headsetPanel) {
                    wristTransform = ConfiguredWristTransform(ttsSettings);
                    wristAttached = attachWristOverlay();
                }
                if (selectedDeck == 4 && wristDisplayChanged) {
                    renderer.SetTtsSettings(ttsSettings.volumePercent, ttsSettings.muted);
                    renderer.SetBroadcastGainDb(
                        static_cast<int>(std::lround(ttsSettings.broadcastGainDb)));
                    if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                            rigLine, rigSlots, mountReady, desktopPanel)) {
                        const auto updatedTexture = renderer.Texture();
                        vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                    }
                }
            }
        }
        const auto batteryNow = std::chrono::steady_clock::now();
        bool batteryStateChanged = false;
        if (batteryStatusPost.valid()
            && batteryStatusPost.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            batteryStatusPost.get();
        }
        if (batteryNow >= nextControllerBatteryPoll) {
            nextControllerBatteryPoll = batteryNow + std::chrono::seconds(15);
            const std::array<int, 2> updated{
                ReadControllerBatteryPercent(system, vr::TrackedControllerRole_LeftHand),
                ReadControllerBatteryPercent(system, vr::TrackedControllerRole_RightHand)};
            batteryStateChanged = updated != controllerBatteries;
            controllerBatteries = updated;
            rigLine = ReadControllerBatteryLine(system);
        }
        if (slimeAvailable && !slimeBatteryPoll.valid() && batteryNow >= nextRigPoll) {
            nextRigPoll = batteryNow + std::chrono::seconds(30);
            slimeBatteryPoll = std::async(std::launch::async,
                [projectRoot] { return ReadSlimeTrackerBatteries(projectRoot); });
        }
        if (slimeBatteryPoll.valid()
            && slimeBatteryPoll.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            const auto updated = slimeBatteryPoll.get();
            if (updated.received) {
                batteryStateChanged = batteryStateChanged || updated.slots != rigSlots
                    || updated.mountReady != mountReady;
                rigSlots = updated.slots;
                mountReady = updated.mountReady;
            }
        }
        if (batteryStateChanged) {
            static constexpr const char* batteryNames[]{
                "Left hand", "Right hand", "Left elbow", "Right elbow", "Chest",
                "Hip", "Left thigh", "Right thigh", "Left foot", "Right foot"};
            std::string payload;
            int lowestBattery = 101;
            for (size_t index = 0; index < controllerBatteries.size(); ++index) {
                if (controllerBatteries[index] >= 0) {
                    lowestBattery = (std::min)(lowestBattery, controllerBatteries[index]);
                    payload += std::string(batteryNames[index]) + "="
                        + std::to_string(controllerBatteries[index]) + "\n";
                }
            }
            for (size_t index = 0; index < rigSlots.size(); ++index) {
                const int percent = rigSlots[index].empty() ? -1 : _wtoi(rigSlots[index].c_str());
                if (percent >= 0) {
                    lowestBattery = (std::min)(lowestBattery, percent);
                    payload += std::string(batteryNames[index + 2]) + "="
                        + std::to_string(percent) + "\n";
                }
            }
            renderer.SetLowestBattery(lowestBattery <= 100 ? lowestBattery : -1);
            if (!payload.empty() && !batteryStatusPost.valid()) {
                batteryStatusPost = std::async(std::launch::async, [payload] {
                    return LocalHttpRequest(
                        "POST", "/battery/status", std::chrono::seconds(2), payload);
                });
            }
            if (selectedDeck == 3 && renderer.Initialize(system, selectedDeck, musicLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (wristUiClick.bChanged && !wristUiClick.bState && restoreHoldActive) {
            const auto heldFor = std::chrono::steady_clock::now() - restoreHoldStarted;
            if (restoreButtonHit && heldFor >= std::chrono::milliseconds(750)) {
                restoreBaseline(sessionBaseline, "wrist restore control");
                previewOffset = {};
                playspaceAdjusted = false;
                renderer.SetPlayspaceAdjusted(false);
                if (selectedDeck == 2 && renderer.Initialize(system, selectedDeck, musicLine,
                        musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            } else {
                std::cout << "wrist restore cancelled: hold for 0.75 seconds\n";
            }
            restoreHoldActive = false;
            restoreHoldSegment = 0;
            renderer.SetPlayspaceHoldProgress(0.0F);
            if (selectedDeck == 2 && renderer.Initialize(system, selectedDeck, musicLine,
                    musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                const auto updatedTexture = renderer.Texture();
                vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
            }
        }
        if (restoreHoldActive && wristUiClick.bState && restoreButtonHit) {
            const auto elapsed = std::chrono::steady_clock::now() - restoreHoldStarted;
            constexpr float restoreSeconds = 0.75F;
            const float progress = (std::min)(1.0F,
                std::chrono::duration<float>(elapsed).count() / restoreSeconds);
            const int segment = (std::min)(12, 1 + static_cast<int>(progress * 12.0F));
            if (segment != restoreHoldSegment) {
                restoreHoldSegment = segment;
                renderer.SetPlayspaceHoldProgress(progress);
                if (selectedDeck == 2 && renderer.Initialize(system, selectedDeck, musicLine,
                        musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
        }
        if (shutdownHoldActive) {
            const bool stillHolding = wristUiClick.bState && shutdownButtonHit;
            const auto elapsed = std::chrono::steady_clock::now() - shutdownHoldStarted;
            constexpr auto requiredHold = std::chrono::seconds(3);
            if (!stillHolding) {
                shutdownHoldActive = false;
                shutdownHoldSegment = 0;
                renderer.SetShutdownHoldProgress(0.0F);
                if (selectedDeck == 4 && renderer.Initialize(system, selectedDeck, musicLine,
                        musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            } else if (elapsed >= requiredHold) {
                renderer.SetShutdownHoldProgress(1.0F);
                running = false;
            } else {
                const float progress = std::chrono::duration<float>(elapsed).count() / 3.0F;
                const int segment = (std::min)(12,
                    1 + static_cast<int>(progress * 12.0F));
                if (segment != shutdownHoldSegment) {
                    shutdownHoldSegment = segment;
                    renderer.SetShutdownHoldProgress(progress);
                    if (selectedDeck == 4 && renderer.Initialize(system, selectedDeck, musicLine,
                            musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                        const auto updatedTexture = renderer.Texture();
                        vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                    }
                }
            }
        }
        if (rigResetHoldActive) {
            const bool fullReset = rigResetKind == std::wstring_view(L"full");
            const bool stillHolding = wristUiClick.bState
                && (fullReset ? rigFullResetHit : (rigMountResetHit && mountReady));
            const auto elapsed = std::chrono::steady_clock::now() - rigResetHoldStarted;
            if (!stillHolding) {
                rigResetHoldActive = false;
                rigResetHoldCompleted = false;
                rigResetHoldSegment = 0;
                rigResetKind = nullptr;
                renderer.SetRigHoldProgress(0.0F, 0.0F);
                if (selectedDeck == 3 && renderer.Initialize(system, selectedDeck, musicLine,
                        musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            } else if (!rigResetHoldCompleted && elapsed >= std::chrono::seconds(1)) {
                rigResetHoldCompleted = true;
                renderer.SetRigHoldProgress(fullReset ? 1.0F : 0.0F, fullReset ? 0.0F : 1.0F);
                LaunchSlimeReset(projectRoot, rigResetKind);
                std::cout << "SlimeVR " << (fullReset ? "full" : "mounting")
                          << " reset requested\n";
                if (renderer.Initialize(system, selectedDeck, musicLine, musicArtPath.wstring(),
                        rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            } else if (!rigResetHoldCompleted) {
                const float progress = std::chrono::duration<float>(elapsed).count();
                const int segment = (std::min)(12,
                    1 + static_cast<int>(progress * 12.0F));
                if (segment != rigResetHoldSegment) {
                    rigResetHoldSegment = segment;
                    renderer.SetRigHoldProgress(
                        fullReset ? progress : 0.0F, fullReset ? 0.0F : progress);
                    if (renderer.Initialize(system, selectedDeck, musicLine,
                            musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                        const auto updatedTexture = renderer.Texture();
                        vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                    }
                }
            }
        }

        const auto desiredHand = leftDrag.bState ? DragHand::Left
            : rightDrag.bState                ? DragHand::Right
                                                : DragHand::None;
        if ((dragPreview || temporaryDrag || sessionDrag) && desiredHand != activeDragHand) {
            if (activeDragHand != DragHand::None && desiredHand == DragHand::None) {
                if (temporaryDrag) {
                    restoreBaseline(temporaryBaseline, "temporary drag");
                }
                std::cout << (dragPreview ? "preview" : temporaryDrag ? "temporary" : "session")
                          << " drag released: x=" << previewOffset.x
                          << " y=" << previewOffset.y << " z=" << previewOffset.z << '\n';
            }
            activeDragHand = desiredHand;
            if (activeDragHand != DragHand::None) {
                if (!sessionDrag) {
                    previewOffset = {};
                }
                dragFaulted = false;
                lastDragPosition = ReadControllerPosition(system, activeDragHand);
                if (temporaryDrag) {
                    vr::HmdMatrix34_t baseline{};
                    if (!chaperone->GetWorkingStandingZeroPoseToRawTrackingPose(&baseline)) {
                        std::cerr << "temporary drag refused: no working standing baseline\n";
                        activeDragHand = DragHand::None;
                        lastDragPosition.reset();
                        continue;
                    }
                    temporaryBaseline = baseline;
                }
                if (sessionDrag && !sessionBaseline) {
                    vr::HmdMatrix34_t baseline{};
                    if (!chaperone->GetWorkingStandingZeroPoseToRawTrackingPose(&baseline)) {
                        std::cerr << "session drag refused: no working standing baseline\n";
                        activeDragHand = DragHand::None;
                        lastDragPosition.reset();
                        continue;
                    }
                    sessionBaseline = baseline;
                }
                std::cout << (dragPreview ? "preview" : temporaryDrag ? "temporary" : "session")
                          << " drag started on "
                          << (activeDragHand == DragHand::Left ? "left" : "right") << " hand\n";
            }
        }
        if ((dragPreview || temporaryDrag || sessionDrag)
            && activeDragHand != DragHand::None && !dragFaulted) {
            const auto currentPosition = ReadControllerPosition(system, activeDragHand);
            if (currentPosition && lastDragPosition) {
                previewOffset.x += currentPosition->x - lastDragPosition->x;
                previewOffset.y += currentPosition->y - lastDragPosition->y;
                previewOffset.z += currentPosition->z - lastDragPosition->z;
            }
            lastDragPosition = currentPosition;
            const bool adjustedNow = std::abs(previewOffset.x) > 0.002F
                || std::abs(previewOffset.y) > 0.002F || std::abs(previewOffset.z) > 0.002F;
            if (adjustedNow != playspaceAdjusted) {
                playspaceAdjusted = adjustedNow;
                renderer.SetPlayspaceAdjusted(playspaceAdjusted);
                if (selectedDeck == 2 && renderer.Initialize(system, selectedDeck, musicLine,
                        musicArtPath.wstring(), rigLine, rigSlots, mountReady, desktopPanel)) {
                    const auto updatedTexture = renderer.Texture();
                    vr::VROverlay()->SetOverlayTexture(wristOverlay, &updatedTexture);
                }
            }
            if ((temporaryDrag || sessionDrag) && (temporaryBaseline || sessionBaseline)) {
                // This is a configurable tracking-fault guard, not a fixed
                // assumption about the user's useful playspace travel.
                const float maximumSessionOffsetMeters =
                    ttsSettings.playspaceTravelLimitMeters;
                if (std::abs(previewOffset.x) > maximumSessionOffsetMeters
                    || std::abs(previewOffset.y) > maximumSessionOffsetMeters
                    || std::abs(previewOffset.z) > maximumSessionOffsetMeters) {
                    std::cerr << "session drag cancelled: implausible offset\n";
                    restoreBaseline(temporaryBaseline, "temporary drag");
                    restoreBaseline(sessionBaseline, "session drag");
                    previewOffset = {};
                    playspaceAdjusted = false;
                    renderer.SetPlayspaceAdjusted(false);
                    dragFaulted = true;
                } else {
                    const auto& baseline = sessionDrag ? *sessionBaseline : *temporaryBaseline;
                    const auto previewOrigin = TranslateOrigin(baseline, previewOffset);
                    chaperone->SetWorkingStandingZeroPoseToRawTrackingPose(&previewOrigin);
                    chaperone->ShowWorkingSetPreview();
                }
            }
            if (std::chrono::steady_clock::now() - lastPreviewLog > std::chrono::milliseconds(250)) {
                std::cout << (dragPreview ? "preview" : temporaryDrag ? "temporary" : "session")
                          << " offset: x=" << previewOffset.x
                          << " y=" << previewOffset.y << " z=" << previewOffset.z << '\n';
                lastPreviewLog = std::chrono::steady_clock::now();
            }
        }

        vr::VREvent_t event{};
        while (system->PollNextEvent(&event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) {
                running = false;
            }
        }
        desktopSurfaces.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    restoreBaseline(temporaryBaseline, "temporary drag shutdown");
    restoreBaseline(sessionBaseline, "Interfayce shutdown");
    if (rigLegProfileTask.valid()) {
        rigLegProfile = rigLegProfileTask.get();
    }
    if (slimeAvailable && rigLegProfile == interfayce::RigLegProfile::Play) {
        std::cout << "Restoring SlimeVR CONFIG leg profile before shutdown\n";
        const auto restored = RunSlimeLegProfile(projectRoot, L"config");
        if (restored != interfayce::RigLegProfile::Config) {
            std::cerr << "Could not verify CONFIG leg profile during shutdown\n";
        }
    }
    broadcast.Stop();
    desktopSurfaces.Shutdown();
    LocalHttpRequest("POST", "/shutdown", std::chrono::seconds(2));
    if (restartRequested) {
        const auto serviceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < serviceDeadline
               && IsLocalTcpPortOpen(kVoiceServicePort, std::chrono::milliseconds(50))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    vr::VROverlay()->DestroyOverlay(wristOverlay);
    vr::VROverlay()->DestroyOverlay(cursorOverlay);
    vr::VROverlay()->DestroyOverlay(leftCursorOverlay);
    if (shutdownEvent != nullptr) CloseHandle(shutdownEvent);
    tray.Shutdown();
    vr::VR_Shutdown();
    CoUninitialize();
    if (restartRequested && !RelaunchInterfayce(std::filesystem::absolute(argv[0]), directory)) {
        return 1;
    }
    return 0;
}
