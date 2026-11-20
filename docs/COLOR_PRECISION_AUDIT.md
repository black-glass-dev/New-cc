# Color pipeline precision and color-space audit (step 1)

Status: audit of the current code on branch `arena/6e511b63-new-cc` (base `637c892`).
This document only records what the code does. It changes no behaviour. Device, Vulkan and
FP32 work is not started here. Nothing below is verified on a phone.

## 1. Paths that exist today

There are three colour paths. They do not share one transfer function.

| # | Path | Used by | Input | Maths | Output | Transfer function |
|---|------|---------|-------|-------|--------|-------------------|
| A | Still editor CPU | `ClipColorEditor.kt:325` -> `ColorFrameBuffers.render` -> `ColorEngine.renderRgba8` -> `nativeRenderV1` -> `color.cpp:apply_rgba8` | Bitmap ARGB 8-bit, copied per pixel in Kotlin (`ColorFrameBuffers.kt:28`) | C++ `double` | RGBA8 (`color.cpp:32`, round to 8-bit) | **sRGB** (`color.cpp:175-176`, `decode_srgb` / `encode_srgb`) |
| B | Live preview / Media3 GL | `GradedFramePipeline.kt:29,59`, `ColorRenderPlan.kt:42`, `InlineGradePreview.kt:65`, `ColorGradeDraft.kt:10`, `ClipColorEditor.kt:247` | Media3 decoder output (8-bit YUV -> GL texture) | GLSL ES 1.00, `precision highp float` (`color_gpu.cpp:103`) | Default GL framebuffer (8-bit) | **BT.709 OETF** (`color_gpu.cpp:116-117`) |
| C | Timeline export | `TimelineRenderExporter.kt` -> Media3 `Transformer` with the same `GlEffect` as B | same as B | same as B | Media3 encoder input (8-bit) | **BT.709 OETF** |

Both A and B run the same `Grade` parameters (`ColorEngine.parametersV1`), so the grade
definition is shared. The transfer function that wraps the grade is not shared.

## 2. Where the colour changes, and at what precision

Stage-by-stage for path B/C (preview and export):

| Step | Format / colour space | Precision | Note |
|------|----------------------|-----------|------|
| Decoder output | YUV 4:2:0, 8-bit, BT.709 limited/full range as tagged | 8-bit integer | Source tags are gated by `ColorEngine.validateVideoSource` and `PreviewColorPolicy` (explicit consent for missing tags) |
| YUV -> RGB conversion | Done by Media3 / platform GL path | Platform-defined | Not verified on device |
| Sampling | `texture2D` on `highp sampler2D` (`color_gpu.cpp:104`) | Float in shader, 8-bit texel | Texel storage is 8-bit |
| Input transfer to linear | `toLight` (`color_gpu.cpp:116`), BT.709 | FP32 in shader | Declared `highp`; not `mediump` |
| Exposure, balance, lift, gain, gamma | FP32 linear, no clamp in the middle | FP32 | Good for this step |
| Contrast, saturation, strength | FP32 linear | FP32 | Good |
| Output transfer | `toSignal` (`color_gpu.cpp:117`) | FP32 in shader | |
| Write | `gl_FragColor` to the default framebuffer / Media3 surface | **8-bit** | Precision lost here; no FP32 target |
| Encoder | Media3 `Transformer` 8-bit encoder input | 8-bit | No 10-bit path for graded export |

Stage-by-stage for path A (still editor):

| Step | Format | Precision | Note |
|------|--------|-----------|------|
| Bitmap ARGB -> `IntArray` | 8-bit sRGB | 8-bit | Kotlin `Bitmap.getPixels` |
| `IntArray` -> `ByteBuffer` | per-pixel Kotlin loop (`ColorFrameBuffers.kt:28`) | 8-bit | Per-pixel Java copy |
| Native math | `double`, `decode_srgb` (`color.cpp:175`) | FP64 | Good precision |
| Native output | RGBA8 (`color.cpp:32`) | **8-bit** | Quantised once per frame |
| Output -> `IntArray` | per-pixel Kotlin loop (`ColorFrameBuffers.kt:14-19`) | 8-bit | |

