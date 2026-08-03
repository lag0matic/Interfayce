"""Small Windows DPAPI-backed store for OAuth tokens and future API keys."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import os
from pathlib import Path


class _DataBlob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_ubyte))]


_ENTROPY = b"Interfayce secure settings v1"
_CRYPTPROTECT_UI_FORBIDDEN = 0x1


def _windows_apis():
    crypt32 = ctypes.windll.crypt32
    kernel32 = ctypes.windll.kernel32
    crypt32.CryptProtectData.argtypes = [
        ctypes.POINTER(_DataBlob), wintypes.LPCWSTR, ctypes.POINTER(_DataBlob),
        ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(_DataBlob),
    ]
    crypt32.CryptProtectData.restype = wintypes.BOOL
    crypt32.CryptUnprotectData.argtypes = [
        ctypes.POINTER(_DataBlob), ctypes.POINTER(wintypes.LPWSTR), ctypes.POINTER(_DataBlob),
        ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(_DataBlob),
    ]
    crypt32.CryptUnprotectData.restype = wintypes.BOOL
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    return crypt32, kernel32


def _blob(data: bytes) -> tuple[_DataBlob, object]:
    buffer = ctypes.create_string_buffer(data)
    return _DataBlob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte))), buffer


def protect(data: bytes) -> bytes:
    crypt32, kernel32 = _windows_apis()
    source, source_buffer = _blob(data)
    entropy, entropy_buffer = _blob(_ENTROPY)
    output = _DataBlob()
    if not crypt32.CryptProtectData(
        ctypes.byref(source), "Interfayce", ctypes.byref(entropy), None, None,
        _CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(output)
    ):
        raise ctypes.WinError()
    try:
        return ctypes.string_at(output.pbData, output.cbData)
    finally:
        kernel32.LocalFree(output.pbData)
        del source_buffer, entropy_buffer


def unprotect(data: bytes) -> bytes:
    crypt32, kernel32 = _windows_apis()
    source, source_buffer = _blob(data)
    entropy, entropy_buffer = _blob(_ENTROPY)
    output = _DataBlob()
    if not crypt32.CryptUnprotectData(
        ctypes.byref(source), None, ctypes.byref(entropy), None, None,
        _CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(output)
    ):
        raise ctypes.WinError()
    try:
        return ctypes.string_at(output.pbData, output.cbData)
    finally:
        kernel32.LocalFree(output.pbData)
        del source_buffer, entropy_buffer


def secure_directory() -> Path:
    configured = os.environ.get("INTERFAYCE_SECURE_DIRECTORY")
    if configured:
        return Path(configured).expanduser()
    local = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    return local / "Interfayce" / "secure"


def write_secret(name: str, data: bytes) -> None:
    directory = secure_directory()
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{name}.dpapi"
    temporary = path.with_suffix(".tmp")
    temporary.write_bytes(protect(data))
    temporary.replace(path)


def read_secret(name: str) -> bytes | None:
    try:
        return unprotect((secure_directory() / f"{name}.dpapi").read_bytes())
    except FileNotFoundError:
        return None


def delete_secret(name: str) -> bool:
    path = secure_directory() / f"{name}.dpapi"
    try:
        path.unlink()
        return True
    except FileNotFoundError:
        return False
