import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('model_archive', Path(__file__).resolve().parents[1] / 'packaging/prepare_model_archive.py')
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)

class ModelArchiveTests(unittest.TestCase):
    def test_reuse_change_and_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, cache = Path(tmp) / 'model', Path(tmp) / 'cache'
            source.mkdir()
            for name in archive.FILES:
                (source / name).write_bytes(name.encode() * 10)
            first = archive.prepare(source, cache)
            self.assertFalse(first['cached'])
            target = Path(first['archive'])
            first_time = target.stat().st_mtime_ns
            self.assertTrue(archive.prepare(source, cache)['cached'])
            self.assertEqual(target.stat().st_mtime_ns, first_time)
            # Same length, different contents must invalidate the cache.
            (source / archive.FILES[0]).write_bytes(b'x' * (source / archive.FILES[0]).stat().st_size)
            self.assertFalse(archive.prepare(source, cache)['cached'])
            target.write_bytes(b'corrupt')
            self.assertFalse(archive.prepare(source, cache)['cached'])
            with zipfile.ZipFile(target) as contents:
                self.assertEqual(set(contents.namelist()), set(archive.FILES))
                for name in archive.FILES:
                    self.assertEqual(contents.read(name), (source / name).read_bytes())
            self.assertTrue(archive.prepare(source, cache)['cached'])
