# Export containers and codecs: verified status

Checked against the Media3 1.8.1 source (androidx/media tag 1.8.1, `libraries/transformer`).

| Format | Status | Evidence |
| --- | --- | --- |
| MP4 + H.264 | Offered | Transformer default; `setVideoMimeType(VIDEO_H264)` |
| MP4 + HEVC | Offered; needs a device HEVC encoder, no silent fallback | `setVideoMimeType(VIDEO_H265)` via `ExportVideoFormat.HEVC` |
| MOV (QuickTime) | **Not available through Transformer** | `FrameworkMuxer.java`: `new MediaMuxer(path, MUXER_OUTPUT_MPEG_4)`; `MimeTypes` has no QuickTime constant |
| MKV, MXF, WEBM | Not offered | No muxer in the Transformer path |
| ProRes, DNxHR, AV1 | Not offered | No platform encoder path; would need a separate native encoder/muxer |
| 10-bit HEVC | Not offered | Format choice has no bit-depth option; needs encoder configuration and device tests |

## Consequence for MOV
The only supported hook is `Transformer.Builder.setMuxerFactory(Muxer.Factory)`.
A MOV output needs a custom QuickTime muxer (or a native FFmpeg muxer). Android `MediaMuxer`
itself has no MOV output. That is a separate, device-tested piece of work and is not claimed here.
