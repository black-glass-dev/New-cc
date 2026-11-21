import contextlib, io, runpy, sys, tempfile, unittest
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace

SCRIPT = Path(__file__).resolve().parents[1]/'scripts/verify_patch.py'
class PatchMetadataTest(unittest.TestCase):
    def check_case(self, expected='aa'*32, badging="package: name='com.owncut.app' versionCode='1050'", success=True, reference_cert='aa'*32, reference_code='1'):
        with tempfile.TemporaryDirectory() as directory:
            apk=Path(directory)/'app.apk'; apk.write_bytes(b'not-a-real-apk-mocked-tool-test')
            reference=Path(directory)/'reference.apk';reference.write_bytes(b'approved-reference-mock')
            outputs=[SimpleNamespace(stdout='Signer #1 certificate SHA-256 digest: '+'aa'*32),SimpleNamespace(stdout=badging),SimpleNamespace(stdout='Signer #1 certificate SHA-256 digest: '+reference_cert),SimpleNamespace(stdout="package: name='com.owncut.app' versionCode='"+reference_code+"'")]
            with patch.object(sys,'argv',[str(SCRIPT),str(apk),'/tools/apksigner',expected,'1050',str(reference)]), patch('subprocess.run',side_effect=outputs), contextlib.redirect_stdout(io.StringIO()):
                if success: runpy.run_path(str(SCRIPT),run_name='__main__'); self.assertTrue((apk.parent/'update-metadata.json').is_file())
                else:
                    with self.assertRaises(ValueError): runpy.run_path(str(SCRIPT),run_name='__main__')
                    self.assertFalse((apk.parent/'update-metadata.json').exists())
    def test_pin_success(self): self.check_case()
    def test_wrong_pin(self): self.check_case(expected='bb'*32,success=False)
    def test_wrong_version(self): self.check_case(badging="package: name='com.owncut.app' versionCode='1'",success=False)
    def test_wrong_package(self): self.check_case(badging="package: name='com.other' versionCode='1050'",success=False)
    def test_debug_rejected(self): self.check_case(badging="package: name='com.owncut.app' versionCode='1050'\napplication-debuggable",success=False)
    def test_legacy_signer_mismatch(self): self.check_case(reference_cert='bb'*32,success=False)
    def test_reference_not_older(self): self.check_case(reference_code='1050',success=False)
if __name__ == '__main__': unittest.main()
