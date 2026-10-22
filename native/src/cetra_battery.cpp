#include "cetra_battery.h"
#include <Windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>
#include <string>

namespace interfayce {
namespace {
struct HandleCloser { void operator()(void* h) const { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
using Handle = std::unique_ptr<void, HandleCloser>;
struct DevicesCloser { void operator()(void* h) const { if (h != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(h); } };
using Devices = std::unique_ptr<void, DevicesCloser>;

bool Transfer(HANDLE device, void* data, DWORD size, bool write, DWORD timeout, DWORD& transferred) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) return false;
    OVERLAPPED pending{};
    pending.hEvent = event.get();
    const BOOL started = write ? WriteFile(device, data, size, nullptr, &pending)
                               : ReadFile(device, data, size, nullptr, &pending);
    if (!started && GetLastError() != ERROR_IO_PENDING) return false;
    if (WaitForSingleObject(event.get(), timeout) != WAIT_OBJECT_0) {
        CancelIoEx(device, &pending);
        // Drain cancellation before releasing OVERLAPPED or its data buffer.
        GetOverlappedResult(device, &pending, &transferred, TRUE);
        return false;
    }
    return GetOverlappedResult(device, &pending, &transferred, FALSE) != FALSE;
}

std::vector<unsigned char> Query(HANDLE device, const HIDP_CAPS& caps, unsigned char key) {
    // ASUS Gear Link O701 v1.00.23: report CC, GET 12, key, index 00 00.
    // These are read-only queries; no notification/configuration writes.
    if (caps.OutputReportByteLength < 5 || caps.InputReportByteLength < 8) return {};
    std::vector<unsigned char> request(caps.OutputReportByteLength);
    request[0]=0xcc; request[1]=0x12; request[2]=key;
    DWORD count{};
    if (!Transfer(device, request.data(), static_cast<DWORD>(request.size()), true, 750, count)) return {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
    std::vector<unsigned char> response(caps.InputReportByteLength);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        if (!Transfer(device, response.data(), static_cast<DWORD>(response.size()), false,
                      static_cast<DWORD>((std::max)(1LL, remaining)), count)) return {};
        if (count >= 5 && response[0]==0xcc && response[1]==0x12 && response[2]==key
            && response[3]==0 && response[4]==0)
            return {response.begin()+5, response.begin()+count};
    }
    return {};
}
}
CetraBatteries DecodeCetraBatteries(unsigned char connected,
    std::span<const unsigned char> battery, std::span<const unsigned char> charging) {
    CetraBatteries result{};
    if (connected==0 || connected>3 || battery.size()<3) return result;
    for (int i=0;i<3;++i) {
        if (battery[i]>100 || (i<2 && !(connected&(1<<i)))) continue;
        result[i].percent=battery[i];
        result[i].charging=charging.size()>=2 && battery[i]<100
            && (i==2 ? charging[1]==1 : charging[0]<=3 && (charging[0]&(1<<i))!=0);
    }
    return result;
}
CetraBatteries ReadCetraBatteries() {
    CetraBatteries result{};
    GUID hid{}; HidD_GetHidGuid(&hid);
    Devices devices(SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (devices.get()==INVALID_HANDLE_VALUE) return result;
    for (DWORD index=0;;++index) {
        SP_DEVICE_INTERFACE_DATA info{sizeof(info)};
        if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &hid, index, &info)) break;
        DWORD needed{};
        SetupDiGetDeviceInterfaceDetailW(devices.get(), &info, nullptr, 0, &needed, nullptr);
        if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<unsigned char> storage(needed);
        auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize=sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.get(), &info, detail, needed, nullptr, nullptr)) continue;
        std::wstring path(detail->DevicePath);
        // Avoid opening unrelated input devices.
        if (path.find(L"vid_0b05&pid_1cac") == std::wstring::npos) continue;
        Handle device(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (device.get()==INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes{sizeof(attributes)};
        if (!HidD_GetAttributes(device.get(), &attributes) || attributes.VendorID!=0x0b05 || attributes.ProductID!=0x1cac) continue;
        PHIDP_PREPARSED_DATA parsed{};
        if (!HidD_GetPreparsedData(device.get(), &parsed)) continue;
        HIDP_CAPS caps{};
        const auto status=HidP_GetCaps(parsed, &caps);
        HidD_FreePreparsedData(parsed);
        if (status != HIDP_STATUS_SUCCESS || caps.UsagePage!=0xff00 || caps.Usage!=1) continue;
        auto connected=Query(device.get(), caps, 0x01);
        if (connected.empty() || connected[0]==0 || connected[0]>3) return result;
        auto battery=Query(device.get(), caps, 0x12);
        auto charging=Query(device.get(), caps, 0x11);
        return DecodeCetraBatteries(connected[0], battery, charging);
    }
    return result;
}
}
