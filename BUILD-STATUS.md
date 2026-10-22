# Interfayce build status

## Built 1.2.41 - Cetra Open Wireless battery readout

Compact left/right/case readout on MUSIC and labelled readings on RIG. Both
share one asynchronous USB HID poll at startup and every THREE MINUTES, per
user preference. No browser or Gear Link dependency. After initial detection,
unavailable readings show --. Charging green, <=20% yellow, <=10% red. Tracker
runtime estimates and voice alerts are unchanged.

Live receiver verified: VID 0B05 PID 1CAC, usage FF00/1. Direct query returned
left 93%, right 66%, case 13% with case charging. ASUS Gear Link O701 v1.00.23
public plugin defines report CC, GET 12 with index 00 00: connection key 01,
battery key 12, charging key 11. Only these read-only queries are sent. Shared
handles, background I/O, timeout/cancellation cleanup, invalid values rejected.
Source: https://gearlink.asus.com/view/7340/manifest.json
Case information may be the receiver's last reported case state. Other Cetra
models and Bluetooth-only connections are unsupported.

Validation: 249 Python tests passed, 2 skipped; native battery tests passed for
charging/full/empty, partial disconnect, malformed and unknown readings. Release
build passed. Production MUSIC/RIG renders checked with Slime online/offline.
Hardware probe did not change playback. Installer 1.2.41 built; not installed
or restarted during this pass. No GitHub update requested for this addition.
Installer SHA256: B1031A7324A2155902FF64B6DFCCC972F764ED6FAA6F6F94CDEF3D590C6FF8FA.

## Built 1.2.40 � review fixes and GitHub checkpoint

Music microphone start/stop, transport, COMMS controls, and wrist settings now
queue HTTP work off the VR input thread. Completions apply on the UI thread;
FIFO ordering preserves a quick press/release while start is pending. Tab
changes use cached state and schedule existing background polls. Settings and
COMMS discard polls predating queued changes; health uses the existing worker.

Keyboard dictation cancellation closes the streaming session, wakes its final
wait immediately, and does not start whole-recording fallback after cancellation.
An already-running whole-recording fallback remains bounded by its existing
backend timeout; it is not forcibly interrupted while decoding.

Music metadata and cover bytes now come from one provider-pinned snapshot.
Spotify reads one media-properties object; Pear reads one song response. The
wire response carries provider identity so a stale provider response is ignored.
Artwork failure retains title/playback information with an empty cover.

Validation: 248 Python tests passed, 2 skipped, followed by the additional
snapshot binary-framing test passing (249 total passing tests). Native Release
build and asynchronous FIFO/nonblocking/UI-completion, drag acceleration,
keyboard dictation, and artwork-cache checks passed. No real microphone,
keyboard injection, playspace movement, or playback commands used in tests.
The previous 1.2.39 drag fix was confirmed by the user as much better.

This checkpoint also includes keyboard dictation and accelerated dragging since
1.2.35. Caption append/buffering remains reverted. Installer built for 1.2.40;
not installed or restarted during this pass. Installed baseline remains 1.2.39.
Installer SHA256: 3CB26476C5B2785544DA9843F547C1266ABAF5C021B765311C79D224681A56F1.

## Installed 1.2.39 — drag jitter and toggle correction

User reported jitter even when Normal was selected. Saved settings and the live
service both reported acceleration off. Normal now restores the pre-acceleration
standing-pose delta path and bypasses all new gain/filter/coordinate conversion.
Accelerated sampling uses calibrated standing poses with the current universe
transform removed, rather than driver-raw poses. Repeated/cached pose samples
accumulate elapsed time so a fresh pose is not interpreted as a speed spike;
long frame stalls retain unboosted motion instead of dropping it. Changing mode
or strength discards the prior sample and resets the gain filter. In-flight
settings polls started before a wrist toggle are discarded to prevent rollback.

242 Python tests pass, 2 skipped. Native acceleration tests include repeated poses
with 19 ms cached + 1 ms fresh samples and unchanged Normal gain through stalls
and rapid samples. The previous code fails these cases. Headset confirmation of
the reported jitter remains necessary; no live playspace movement was automated.

