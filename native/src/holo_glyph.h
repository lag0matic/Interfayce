#pragma once
#include <Windows.h>
namespace interfayce {
enum class HoloGlyph : UINT32 {
    Music, Comms, Desktop, Playspace, Rig, Settings,
    Previous, Play, Pause, Next, Broadcast, VoiceMic,
    NewSurface, Keyboard, SurfaceStack, Back, BringAll, ReturnPicker,
    Lock, Unlock, BringView, Close, Favorite,
    CommsMic, ClearChat, PlayspaceRestore, RigReset, RigMount, DesktopSettings,
    VolumeDown, VolumeUp, Speaker, Mute, BroadcastGainDown, BroadcastGainUp,
    Shutdown, Copy, Paste,
    Assistant,
};

} // namespace interfayce
