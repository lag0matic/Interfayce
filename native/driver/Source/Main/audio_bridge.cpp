/*++

Interfayce additions are Copyright (c) 2026 David Armstrong.
The surrounding driver is derived from Microsoft's Simple Audio Sample
under the license retained at ../../MICROSOFT-SAMPLE-LICENSE.txt.

--*/

#include "definitions.h"
#include "audio_bridge.h"

namespace {

// Half a second of 48 kHz, stereo, signed 16-bit PCM. Keeping the bridge
// bounded prevents a stalled capture client from accumulating stale music.
constexpr ULONG kBridgeCapacity = 48000 * 2 * sizeof(SHORT) / 2;
BYTE g_BridgeBuffer[kBridgeCapacity]{};
KSPIN_LOCK g_BridgeLock{};
ULONG g_ReadOffset{};
ULONG g_WriteOffset{};
ULONG g_AvailableBytes{};

void CopyIntoRing(const BYTE* source, ULONG byteCount) {
    while (byteCount > 0) {
        const ULONG run = min(byteCount, kBridgeCapacity - g_WriteOffset);
        RtlCopyMemory(g_BridgeBuffer + g_WriteOffset, source, run);
        g_WriteOffset = (g_WriteOffset + run) % kBridgeCapacity;
        source += run;
        byteCount -= run;
    }
}

void CopyOutOfRing(BYTE* destination, ULONG byteCount) {
    while (byteCount > 0) {
        const ULONG run = min(byteCount, kBridgeCapacity - g_ReadOffset);
        RtlCopyMemory(destination, g_BridgeBuffer + g_ReadOffset, run);
        g_ReadOffset = (g_ReadOffset + run) % kBridgeCapacity;
        destination += run;
        byteCount -= run;
    }
}

} // namespace

#pragma code_seg("INIT")
void InterfayceAudioBridgeInitialize() {
    KeInitializeSpinLock(&g_BridgeLock);
    g_ReadOffset = 0;
    g_WriteOffset = 0;
    g_AvailableBytes = 0;
}

#pragma code_seg()
void InterfayceAudioBridgeReset() {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_BridgeLock, &oldIrql);
    g_ReadOffset = 0;
    g_WriteOffset = 0;
    g_AvailableBytes = 0;
    KeReleaseSpinLock(&g_BridgeLock, oldIrql);
}

#pragma code_seg()
void InterfayceAudioBridgeWrite(const BYTE* data, ULONG byteCount) {
    if (data == nullptr || byteCount == 0) return;
    if (byteCount > kBridgeCapacity) {
        data += byteCount - kBridgeCapacity;
        byteCount = kBridgeCapacity;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_BridgeLock, &oldIrql);
    const ULONG freeBytes = kBridgeCapacity - g_AvailableBytes;
    if (byteCount > freeBytes) {
        const ULONG dropped = byteCount - freeBytes;
        g_ReadOffset = (g_ReadOffset + dropped) % kBridgeCapacity;
        g_AvailableBytes -= dropped;
    }
    CopyIntoRing(data, byteCount);
    g_AvailableBytes += byteCount;
    KeReleaseSpinLock(&g_BridgeLock, oldIrql);
}

#pragma code_seg()
void InterfayceAudioBridgeRead(BYTE* data, ULONG byteCount) {
    if (data == nullptr || byteCount == 0) return;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_BridgeLock, &oldIrql);
    const ULONG available = min(byteCount, g_AvailableBytes);
    CopyOutOfRing(data, available);
    g_AvailableBytes -= available;
    KeReleaseSpinLock(&g_BridgeLock, oldIrql);

    if (available < byteCount) {
        RtlZeroMemory(data + available, byteCount - available);
    }
}