No RGBA16F, RGBA32F, RGBA_F16, half, `mediump` or `lowp` use was found in
`app/`, `engine/`, `modules/`, or `platform/`. A search for `rgba32f|rgba16f|fp16|half|16F|R32F|RGBA_F16|HDR_FLOAT|RelaxedPrecision|mediump|lowp` returned no colour-pipeline hit.
No Vulkan code exists in the repository.

## 3. Findings

### F1. Two transfer functions for the same grade (correctness, high)
Path A uses sRGB; paths B and C use BT.709. A grade that is not identity gives different
colours in the still editor and in the live preview or export.

Computed with the same maths as the code (single channel, `exposure = +1 stop`):

| Input (encoded) | sRGB path (A) | BT.709 path (B/C) | Difference |
|-----------------|---------------|-------------------|------------|
| 0.25 | 0.3521 | 0.3777 | 6.5 / 255 |
| 0.50 | 0.6858 | 0.7193 | 8.5 / 255 |
| 0.75 | 1.0000 (clipped) | 1.0000 (clipped) | 0 |

With zero grade (identity) both paths return the source (checked: 0.25 -> 0.25, 0.5 -> 0.5),
so the mismatch appears only when the grade changes the image.

**Consequence:** the parity test `GradedExportParityTest.kt:56` uses path A
(`ColorFrameBuffers.render`) as its oracle for path C. That oracle uses a different transfer
function, so the test will either fail on device or pass only with a loose tolerance. It has
not been run. This needs to be confirmed on a device or host GL run before any claim of
preview/export parity.

### F2. 8-bit intermediate and output (precision, high for the requested spec)
Path B/C writes to an 8-bit surface. Path A quantises to RGBA8. The requested rule (FP32 for
every intermediate, RGBA32F between steps) is not met. There is one grade pass, so there is no
intermediate texture to convert today. The Media3 pipeline between effects is not controlled
by this code and is assumed to be 8-bit. This is not verified.

### F3. Per-pixel Java copies in path A (performance, medium)
`ColorFrameBuffers.pack` and the output loop run per pixel in Kotlin. The requested design says
the preview must not copy each frame's pixels through Java arrays. Path A is a still/editor path
and not the live path, but the copy is still per frame.

### F4. Source colour detection (good, with gaps)
`VideoSourceProbe.color` reads tags. `ColorEngine.validateVideoSource` and
`validate_sdr_video_source` (`color_gpu.cpp:9-16`) accept only explicit BT.709 SDR, full or limited
range, and no HDR static metadata. Missing tags need explicit consent (`PreviewColorPolicy`).
Not implemented: Rec.2020 PQ/HLG, camera Log transforms, and matrix-coefficient checks beyond
the tag read. HDR and Log sources are refused by design, which is the safe behaviour.

### F5. HDR presence is gated, not converted (good)
HDR static metadata causes refusal (`color_gpu.cpp:10`, `GradedFramePipeline.kt:50`). Good for
safety. HDR export is not supported.

### F6. Old projects (migration) (not checked here)
Project grade v2 is read by the current code (AGENTS.md ADR0017). Whether an old project changes
look after a colour-engine change is not checked. Any new engine needs a version field and a
side-by-side comparison, as the request asks.

## 4. What the requested work needs (not started)

1. Pick one transfer function for the whole pipeline. Document it. Make path A use the same
   function as path C, or make both explicit and test them against a common reference.
2. Replace `gl_FragColor` output with an explicit FP32 render target (RGBA32F, with
   `EXT_color_buffer_float` on GLES 3.2 or the Vulkan equivalent). Confirm support at run time.
   Until then, the 8-bit output must stay labelled as 8-bit.
3. Move path A's pixel transport out of Kotlin per-pixel loops (direct buffers, not
   `IntArray` loops).
4. Fix the parity oracle (F1) before any export parity claim.
5. Add Vulkan only if the GLES path cannot meet the FP32 rule on the target devices. The request
   says to use Vulkan. This needs a decision, because the current Media3 effect API is GL-based
   and a Vulkan path would replace it.

## 5. Decision needed before step 2

- Media3 `GlEffect` is GL. The request asks for Vulkan. Options:
  (a) GLES 3.2 with RGBA32F targets (fits Media3 today, no new dependency), or
  (b) a Vulkan compute path, with the Media3 integration replaced. Large work, device-specific.
- Which device set counts as the target (minimum API, GPU)? RGBA32F render support differs by GPU.

Nothing is verified on a phone. The steps above are the audit only.
