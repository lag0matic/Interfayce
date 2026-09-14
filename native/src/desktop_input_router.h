#pragma once
#include "desktop_capture.h"
#include <cstdint>
#include <optional>

namespace interfayce {
enum class DesktopPointerEvent {
    Move,
    PrimaryDown,
    PrimaryUp,
    SecondaryDown,
    SecondaryUp,
};
// Physical coordinates and window ownership are shared by pointer and wheel input.
std::optional<POINT> DesktopPointForHit(const DesktopSource& source, float u, float v);
void PrepareWindowForInput(HWND window);
HWND CapturedChildAtPoint(HWND rootWindow, POINT screenPoint);
bool InjectDesktopPointer(POINT point, DesktopPointerEvent event);
bool InjectWindowPointer(HWND rootWindow, POINT screenPoint, DesktopPointerEvent event,
                         HWND& primaryTarget, HWND& secondaryTarget);
bool InjectDesktopScroll(POINT point, int32_t verticalDelta, int32_t horizontalDelta);
// Each surface owns its button targets until release or teardown.
class DesktopInputRouter {
public:
    bool SendPointer(const DesktopSource& source, POINT stable, DesktopPointerEvent event);
    bool SendScroll(const DesktopSource& source, POINT point, int32_t verticalDelta, int32_t horizontalDelta);
    void Release(const DesktopSource* source);
private:
    HWND primaryTarget{}, secondaryTarget{};
    bool primaryInjected{}, secondaryInjected{};
    POINT lastPointerPoint{};
};
} // namespace interfayce
