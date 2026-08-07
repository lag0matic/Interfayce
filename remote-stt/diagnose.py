from __future__ import annotations

import argparse
import ctypes.util
import importlib.util
import json
from pathlib import Path
import platform
import os
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8-sig"))
    print(f"Python: {sys.version.split()[0]} ({platform.machine()})")
    failed = False
    for name, options in config["models"].items():
        backend = options.get("backend")
        module = "faster_whisper" if backend == "faster-whisper" else "moonshine_voice"
        present = importlib.util.find_spec(module) is not None
        print(f"{name}: {backend}; package={'ok' if present else 'MISSING'}")
        failed |= not present
        if backend == "faster-whisper" and options.get("device") == "cuda":
            cuda_dlls = ["cublas64_12", "cudnn_ops64_9"]
            search_dirs = [Path(part) for part in os.environ.get("PATH", "").split(os.pathsep)
                           if part]
            found = {dll: bool(ctypes.util.find_library(dll)) or any(
                (directory / f"{dll}.dll").is_file() for directory in search_dirs
            ) for dll in cuda_dlls}
            print("  GPU runtime DLL discovery: " + ", ".join(
                f"{dll}={'found' if value else 'not on PATH'}" for dll, value in found.items()
            ))
            if not all(found.values()):
                print("  NOTE: model warm-up is the definitive CUDA test. CUDA 12 cuBLAS and cuDNN 9 are required.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
