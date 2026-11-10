#!/usr/bin/env python3
"""Host policy checks using fake command results, NOT a real APK/signing run."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('signing', ROOT/'scripts/verify_signing_identity.py')
signing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(signing)


class SigningPreflightTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.key = Path(self.temp.name)/'existing.jks'
        self.key.touch()
        self.env = patch.dict(os.environ, {
            'OWNCUT_KEYSTORE_PATH': str(self.key), 'OWNCUT_KEY_ALIAS': 'old-key',
            'OWNCUT_KEYSTORE_PASSWORD': 'private-store', 'OWNCUT_KEY_PASSWORD': 'private-key'})
        self.env.start()
        self.addCleanup(self.env.stop)
        self.cert = b'fake public DER bytes'
        self.pin = hashlib.sha256(self.cert).hexdigest()

    def run_check(self, cert=None, signer=None, package='com.owncut.app', old=1, version='2000'):
        calls = [subprocess.CompletedProcess([], 0, self.cert if cert is None else cert),
                 subprocess.CompletedProcess([], 0, f'Signer #1 certificate SHA-256 digest: {signer or self.pin}\n'),
                 subprocess.CompletedProcess([], 0, f"package: name='{package}' versionCode='{old}'")]
        with patch.object(signing.subprocess, 'run', side_effect=calls) as run:
            signing.verify('approved.apk', '/sdk/apksigner', self.pin, version)
            args = run.call_args_list[0].args[0]
            self.assertIn('-storepass:env', args)
            self.assertNotIn('private-store', args)
            self.assertNotIn('private-key', args)

    def test_match_and_password_not_in_argv(self):
        self.run_check()

    def test_wrong_keystore(self):
        with self.assertRaises(ValueError): self.run_check(cert=b'new identity')

    def test_wrong_approved_signer(self):
        with self.assertRaises(ValueError): self.run_check(signer='f'*64)

    def test_wrong_package(self):
        with self.assertRaises(ValueError): self.run_check(package='other.app')

    def test_non_increasing_version(self):
        with self.assertRaises(ValueError): self.run_check(old=2000)

    def test_missing_existing_key(self):
        self.key.unlink()
        with patch.object(signing.subprocess, 'run') as run:
            with self.assertRaises(ValueError):
                signing.verify('approved.apk', '/sdk/apksigner', self.pin, '2000')
            run.assert_not_called()

    def test_invalid_pin(self):
        for pin in ['', 'f'*63, 'x'*64, 'f'*64+'junk']:
            with self.assertRaises(ValueError): signing.fingerprint(pin)

    def test_invalid_version(self):
        for version in ['1', '-2', '2.0', '2100000001']:
            with self.assertRaises(ValueError): self.run_check(version=version)

    def test_tool_failure_is_not_success(self):
        with patch.object(signing.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'keytool')):
            with self.assertRaises(subprocess.CalledProcessError):
                signing.verify('approved.apk', '/sdk/apksigner', self.pin, '2000')


if __name__ == '__main__':
    unittest.main()
