#include "desktop_input_router.h"
#include "desktop_coordinates.h"
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace interfayce {
std::optional<POINT> DesktopPointForHit(const interfayce::DesktopSource& source, float u, float v) {
    PhysicalDesktopCoordinates physicalCoordinates;
    RECT bounds{};
    if (source.kind == interfayce::DesktopSource::Kind::Display) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (source.monitor == nullptr || !GetMonitorInfoW(source.monitor, &info)) return std::nullopt;
        bounds = info.rcMonitor;
    } else {
        if (source.window == nullptr || !IsWindow(source.window)) return std::nullopt;
        if (FAILED(DwmGetWindowAttribute(source.window, DWMWA_EXTENDED_FRAME_BOUNDS,
                &bounds, sizeof(bounds))) && !GetWindowRect(source.window, &bounds)) return std::nullopt;
    }
    const auto width = (std::max)(bounds.right - bounds.left, 1L);
    const auto height = (std::max)(bounds.bottom - bounds.top, 1L);
    return POINT{
        bounds.left + static_cast<LONG>(std::lround(std::clamp(u, 0.0F, 1.0F) * (width - 1))),
        bounds.top + static_cast<LONG>(std::lround((1.0F - std::clamp(v, 0.0F, 1.0F)) * (height - 1))),
    };
}

void PrepareWindowForInput(HWND window) {
    PhysicalDesktopCoordinates physicalCoordinates;
    if (window == nullptr || !IsWindow(window)) return;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
}

HWND CapturedChildAtPoint(HWND rootWindow, const POINT screenPoint) {
    PhysicalDesktopCoordinates physicalCoordinates;
    if (rootWindow == nullptr || !IsWindow(rootWindow)) return nullptr;

    // Deliberately walk from the captured root rather than using WindowFromPoint.
    // WindowFromPoint follows the real desktop Z-order, so an unrelated window
    // covering this coordinate can steal hit-testing from an otherwise valid
    // captured surface.
    HWND target = rootWindow;
    while (true) {
        POINT clientPoint = screenPoint;
        if (!ScreenToClient(target, &clientPoint)) break;
        const auto child = ChildWindowFromPointEx(target, clientPoint,
            CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (child == nullptr || child == target) break;
        target = child;
    }
    return target;
}

bool InjectDesktopPointer(const POINT point, interfayce::DesktopPointerEvent event) {
    PhysicalDesktopCoordinates physicalCoordinates;
    const auto virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const auto virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const auto virtualWidth = (std::max)(GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1, 1);
    const auto virtualHeight = (std::max)(GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1, 1);
    INPUT movement{};
    movement.type = INPUT_MOUSE;
    movement.mi.dx = static_cast<LONG>(std::lround(
        static_cast<double>(point.x - virtualLeft) * 65535.0 / virtualWidth));
    movement.mi.dy = static_cast<LONG>(std::lround(
        static_cast<double>(point.y - virtualTop) * 65535.0 / virtualHeight));
    movement.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK
        | MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
    if (event == interfayce::DesktopPointerEvent::Move) {
        return SendInput(1, &movement, sizeof(movement)) == 1;
    }
    INPUT button{};
    button.type = INPUT_MOUSE;
    switch (event) {
    case interfayce::DesktopPointerEvent::PrimaryDown:
        button.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        break;
    case interfayce::DesktopPointerEvent::PrimaryUp:
        button.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        break;
    case interfayce::DesktopPointerEvent::SecondaryDown:
        button.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        break;
    case interfayce::DesktopPointerEvent::SecondaryUp:
        button.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        break;
    case interfayce::DesktopPointerEvent::Move:
        return SendInput(1, &movement, sizeof(movement)) == 1;
    }
    std::array<INPUT, 2> inputs{movement, button};
    return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT))
        == inputs.size();
}

