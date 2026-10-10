"""Cache the bundled model archive by source content and verify it before reuse."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

FILES = ('encoder.int8.onnx', 'decoder.int8.onnx', 'joiner.int8.onnx', 'tokens.txt', 'MODEL-SOURCE.md')

def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def prepare(model_dir, cache_dir):
    model_dir, cache_dir = Path(model_dir), Path(cache_dir)
    cache_dir.mkdir(parents=True, exist_ok=True)
    archive = cache_dir / 'parakeet-model.zip'
    manifest_path = cache_dir / 'parakeet-model.json'
    files = [{'name': name, 'size': (model_dir / name).stat().st_size,
              'sha256': sha256(model_dir / name)} for name in FILES]
    cached = False
    try:
        manifest = json.loads(manifest_path.read_text())
        cached = (manifest['format'] == 'zip-deflate-6-v1' and manifest['files'] == files
                  and manifest['archive_sha256'] == sha256(archive))
    except (OSError, ValueError, KeyError):
        pass
    if not cached:
        temporary = archive.with_suffix('.zip.tmp')
        try:
            with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
                for entry in files:
                    output.write(model_dir / entry['name'], entry['name'])
            # Verify the archive contains exactly the bytes whose identity we cached.
            with zipfile.ZipFile(temporary) as check:
                for entry in files:
                    with check.open(entry['name']) as stream:
                        if hashlib.file_digest(stream, 'sha256').hexdigest() != entry['sha256']:
                            raise RuntimeError('Model changed while building the archive')
            temporary.replace(archive)
            manifest = {'format': 'zip-deflate-6-v1', 'files': files, 'archive_sha256': sha256(archive)}
            manifest_tmp = manifest_path.with_suffix('.json.tmp')
            manifest_tmp.write_text(json.dumps(manifest, indent=2))
            manifest_tmp.replace(manifest_path)
        finally:
            temporary.unlink(missing_ok=True)
    return {'archive': str(archive.resolve()), 'unpacked_size': sum(e['size'] for e in files), 'cached': cached}

if __name__ == '__main__':
    print(json.dumps(prepare(sys.argv[1], sys.argv[2])))
