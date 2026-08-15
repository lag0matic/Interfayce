# Interfayce Parakeet model payload

Interfayce uses the four adjacent, git-ignored runtime files from
`sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8`:

- `encoder.int8.onnx` — `ACFC2B4456377E15D04F0243AF540B7FE7C992F8D898D751CF134C3A55FD2247`
- `decoder.int8.onnx` — `179E50C43D1A9DE79C8A24149A2F9BAC6EB5981823F2A2ED88D655B24248DB4E`
- `joiner.int8.onnx` — `3164C13FC2821009440D20FCB5FDC78BFF28B4DB2F8D0F0B329101719C0948B3`
- `tokens.txt` — `D58544679EA4BC6AC563D1F545EB7D474BD6CFA467F0A6E2C1DC1C7D37E3C35D`

The local payload was copied from the installed COVAS Labs Parakeet STT plugin.
Upstream project and model identification:
https://github.com/COVAS-Labs/plugin-parakeet-stt

The binaries are intentionally excluded from ordinary Git history because the
model is approximately 639 MB. The installer build fails clearly if any required
file is absent, and packages the local payload under `models/parakeet`.