Installed/restarted October 12: version 1.2.39, health 200 ready, one overlay
and service. Four executable hashes match staging; settings/key unchanged.
Installer SHA256: 320E4691135ED18C58C190726E0E26C281450453365EB65727E0ACE6D7437216.
Native Release/installer builds passed. Local checkpoint only; no GitHub push.


## Installed 1.2.38 — optional accelerated space dragging

SPACE panel toggles Normal/Accelerated; desktop settings expose the same switch
and maximum boost (1-6x, default 4x). Defaults to Normal for existing installations.
A smooth speed curve ramps from 1x below 0.15 m/s to max at 1 m/s, with 60 ms rise
smoothing and immediate precision when slowing. No momentum after release.
Physical controller samples use raw tracking coordinates, converted through the
saved baseline rotation for the existing origin translation; applied playspace
offsets cannot feed back into speed. Tracking loss, hand changes, implausible
samples and stalls reset the speed filter. Existing travel limit/restore retained.

242 Python tests pass (2 skipped). Native checks cover precision, boost, stopping,
reset, nonfinite/jump/stall samples, and consistent travel at 72/90/144 Hz.
Headset feel and travel still need user testing; no live playspace manipulation
was performed during automated validation.

Installed/restarted October 12: 1.2.38, health 200 ready, one overlay/service,
four executable hashes match staging, settings and protected key unchanged.
Installed settings endpoint returns the new fields. SPACE production render
visually checked; General settings now scroll and build correctly off-screen.
Installer SHA256: FF403EA306A55B10993B5079CEADBDF24B6AD559A3EA85248E6D6EC13FB496D3.
Local checkpoint only; no GitHub push.


## Installed 1.2.37 — keyboard microphone dictation

Added a mic key beside Space on the independent desktop keyboard. Tap to start,
tap again to stop; the final cleaned transcript is inserted once into the original
focused field with Unicode input, without clipboard changes or Enter. Uses COMMS'
configured streaming STT backend and whole-recording fallback; no LLM or OSC.
Capture is bounded to 30 seconds and shares the global microphone lock. HTTP runs
asynchronously off the overlay loop. Target window/control changes, desktop clicks,
other keyboard keys, close/hide and shutdown cancel and discard pending output.
Status appears in the keyboard header and the mic glows while listening. Native
focus checks cannot identify browser DOM fields changed without a desktop click
or native focus event. Discord insertion and headset ergonomics need user trial.

Validation: 241 Python tests pass (2 skipped), including six new recording tests.
Six isolated native scenarios pass: insertion once, changed focus, cancellation,
stop during startup, unavailable service, and failed polling. Tests inject no real
keystrokes, microphone audio, or remote requests. Chatbox behavior stays reverted.

Installed/restarted October 10: 1.2.37, health 200 ready, one overlay/service.
All four executable hashes match staging; settings and protected key unchanged.
Installed dictation status endpoint verified using an expired token (no recording).
Installer SHA256: 20431FB0705F7666932C1A62BA8D100CC05775AE78FAB759C7E620406FEF7CCA.
Native build and installer succeeded. Local checkpoint only; no GitHub push.


## Installed 1.2.35 — cached model archive and dual SlimeVR support

Model ZIP cache is keyed by all five source files' contents, with an archive
checksum checked before reuse. Changed sources/corrupt cache rebuild atomically.
Installer embeds it uncompressed and extracts through Inno's archive support;
the installed model paths and offline single-file distribution stay the same.
Compiler stage measured 48.015 seconds versus 205.438 for 1.2.34; verified cache
reuse measured 0.585 seconds. These are packaging-stage timings, not full builds.

Both former legacy and v22 SolarXR definitions are pinned/bundled. A common
read-only guard poll identifies the response layout before requesting data or
issuing resets. Resets stay on that same confirmed connection. No saved protocol
choice; switching servers is detected on the next query. Legacy body-role IDs,
mounting guards and reset completion format retained separately from v22.
235 Python tests and 13 Node adapter checks pass; native and installer build pass.
Live beta detection passed; legacy detection/encoding tested with simulated
connections using its actual pinned schema, not a live downgraded server.
Installed/launched October 10; health 200 ready, version 1.2.35, one overlay and
service. Settings/key unchanged; four executable hashes and six adapter/bundle
files match staging. All five extracted model files match source hashes. Installed
live beta probe succeeds. No live reset dispatched. Installer 559,005,988 bytes
versus 523,088,666 previously (about 6.9% larger).
Installer SHA256: 4EB238337D70FDEFB5C44BA054A8FC79C30CA83DECECA0643C5D14F7C4B421E7.

