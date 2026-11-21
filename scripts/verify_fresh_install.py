#!/usr/bin/env python3
"""Verify an explicitly requested fresh-install DEBUG candidate, not an update patch."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def verify(apk, signer, version, revision):
    if not version.isdigit() or not 2 <= int(version) <= 2100000000:
        raise ValueError('Invalid candidate version')
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('An exact source revision is required')
    certificates = subprocess.run([signer, 'verify', '--verbose', '--print-certs', apk],
                                  check=True, text=True, capture_output=True).stdout
    matches = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]{64})\b', certificates)
    if len(matches) != 1:
        raise ValueError('Exactly one verified APK signer is required')
    badging = subprocess.run([str(Path(signer).with_name('aapt2')), 'dump', 'badging', apk],
                             check=True, text=True, capture_output=True).stdout
    identity = re.search(r"package: name='([^']+)' versionCode='(\d+)'", badging)
    if not identity or identity[1] != 'com.owncut.app' or int(identity[2]) != int(version):
        raise ValueError('Fresh-install APK package/version mismatch')
    if 'application-debuggable' not in badging:
        raise ValueError('This publishing route is restricted to debug test APKs')
    file = Path(apk)
    metadata = dict(packageName=identity[1], versionCode=int(version), sourceRevision=revision,
                    apkSha256=hashlib.sha256(file.read_bytes()).hexdigest(),
                    signerSha256=matches[0].lower(), format='fresh-install-debug-apk',
                    updateCompatibility='not-guaranteed', stableSigning=False,
                    deviceValidation='not-run', phoneValidation='owned-by-user; not asserted by CI',
                    warning='Back up projects before uninstalling; uninstall may remove app-local data. '
                            'This temporary debug signer may change between builds.')
    (file.parent/'fresh-install-metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
    print('Fresh-install debug APK identity verified; update compatibility NOT asserted')


if __name__ == '__main__':
    if len(sys.argv) != 5:
        sys.exit('Usage: verify_fresh_install.py apk apksigner version sourceRevision')
    verify(*sys.argv[1:])
