# Codex in ASK

Interfayce 1.2.20 adds a Codex assistant using the installed Codex runtime and its
ChatGPT sign-in. Tested with codex-cli 0.153.4. Install/sign in to the Codex desktop
app first. Interfayce discovers its executable; it does not bundle credentials or
require an OpenAI API key for this backend.

Settings -> ASK contains the model picker. GPT-5.6 Luna is the default; low
reasoning and standard service tier are explicit. Apply changes the next turn,
including in an existing conversation. Refresh available models queries the
installed runtime without generating a reply. Music/API model settings are
separate. If a selected model becomes unavailable, ASK reports the error rather
than silently upgrading to a more expensive model.

In ASK, use the microphone as before. Replies stream into the text area. Aim at
the reply and use the right thumbstick to scroll. STOP interrupts generation and
the answer's speech. The clear-conversation icon starts a new conversation;
previous Codex history is retained. SWITCH selects the previous API assistant.
Switching is available after the current operation finishes.

Codex uses a separate Interfayce conversation, remembered across restarts. It does
not mirror the conversation currently open in the Codex desktop window. Desktop
capture remains available for that interface. The most recent conversation text
is restored through Codex's supported history protocol, with a bounded display
history. Longer spoken responses are limited to the existing 1,200-character
speech budget; the wrist text remains scrollable.

Command and file approvals offer Allow once or Decline. Questions show up to three
suggested replies; ANSWER records a spoken response, including a different answer.
STOP remains available. Requests without a supported wrist presentation explain
that they need the desktop interface and offer Stop; they are never auto-approved.
There is no automatic transfer of a pending request to the desktop app.

The LLM status light follows the selected backend. Codex connects lazily, or when
restoring a saved conversation. An unavailable runtime, missing subscription
sign-in or failed turn appears in ASK. Failed prompts are not automatically sent
again or redirected to the API assistant.

The Codex child runs with an explicitly requested read-only filesystem sandbox and
on-request approvals. Its tools/configuration are supplied by the installed Codex
runtime; desktop-only plugin parity is not promised. Interfayce owns this child
process and stops it on normal service shutdown, leaving the desktop app alone.

Validation: live subscription streaming, process restart and conversation recall,
live interruption, backend failure/correlation tests, native Release build,
production panel preview, and existing Python/native regressions. In-headset
acceptance is still required before the planned independent reviews and refactor.
