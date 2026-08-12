#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace interfayce {

enum class BatteryEstimateStatus {
    None,
    Learning,
    Ready,
};

struct BatteryEstimate {
    BatteryEstimateStatus status{BatteryEstimateStatus::None};
    int minutesRemaining{-1};
    int lowestPercent{-1};
    std::string limitingDevice;
};

class BatteryRuntimeEstimator {
public:
    using Clock = std::chrono::steady_clock;

    explicit BatteryRuntimeEstimator(std::filesystem::path persistencePath);

    // Returns true when a learned rate changed and should be persisted.
    bool Observe(const std::string& device, int percent, Clock::time_point now);
    BatteryEstimate Estimate() const;
    bool Save() const;

private:
    struct Sample {
        Clock::time_point time;
        int percent{};
    };

    struct DeviceState {
        bool connected{};
        int percent{-1};
        double learnedPercentPerHour{};
        std::vector<Sample> samples;
    };

    static double SessionRate(const DeviceState& state);
    void Load();

    std::filesystem::path persistencePath_;
    std::unordered_map<std::string, DeviceState> devices_;
};

std::wstring FormatBatteryEstimate(const BatteryEstimate& estimate);

}  // namespace interfayce