## Installed 1.2.34 — SlimeVR 22 server compatibility

SolarXR pinned to 7a5e15c28bb7a0a15b722a93077a7abf8d97a845 for the v22 rewrite.
New adapter uses named body-role enums, v22 mounting availability and reset
lifecycle messages. Single read-only polls request only battery/device info and
tracker roles/status. Missing/unassigned/disconnected roles remain blank; empty
valid feeds are healthy, while close/decode/timeout failures exit nonzero.
Slime status requires a valid feed response; stale rig values clear on failure.
Reset requests retain server-configured delay/mounting method; no live resets
were dispatched. Packaging includes ESM metadata and checks the protocol pin.

Validation: ten offline adapter tests and 234 Python tests passed (2 skipped).
Live v22.0.0-rc.1 server returned ten trackers with battery readings; all roles
were NONE/unassigned, so body slots correctly remain blank. Native Release and
installer passed. Installed/launched October 10: version 1.2.34, health 200 ready,
one overlay/service, all four binaries and five adapter/protocol files verified
against staging, settings and key unchanged. Installed JS probe and native
--slime-summary both returned valid data with MOUNT_WAIT; native exit 0.
Actual calibration remains for the user's headset test. No GitHub push.
Installer SHA256: 5FB62362206A1272F09B9DB879FD1A2B6E8696175C772F1F2511DFAF7C1E0F95.
Older pre-rewrite servers are not supported
by this adapter; see README. No server configuration or assignments changed.

## Installed 1.2.33 — music microphone hold-to-talk

Music wrist capture previously started on button-down and ignored button-up;
silence detection ended recording and LISTENING persisted through the request.
The wrist now starts a release-aware session, records while held, and sends
button-up even after the pointer leaves the mic. Recording uses the existing
200 ms tail and 30-second safety maximum. Release shows PROCESSING while the
existing transcriber and music planner finish. Legacy /listen/music is unchanged.
Each capture owns its stop event and token, preventing early-release and stale
release races. Command locking still serializes music with other voice work.
234 tests passed; two server modules skipped in the base environment. Native
Release build and installer passed. Installed/restarted October 9; overlay
version 1.2.33, authenticated health 200 ready, one overlay and service. All four
installed executable hashes match staging; settings and protected LLM key are
unchanged. No model requests or actual music playback dispatched during
validation; headset release behavior awaits the user's trial. No GitHub push.
Installer SHA256: 5C2D9F10AD3D74DBBC29B4303677C5083DFA2570400FE390CABE47D48ECDA0DD.

## Installed 1.2.32 — direct OpenAI music compatibility

For api.openai.com, the client sends max_completion_tokens instead of the
rejected legacy max_tokens field. Sampling temperature is omitted for GPT-5/6
and o-series reasoning configurations (unless effort is explicitly none).
Other compatible providers retain their original payload and output bounds.
No playback retries or model fallback changes were introduced.

User configured direct OpenAI GPT-5.6 Luna, low reasoning, and entered the key
through the existing protected settings field. Live interpretation-only tests:
All Hallows Eve / Type O Negative search 2.33 s (673 input + 36 output tokens),
synthetic candidate selection 1.67 s (745 input + 19 output). Both correct;
1,473 tokens total, no playback dispatched. These do not include actual Spotify
search latency or verify account complimentary-token billing. ASK remains Codex.
229 existing tests passed, two server modules skipped in the base environment.
Native Release build and installer passed. Installed and restarted October 9;
authenticated health returned 200 ready, with exactly one overlay and service.
Settings and protected LLM key hashes unchanged; all four installed executable
hashes match staging. Overlay reports 1.2.32. Actual headset music playback is
left for the user's next trial. No GitHub push performed.
Installer SHA256: 90BECAA24F8E5BE96AC7CEEF4D7B2EA7823E6FAEC72116A99BCD2C737C19DC47.

