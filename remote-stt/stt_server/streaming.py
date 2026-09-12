"""Authenticated, ordered PCM streaming over the existing HTTP port."""
from dataclasses import dataclass, field
import secrets
import threading
import time

from flask import jsonify, request
from .backends import MoonshineBackend, MoonshineStream


@dataclass
class Session:
    stream: object
    touched: float
    sequence: int = 0
    samples: int = 0
    lock: object = field(default_factory=threading.Lock)


def install_streaming(app, registry, config):
    sessions = {}
    lock = threading.RLock()
    model_name = config.get("streaming_model", "moonshine")

    def expire():
        now = time.monotonic()
        for key, session in list(sessions.items()):
            if now - session.touched > 90 and session.lock.acquire(blocking=False):
                try:
                    session.stream.close()
                    del sessions[key]
                finally:
                    session.lock.release()

    @app.post("/v1/audio/streams")
    def start():
        with lock:
            expire()
            if len(sessions) >= 2:
                return jsonify(error="Streaming sessions busy"), 429
            if model_name not in registry.names:
                return jsonify(error="Streaming Moonshine model is not configured"), 503
            try:
                backend = registry.get(model_name)
                if not isinstance(backend, MoonshineBackend):
                    return jsonify(error="Streaming requires the Moonshine backend"), 503
                stream = MoonshineStream(backend)
            except Exception:
                app.logger.exception("Could not open streaming recognizer")
                return jsonify(error="Streaming model unavailable; check server log"), 503
            key = secrets.token_hex(16)
            sessions[key] = Session(stream, time.monotonic())
            return jsonify(session=key, sample_rate=16000)

    @app.route("/v1/audio/streams/<key>", methods=["POST", "DELETE"])
    def update(key):
        with lock:
            expire()
            session = sessions.get(key)
            if session is None:
                return jsonify(error="Unknown or expired stream"), 404
            if not session.lock.acquire(blocking=False):
                return jsonify(error="Stream already processing"), 409
        try:
            if request.method == "DELETE":
                session.stream.close()
                with lock:
                    sessions.pop(key, None)
                return jsonify(status="closed")
            sequence = request.args.get("sequence", type=int)
            if sequence != session.sequence:
                return jsonify(error="Out-of-order audio"), 409
            pcm = request.get_data()
            if len(pcm) % 2 or len(pcm) > 16000 * 2 * 5:
                return jsonify(error="Expected at most 5 seconds of mono PCM16 at 16 kHz"), 400
            if session.samples + len(pcm) // 2 > 16000 * 31:
                return jsonify(error="Stream exceeds 31 seconds"), 400
            final = request.args.get("final") == "1"
            try:
                text = session.stream.push(pcm, final=final)
            except Exception:
                session.stream.close()
                with lock:
                    sessions.pop(key, None)
                app.logger.exception("Streaming recognition failed")
                return jsonify(error="Streaming recognition failed"), 500
            session.sequence += 1
            session.samples += len(pcm) // 2
            session.touched = time.monotonic()
            if final:
                session.stream.close()
                with lock:
                    sessions.pop(key, None)
            return jsonify(text=text, final=final, sequence=sequence)
        finally:
            session.lock.release()
