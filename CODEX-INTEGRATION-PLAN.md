# Codex integration plan

Status: 1.2.20 implementation candidate, September 13, 2026. Baseline: verified Interfayce 1.2.19.

Implemented: owned stdio connection, subscription authentication check, persistent
conversation/resumption, streaming, cancellation and cancellable speech, structured
wrist snapshots, scrolling, backend switching, approval/question cards, and a
Settings -> ASK model picker. GPT-5.6 Luna with low reasoning and standard service
tier is the explicit default. Music's API model remains separate.

Verified: live streaming, restart/recall, interruption, Luna selection, 182 Python
tests (one skipped), native Release build and regression executables, production
wrist preview, Settings layout, and a packaged service startup/shutdown outside the
repository. The maintainer confirmed in-headset acceptance. Independent ASK and full-application reviews are complete; findings and proposed
code division are in APPLICATION-REVIEW.md. See CODEX-USAGE.md for supported
controls and limitations, including unsupported server requests and no direct
desktop handoff.

## Outcome

Use Codex through ChatGPT subscription sign-in from ASK: speak a question, read
the streamed answer in VR, optionally hear it through Kokoro, and continue the
same conversation after restarting Interfayce. The captured desktop app remains
available for its full interface and this existing development conversation.

Keep the current assistant as a selectable alternative. Do not automatically
resubmit failed Codex requests to another backend: that could duplicate actions,
spend API credit, or silently lose conversation context.

## 1. Prove the local connection

Use the installed Codex executable through its documented app-server interface.
Discover the executable rather than hardcoding the desktop app's versioned path.
Record its version and generate matching protocol schemas during development.
Use an Interfayce-owned child process over stdio, with a hidden Windows launch.
Do not attach to or terminate the desktop application's own backend.

Verify subscription account status, model availability, a streamed test exchange,
conversation resumption after process restart, interruption, and a recoverable
disconnect. Let Codex manage sign-in and credential refresh. Avoid copying tokens
or modifying the desktop app's global configuration. Check which configuration,
plugins and stored conversations are shared; do not assume complete app parity.

Exit criterion: a small outside-VR harness can send, stream, resume and cancel
using subscription access. Resolve protocol or account limitations here before UI
implementation. No new billable API-key configuration is required for this path.

## 2. Add an assistant backend boundary

Keep microphone capture, transcript preparation and speech playback shared.
Introduce a narrow assistant-backend contract: start/resume, submit, interrupt,
new conversation, status/events and respond to a pending request. Wrap the existing
AssistantHarness rather than rewriting its working tool loop.

Suggested new modules:

- assistant_backend.py: common request/event/state types and legacy adapter.
- codex_transport.py: process lifetime, protocol handshake, request IDs, responses,
  notifications, stderr, deadlines and bounded queues.
- codex_backend.py: thread/turn lifecycle, streamed text, approval mapping and usage.
- assistant_session.py: selected backend and locally saved conversation reference.

Names may change during implementation. Keep transport and Codex-specific details
out of native main.cpp. Extend the authenticated local service with structured,
versioned assistant snapshots; do not grow tab-separated text into a conversation
or approval protocol. Throttle UI updates and bound the visible history.

Use a dedicated assistant working directory, not the Interfayce repository by
default. Maintain separate backend histories. Save conversation references
atomically; load history through supported methods instead of reading rollout
files. A missing conversation offers recovery or a new conversation explicitly.

## 3. Build the ASK experience

Keep the tab name ASK. Show the selected backend and useful state: connecting,
ready, listening, thinking, responding, awaiting input, interrupted, or unavailable.

- Existing microphone control starts the normal STT flow.
- Stream assistant answer text; show concise tool activity separately.
- Provide scrollable conversation text with intentional follow-to-bottom behavior:
  incoming text must not pull the view away while the user reads older content.
- Stop interrupts the active turn and stops speech for that turn.
- New conversation starts fresh without deleting old Codex history.
- Keep completed answers readable after a disconnect.
- Speak the final answer by default; do not speak partial edits, tool output or
  approval commands. Use an explicit bounded spoken version for long responses,
  while retaining the complete answer in the scrollable view.

