#!/usr/bin/env python3
"""Compile/run production pure Kotlin timeline projections, not Android code.
Usage: python3 tests/run_host_kotlin.py JAVA_HOME KOTLIN_HOME [HOST_JNI_LIBRARY]
Requires a compiler distribution matching the app Kotlin pin (currently 2.3.21).
Generated jars live only in a temporary directory. No Android stubs are used.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
java_home, kotlin_home = [Path(arg).resolve() for arg in sys.argv[1:3]]
env = dict(os.environ, JAVA_HOME=str(java_home), PATH=str(java_home/'bin')+os.pathsep+os.environ['PATH'])
with tempfile.TemporaryDirectory(prefix='owncut-kotlin-') as directory:
    jar = Path(directory)/'projection-tests.jar'
    sources = [root/'app/src/main/java/com/owncut/app'/name for name in ('TimelineSnapshot.kt','TimelineClipIndex.kt','ColorEngine.kt')]
    subprocess.run(['bash',str(kotlin_home/'bin/kotlinc'),*[str(p) for p in sources],
                    str(root/'tests/kotlin/TimelineProjectionTest.kt'),'-Werror','-include-runtime','-d',str(jar)],env=env,check=True)
    subprocess.run([str(java_home/'bin/java'),'-jar',str(jar)],env=env,check=True)

# Pure production preview UI/transport policies need neither Android stubs nor JNI.
with tempfile.TemporaryDirectory(prefix='owncut-preview-policy-') as directory:
    jar = Path(directory)/'preview-policy.jar'
    subprocess.run(['bash',str(kotlin_home/'bin/kotlinc'),
        *[str(root/'app/src/main/java/com/owncut/app'/name) for name in
          ('PreviewPlaybackState.kt','PreviewFrameSize.kt','PreviewProgress.kt','PreviewWorkQueue.kt')],
        str(root/'tests/kotlin/PreviewPolicyTest.kt'),'-Werror','-include-runtime','-d',str(jar)],env=env,check=True)
    subprocess.run([str(java_home/'bin/java'),'-jar',str(jar)],env=env,check=True)

# Original slider transport and bounded error diagnostics (no Android/JNI stubs).
with tempfile.TemporaryDirectory(prefix='owncut-controls-') as directory:
    jar = Path(directory)/'controls.jar'
    subprocess.run(['bash',str(kotlin_home/'bin/kotlinc'),
        *[str(root/'app/src/main/java/com/owncut/app'/name) for name in
          ('ColorSliderScale.kt','ColorPreviewFailure.kt','ExportFailureReport.kt','ExportRoutePolicy.kt','WheelDragSession.kt','WheelPalette.kt')],
        str(root/'tests/kotlin/ColorControlsTest.kt'),'-Werror','-include-runtime','-d',str(jar)],env=env,check=True)
    subprocess.run([str(java_home/'bin/java'),'-jar',str(jar)],env=env,check=True)

# Optional actual production ColorEngine bridge, not generated Android stubs.
if len(sys.argv) == 4:
    import shutil
    library = Path(sys.argv[3]).resolve()
    with tempfile.TemporaryDirectory(prefix='owncut-color-kotlin-') as directory:
        jar = Path(directory)/'color-bridge.jar'
        shutil.copyfile(library,Path(directory)/'libowncut_android.so')
        subprocess.run(['bash',str(kotlin_home/'bin/kotlinc'),
            str(root/'app/src/main/java/com/owncut/app/ColorEngine.kt'),
            str(root/'app/src/main/java/com/owncut/app/ColorFrameBuffers.kt'),
            str(root/'app/src/main/java/com/owncut/app/ColorGradeDraft.kt'),
            str(root/'tests/kotlin/ColorBridgeTest.kt'),'-Werror','-include-runtime','-d',str(jar)],env=env,check=True)
        subprocess.run([str(java_home/'bin/java'),'-Xcheck:jni','--enable-native-access=ALL-UNNAMED',
            '-Djava.library.path='+directory,'-jar',str(jar)],env=env,check=True)
    with tempfile.TemporaryDirectory(prefix='owncut-color-plan-') as directory:
        jar = Path(directory)/'color-plan.jar'
        shutil.copyfile(library,Path(directory)/'libowncut_android.so')
        subprocess.run(['bash',str(kotlin_home/'bin/kotlinc'),
            *[str(root/'app/src/main/java/com/owncut/app'/name) for name in
                ('ColorEngine.kt','TimelineSnapshot.kt','ColorRenderPlan.kt','DecoderColorGate.kt','DecoderOutputSequence.kt','PreviewProgress.kt','PreviewColorPolicy.kt')],
            str(root/'tests/kotlin/ColorRenderPlanTest.kt'),'-Werror','-include-runtime','-d',str(jar)],env=env,check=True)
        subprocess.run([str(java_home/'bin/java'),'-Xcheck:jni','--enable-native-access=ALL-UNNAMED',
            '-Djava.library.path='+directory,'-jar',str(jar)],env=env,check=True)

# Actual codec buffer/format transport state; no Android stubs or fake JNI.
with tempfile.TemporaryDirectory(prefix='owncut-decoder-lease-') as directory:
    jar = Path(directory)/'decoder-lease.jar'
    subprocess.run(['bash',str(kotlin_home/'bin/kotlinc'),
        str(root/'app/src/main/java/com/owncut/app/DecoderBufferLease.kt'),
        str(root/'tests/kotlin/DecoderBufferLeaseTest.kt'),'-Werror','-include-runtime','-d',str(jar)],env=env,check=True)
    subprocess.run([str(java_home/'bin/java'),'-jar',str(jar)],env=env,check=True)

with tempfile.TemporaryDirectory(prefix='owncut-color-input-') as directory:
    jar = Path(directory)/'color-input.jar'
    subprocess.run(['bash',str(kotlin_home/'bin/kotlinc'),
        str(root/'app/src/main/java/com/owncut/app/ColorEngine.kt'),
        str(root/'app/src/main/java/com/owncut/app/PreviewColorPolicy.kt'),
        str(root/'tests/kotlin/ColorInputPolicyTest.kt'),'-Werror','-include-runtime','-d',str(jar)],env=env,check=True)
    subprocess.run([str(java_home/'bin/java'),'-jar',str(jar)],env=env,check=True)
