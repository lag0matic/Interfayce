"""Measure incremental decode speed and save partials for a supplied test WAV."""
import argparse
import json
from pathlib import Path
import statistics
import time
import wave

from stt_server.backends import create_backend


def benchmark(model_root, wav_path, options, repeat_seconds=None):
    backend = create_backend(options, model_root)
    started = time.perf_counter()
    backend.warm()
    warm_seconds = time.perf_counter() - started
    with wave.open(str(wav_path), "rb") as audio:
        if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 16000):
            raise ValueError("Use a mono PCM16, 16 kHz WAV recording.")
        pcm = audio.readframes(audio.getnframes())
    if repeat_seconds and pcm:
        size = int(repeat_seconds * 32000)
        pcm = (pcm * (size // len(pcm) + 1))[:size]
    duration = len(pcm) / 32000
    if duration < 2:
        raise ValueError("Use at least two seconds of test speech.")
    stream = backend.create_stream()
    partials = []
    try:
        for offset in range(0, len(pcm), 16000):
            started = time.perf_counter()
            text = stream.push(pcm[offset:offset + 16000])
            partials.append({"audio_seconds": min(duration, (offset + 16000) / 32000),
                             "decode_seconds": time.perf_counter() - started, "text": text})
        started = time.perf_counter()
        text = stream.push(b"", final=True)
        final_seconds = time.perf_counter() - started
    finally:
        stream.close()
    times = [p["decode_seconds"] for p in partials]
    rtf = (sum(times) + final_seconds) / duration
    return {"backend": options["backend"], "warm_seconds": warm_seconds,
            "audio_seconds": duration, "real_time_factor": rtf,
            "median_chunk_seconds": statistics.median(times),
            "max_chunk_seconds": max(times), "final_seconds": final_seconds,
            "streaming_seen": any(p["text"].strip() for p in partials[:-1]),
            "text": text, "partials": partials}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-root", type=Path, default=Path(__file__).parent / "models")
    parser.add_argument("--audio", type=Path)
    parser.add_argument("--backend", choices=["nemotron", "moonshine", "faster-whisper"], default="nemotron")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--repeat-seconds", type=float)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-realtime", action="store_true")
    args = parser.parse_args()
    audio = args.audio or args.model_root / "nemotron-en-560ms" / "0.wav"
    options = {"backend": args.backend, "num_threads": 4, "language": "en", "model_arch": 5}
    if args.config:
        options = json.loads(args.config.read_text(encoding="utf-8-sig"))["models"]["whisper-turbo"]
    result = benchmark(args.model_root, audio, options, args.repeat_seconds)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "partials"}, indent=2))
    if args.require_realtime and (result["real_time_factor"] >= 0.8 or
                                 result["max_chunk_seconds"] >= 2 or
                                 not result["streaming_seen"] or not result["text"].strip()):
        raise SystemExit("Streaming test failed; leaving the current model selected.")