Kokoro currently uses a background worker without turn-scoped cancellation. Add
that ownership explicitly so an old answer cannot speak after Stop, New
conversation, or backend switching.

The status strip's LLM indicator follows the selected assistant backend. Show
subscription exhaustion, sign-in needed and disconnected states distinctly in its
details. Gray covers uninitialized/disabled; yellow is a deliberately selected
fallback, not permission to silently reroute requests.

## 4. Handle tools and decisions honestly

Prove the supported permission configuration in the connection harness. Start
with conversational/read-only behavior and explicitly configured tool access;
do not inherit unrestricted project execution just because this development task
uses it. Expose command/file/permission approval requests and questions as readable
VR cards with the actual available choices. Preserve approved scope; do not invent
additional confirmation prompts for ordinary conversation.

Match each response to its pending request and turn. Disable stale cards after
cancel/restart. Unsupported request types must produce an actionable message,
never an invisible wait. Complex review can remain available in the desktop app,
but only promise a direct handoff if the connection experiment establishes one.

Basic streamed conversation is required. Desktop-only plugins, connectors, native
voice, images, arbitrary task synchronization and Interfayce-control tools are
not assumed. Record verified capabilities and add only those needed for this
feature; preserve the desktop capture option for the rest.

## 5. Reliability, packaging and completion

One active assistant turn at a time initially. Bound pending events and cached
text; drain output continuously. Reconcile thread/turn state after reconnecting
instead of automatically replaying the last prompt. Never retry an uncertain
action merely because the connection dropped. Restart only the child process
owned by Interfayce, with backoff, and preserve the rest of the app on failure.

Pin/document the tested Codex runtime and detect incompatible versions. Decide
after the harness whether the installer uses an existing compatible installation
or distributes a separately reviewed runtime. Do not bundle cached credentials.

Acceptance checks:

- Multi-turn conversation survives app restart without duplicate submissions.
- Cancel, backend changes, new conversation and process loss clear pending work.
- Missing login, expired login, usage limits and unavailable runtime have useful UI.
- Approvals/questions work, including rejection and stale-response handling.
- Long replies scroll correctly; speech does not replay or outlive its turn.
- Slow/disconnected Codex does not stall wrist input, STT, music or private recovery.
- Packaged build works outside the repository; existing regression tests pass.
- Native previews and actual VR use verify text size, controls and pointer mapping.

Completion means these checks pass and the user has tried the integration in VR,
not merely that the first prompt receives a response.

## 6. Independent reviews, after the feature works

The user requested review agents after integration. At that point, run three
bounded read-only reviews in parallel:

1. Lifecycle/reliability: concurrency, cancellation, reconnects and resource cleanup.
2. VR interaction: input mapping, readability, scrolling, approvals and regressions.
3. Integration/maintenance: protocol assumptions, permissions, packaging, tests and
   module boundaries.

Each reviewer returns evidence, severity and a concrete recommendation. Reconcile
conflicting findings centrally, fix substantive issues and repeat affected checks.
Do not start these agents during the planning phase or let them independently
rewrite overlapping files.

## 7. Divide the existing code after review

Checkpoint the validated feature before structural work. Use reviewer findings to
prioritize small behavior-preserving extractions: native service communication,
wrist input/hit testing, panel layout/rendering, and application orchestration.
The Codex transport is already separate by design. Avoid changing features and
moving their implementation simultaneously. Build/test after each extraction and
finish with in-headset validation and a separate checkpoint.

## Official protocol reference

The documented app-server supports subscription authentication, stdio transport,
thread start/resume, streamed events, interruption and client-side decisions.
This plan proposes how Interfayce will use those capabilities; local compatibility
still needs the connection experiment.

[Codex App Server](https://learn.chatgpt.com/docs/app-server)
and [authentication](https://learn.chatgpt.com/docs/auth).
