"""Download a pinned official sherpa-onnx export without changing server config."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tarfile
from urllib.request import urlopen

PACKAGE = "sherpa-onnx-nemotron-speech-streaming-en-0.6b-560ms-int8-2026-04-25"
SHA256 = "78e2b79fcf7271553a74402a76b771b09ea40117a39566a79f52235b23db6358"
URL = f"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/{PACKAGE}.tar.bz2"


def prepare(root):
    root.mkdir(parents=True, exist_ok=True)
    archive = root / (PACKAGE + ".tar.bz2")
    def checksum(path):
        with path.open("rb") as f:
            return hashlib.file_digest(f, "sha256").hexdigest()
    if not archive.exists() or checksum(archive) != SHA256:
        partial = archive.with_suffix(".partial")
        print("Downloading verified Nemotron package (464 MB)...", flush=True)
        with urlopen(URL, timeout=60) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        if checksum(partial) != SHA256:
            raise RuntimeError("Model download checksum mismatch; not activating.")
        partial.replace(archive)
    folder = root / "nemotron-en-560ms"
    folder.mkdir(exist_ok=True)
    # Only explicit regular files; never unpack archive paths or links.
    with tarfile.open(archive) as tar:
        for filename in ("encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt", "test_wavs/0.wav"):
            member = tar.getmember(PACKAGE + "/" + filename)
            if not member.isfile():
                raise ValueError("Unexpected model archive member")
            destination = folder / Path(filename).name
            temporary = destination.with_suffix(destination.suffix + ".partial")
            with tar.extractfile(member) as source, temporary.open("wb") as output:
                shutil.copyfileobj(source, output)
            temporary.replace(destination)
    print(f"Model ready: {folder}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-root", type=Path, default=Path(__file__).parent / "models")
    prepare(parser.parse_args().model_root)
