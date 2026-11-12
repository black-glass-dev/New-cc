#!/usr/bin/env python3
"""Fail closed on unexpected signer; publish only public identity/upgrade metadata."""
import hashlib,json,re,subprocess,sys
from pathlib import Path
apk, signer, expected, version, reference = sys.argv[1:]
def require(condition, message):
    if not condition: raise ValueError(message)
require(2 <= int(version) <= 2100000000, 'Invalid version code')
normalize = lambda value: re.sub('[^0-9a-f]', '', value.lower())
expected = normalize(expected)
require(len(expected) == 64, 'A pinned SHA-256 certificate fingerprint is required')
result = subprocess.run([signer,'verify','--verbose','--print-certs',apk],check=True,text=True,capture_output=True).stdout
certs = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]+)',result)
require(len(certs) == 1 and normalize(certs[0]) == expected, 'Signing certificate mismatch')
file = Path(apk)
aapt = str(Path(signer).with_name('aapt2'))
badging = subprocess.run([aapt,'dump','badging',apk],check=True,text=True,capture_output=True).stdout
identity = re.search(r"package: name='([^']+)' versionCode='(\d+)'", badging)
require(identity and identity[1] == 'com.owncut.app' and int(identity[2]) == int(version), 'APK package/version mismatch')
require('application-debuggable' not in badging, 'Publish the non-debuggable release APK')
# The first stable patch must also match the last user-approved legacy APK.
# A newly generated, self-consistent key+pin is not proof it can update that app.
reference_result = subprocess.run([signer,'verify','--print-certs',reference],check=True,text=True,capture_output=True).stdout
reference_certs = re.findall(r'Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]+)',reference_result)
require(len(reference_certs) == 1 and normalize(reference_certs[0]) == expected, 'Approved previous APK has a different signer; update publication refused')
reference_badging = subprocess.run([aapt,'dump','badging',reference],check=True,text=True,capture_output=True).stdout
reference_identity = re.search(r"package: name='([^']+)' versionCode='(\d+)'",reference_badging)
require(reference_identity and reference_identity[1] == 'com.owncut.app' and int(reference_identity[2]) < int(version), 'Invalid approved upgrade reference')
metadata = dict(packageName='com.owncut.app',versionCode=int(version),signerSha256=expected,
                apkSha256=hashlib.sha256(file.read_bytes()).hexdigest(),
                upgradeReferenceSha256=hashlib.sha256(Path(reference).read_bytes()).hexdigest(),format='full-apk-update',
                legacyDebugCompatibility='not-guaranteed',requiresSameSigningCertificate=True)
(file.parent/'update-metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
print('Pinned signer verified; update metadata generated')
