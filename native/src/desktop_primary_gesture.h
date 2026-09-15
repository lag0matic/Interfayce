#pragma once
#include "desktop_surface_registry.h"

namespace interfayce {
// Windows exposes one primary mouse button. The first hand to press owns it
// until release, even if the other hand points at another surface meanwhile.
class DesktopPrimaryGesture {
public:
    bool Active() const { return hit_.has_value(); }
    DesktopGrabHand Owner() const { return owner_; }
    template<class Send>
    bool Begin(DesktopGrabHand hand, const DesktopSurfaceHit& hit, Send send) {
        if (hit_ || !hit.captured || !send(hit, DesktopPointerEvent::PrimaryDown)) return false;
        owner_ = hand;
        hit_ = hit;
        return true;
    }
    template<class Send>
    void Update(DesktopGrabHand hand, bool held, const std::optional<DesktopSurfaceHit>& aim, Send send) {
        if (!hit_ || hand != owner_) return;
        if (!held) {
            send(*hit_, DesktopPointerEvent::PrimaryUp);
            hit_.reset();
        } else if (aim && aim->captured && aim->id == hit_->id) {
            hit_ = aim;
            send(*hit_, DesktopPointerEvent::Move);
        }
    }
private:
    DesktopGrabHand owner_{DesktopGrabHand::Right};
    std::optional<DesktopSurfaceHit> hit_;
};
} // namespace interfayce
