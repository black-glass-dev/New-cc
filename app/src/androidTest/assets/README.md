# Synthetic render fixtures

Original synthetic test media generated for Owncut, no third-party footage.
Generated with FFmpeg (imageio-ffmpeg's build, used only as an offline development tool,
not shipped with the app). Each fixture is 3 s / 30 fps, H.264 yuv420p, GOP 90, no B frames:

- render-a.mp4: `testsrc2=size=160x120:rate=30`, 440 Hz sine / AAC / 48 kHz.
- render-b.mp4: `testsrc2=size=320x180:rate=30`, no audio.

The 500 ms trim deliberately falls between keyframes. Tests require re-encoding
720p output, three sequential clips, mixed aspect ratios, missing-audio silence,
first/last-frame decoding, duration and cancellation cleanup.
