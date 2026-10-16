#pragma once
#include <array>
#include <span>
namespace interfayce {
struct CetraBattery {
    int percent{-1};
    bool charging{};
    bool operator==(const CetraBattery&) const = default;
};
using CetraBatteries = std::array<CetraBattery, 3>;
CetraBatteries DecodeCetraBatteries(unsigned char connected,
    std::span<const unsigned char> battery, std::span<const unsigned char> charging);
// Left, right, case. Missing/disconnected/invalid readings remain -1.
CetraBatteries ReadCetraBatteries();
}
