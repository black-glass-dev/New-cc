# ADR0032: Native wheel input, no slider UI, conservative HDR-placeholder policy

Date: 2026-10-25. Status: APK published, native/JNI policies pass; phone acceptance pending.

User chose original lawful implementation, explicitly rejects sliders, reports4K10-bit.
Do not use downloaded decompiled NodeVideo code/assets. No such material is imported.

## Input design

Primary / Wheels / Channels tabs. Four original painted discs: Balance, Lift, Gamma,
Gain; separate relative drag-number controls and48dp fine-step buttons for all17settings.
Remove GradePrecisionSlider; no SeekBar widget in Color editor. Numeric controls expose
range actions/keyboard as accessible alternatives to pointer-only decorative discs.

C++ wheel_adjust/wheel_position own a zero-mean RGB-plane input mapping, not a copied
Resolve grading model. Preserve the base group's arithmetic master, other settings and
enabled flag; radial pointer normalization and existing parameter bounds apply. Clamping
can change master at limits. Group radii are explicit tool sensitivity, not calibrated
Kelvin/hue/saturation or professional output parity. Snapshot gesture base avoids drift.
Kotlin only renders disc, transports coordinates and edits numerical parameters.

## HDR false-presence mitigation

Previously every nonempty HDR byte buffer was present. New native policy treats absent
and exact25-byte all-zero CTA-sized padding as unset. Any nonzero, unknown type or malformed
nonempty length remains present/rejected. Apply consistently to extractor, raw decoder and
Media3 ColorInfo. ByteBuffer cursor is preserved; inspect bytes0..limit, not capacity or
position. This fixes a possible placeholder false rejection; user's exact payload not
captured, so no claim root cause confirmed. PQ/HLG/unknown primaries/transfer still reject
independently; no tone-map bypass or universal HDR admission.

## 4K / bit depth boundary

10-bit does not imply HDR. User reports4K10-bit; old screenshot reports SDR/8-bit decoder
format, not authoritative source-file verification. No exact file profile/depth measured.
Native still path remains8-bit sRGB proxy within1280×720, not10-bit grading/export proof;
source media/resolution unchanged. Decoder-internal memory not bounded by proxy request.
Shipping graded-export guards unchanged. No new dependency or proprietary code.

## Evidence

Local actual C++ color suite passes: all25nonzero-byte cases, malformed/empty lengths,
4wheel groups/1,764pointer pairs, identity, coordinate roundtrip and invalid inputs plus
prior color regressions.331strings/3themes/17labels and whitespace pass. Added actual JNI
host-test source and Android transport/UI regression source (no sliders/4wheels/numeric
Apply/reference/fine steps); not run yet. Android compile/publication and phone execution
pending;25%/75%. Exact metadata classification/device behavior remains acceptance gate.

## Executed checkpoint

Android40199335976 and Portable40199335868 atde4bad3 PASS; actual fresh-install prerelease
fresh-install-40199335976 verified8,045,179byte APK SHA256
97627b9f8eb07049fe8cfd3f80b5e2dc58eb5ff450452620f201e65ae6f3787b.
Make6/color ASan+UBSan, strict production JNI build (-Wconversion/-Wsign-conversion), real
JVM/JNI and pinned Kotlin2.3.21 -Werror checks PASS including new wheel/native HDR policy
and prior buffers/settings/plan/gates. Host tools reused reviewed jdk4py25.0.2.1/JPype1.7.1,
OpenJDK17u JNI headers and compiler archive SHA512 verified outside Git. UI regressions
compiled, no phone/emulator run. Live failures are now compact with selectable Details;
wheel shaders/unchanged labels cached, not a measured performance claim.4K10-bit source
not independently inspected;8-bit proxy does not preserve/prove10-bit output. Only docs
changed after artifact commit. The initial3226a94 Android run was cancelled by final retry.
