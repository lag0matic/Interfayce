#include "battery_estimator.h"

#include <cassert>
#include <chrono>
#include <filesystem>

int main() {
    using namespace std::chrono_literals;
    using interfayce::BatteryEstimateStatus;
    using interfayce::BatteryRuntimeEstimator;
    const auto path = std::filesystem::temp_directory_path()
        / "interfayce-battery-estimator-test.tsv";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    const auto start = BatteryRuntimeEstimator::Clock::time_point{};
    BatteryRuntimeEstimator estimator(path);
    assert(estimator.Estimate().status == BatteryEstimateStatus::None);
    estimator.Observe("Left hand", 100, start);
    assert(estimator.Estimate().status == BatteryEstimateStatus::Learning);
    estimator.Observe("Left hand", 99, start + 10min);
    assert(estimator.Estimate().status == BatteryEstimateStatus::Learning);
    assert(estimator.Observe("Left hand", 97, start + 30min));
    auto estimate = estimator.Estimate();
    assert(estimate.status == BatteryEstimateStatus::Ready);
    assert(estimate.minutesRemaining == 870);
    assert(interfayce::FormatBatteryEstimate(estimate) == L"~14H30");
    assert(estimator.Save());

    // A saved baseline is immediately useful in a later session.
    BatteryRuntimeEstimator reloaded(path);
    reloaded.Observe("Left hand", 70, start);
    assert(reloaded.Estimate().status == BatteryEstimateStatus::Ready);
    // A newly connected device can learn in the background without hiding an
    // estimate based on another device's persisted rate.
    reloaded.Observe("Chest", 90, start);
    estimate = reloaded.Estimate();
    assert(estimate.status == BatteryEstimateStatus::Ready);
    assert(estimate.minutesRemaining == 600);
    assert(estimate.limitingDevice == "Left hand");

    // Once the new device has a usable rate, it participates normally and can
    // become the limiting battery for the combined estimate.
    assert(reloaded.Observe("Chest", 84, start + 30min));
    estimate = reloaded.Estimate();
    assert(estimate.status == BatteryEstimateStatus::Ready);
    assert(estimate.minutesRemaining == 370);
    assert(estimate.limitingDevice == "Chest");

    std::filesystem::remove(path, ignored);
    return 0;
}
