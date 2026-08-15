#include "battery_estimator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace interfayce {
namespace {
constexpr int kCriticalPercent = 10;
constexpr auto kMinimumLearningTime = std::chrono::minutes(15);
constexpr int kMinimumLearningDrop = 2;
constexpr auto kMaximumSampleAge = std::chrono::hours(3);
constexpr double kMinimumPlausibleRate = 0.1;
constexpr double kMaximumPlausibleRate = 30.0;
}

BatteryRuntimeEstimator::BatteryRuntimeEstimator(std::filesystem::path persistencePath)
    : persistencePath_(std::move(persistencePath)) {
    Load();
}

double BatteryRuntimeEstimator::SessionRate(const DeviceState& state) {
    if (state.samples.size() < 2) return 0.0;
    const auto& first = state.samples.front();
    const auto& last = state.samples.back();
    const auto elapsed = last.time - first.time;
    const int drop = first.percent - last.percent;
    if (elapsed < kMinimumLearningTime || drop < kMinimumLearningDrop) return 0.0;
    const double hours = std::chrono::duration<double, std::ratio<3600>>(elapsed).count();
    const double rate = drop / hours;
    return rate >= kMinimumPlausibleRate && rate <= kMaximumPlausibleRate ? rate : 0.0;
}

bool BatteryRuntimeEstimator::Observe(
        const std::string& device, int percent, Clock::time_point now) {
    auto& state = devices_[device];
    if (percent < 0 || percent > 100) {
        state.connected = false;
        state.percent = -1;
        state.samples.clear();
        return false;
    }

    state.connected = true;
    if (state.percent < 0) {
        state.percent = percent;
        state.samples = {{now, percent}};
        return false;
    }
    if (percent == state.percent) return false;

    const auto elapsedSinceLast = state.samples.empty()
        ? Clock::duration::zero() : now - state.samples.back().time;
    const int dropSinceLast = state.percent - percent;
    // Charging, percentage rebound, or a large implausible short jump starts a
    // fresh observation window without poisoning the historical rate.
    if (percent > state.percent
        || (dropSinceLast > 5 && elapsedSinceLast < std::chrono::minutes(10))) {
        state.percent = percent;
        state.samples = {{now, percent}};
        return false;
    }

    state.percent = percent;
    state.samples.push_back({now, percent});
    while (state.samples.size() > 2
        && now - state.samples.front().time > kMaximumSampleAge) {
        state.samples.erase(state.samples.begin());
    }

    const double sessionRate = SessionRate(state);
    if (sessionRate <= 0.0) return false;
    state.learnedPercentPerHour = state.learnedPercentPerHour > 0.0
        ? state.learnedPercentPerHour * 0.8 + sessionRate * 0.2
        : sessionRate;
    return true;
}

BatteryEstimate BatteryRuntimeEstimator::Estimate() const {
    BatteryEstimate result;
    double lowestMinutes = std::numeric_limits<double>::infinity();
    bool hasUsableRate = false;
    for (const auto& [name, state] : devices_) {
        if (!state.connected || state.percent < 0) continue;
        result.status = BatteryEstimateStatus::Ready;
        result.lowestPercent = result.lowestPercent < 0
            ? state.percent : (std::min)(result.lowestPercent, state.percent);
        const double sessionRate = SessionRate(state);
        const double rate = sessionRate > 0.0 ? sessionRate : state.learnedPercentPerHour;
        if (rate <= 0.0) continue;
        hasUsableRate = true;
        const double minutes = state.percent <= kCriticalPercent
            ? 0.0 : (state.percent - kCriticalPercent) * 60.0 / rate;
        if (minutes < lowestMinutes) {
            lowestMinutes = minutes;
            result.limitingDevice = name;
        }
    }
    if (result.status == BatteryEstimateStatus::None) return result;
    // A newly seen or slow-draining tracker should not hide useful estimates
    // from devices whose discharge rates are already known. Keep learning it
    // in the background and use every rate that is currently trustworthy.
    if (!hasUsableRate || !std::isfinite(lowestMinutes)) {
        result.status = BatteryEstimateStatus::Learning;
        return result;
    }
    result.minutesRemaining = (std::max)(0, static_cast<int>(std::lround(lowestMinutes / 5.0) * 5));
    return result;
}

void BatteryRuntimeEstimator::Load() {
    std::ifstream input(persistencePath_);
    std::string line;
    while (std::getline(input, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab > 64) continue;
        try {
            const double rate = std::stod(line.substr(tab + 1));
            if (rate >= kMinimumPlausibleRate && rate <= kMaximumPlausibleRate) {
                devices_[line.substr(0, tab)].learnedPercentPerHour = rate;
            }
        } catch (...) {
        }
    }
}

bool BatteryRuntimeEstimator::Save() const {
    std::error_code ignored;
    std::filesystem::create_directories(persistencePath_.parent_path(), ignored);
    std::ofstream output(persistencePath_, std::ios::trunc);
    if (!output) return false;
    output << "# Interfayce battery discharge rates (% per hour)\n";
    output << std::fixed << std::setprecision(4);
    for (const auto& [name, state] : devices_) {
        if (state.learnedPercentPerHour > 0.0) {
            output << name << '\t' << state.learnedPercentPerHour << '\n';
        }
    }
    return output.good();
}

std::wstring FormatBatteryEstimate(const BatteryEstimate& estimate) {
    if (estimate.status == BatteryEstimateStatus::None) return L"";
    if (estimate.status == BatteryEstimateStatus::Learning) return L"LEARN";
    const int minutes = (std::max)(0, estimate.minutesRemaining);
    if (minutes < 60) return L"~" + std::to_wstring(minutes) + L"M";
    const int hours = minutes / 60;
    const int remainder = minutes % 60;
    std::wostringstream formatted;
    formatted << L'~' << hours << L'H';
    if (remainder > 0) formatted << std::setw(2) << std::setfill(L'0') << remainder;
    return formatted.str();
}

}  // namespace interfayce