## Active server: Nemotron live STT — October 9, 2026

Server-only update (desktop was 1.2.31 at activation). Streaming backends now expose a
common create_stream interface. Nemotron English 0.6B uses sherpa-onnx 1.13.8,
560 ms chunks, 128-bin features, four CPU threads and a final 660 ms padding
flush. This is incremental recognition, not a post-release second pass. Model
weights are pinned to the official April 25 export and verified by SHA256.
Moonshine and Whisper remain available; music/ASK recognition is unchanged.

Enable Nemotron.cmd installs the additional runtime, benchmarks incremental
recognition, backs up config and changes only the streaming choice/adds its model.
Failed activation restores the previous config. Restore Moonshine.cmd reverses
the selection. Health reports streaming_model and only reports loaded after
warm-up finishes. Normal chat audio is not saved. Explicit WAV benchmarks can
save partial/final transcripts and timings for comparison.

Validation: 227 existing tests passed (two server modules skipped in the base
environment); 15 server/client streaming checks passed in the isolated Flask
runtime. Real Nemotron HTTP-route test passed for partials, finalization, cleanup
and silent audio. Upstream 6.625-second sample: median chunk 71 ms, max 76 ms,
RTF 0.131 on the development PC CPU; not a server or headset accuracy benchmark.
Activation script parses successfully. Source/model copied and hash-verified on
the server share with source backups; config is unchanged. User must run the
helper on the server because SSH/WinRM are unavailable. User ran the helper successfully. Authenticated health confirms streaming_model=
nemotron, loaded=true, no errors; Whisper is also loaded. Server sample benchmark:
median chunk 82 ms, max 91 ms, final flush 81 ms, RTF 0.151. Personal headset
accuracy testing is pending. No app restart, GitHub push or installer rebuild.

## Installed candidate: 1.2.31 — retained music suggestions

Spotify and YouTube Music retain explicitly offered alternatives in spoken order
for five minutes, separately per provider. The new offer action produces the
spoken numbered question from actual search candidates. Exact ordinal replies
resolve without another LLM call or search; descriptive follow-ups receive the
saved candidates. Fresh searches discard pending choices, and dispatch consumes
choices even on an uncertain response. Missing/expired ordinal references ask
for a fresh request. Memory is bounded to five alternatives per provider and is
not written to disk. Music history now keeps five exchanges/five minutes per
provider instead of a shared three exchanges/two minutes.

227 Python tests passed, one skipped. Configured LLM was tested with synthetic
search results and mocked playback for both providers: it offered two versions,
then selected the correct version by artist without searching again. No real
playback was changed by these tests. Native Release build passed.
Installed September 29, 2026; installer exit 0. All four installed binaries
match staging; settings stayed byte-for-byte unchanged. Restarted with one
overlay and one service; authenticated health returned 200 ready. Physical VR
follow-up playback is left for the next user test. Spotify's separate search
HTTP 502 incident is not changed by this update.
Installer SHA256: 3E97B480D8A3BCBA673E88068F728F626C9E6BA4CE18BFF37BF68CFF7F58CFEE.

## Installed candidate: 1.2.30 — wrist music-provider switch

Music now has separate PLAYER and BROADCAST buttons with shared render/hit bounds.
The authenticated provider endpoint makes an idempotent selection, preserving
other settings and refusing to switch during an active voice command. Native
requests run off the render loop, show SWITCHING, and refresh metadata/art after
acknowledged settings changes. Old-provider poll results are discarded.

When players change, any active broadcast stops and remains off. Its source
follows only when it matched the old player; independently selected sources stay
selected. Desktop-settings changes use the same native transition behavior.

216 Python tests passed, one skipped. Native Release build and compile-time
routing/hit-bound assertions passed. Production wrist preview inspected with
both new labels visible. Installed September 27, 2026; installer exit 0 and all
four installed binaries match staging. Settings were unchanged by installation.
Installed authenticated endpoint smoke tests passed for Spotify/YouTube selection,
repeated selection and invalid-provider rejection. Initial startup returned 409
while assistant restoration held the command lock; the repeat check waited for
startup to finish. Original Spotify selection and all settings were restored.
The smoke service stopped cleanly. SteamVR/overlay were not running, so the app
was left stopped for the next VR session; physical wrist validation remains.
Installer SHA256: D41C5131F4AA356B9BF41926A24D1612D09031DD1BD0841EED3383A678F1CE56.

