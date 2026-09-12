# Interfayce Remote STT

A copyable Windows speech-to-text service for Interfayce. It exposes the same
`POST /v1/audio/transcriptions` shape as OpenAI, keeps the selected model warm,
and can compare two local engines on the same recordings:

- `whisper-turbo`: Faster-Whisper Turbo on NVIDIA CUDA (`int8_float16`). This is
  the accuracy-first candidate for names, artists, and song titles.
- `moonshine`: Moonshine Voice Medium Streaming (architecture 5) on CPU. This
  is the latency/resource candidate.

The server defaults to TCP port `5010`, leaving the existing Kokoro server on
port `5000` alone.

## Copy and install

1. Copy this entire `remote-stt` directory to the server.
2. Open PowerShell in that directory.
3. Run:

   ```powershell
   Set-ExecutionPolicy -Scope Process Bypass
   .\install.ps1
   ```

The installer finds Python 3.12 or 3.11, creates a private `.venv`, installs
both engines, generates a random API key in `config.json`, and registers the
server to start silently for the current user at Windows login. It does not
need administrator rights. Pass `-NoAutoStart` if this is not wanted. If
Windows Firewall blocks the connection, rerun an elevated shell with:

```powershell
.\install.ps1 -OpenFirewall
```

Use `-Backend moonshine` or `-Backend whisper` to install only one engine. Add
`-Warm` if you want installation to download and load models immediately;
otherwise the first `start.ps1` does that for the default model.

## Start, inspect, and stop

```powershell
.\start.ps1
.\status.ps1
.\stop.ps1
```

`start.ps1` runs silently in the background. Logs live in `logs`. For a visible
console during troubleshooting, use `start.ps1 -Foreground`. The health endpoint
starts before model warm-up completes; `/health` reports `loaded: false` until
the selected engine is ready, or includes its initialization error on failure.
`stop.ps1` first uses an authenticated local shutdown endpoint, avoiding Windows
privilege/session mismatches, and retains direct process termination as a
fallback for older server builds.

Auto-start can be inspected, disabled, or restored without reinstalling:

```powershell
.\autostart.ps1 status
.\autostart.ps1 remove
.\autostart.ps1 install
```

The login shortcut records this directory's absolute path. If you move the
portable server, rerun `install.ps1` or `autostart.ps1 install` in its new home.

The first Faster-Whisper start downloads its model and may take several
minutes. Warm-up performs one discarded silent decode, so it validates the full
CUDA path and absorbs kernel initialization before the first voice command. GPU
inference requires CUDA 12 cuBLAS and cuDNN 9 to be discoverable by the server
process. The installer reports obvious missing DLLs, while model warm-up is the
definitive test. Moonshine remains independently available if the CUDA runtime
is incomplete.

If another local Python service already has CUDA 12 and cuDNN 9 (for example a
GPU-enabled PyTorch/Kokoro environment), its `torch\lib` directory can seed the
portable runtime without changing machine-wide PATH:

```powershell
.\import-cuda-runtime.ps1 -SourceDirectory C:\path\to\venv\Lib\site-packages\nvidia
.\stop.ps1
.\start.ps1
```

The helper copies only `cublas*.dll` and `cudnn*.dll` into the kit's ignored
`cuda-runtime` directory and verifies the principal required files. Do not use
DLLs downloaded from random aggregation sites.

## Connect from another PC

Read `api_key` and `port` from `config.json`. The endpoint is:

```text
http://SERVER-IP:5010/v1/audio/transcriptions
```

Requests need `Authorization: Bearer <api_key>`. Keep the server on a trusted
private LAN; do not forward this port to the internet.

Example PowerShell request:

```powershell
$headers = @{ Authorization = "Bearer API_KEY_FROM_CONFIG" }
$form = @{ model = "whisper-turbo"; file = Get-Item ".\sample.wav" }
Invoke-RestMethod -Method Post -Uri "http://SERVER-IP:5010/v1/audio/transcriptions" -Headers $headers -Form $form
```

Health and model discovery are available at `GET /health` and `GET /v1/models`.

## Compare engines

Put representative WAVs in one directory. Include the phrases and difficult
music names that matter in actual use, then run:

```powershell
.\benchmark.ps1 -AudioDirectory C:\stt-samples -Repeat 3
```

Both engines receive every file. Console output shows latency and transcript;
JSON and CSV results are written beneath `benchmark-results`. The transcript
quality decision is deliberately left to the human rather than pretending a
latency number measures name accuracy.

## Configuration

`config.json` controls host, port, default model, and backend options. Useful
Faster-Whisper alternatives are `large-v3` (more compute, accuracy-oriented)
and `distil-large-v3` (faster). After editing the file, stop and start the
server. Never commit or share the generated API key.

## Live Interfayce chatbox captions (1.2.15)

The chatbox streams mono PCM16 audio at 16 kHz in half-second chunks over the
existing authenticated HTTP port. Whisper remains the whole-utterance command
backend. Moonshine handles live chat using the existing English model (arch 5).
The tested runtime is moonshine-voice 0.1.0.

POST /v1/audio/streams opens a session; ordered
POST /v1/audio/streams/<session>?sequence=N&final=0 sends raw PCM and returns
the current transcript. Send the last chunk with final=1; DELETE cancels.
The same bearer key protects all routes. Sessions are bounded to 31 seconds,
at most two active streams, and abandoned sessions expire on subsequent requests.
Do not replay a chunk after an ambiguous network failure: the client falls back
to its saved whole recording instead.

streaming_model optionally names the Moonshine entry (default moonshine).
start.ps1 warms both the default command model and streaming model.
For an existing installation, copy the updated Python source and scripts into
the server folder, preserving config.json, .venv, models, and cuda-runtime.
Run **Restart STT.cmd on the server PC** to load the changes. No firewall or
credential changes are required. The restart script does not upgrade packages.

Interfayce sends settled captions at most once per second, then a final caption
on release. Long text rolls forward at word boundaries. Clearing or sending a
shortcut suppresses further captions from that utterance. Stream failures use
the complete local recording for the existing STT fallback.
