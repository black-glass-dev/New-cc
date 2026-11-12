# Export containers and codecs: verified status

Media3 1.8.1 (`androidx/media` tag 1.8.1, `libraries/transformer`) only muxes MP4
(`FrameworkMuxer` uses `MUXER_OUTPUT_MPEG_4`). MOV and MKV therefore cannot come from Transformer.

| Format | Status | How it is produced |
| --- | --- | --- |
| MP4 + H.264 | Offered | Transformer, `setVideoMimeType(VIDEO_H264)` |
| MP4 + HEVC | Offered; needs a device HEVC encoder, no silent fallback | Transformer, `VIDEO_H265` |
| MOV + H.264 / HEVC | Offered (implementation added; device not tested) | Transformer MP4 export, then a stream-copy remux with the LGPL FFmpeg `mov` muxer (`FfmpegRemux`, `ffmpeg_remux_jni.cpp`). No re-encode. |
| MKV + H.264 / HEVC | Offered (implementation added; device not tested) | Same as MOV, using the FFmpeg `matroska` muxer |
| MXF, WEBM | Not offered | Not built. Muxers exist in FFmpeg n8.1.3 but are not enabled in `scripts/build_ffmpeg_android.sh` |
| ProRes, DNxHR | Not offered | FFmpeg has `prores_ks` but it is not enabled. Needs an encoder configuration and device tests |
| AV1 | Not offered | No encoder enabled |
| 10-bit HEVC | Not offered | Needs a 10-bit encoder configuration and device tests |

## FFmpeg build
- Version `n8.1.3`, sha256 `09f990289327d3ebfedf4fc15cae3f884a65dab7a5aac7bbb5b01ff83900efd9`.
- LGPL v2.1+. No `--enable-gpl`, no external codec libraries.
- Built by `scripts/build_ffmpeg_android.sh` for `arm64-v8a` and `x86_64` into `build_ffmpeg/`
  (git-ignored). CI caches it and sets `OWNCUT_FFMPEG_ROOT`. The CMake Android build fails without it.
- Adds size to the APK. The exact size is not measured here.

## Limits
- The remux copies streams as they are, so the MP4 must already hold the chosen codec.
- Cancellation is not checked during the remux. It is a fast stream copy, but a long file is not interruptible.
- Device output, playback in other apps, and the color result of the remuxed file are not verified here.
