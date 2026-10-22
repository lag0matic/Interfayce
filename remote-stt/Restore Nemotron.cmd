@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0enable-whisper-streaming.ps1" -Rollback
pause
