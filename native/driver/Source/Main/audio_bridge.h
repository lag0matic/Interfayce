/*++

Interfayce additions are Copyright (c) 2026 David Armstrong.
The surrounding driver is derived from Microsoft's Simple Audio Sample
under the license retained at ../../MICROSOFT-SAMPLE-LICENSE.txt.

--*/

#pragma once

void InterfayceAudioBridgeInitialize();
void InterfayceAudioBridgeReset();
void InterfayceAudioBridgeWrite(_In_reads_bytes_(byteCount) const BYTE* data, ULONG byteCount);
void InterfayceAudioBridgeRead(_Out_writes_bytes_(byteCount) BYTE* data, ULONG byteCount);
