#!/usr/bin/env python3
"""Host check of the native 16-bit FFV1 writer (platform/android/jni/ffv1_writer_jni.cpp).

Usage:
  python3 tests/native/run_ffv1_host_check.py --ffmpeg-prefix DIR --ffmpeg BIN [--jni-include DIR]

--ffmpeg-prefix: a host FFmpeg install with headers in include/ and static libs in lib/ (libavformat, libavcodec,
                 libavutil). The writer code is compiled against it.
--ffmpeg:        an ffmpeg executable that can decode FFV1 MKV, used only to decode the output for comparison.

Pass means: the decoded RGBA64 samples equal an independent NumPy computation of clamp(v,0,1)*65535 rounded to
nearest even, for every sample of every frame. Not an Android or device result.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
WIDTH, HEIGHT, FRAMES = 64, 48, 3


def expected_frames():
    y, x = np.mgrid[0:HEIGHT, 0:WIDTH]
    out = np.zeros((FRAMES, HEIGHT, WIDTH, 4), dtype=np.uint16)
    for f in range(FRAMES):
        for c in range(4):
            v = ((x * 7 + y * 13 + f * 29 + c * 53) % 400).astype(np.float32) / np.float32(200.0) - np.float32(0.5)
            out[f, :, :, c] = np.rint(np.clip(v, 0.0, 1.0) * np.float32(65535.0)).astype(np.uint16)
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--ffmpeg-prefix', required=True)
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--jni-include', action='append', default=[])
    args = parser.parse_args()

    prefix = Path(args.ffmpeg_prefix).resolve()
    with tempfile.TemporaryDirectory(prefix='owncut-ffv1-') as directory:
        work = Path(directory)
        exe = work / 'ffv1_host_test'
        cmd = ['g++', '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
               '-I', str(ROOT / 'engine/color/include'), '-I', str(prefix / 'include')]
        for include in args.jni_include:
            cmd += ['-I', include]
        cmd += [str(ROOT / 'tests/native/ffv1_writer_host_test.cpp'), '-o', str(exe),
                str(prefix / 'lib/libavformat.a'), str(prefix / 'lib/libavcodec.a'), str(prefix / 'lib/libavutil.a'),
                '-lm', '-lpthread']
        # Only the static engine source is needed for to_rgba16 (no JNI).
        cmd.insert(cmd.index(str(ROOT / 'tests/native/ffv1_writer_host_test.cpp')) + 1,
                   str(ROOT / 'engine/color/src/color_output.cpp'))
        cmd.insert(cmd.index(str(ROOT / 'tests/native/ffv1_writer_host_test.cpp')) + 1,
                   str(ROOT / 'engine/color/src/color_fp32.cpp'))
        subprocess.run(cmd, check=True)

        mkv = work / 'out.mkv'
        subprocess.run([str(exe), str(mkv), str(WIDTH), str(HEIGHT), str(FRAMES)], check=True)
        raw = work / 'decoded.raw'
        subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', str(mkv),
                        '-f', 'rawvideo', '-pix_fmt', 'rgba64le', str(raw)], check=True)

        decoded = np.fromfile(raw, dtype='<u2')
        expected = expected_frames()
        if decoded.size != expected.size:
            print('FAIL: decoded %d samples, expected %d' % (decoded.size, expected.size))
            return 1
        decoded = decoded.reshape(expected.shape)
        mismatches = int(np.count_nonzero(decoded != expected))
        if mismatches:
            print('FAIL: %d of %d samples differ' % (mismatches, expected.size))
            return 1
        print('PASS: %d frames, %dx%d, %d RGBA64 samples identical to the independent 16-bit reference'
              % (FRAMES, WIDTH, HEIGHT, expected.size))
    return 0


if __name__ == '__main__':
    sys.exit(main())
