"""Only exercise refusal paths; never connect to or reset an Android device."""
import os,subprocess,tempfile,unittest
from pathlib import Path
SCRIPT=Path(__file__).resolve().parents[1]/'scripts/verify_install_over.sh'
class InstallOverGuardTest(unittest.TestCase):
    def run_guard(self, devices, ci=True):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); adb=root/'adb'; log=root/'commands'
            adb.write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> "$GUARD_LOG"\nprintf "%s\\n" "$FAKE_DEVICES"\n');adb.chmod(0o700)
            env=dict(os.environ,PATH=str(root)+os.pathsep+os.environ['PATH'],GUARD_LOG=str(log),FAKE_DEVICES=devices,CI='true' if ci else 'false',GITHUB_ACTIONS='true' if ci else 'false')
            result=subprocess.run(['bash',str(SCRIPT)],env=env,text=True,capture_output=True)
            self.assertEqual(2,result.returncode,result.stdout+result.stderr)
            self.assertEqual('devices\n' if ci else '',log.read_text() if log.exists() else '')
    def test_outside_ci_no_adb_called(self): self.run_guard('emulator-5554\tdevice',ci=False)
    def test_physical_phone_refused(self): self.run_guard('phone-serial\tdevice')
    def test_two_emulators_refused(self): self.run_guard('emulator-5554\tdevice\nemulator-5556\tdevice')
if __name__=='__main__': unittest.main()
