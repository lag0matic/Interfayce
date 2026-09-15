#include "desktop_input_blocking.h"
#include <iostream>

int main() {
    using interfayce::InputBlockOwnership;
    InputBlockOwnership left, right;
    bool ok = !right.Update(false, true, true, true, false);
    ok &= !right.Update(true, false, true, true, false);
    ok &= right.Update(true, true, true, true, false);
    ok &= !left.Update(true, true, true, false, false);
    ok &= right.Update(true, true, true, false, true); // drag beyond window
    ok &= right.Update(false, true, true, false, true); // toggle off while held
    ok &= !right.Update(false, true, true, false, false); // released
    ok &= !right.Update(true, true, true, false, true); // press started in game
    right.Update(true, true, true, true, true);
    ok &= !right.Update(true, true, false, false, true); // tracking lost
    right.Update(true, true, true, true, true);
    ok &= !right.Update(true, false, true, true, true); // support disabled
    static_assert(interfayce::DesktopBlockButton::Contains(450, 360));
    static_assert(!interfayce::DesktopBlockButton::Contains(450, 330));
    std::cout << (ok ? "Input blocking ownership tests passed\n" : "Input blocking tests failed\n");
    return ok ? 0 : 1;
}