bool InjectWindowPointer(HWND rootWindow, const POINT screenPoint,
                         interfayce::DesktopPointerEvent event, HWND& primaryTarget, HWND& secondaryTarget) {
    PhysicalDesktopCoordinates physicalCoordinates;
    if (rootWindow == nullptr || !IsWindow(rootWindow)) return false;
    // Some retained-mode/web UI frameworks validate posted client messages
    // against the global cursor position. Keep both coordinate spaces aligned.
    SetCursorPos(screenPoint.x, screenPoint.y);
    const bool rightEvent = event == interfayce::DesktopPointerEvent::SecondaryDown || event == interfayce::DesktopPointerEvent::SecondaryUp;
    HWND target = rightEvent ? secondaryTarget : primaryTarget;
    if (!target) target = CapturedChildAtPoint(rootWindow, screenPoint);
    if (!IsWindow(target) || (target != rootWindow && !IsChild(rootWindow, target))) return false;
    if (target == nullptr) return false;
    const auto mappedPoint = interfayce::PhysicalClientPoint(target, screenPoint);
    if (!mappedPoint) return false;
    const POINT clientPoint = *mappedPoint;
    const LPARAM coordinates = MAKELPARAM(
        static_cast<short>(clientPoint.x), static_cast<short>(clientPoint.y));
    const bool primary = event == interfayce::DesktopPointerEvent::PrimaryDown
        || event == interfayce::DesktopPointerEvent::PrimaryUp;
    const bool secondary = event == interfayce::DesktopPointerEvent::SecondaryDown
        || event == interfayce::DesktopPointerEvent::SecondaryUp;
    const bool released = event == interfayce::DesktopPointerEvent::PrimaryUp
        || event == interfayce::DesktopPointerEvent::SecondaryUp;
    WPARAM buttons = (primaryTarget ? MK_LBUTTON : 0) | (secondaryTarget ? MK_RBUTTON : 0);
    if (primary) buttons = released ? (buttons & ~MK_LBUTTON) : (buttons | MK_LBUTTON);
    if (secondary) buttons = released ? (buttons & ~MK_RBUTTON) : (buttons | MK_RBUTTON);
    if (!PostMessageW(target, WM_MOUSEMOVE,
            event == interfayce::DesktopPointerEvent::Move ? buttons : 0, coordinates)) {
        return false;
    }
    const UINT message = event == interfayce::DesktopPointerEvent::PrimaryDown
        ? WM_LBUTTONDOWN : event == interfayce::DesktopPointerEvent::PrimaryUp
            ? WM_LBUTTONUP : event == interfayce::DesktopPointerEvent::SecondaryDown
                ? WM_RBUTTONDOWN : event == interfayce::DesktopPointerEvent::SecondaryUp
                    ? WM_RBUTTONUP : 0;
    const bool sent = message == 0 || PostMessageW(target, message, buttons, coordinates) != FALSE;
    if (sent && primary) primaryTarget = released ? nullptr : target;
    if (sent && secondary) secondaryTarget = released ? nullptr : target;
    return sent;
}

bool InjectDesktopScroll(const POINT point, int32_t verticalDelta, int32_t horizontalDelta) {
    if (!InjectDesktopPointer(point, interfayce::DesktopPointerEvent::Move)) return false;
    std::array<INPUT, 2> inputs{};
    UINT count = 0;
    if (verticalDelta != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[count].mi.mouseData = static_cast<DWORD>(verticalDelta);
        ++count;
    }
    if (horizontalDelta != 0) {
        inputs[count].type = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        inputs[count].mi.mouseData = static_cast<DWORD>(horizontalDelta);
        ++count;
    }
    return count == 0 || SendInput(count, inputs.data(), sizeof(INPUT)) == count;
}

bool DesktopInputRouter::SendPointer(const DesktopSource& source, POINT stable, DesktopPointerEvent event) {
    lastPointerPoint = stable;
    if (source.kind == DesktopSource::Kind::Window) {
        return InjectWindowPointer(source.window, stable, event, primaryTarget, secondaryTarget);
    }
    const bool sent = InjectDesktopPointer(stable, event);
    if (sent) {
        if (event == DesktopPointerEvent::PrimaryDown) primaryInjected = true;
        if (event == DesktopPointerEvent::SecondaryDown) secondaryInjected = true;
        if (event == DesktopPointerEvent::PrimaryUp) primaryInjected = false;
        if (event == DesktopPointerEvent::SecondaryUp) secondaryInjected = false;
    }
    return sent;
}

bool DesktopInputRouter::SendScroll(const DesktopSource& source, POINT point, int32_t verticalDelta, int32_t horizontalDelta) {
    if (source.kind == DesktopSource::Kind::Window) {
        const HWND target = CapturedChildAtPoint(source.window, point);
        if (!IsWindow(target)) return false;
        const auto coordinates = MAKELPARAM(static_cast<short>(point.x), static_cast<short>(point.y));
        const WPARAM buttons = (primaryTarget ? MK_LBUTTON : 0) | (secondaryTarget ? MK_RBUTTON : 0);
        const bool vertical = !verticalDelta || PostMessageW(target, WM_MOUSEWHEEL, MAKEWPARAM(buttons, static_cast<short>(verticalDelta)), coordinates);
        const bool horizontal = !horizontalDelta || PostMessageW(target, WM_MOUSEHWHEEL, MAKEWPARAM(buttons, static_cast<short>(horizontalDelta)), coordinates);
        return vertical && horizontal;
    }
    return InjectDesktopScroll(point, verticalDelta, horizontalDelta);
}

void DesktopInputRouter::Release(const DesktopSource* source) {
    if (source && source->kind == DesktopSource::Kind::Window) {
        if (primaryTarget) InjectWindowPointer(source->window, lastPointerPoint, DesktopPointerEvent::PrimaryUp, primaryTarget, secondaryTarget);
        if (secondaryTarget) InjectWindowPointer(source->window, lastPointerPoint, DesktopPointerEvent::SecondaryUp, primaryTarget, secondaryTarget);
    }
    INPUT release{};
    release.type = INPUT_MOUSE;
    release.mi.dwFlags = (primaryInjected ? MOUSEEVENTF_LEFTUP : 0) | (secondaryInjected ? MOUSEEVENTF_RIGHTUP : 0);
    if (release.mi.dwFlags) SendInput(1, &release, sizeof(release));
    primaryTarget = secondaryTarget = nullptr;
    primaryInjected = secondaryInjected = false;
}
} // namespace interfayce
