#!/usr/bin/env python3
"""Pre-build key/pin/approved-APK check. Never generate a replacement signing key.

The real built APK must still pass verify_patch.py afterwards. This check does not
prove phone installation, available billing, or possession of a usable key password.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fingerprint(value):
    normalized = value.replace(':', '').strip().lower()
    require(re.fullmatch(r'[0-9a-f]{64}', normalized), 'Invalid pinned certificate SHA-256')
    return normalized


def verify(reference, signer, expected, version):
    expected = fingerprint(expected)
    require(str(version).isdigit() and 2 <= int(version) <= 2100000000, 'Invalid version code')
    keystore = os.environ.get('OWNCUT_KEYSTORE_PATH', '')
    alias = os.environ.get('OWNCUT_KEY_ALIAS', '')
    require(keystore and Path(keystore).is_file() and alias and
            os.environ.get('OWNCUT_KEYSTORE_PASSWORD') and os.environ.get('OWNCUT_KEY_PASSWORD'),
            'Complete existing signing configuration is required; do not generate a new key')
    # Password remains in the environment, never argv/logs or a source-controlled file.
    result = subprocess.run(['keytool', '-exportcert', '-keystore', keystore,
                             '-alias', alias, '-storepass:env', 'OWNCUT_KEYSTORE_PASSWORD'],
                            check=True, capture_output=True)
    require(hashlib.sha256(result.stdout).hexdigest() == expected,
            'Keystore certificate differs from pin; signing identity not changed')
    certs = subprocess.run([signer, 'verify', '--print-certs', reference], check=True,
                           capture_output=True, text=True).stdout
    matches = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]+)', certs)
    require(len(matches) == 1 and fingerprint(matches[0]) == expected,
            'Approved APK signer differs from key/pin; install-over candidate refused')
    badging = subprocess.run([str(Path(signer).with_name('aapt2')), 'dump', 'badging', reference],
                             check=True, capture_output=True, text=True).stdout
    identity = re.search(r"package: name='([^']+)' versionCode='(\d+)'", badging)
    require(identity and identity[1] == 'com.owncut.app' and int(identity[2]) < int(version),
            'Approved APK package/version is not a valid update reference')
    print('Existing key, pinned certificate and approved APK match; final APK verification still required')


if __name__ == '__main__':
    try:
        require(len(sys.argv) == 5, 'Usage: verify_signing_identity.py reference.apk apksigner certSha256 versionCode')
        verify(*sys.argv[1:])
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        # Do not dump tool stderr/environment: private signing diagnostics stay private.
        print(f'Signing preflight failed: {error}', file=sys.stderr)
        sys.exit(1)
