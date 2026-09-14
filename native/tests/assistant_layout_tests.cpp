#include "assistant_panel_layout.h"

using interfayce::assistant_panel::Choice;
// The old circular target missed this point inside the visible Decline button.
static_assert(Choice(1).Contains(325, 286));
static_assert(Choice(0).Contains(145, 261));
static_assert(Choice(2).Contains(623, 313));
static_assert(!Choice(1).Contains(300, 286));
static_assert(!Choice(1).Contains(325, 320));
int main() { return 0; }
