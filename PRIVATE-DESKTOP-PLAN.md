# Private desktop windows in VR

Implemented in 1.2.17 and retained in 1.2.19.

Keep in VR parks a captured app on a recognized extended virtual monitor. Return
to desktop restores it visibly. Automatic recovery returns it minimized to a
physical screen. This is casual desktop privacy, not complete isolation.

Original placement is recorded durably before moving the window. Process creation
time and per-window markers guard against reused window handles. An independent
helper watches the parent, heartbeat and destination. Recovery clamps placement
to a physical work area and retains records on failure.

Parking requires one unambiguous virtual destination and a physical recovery
screen. Private displays are excluded from the normal picker. The display stays
attached on exit; its driver is managed separately.

Tests cover placement, records, stale identities, corrupt-record retention,
workspace coordinates, minimized return and an independent missing-display
watchdog. Live parking and minimized return passed on MolotovCherry v0.3.1 with
NVIDIA 616.92. Unexpected popups/display loss prevent a zero-exposure guarantee.

See RECOVERY.md and packaging/PRIVATE-DISPLAY-SETUP.md.
