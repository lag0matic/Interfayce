from interfayce.battery_alerts import BatteryAlertMonitor


def test_announces_threshold_crossings_once_and_rearms_after_recovery():
    monitor = BatteryAlertMonitor()
    assert monitor.observe({"Right hand": 100}) is None
    assert monitor.observe({"Right hand": 20}) == "Right hand battery 20 percent."
    assert monitor.observe({"Right hand": 19}) is None
    assert monitor.observe({"Right hand": 10}) == "Right hand battery critical."
    assert monitor.observe({"Right hand": 9}) is None
    assert monitor.observe({"Right hand": 30}) is None
    assert monitor.observe({"Right hand": 20}) == "Right hand battery 20 percent."


def test_combines_simultaneous_alerts_for_the_single_item_speech_queue():
    monitor = BatteryAlertMonitor()
    assert monitor.observe({"Chest": 8, "Left foot": 20}) == (
        "Chest battery critical. Left foot battery 20 percent."
    )


def test_threshold_jitter_does_not_repeat_low_or_critical_alerts():
    monitor = BatteryAlertMonitor()
    assert monitor.observe({"Left thigh": 20}) == "Left thigh battery 20 percent."
    assert monitor.observe({"Left thigh": 21}) is None
    assert monitor.observe({"Left thigh": 19}) is None
    assert monitor.observe({"Left thigh": 22}) is None
    assert monitor.observe({"Left thigh": 10}) == "Left thigh battery critical."
    assert monitor.observe({"Left thigh": 11}) is None
    assert monitor.observe({"Left thigh": 9}) is None


def test_critical_first_does_not_emit_a_later_low_warning_without_recovery():
    monitor = BatteryAlertMonitor()
    assert monitor.observe({"Hip": 8}) == "Hip battery critical."
    assert monitor.observe({"Hip": 18}) is None
    assert monitor.observe({"Hip": 20}) is None
