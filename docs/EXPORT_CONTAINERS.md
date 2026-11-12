# Export containers and codecs: verified status

Media3 1.8.1 (`androidx/media` tag 1.8.1, `libraries/transformer`) only muxes MP4
(`FrameworkMuxer` uses `MUXER_OUTPUT_MPEG_4`). MOV, MKV and MXF therefore come from a
native FFmpeg remux of the finished MP4 export (`FfmpegRemux`, `ffmpeg_remux_jni.cpp`).

| Format | Status | How it is produced | Host test |
| --- | --- | --- | --- |
| MP4 + H.264 | Offered | Transformer, `VIDEO_H264` | n/a |
| MP4 + HEVC | Offered; needs a device HEVC encoder, no silent fallback | Transformer, `VIDEO_H265` | n/a |
| MOV + H.264 / HEVC | Offered | Stream copy of all streams, `mov` muxer, brand `qt` | H.264 and HEVC decode, frames identical to source |
| MKV + H.264 / HEVC | Offered | Stream copy, `matroska` muxer | H.264 frames identical to source; first pts starts one frame later |
| MXF + H.264 | Offered, file save only | Video stream copy; AAC decoded and resampled to 48 kHz 16-bit PCM | Decodes; audio level matches source; refused for B-frame H.264 |
| MXF + HEVC | Not offered | The FFmpeg MXF muxer has no HEVC essence mapping | - |
| WEBM | Not offered | VP8/VP9/AV1 only. Needs a VP9/AV1 encoder, which this build does not have | - |
| MOV + ProRes 422 HQ | Offered | H.264 export decoded, then `prores_ks` (profile `hq`, `yuv422p10le`), audio stream-copied AAC | Decodes; ffprobe reports `apch`; Y PSNR 63 dB vs source (320x240) |
| MOV + DNxHR HQ | Offered | H.264 export decoded, then `dnxhd` (`dnxhr_hq`, `yuv422p`), audio AAC copy | Decodes, 60 frames; Y PSNR about 26 dB on detailed content (encoder limit, see below) |
| MXF + DNxHR HQ | Offered, file save only | As MOV DNxHR, audio 48 kHz PCM | Decodes; audio 48 kHz PCM; no audio input decodes |
| AV1 | Not offered | No AV1 encoder enabled | - |
| MP4 / MOV / MKV + HEVC 10-bit (Main 10) | Offered; needs a device HEVC encoder that advertises Main 10, no fallback | Transformer with `VideoEncoderSettings` profile Main 10 (level 5.1, else 4.1); the MP4 is then checked for hvcC `general_profile_idc` 2 before publishing | Host: checker reads profile 1 for an 8-bit Main file and 2 for a 10-bit file (libx265), MOV copy keeps 2. Device encode not tested |

"Host test" means the same C++ as the app, compiled with a host FFmpeg of the same configuration,
run against a generated MP4 (H.264 + AAC, or H.264 without B-frames). The device path is not tested.

## FFmpeg build
- Version `n8.1.3`, sha256 `09f990289327d3ebfedf4fc15cae3f884a65dab7a5aac7bbb5b01ff83900efd9`.
- LGPL v2.1+. No `--enable-gpl`, no external codec libraries.
- Built by `scripts/build_ffmpeg_android.sh` for `arm64-v8a` and `x86_64` into `build_ffmpeg/`
  (git-ignored). CI caches it and sets `OWNCUT_FFMPEG_ROOT`. The CMake Android build fails without it.
- Enabled: `mov`, `matroska`, `mxf`, `mp4` muxers; `mov`, `matroska` demuxers; `h264`, `hevc` parsers;
  `aac` decoder; `swresample`; `h264_mp4toannexb` bitstream filter; `file` protocol.
- Adds size to the APK. The exact size is not measured here.

## Limits found by host tests
- The MXF muxer in FFmpeg 8.1 drops the final frame when the H.264 has B-frames (the CLI does the same).
  The app refuses such input with a message instead of shortening the video.
- The MXF muxer accepts only 48 kHz PCM, so the audio is resampled.
- The MP4 demuxer leaves the video pixel format unset. MXF needs it, so `yuv420p` is declared
  for MXF output (the export encoder produces 8-bit 4:2:0).
- MXF is not published to the gallery. The save dialog is used; the publish path also refuses MXF.
- MOV and MKV are published to the gallery with `video/quicktime` and `video/x-matroska` MIME types.
  The MKV row in MediaStore is not verified on a device; if the insert fails the user sees the error.
- Cancellation is not checked during the remux. It is a fast stream copy, but a long file is not interruptible.
- Device playback, MediaStore publishing, and colour in the remuxed files are not verified.

- ProRes and DNxHR are a second-generation encode of the H.264 export, so they are not lossless
  with respect to the Media3 export. The source must be H.264 (other sources are refused).
- DNxHR PSNR on detailed content (mandelbrot test, 29 dB) is much lower than ProRes (53 dB) with
  FFmpeg's `dnxhd` encoder. A flat source gives 48 dB. Not yet resolved; device output is to be checked.
- Transcodes run on the device CPU through FFmpeg. Large clips take time.

- 10-bit HEVC: the source is 8-bit H.264 decoded by Media3, so the 10-bit file is a bit-depth
  upconversion of the 8-bit frames. Whether the device encoder accepts 8-bit surface input for Main 10
  depends on the device; a refusal shows as an export error (no codec fallback).
- Media3 silently drops a requested profile the encoder does not advertise, so the app checks the
  encoder before encoding and checks the hvcC profile after encoding.
