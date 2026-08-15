"""Authenticated OpenAI-style transcription HTTP service."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hmac
import json
import logging
import os
from pathlib import Path
import tempfile
import threading
import time
from typing import Any

from flask import Flask, Response, jsonify, request

from .backends import Backend, create_backend


LOGGER = logging.getLogger("interfayce.remote_stt")


@dataclass(slots=True)
class ModelState:
    options: dict[str, Any]
    backend: Backend | None = None
    error: str = ""


class ModelRegistry:
    def __init__(self, model_options: dict[str, dict[str, Any]], model_root: Path):
        self._states = {
            name: ModelState(options) for name, options in model_options.items()
        }
        self._model_root = model_root
        self._lock = threading.Lock()

    @property
    def names(self) -> list[str]:
        return list(self._states)

    def status(self) -> dict[str, dict[str, Any]]:
        return {
            name: {
                "backend": state.options.get("backend", "unknown"),
                "loaded": state.backend is not None and not state.error,
                "error": state.error or None,
            }
            for name, state in self._states.items()
        }

    def get(self, name: str) -> Backend:
        state = self._states.get(name)
        if state is None:
            raise KeyError(name)
        with self._lock:
            if state.backend is None:
                state.backend = create_backend(state.options, self._model_root)
            backend = state.backend
        try:
            backend.warm()
            state.error = ""
            return backend
        except Exception as error:
            state.error = str(error)
            raise

    def warm(self, name: str) -> None:
        self.get(name)

    def transcribe(self, name: str, audio_path: Path,
                   language: str | None = None) -> str:
        state = self._states.get(name)
        if state is None:
            raise KeyError(name)
        if state.error:
            raise RuntimeError(
                f"Model {name!r} is unavailable after an earlier failure: {state.error}. "
                "Correct the runtime problem and restart the service."
            )
        backend = self.get(name)
        try:
            return backend.transcribe(audio_path, language)
        except Exception as error:
            state.error = str(error)
            raise


def load_config(path: Path) -> dict[str, Any]:
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    if not config.get("api_key") or config["api_key"] == "CHANGE_ME":
        raise ValueError("config.json needs a generated api_key; run install.ps1.")
    if not config.get("models"):
        raise ValueError("config.json does not define any models.")
    if config.get("default_model") not in config["models"]:
        raise ValueError("default_model is not present in models.")
    return config


def create_app(config: dict[str, Any], root: Path) -> Flask:
    app = Flask(__name__)
    app.config["MAX_CONTENT_LENGTH"] = int(config.get("max_upload_mb", 25)) * 1024 * 1024
    registry = ModelRegistry(config["models"], root / "models")
    started_at = datetime.now(timezone.utc)
    api_key = str(config["api_key"])

    @app.before_request
    def authorize():
        supplied = request.headers.get("Authorization", "")
        if supplied.startswith("Bearer "):
            supplied = supplied[7:]
        else:
            supplied = request.headers.get("X-API-Key", "")
        if not hmac.compare_digest(supplied.encode(), api_key.encode()):
            return jsonify(error={"message": "Unauthorized", "type": "auth_error"}), 401
        return None

    @app.get("/health")
    def health():
        return jsonify(
            service="Interfayce Remote STT",
            status="ok",
            started_at=started_at.isoformat(),
            default_model=config["default_model"],
            models=registry.status(),
        )

    @app.get("/v1/models")
    def models():
        return jsonify(
            object="list",
            data=[{"id": name, "object": "model", "owned_by": "local"}
                  for name in registry.names],
        )

    @app.post("/v1/audio/transcriptions")
    def transcriptions():
        upload = request.files.get("file")
        if upload is None or not upload.filename:
            return jsonify(error={"message": "Multipart field 'file' is required."}), 400
        model_name = request.form.get("model") or config["default_model"]
        response_format = request.form.get("response_format", "json")
        language = request.form.get("language") or None
        if model_name not in registry.names:
            return jsonify(error={"message": f"Unknown model: {model_name}"}), 400
        suffix = Path(upload.filename).suffix[:12] or ".wav"
        started = time.perf_counter()
        try:
            with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as temporary:
                upload.save(temporary)
                audio_path = Path(temporary.name)
            try:
                text = registry.transcribe(model_name, audio_path, language)
            finally:
                audio_path.unlink(missing_ok=True)
        except Exception as error:
            LOGGER.exception("Transcription failed for model %s", model_name)
            return jsonify(error={"message": str(error), "type": "transcription_error"}), 500
        elapsed = time.perf_counter() - started
        LOGGER.info("Transcribed model=%s seconds=%.3f chars=%s", model_name, elapsed, len(text))
        if response_format == "text":
            return Response(text, content_type="text/plain; charset=utf-8")
        if response_format not in {"json", "verbose_json"}:
            return jsonify(error={"message": f"Unsupported response_format: {response_format}"}), 400
        payload = {"text": text}
        if response_format == "verbose_json":
            payload.update(model=model_name, processing_seconds=round(elapsed, 4))
        return jsonify(payload)

    @app.post("/admin/shutdown")
    def shutdown():
        # Authentication is enforced by before_request. Terminating from inside
        # the process avoids Windows privilege/session mismatches that can make
        # an otherwise authorized Stop-Process fail with Access Denied.
        threading.Timer(0.25, os._exit, args=(0,)).start()
        return jsonify(status="stopping")

    app.extensions["stt_registry"] = registry
    return app


def main() -> None:
    parser = argparse.ArgumentParser(description="Interfayce portable STT server")
    parser.add_argument("--config", type=Path, default=Path("config.json"))
    parser.add_argument("--warm", action="store_true", help="Load the default model before serving")
    parser.add_argument("--warm-only", action="store_true", help="Load models, then exit")
    parser.add_argument("--model", action="append", help="Model to warm; may be repeated")
    args = parser.parse_args()

    root = args.config.resolve().parent
    config = load_config(args.config.resolve())
    log_dir = root / "logs"
    log_dir.mkdir(exist_ok=True)
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=[logging.FileHandler(log_dir / "server.log", encoding="utf-8"), logging.StreamHandler()],
    )
    app = create_app(config, root)
    registry: ModelRegistry = app.extensions["stt_registry"]
    warm_models = args.model or ([config["default_model"]] if args.warm or args.warm_only else [])
    if args.warm_only:
        for name in warm_models:
            LOGGER.info("Warming model %s", name)
            registry.warm(name)
            LOGGER.info("Model %s ready", name)
        return

    def warm_in_background() -> None:
        for name in warm_models:
            try:
                LOGGER.info("Warming model %s", name)
                registry.warm(name)
                LOGGER.info("Model %s ready", name)
            except Exception:
                # The HTTP service remains available so /health exposes the
                # backend error and another model can still be selected.
                LOGGER.exception("Model %s warm-up failed", name)

    if warm_models:
        threading.Thread(
            target=warm_in_background,
            name="InterfayceSttWarm",
            daemon=True,
        ).start()

    from waitress import serve

    LOGGER.info("Listening on http://%s:%s", config["host"], config["port"])
    serve(app, host=config["host"], port=int(config["port"]), threads=4)


if __name__ == "__main__":
    main()