## Installed candidate: 1.2.29 — YouTube Music via Pear Desktop

Adds a Settings music-provider selector and authenticated local Pear API support.
YouTube Music supplies transport, artwork, and the existing OSC song announcement
pipeline. Spotify and Chrome broadcast choices remain; YouTube adds a third
app-only capture source using the same VB-CABLE output and broadcast gain.
The service indicator is labeled MUSIC. Voice supports real search-result song
selection and long mixes, transport and absolute volume. Playlist/album/radio,
liked-song, and shuffle parity are intentionally not included in this version.

212 Python tests passed, one skipped. Native Release build passed and the
production wrist preview was inspected. Live Pear 3.12.0 checks passed for
next-track, search playback, pause/resume, metadata and artwork. Its process
capture returned 48 kHz stereo with no discontinuities during the 3-second probe.
No live VRChat broadcast was triggered by these checks. Headset validation remains.
Installed and restarted September 25, 2026. Installer exit 0; existing settings
were preserved. Only music_provider was then changed to youtube; announcements
remain enabled. Overlay, audio engine, recovery helper and frozen service hashes
match staging. Installed health returned 200 ready; current track and 150354-byte
artwork came from Pear. Exactly one overlay and service were observed.
Installer SHA256: E330F134A47C29DC1D8E0D90968371FD28E38CDDB3CD8D4F2E331C071EC79613.
An initial installer compilation was superseded before installation to include
cold-start broadcast-source initialization; the final build includes this fix.

## Installed candidate: 1.2.28

Adds a 28-pixel CPU/GPU/VRAM/RAM row between tabs and service lights. The panel
is 768x452; existing content sizes and relative hit targets are preserved.
Status hover regions move with the status strip. Values use neutral colors and
fixed columns; RAM and VRAM are used/total (GiB quantities labeled GB).

Native PerformanceMonitor uses one worker, direct Windows CPU/RAM APIs and the
installed NVIDIA NVML driver. Selects the NVIDIA adapter with most VRAM (RTX5080
on this machine), excluding AMD integrated/virtual adapters. Polls every second
visible, five seconds hidden; missing/failed reads show --. No sensors, helper
processes, network requests, per-frame sampling or history logging.

201 Python tests passed, one skipped; native Release and production previews
passed. Input blocking/layout checks passed. Live probe returned all four values.
After warmup, process CPU time was below timer resolution over 10s (not zero-cost).
Cached production redraw/submission averaged 0.36ms across 100 iterations; this
is a desktop preview measurement, not a complete VR frame-time benchmark.
Music/Desktop/ASK previews inspected; VRAM label clipping corrected.
Installed and launched September 24, 2026: health 200 ready, settings preserved,
installed binaries match staging. User reported the metrics work very well.
Installer SHA256: C098F2F42DDD011CF01D75B70B0A668CA6A325861762808106AF997B3081E539.

## Installed candidate: 1.2.27

Confirmed the shuffle warning's cause against live Spotify: shuffle returned HTTP
200 with an opaque plain-text body and no Content-Type, which the generic API
handler tried to decode as JSON. Shuffle now treats successful HTTP completion
as acknowledgment without parsing that body. HTTP errors still propagate; read
endpoints continue to require JSON. The 1.2.26 no-retry protection remains.

201 tests passed, one skipped; native Release passed. Reapplying the current live
shuffle setting succeeded with the fix without changing playback or the setting.
Regressions cover text/empty successful responses, HTTP rejection, and strict
JSON parsing for reads. Installed September 22, 2026: health 200 ready, settings
preserved, installed binaries match stage, overlay/service running.
Installer SHA256: 0FEA9A45F8326F1E191209B5F834A0EF2D153FE99F287DAF9CC0FA07B3458218.

## Installed candidate: 1.2.26

Fixes planner retries after Spotify command dispatch: an unreadable response can
no longer cause repeated playback or repeated skips. After confirmed playback,
a shuffle failure returns playback success with a separate shuffle warning.
Artist metadata tolerates nulls, and acknowledgment text is prepared before play.
Diagnostic logs record exception class and whether an action was dispatched.

