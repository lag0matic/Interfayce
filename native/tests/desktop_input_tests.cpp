#include "desktop_input_router.h"
#include "desktop_coordinates.h"
#include <iostream>
#include <vector>

namespace {
struct Event { HWND target; UINT message; WPARAM buttons; };
std::vector<Event> events;
LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
        events.push_back({window, message, wparam});
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void Drain() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
}
bool Saw(HWND target, UINT message, WPARAM flags = 0) {
    for (const auto& event : events)
        if (event.target == target && event.message == message
            && (event.buttons & flags) == flags) return true;
    return false;
}
}

int main() {
    interfayce::PhysicalDesktopCoordinates physical;
    POINT cursor{};
    GetCursorPos(&cursor);
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProcedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"InterfayceInputRegression";
    RegisterClassW(&type);
    const HWND root = CreateWindowW(type.lpszClassName, L"", WS_POPUP,
        20, 20, 400, 300, nullptr, nullptr, type.hInstance, nullptr);
    const HWND first = CreateWindowW(type.lpszClassName, L"", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100, root, nullptr, type.hInstance, nullptr);
    const HWND second = CreateWindowW(type.lpszClassName, L"", WS_CHILD | WS_VISIBLE,
        150, 0, 100, 100, root, nullptr, type.hInstance, nullptr);
    if (!root || !first || !second) return 2;
    POINT press{25, 25}, drag{175, 25};
    ClientToScreen(root, &press);
    ClientToScreen(root, &drag);
    interfayce::DesktopSource source{};
    source.kind = interfayce::DesktopSource::Kind::Window;
    source.window = root;
    interfayce::DesktopInputRouter input;
    using EventType = interfayce::DesktopPointerEvent;
    bool ok = input.SendPointer(source, press, EventType::PrimaryDown);
    Drain();
    ok = ok && Saw(first, WM_LBUTTONDOWN, MK_LBUTTON);
    events.clear();
    ok = input.SendPointer(source, drag, EventType::Move) && ok;
    Drain();
    ok = ok && Saw(first, WM_MOUSEMOVE, MK_LBUTTON) && !Saw(second, WM_MOUSEMOVE);
    events.clear();
    input.Release(&source);
    Drain();
    ok = ok && Saw(first, WM_LBUTTONUP) && !Saw(second, WM_LBUTTONUP);
    events.clear();
    ok = input.SendScroll(source, drag, 120, -120) && ok;
    Drain();
    ok = ok && Saw(second, WM_MOUSEWHEEL) && Saw(second, WM_MOUSEHWHEEL)
        && !Saw(first, WM_MOUSEWHEEL);
    events.clear();
    input.Release(&source);
    Drain();
    ok = ok && !Saw(first, WM_LBUTTONUP);
    DestroyWindow(root);
    SetCursorPos(cursor.x, cursor.y);
    std::cout << (ok ? "Desktop input ownership, teardown and wheel targeting passed\n"
                     : "Desktop input regression failed\n");
    return ok ? 0 : 1;
}
