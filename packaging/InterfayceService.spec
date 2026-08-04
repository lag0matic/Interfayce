from pathlib import Path

project_root = Path(SPECPATH).parent

a = Analysis(
    [str(project_root / "packaging" / "interfayce_service.py")],
    pathex=[str(project_root / "src")],
    binaries=[],
    datas=[(str(project_root / "VERSION"), ".")],
    hiddenimports=[
        "_sherpa_onnx",
        "numpy",
        "pyaudio",
        "sherpa_onnx",
        "speech_recognition",
        "winrt.windows.foundation",
        "winrt.windows.foundation.collections",
        "winrt.windows.media.control",
        "winrt.windows.storage.streams",
    ],
    excludes=[
        "boto3",
        "botocore",
        "faster_whisper",
        "google",
        "grpc",
        "matplotlib",
        "openai",
        "pandas",
        "pocketsphinx",
        "scipy",
        "soundfile",
        "tensorflow",
        "torch",
        "torchaudio",
        "torchvision",
        "transformers",
        "whisper",
    ],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="InterfayceService",
    console=False,
    icon=str(project_root / "assets" / "branding" / "interfayce.ico"),
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    name="InterfayceService",
)