198 tests passed, one skipped; native Release passed. New regressions cover
shuffle errors after successful play, uncertain playback responses, and uncertain
next-track responses. Original live exception was not logged, so its exact trigger
is unconfirmed; the retry path matching the observed repeats is fixed.
Installed September 22, 2026: health 200 ready, overlay/service running, settings
preserved, and installed binaries match stage. Await live VR confirmation.
Installer SHA256: 8313732845034F8624BF19A7B6307C49F696F59F73AB988D7AABC3F5362399F6.

## Installed candidate: 1.2.25

Music voice requests now explicitly interpret genres, moods and activities as
playlist searches, selecting a suitable result with shuffle unless requested
otherwise. Named songs retain track searches. Playlist descriptions are supplied
as bounded plain text to help selection, and failed searches no longer demand
a specific song or artist.

195 Python tests passed, one skipped; native Release build passed. Added playlist
selection and empty-result retry coverage, including shuffle preference and
conversation context. The configured live LLM correctly routed synthwave and
mellow instrumental requests to playlists and Hysteria by Muse to tracks; no
Spotify playback was changed during that check. Installed September 22, 2026:
overlay/service running, health 200 ready, staged binaries match installed,
settings preserved. In-headset genre playback confirmation pending.
Installer SHA256: 752AC78196FF527380BB8EAD2F914660E02BC2463E2D00A41B2BA957E0402DA7.

## Installed candidate: 1.2.24

Fixes left-hand desktop clicks: both triggers now use the same captured-window
and source-picker path. The hand starting a primary press owns its drag through
release; the other hand cannot steal or release it. Losing tracking also releases
the press. Wrist occlusion is evaluated for the actual pointing hand.

Native Release build and primary gesture, input ownership, input blocking and
click filter regressions passed. Installed September 15, 2026; in-headset left-hand
verification remains necessary. User confirmed 1.2.23 input blocking works.
Overlay/service running; health 200 ready; installed binaries match staging and
settings preserved. Installer SHA256:
483766C9DE98470FDE04373A79E17B3037D941D54B277FF578DF159E80E3EA0C.

## Installed candidate: 1.2.23

Adds a persistent BLOCK GAME INPUT toggle in the DESK wrist footer, visible on
both the home and window-list views. Default Off. Turning it On enables SteamVR's
globalActionSetPriority prerequisite and selectively raises the Interfayce action
set priority for the hand aiming at a desktop surface, keyboard, frame, or DESK
wrist view. This covers sources bound to Interfayce, not every controller input.
Ownership lasts through held clicks, grabs and scrolling, including pointer drift;
tracking loss, unavailable support and app exit release the claim. Off uses the
original unrestricted input path once held interactions finish.

The preference is stored through SteamVR settings in com.lag0matic.interfayce /
desktopBlockGameInput. Turning it off leaves SteamVR's general overlay support
available to other apps. A disabled prerequisite or rejected priority update shows
UNAVAILABLE; rejected input updates fall back to ordinary controls. Windows mouse
injection is a separate path and is not blocked by SteamVR action priority.

Validation: native Release and production preview builds passed; ownership tests
cover Off, unavailable support, independent hands, dragging out, toggling off
while held, release and tracking loss. SteamVR accepted the per-hand action-set
probe (error 0). Footer previews checked. 193 Python tests passed, one skipped.
Installed September 15, 2026: overlay and service running, health 200 ready,
installed binaries match staging, settings preserved. Actual VRChat suppression
needs an in-headset test with the toggle enabled.
Installer SHA256: 5163ADBE75BEA4EDB9EE2272B08B2E586DD5F335BA772B5D3A1290DAE23FB193.

## Installed candidate: 1.2.22

Adds approximately 200 ms of microphone capture after releasing chatbox
push-to-talk, so releasing while finishing a word does not immediately end the
audio. The additional frames reach both streaming STT and the retained fallback
recording. The original maximum recording duration still applies. This is an
experiment for final-word errors, not a confirmed fix for recognition accuracy.

