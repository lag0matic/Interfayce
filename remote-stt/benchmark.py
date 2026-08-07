"""Benchmark every configured model against the same WAV files."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
import json
import mimetypes
from pathlib import Path
import time
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError
import uuid


def multipart(path: Path, model: str) -> tuple[bytes, str]:
    boundary = "----Interfayce" + uuid.uuid4().hex
    chunks = [
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n{model}\r\n".encode(),
        (f"--{boundary}\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\n"
         "verbose_json\r\n").encode(),
        (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{path.name}\"\r\n"
         f"Content-Type: {mimetypes.guess_type(path.name)[0] or 'audio/wav'}\r\n\r\n").encode(),
        path.read_bytes(),
        f"\r\n--{boundary}--\r\n".encode(),
    ]
    return b"".join(chunks), f"multipart/form-data; boundary={boundary}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("audio_directory", type=Path)
    parser.add_argument("--config", type=Path, default=Path("config.json"))
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--server", default="http://127.0.0.1",
                        help="Server base URL; defaults to the local machine")
    parser.add_argument("--model", action="append",
                        help="Configured model to test; may be repeated")
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8-sig"))
    files = sorted(path for path in args.audio_directory.iterdir()
                   if path.suffix.lower() in {".wav", ".mp3", ".m4a", ".flac", ".ogg"})
    if not files:
        raise SystemExit("No audio files found.")
    server = args.server.rstrip("/")
    parsed_port = server.rsplit(":", 1)[-1]
    if not parsed_port.isdigit():
        server = f"{server}:{config['port']}"
    endpoint = f"{server}/v1/audio/transcriptions"
    rows = []
    models = args.model or list(config["models"])
    unknown_models = [model for model in models if model not in config["models"]]
    if unknown_models:
        raise SystemExit(f"Unknown configured model(s): {', '.join(unknown_models)}")
    for model in models:
        for audio_path in files:
            for run in range(1, args.repeat + 1):
                body, content_type = multipart(audio_path, model)
                request = Request(endpoint, data=body, method="POST", headers={
                    "Authorization": f"Bearer {config['api_key']}",
                    "Content-Type": content_type,
                })
                started = time.perf_counter()
                error = ""
                try:
                    with urlopen(request, timeout=300) as response:
                        payload = json.load(response)
                    transcript = payload["text"]
                except HTTPError as failure:
                    try:
                        payload = json.loads(failure.read().decode("utf-8", errors="replace"))
                        error = str(payload.get("error", {}).get("message", payload))
                    except (ValueError, AttributeError):
                        error = f"HTTP {failure.code}: {failure.reason}"
                    transcript = ""
                except (URLError, TimeoutError) as failure:
                    error = str(failure)
                    transcript = ""
                wall = time.perf_counter() - started
                row = {"model": model, "file": audio_path.name, "run": run,
                       "wall_seconds": round(wall, 4), "text": transcript,
                       "error": error}
                rows.append(row)
                result = f"ERROR: {error}" if error else transcript
                print(f"{model:18} {audio_path.name:30} run={run} {wall:.3f}s  {result}",
                      flush=True)
    output = args.config.resolve().parent / "benchmark-results"
    output.mkdir(exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    (output / f"benchmark-{stamp}.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    with (output / f"benchmark-{stamp}.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f"Results written to {output}")


if __name__ == "__main__":
    main()
