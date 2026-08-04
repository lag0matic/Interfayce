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
