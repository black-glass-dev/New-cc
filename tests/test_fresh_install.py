"""Mock-based metadata/refusal checks; not real signing or phone installation."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fresh', ROOT/'scripts/verify_fresh_install.py')
fresh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fresh)


class FreshInstallTest(unittest.TestCase):
    def check_case(self, *, cert='aa'*32, package='com.owncut.app', version='1234',
                   actual=1234, revision='a'*40, debug=True, success=False):
        with tempfile.TemporaryDirectory() as folder:
            apk = Path(folder)/'candidate.apk'
            apk.write_bytes(b'mock-not-an-apk')
            results = [subprocess.CompletedProcess([], 0, 'Signer #1 certificate SHA-256 digest: '+cert),
                       subprocess.CompletedProcess([], 0, f"package: name='{package}' versionCode='{actual}'\n"+
                                                   ('application-debuggable' if debug else ''))]
            with patch.object(fresh.subprocess, 'run', side_effect=results), contextlib.redirect_stdout(io.StringIO()):
                if success:
                    fresh.verify(str(apk), '/sdk/apksigner', version, revision)
                    meta = json.loads((apk.parent/'fresh-install-metadata.json').read_text())
                    self.assertEqual(meta['format'], 'fresh-install-debug-apk')
                    self.assertFalse(meta['stableSigning'])
                    self.assertEqual(meta['updateCompatibility'], 'not-guaranteed')
                    self.assertEqual(meta['deviceValidation'], 'not-run')
                    self.assertEqual(meta['sourceRevision'], revision)
                    self.assertNotIn('upgradeReferenceSha256', meta)
                else:
                    with self.assertRaises(ValueError):
                        fresh.verify(str(apk), '/sdk/apksigner', version, revision)
                    self.assertFalse((apk.parent/'fresh-install-metadata.json').exists())

    def test_explicit_fresh_metadata(self): self.check_case(success=True)
    def test_wrong_package(self): self.check_case(package='other.app')
    def test_wrong_version(self): self.check_case(actual=1)
    def test_release_refused(self): self.check_case(debug=False)
    def test_bad_certificate(self): self.check_case(cert='not-a-fingerprint')
    def test_bad_version(self): self.check_case(version='-1')
    def test_invalid_revision(self): self.check_case(revision='HEAD')
    def test_unsigned_refused(self):
        with patch.object(fresh.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'apksigner')):
            with self.assertRaises(subprocess.CalledProcessError):
                fresh.verify('unsigned.apk', '/sdk/apksigner', '1234', 'a'*40)


if __name__ == '__main__':
    unittest.main()
