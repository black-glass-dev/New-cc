# Color science / numerical contract

## Signal domains

- Frame storage: unbounded finite float32 RGB, straight alpha0..1; row stride ও capacity float units-এ। NaN/Inf বা render overflow হলে destination commit হয় না।
- `Transfer` শুধু transfer function, `Gamut` আলাদা primaries/white-point। Gamma2.4 একটি signed power extension; sRGB/709-এর linear toe negative values বহন করে। এটি automatic camera characterization নয়।
- PQ: linear1 = **10000 nit**, encoded signal0..1। PQ encode/decode invalid normalized domains reject করে।
- HLG: scene-linear0..1 OETF/inverse; display OOTF/system gamma/viewing environment এখানে প্রয়োগ করা হয় না।
- Scene-relative linear1 এবং PQ absolute linear1 এক reference নয়। Caller-কে explicit exposure/unit normalization করতে হবে; উদাহরণ scene white1→100nit হলে PQ encoding-এর আগে0.01 scaling। কোনো guessed HDR metadata বা implicit SDR/HDR conversion নেই।
- AP0/AP1 D60 ↔ D65 matrices Bradford adaptation-সহ। Negative/wide-gamut linear RGB স্বাভাবিক; PQ/HLG output-এর আগে legal-domain mapping caller-এর দায়িত্ব।

## Matrices / CST

Primary xy থেকে normalized RGB→XYZ matrix solve করা হয়; inverse, Bradford এবং concatenation double arithmetic-এ; frame pixel values float32। `PreparedCST` একবার matrix/exposure তৈরি করে; DAG executor প্রতি pixel-এ inverse recompute করে না।

CST order: input decode → adapted RGB matrix → exposure → optional luminance knee → optional analytic chroma compression → output encode। Knee above `k`: `k+(Y-k)/(1+(Y-k)/k)`; RGB একই scale-এ যায়। Chroma compression luminance থেকে radial shrink, excess based on negative/min/max channel bounds। কোনো proprietary hue-preserving DRT clone নয়; compression legal-range guarantee নয়।

## Operator decisions

- Primaries: exposure, diagonal temperature/tint factors, gain/lift/offset, signed gamma, pivoted power contrast, opponent-plane hue rotation, luma/chroma saturation-vibrance, optional original-luma mix। Temperature/tint normalized artistic controls, calibrated Kelvin/Duv নয়।
- Log wheels: positive luminance-এর DI coordinates-এ smooth low/high weights; RGB delta DI-domain-এ যোগ করে inverse করা হয়।
- HDR wheels: log2(Y/.18) exposure coordinate; six normalized Gaussian weights; optional per-channel knee। Vendor HDR wheel behavior-equivalence দাবি নেই।
- Slices: six hue centers plus skin center.08turn; compact squared tent weights; hue angle, saturation delta ও subtractive exposure density।
- HSL qualification uses clamped RGB0..1 only for **mask measurement**, not hidden clipping of grade RGB। Hue is circular; widths full band width, softness extends the edges। Qualifier/window nodes multiply alpha; masked grading requires explicit layer/outside composition।
- Rectangle uses analytic signed distance. Equal-axis circle uses radial signed distance; unequal-axis ellipse uses normalized radial distance scaled by the smaller axis (not exact Euclidean ellipse SDF)। Feather edges are smoothstep; rotation radians, position normalized pixel centers।
- Cubic curves use sign-aware harmonic tangents plus Fritsch–Carlson radius limiter;2..64 finite points, x strictly increasing with min spacing1e-6, endpoint x0/1, y0..1। Evaluation clamps to0..1; **curves intentionally have a bounded domain**, unlike general scene-linear operators। GPU uses baked float4 table with linear interpolation; approximation error depends on table density and curve shape, not bit-exact polynomial equality।
- LUT data red-fastest `(b*N+g)*N+r`,2..65 edge, finite samples, explicit domain min/max. Domain clamps explicitly. Six tetrahedra use ordered RGB fractions; trilinear is a genuinely different evaluator।
- Grain uses uint32 hash/24-bit normalized noise; explicit `GradeContext.frame_index` low32 bits seed temporal variation, not guessed FPS from timeline time। Halation uses a clamped-edge separable Gaussian thresholded luma bloom, warm `(1,.25,0)` tint; not a physical film model।
- Histogram/parade/waveform/vectorscope measure supplied signal-domain RGB with fixed BT.709 luma/chroma coefficients. CIE-xy first decodes `ScopeConfig.transfer`, then uses chosen gamut XYZ. Undefined/non-positive chromaticity is counted separately. Transparent pixels are counted, not dropped।

## References (math/data, not copied implementation)

- [1](https://documents.blackmagicdesign.com/InformationNotes/DaVinci_Resolve_17_Wide_Gamut_Intermediate.pdf): DWG xy, DI `A=.0075,B=7,C=.07329248,M=10.44426855`, cuts `.00262409/.02740668`।
- [2](https://docs.acescentral.com/encodings/acescct/): ACEScct `.0078125`, slope10.5402377416545, offset.0729055341958355, log constants9.72/17.52। AP0/AP1 chromaticities follow ACES encoding specifications।
- [3](https://www.arri.com/resource/blob/278790/dc29f7399c1dc9553d329e27f1409a89/2022-05-arri-logc4-specification-data.pdf): ARRI LogC4/AWG4, including extended linear toe and a/b/c/s/t constants।
- [4](https://antlerpost.com/colour-spaces/LogC3.html): LogC3 EI800 scene-linear coefficients/AWG3; only EI800 is named/supported, not arbitrary EI calibration।
- [5](http://starcentral.ca/forums/LUTs/TechnicalSummary_for_S-Gamut3Cine_S-Gamut3_S-Log3_V1_01.pdf): Sony technical-summary PDF mirror, S-Log3 code-value equations। Generic S-Gamut3/Cine primaries; no claim of VENICE-specific IDT calibration।
- [6](https://pro-av.panasonic.net/en/cinema_camera_varicam_eva/support/pdf/VARICAM_V-Log_V-Gamut.pdf): Panasonic V-Log/V-Gamut reference manual, piecewise constants and D65 primaries।
- IEC61966-2-1 sRGB; ITU-R BT.709/BT.2020; SMPTE ST2084 PQ; ITU-R BT.2100 HLG: named transfer/primary equations, not standards certification।

Published rounded piecewise equations can have tiny discontinuities. Rec.709 branch knees use float32 representations of.018/.081, avoiding the double-comparison knee flip when input is already float32. Tests sample both normal values and named breakpoints; no claim that every possible encoded float is exactly invertible across a published discontinuity।