193 Python tests passed, one skipped. The new capture test verifies tail delivery
and the hard duration limit. Native Release build passed. Installed September 14, 2026; overlay and service
are running, authenticated health is ready, installed binaries match staging,
and settings were preserved. The 1.2.21 checkpoint remains the last VR-validated
build. Awaiting practical confirmation of the release buffer.

Installer SHA256: 827095F2C6967A55E38CE6D665B0E023702061C11C7C6C82A96B591C6E91034A.

## Validated checkpoint: 1.2.21

Built and installed September 13, 2026. Overlay and resident service are running;
authenticated health returns 200 ready and ASK reports Codex / READY. Installed
native, service and recovery binaries match staging. Settings were preserved.
Installer completed successfully without requiring a Windows restart.

Includes the ASK and full-application review fixes, then a separate structural
pass for native service transport, desktop input ownership, settings persistence
and ASK rendering. See APPLICATION-REVIEW.md for findings and scope. Codex ASK
retains the economical Luna default and Settings -> ASK model selection.

Validation: 192 Python tests passed, one skipped. Release and production-preview
builds passed. Native checks passed for input ownership/release/scroll targeting,
click filtering, real mixed-DPI coordinates, decision geometry, artwork, battery
estimation and private-window recovery. ASK preview visually checked. The maintainer confirmed a successful VR session with 1.2.21 and approved this
checkpoint for GitHub. Occasional plausible STT word substitutions remain an
observation; diagnostic recording is deferred. No new model turn was needed
for the fix/refactor validation.

Local checkpoints: d7efea8 (feature and review fixes), c26e109 (structural pass).
Installer: packaging/out/installer/Interfayce-Setup-1.2.21.exe. SHA-256:
88482CAF9101C5F39283FC525F7C171DB6AEC103A6C9118FC58B5F0404EF3EA4

## Previous checkpoint: 1.2.19

Built and installed September 12, 2026. Wrist scrolling and captured-app input on
a 125% secondary monitor are confirmed working in VR by the maintainer.

Recent changes: persistent battery/service/clock strip with hover explanations;
scrollable wrist window list; mixed-DPI app input and click-jitter protection;
private VR windows with independent recovery; streaming STT and local fallback;
capture cues; queued song announcements; flexible Spotify requests; artwork fixes.

171 Python tests pass. Native tests cover desktop clicks, actual monitor coordinate
mapping, private-window recovery, artwork and battery estimates. Production UI
previews cover seven panels and a scrolled seven-window list.

Installed binaries matched the stage; settings and protected credentials were
unchanged. Installer: packaging/out/installer/Interfayce-Setup-1.2.19.exe
Size: 523029575 bytes. SHA-256:
3C87B4D092972B63F44E26FD1C6334FC83584368402A5DF1068CE10750781B61

Build output, model payloads, local configuration and driver packages are excluded
from Git. See RECOVERY.md for rebuild and recovery instructions.

Service lights report availability, not speaker audibility or guaranteed request
success. Spotify status reflects its media session, not OAuth. Private windows
require a working extended virtual display; recover windows before disabling it.
MolotovCherry v0.3.1 was validated with NVIDIA 616.92. Windows/app popup behavior
prevents promising absolute privacy.

Build runtime is Python 3.12; audioop requires attention before Python 3.13+.
Native orchestration remains concentrated in main.cpp and should be split gradually
as features change. In-headset testing remains essential.

## 1.2.42 - caption reading pause

Live captions hold for three seconds before dropping older words on overflow.
Further rollovers are spaced three seconds apart; release respects the pause.
Clear and shortcuts suppress pending text. No queue of 30-second pages.
Validation: 252 Python tests passed, 2 skipped, including rollover timing,
release flushing and clear during the final pause. Panel previews also support
shareable production renders with supplied music/status and sample chat content.

## 1.2.43 - preserve captions during early transcript revisions

The live publisher holds the displayed caption when a partial revision reduces
its settled prefix below the previously published word count. This prevents an
early wording or punctuation correction from collapsing the chatbox to a short
prefix. A settled replacement and authoritative final result can still update;
three-second overflow timing remains intact. Validation: 251 application tests
passed, including early corrections, shorter final corrections and overflow
reading-deadline preservation.
