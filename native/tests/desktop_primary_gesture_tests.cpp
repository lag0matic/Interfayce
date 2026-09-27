#include "desktop_primary_gesture.h"
#include <iostream>
#include <vector>

int main() {
    using namespace interfayce;
    struct Sent { uint64_t id; DesktopPointerEvent event; float u; };
    std::vector<Sent> sent;
    const auto send = [&](const DesktopSurfaceHit& hit, DesktopPointerEvent event) {
        sent.push_back({hit.id, event, hit.u}); return true;
    };
    DesktopSurfaceHit first{}, other{};
    first.id = 1; first.captured = true; first.u = 0.2F;
    other.id = 2; other.captured = true;
    DesktopPrimaryGesture gesture;
    bool ok = gesture.Begin(DesktopGrabHand::Left, first, send);
    ok &= !gesture.Begin(DesktopGrabHand::Right, other, send);
    gesture.Update(DesktopGrabHand::Right, false, other, send);
    ok &= gesture.Active() && sent.size() == 1;
    first.u = 0.7F;
    gesture.Update(DesktopGrabHand::Left, true, first, send);
    gesture.Update(DesktopGrabHand::Left, true, other, send);
    ok &= sent.size() == 2 && sent.back().u == 0.7F;
    gesture.Update(DesktopGrabHand::Left, false, std::nullopt, send);
    ok &= !gesture.Active() && sent.back().event == DesktopPointerEvent::PrimaryUp
        && sent.back().id == 1 && sent.back().u == 0.7F;
    ok &= gesture.Begin(DesktopGrabHand::Right, other, send);
    gesture.Update(DesktopGrabHand::Right, false, std::nullopt, send);
    ok &= !gesture.Active() && sent.back().id == 2;
    ok &= !gesture.Begin(DesktopGrabHand::Left, first,
        [](const auto&, auto) { return false; });
    ok &= !gesture.Active();
    std::cout << (ok ? "Both-hand primary ownership, drag, release and failed press passed\n"
                     : "Primary gesture regression failed\n");
    return ok ? 0 : 1;
}
