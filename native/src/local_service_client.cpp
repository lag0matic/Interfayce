#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <vector>
#include "local_service_client.h"
namespace interfayce {
namespace {
constexpr uint16_t kVoiceServicePort = 43817;
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

}
std::optional<std::string> LocalHttpRequest(std::string_view method, std::string_view path,
                                            std::chrono::milliseconds timeout,
                                            std::string_view body) {
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

}
