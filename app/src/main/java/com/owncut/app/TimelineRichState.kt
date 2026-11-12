package com.owncut.app

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.Locale

internal data class TransformKeyframe(
    val timeMs: Long,
    val scale: Float,
    val panX: Float,
    val panY: Float,
    val rotationDeg: Float,
)

internal data class TransformSample(val scale: Float, val panX: Float, val panY: Float, val rotationDeg: Float)
internal data class SpeedKeyframe(val timeMs: Long, val speed: Float)

internal data class ClipRichFx(
    val clipId: String,
    val trackId: String = "v1",
    val speed: Double = 1.0,
    val reversed: Boolean = false,
    val freezeMs: Long = -1L,
    val volume: Float = 1.0f,
    val fadeInMs: Long = 0L,
    val fadeOutMs: Long = 0L,
    val muted: Boolean = false,
    val duckingDb: Float = 0f,
    val opacity: Float = 1.0f,
    val blendMode: String = "normal",
    val scale: Float = 1.0f,
    val panX: Float = 0.0f,
    val panY: Float = 0.0f,
    val rotationDeg: Float = 0.0f,
    val dynamicZoomStart: Float = 1.0f,
    val dynamicZoomEnd: Float = 1.0f,
    val transitionIn: String = "none",
    val transitionMs: Long = 0L,
    val chromaKeyHex: String = "",
    val chromaTolerance: Float = 0.0f,
    val audioPeaks: List<Float> = emptyList(),
    val transformKeyframes: List<TransformKeyframe> = emptyList(),
    val speedKeyframes: List<SpeedKeyframe> = emptyList(),
    val eqBandsDb: List<Float> = List(6) { 0f },
    val compressorEnabled: Boolean = false,
    val compressorThresholdDb: Float = -18f,
    val compressorRatio: Float = 4f,
    val limiterEnabled: Boolean = false,
    val limiterCeilingDb: Float = -1f,
    val gateEnabled: Boolean = false,
    val gateThresholdDb: Float = -45f,
) {
    fun hasVideoTransformOrFx(): Boolean =
        kotlin.math.abs(speed - 1.0) > 1e-3 ||
            speedKeyframes.isNotEmpty() ||
            reversed ||
            freezeMs >= 0L ||
            kotlin.math.abs(opacity - 1.0f) > 1e-3f ||
            blendMode != "normal" ||
            kotlin.math.abs(scale - 1.0f) > 1e-3f ||
            kotlin.math.abs(panX) > 1e-3f ||
            kotlin.math.abs(panY) > 1e-3f ||
            kotlin.math.abs(rotationDeg) > 1e-2f ||
            kotlin.math.abs(dynamicZoomStart - 1.0f) > 1e-3f ||
            kotlin.math.abs(dynamicZoomEnd - 1.0f) > 1e-3f ||
            (transitionIn != "none" && transitionMs > 0L) ||
            chromaKeyHex.isNotBlank() ||
            transformKeyframes.isNotEmpty() ||
            fadeInMs > 0L ||
            fadeOutMs > 0L

    fun hasAudioEnvelope(): Boolean =
        muted ||
            kotlin.math.abs(volume - 1.0f) > 1e-3f ||
            fadeInMs > 0L ||
            fadeOutMs > 0L ||
            kotlin.math.abs(duckingDb) > 1e-2f ||
            eqBandsDb.any { kotlin.math.abs(it) > 1e-2f } || compressorEnabled || limiterEnabled || gateEnabled

    fun effectiveGainAt(localMs: Long, clipDurationMs: Long, duckMultiplier: Float = 1.0f): Float {
        if (muted) return 0f
        var g = volume.coerceIn(0f, 2f) * duckMultiplier
        val dur = clipDurationMs.coerceAtLeast(1L)
        if (fadeInMs > 0L && localMs < fadeInMs) {
            g *= (localMs.coerceAtLeast(0L).toFloat() / fadeInMs.toFloat()).coerceIn(0f, 1f)
        }
        if (fadeOutMs > 0L && (dur - localMs) < fadeOutMs) {
            g *= ((dur - localMs).coerceAtLeast(0L).toFloat() / fadeOutMs.toFloat()).coerceIn(0f, 1f)
        }
        return g.coerceIn(0f, 2f)
    }

    fun transformAt(localMs: Long, durationMs: Long): TransformSample {
        val frames = transformKeyframes.sortedBy { it.timeMs }
        if (frames.isNotEmpty()) {
            val t = localMs.coerceIn(0L, durationMs.coerceAtLeast(0L))
            val before = frames.lastOrNull { it.timeMs <= t }
            val after = frames.firstOrNull { it.timeMs >= t }
            if (before == null) return frames.first().sample()
            if (after == null || before.timeMs == after.timeMs) return before.sample()
            val u = ((t - before.timeMs).toFloat() / (after.timeMs - before.timeMs).toFloat()).coerceIn(0f, 1f)
            return TransformSample(
                scale = lerp(before.scale, after.scale, u).coerceIn(0.1f, 8f),
                panX = lerp(before.panX, after.panX, u).coerceIn(-2f, 2f),
                panY = lerp(before.panY, after.panY, u).coerceIn(-2f, 2f),
                rotationDeg = lerp(before.rotationDeg, after.rotationDeg, u),
            )
        }
        val u = if (durationMs <= 0L) 0f else (localMs.toFloat() / durationMs.toFloat()).coerceIn(0f, 1f)
        return TransformSample(
            (scale * lerp(dynamicZoomStart, dynamicZoomEnd, u)).coerceIn(0.1f, 8f),
            panX.coerceIn(-2f, 2f), panY.coerceIn(-2f, 2f), rotationDeg,
        )
    }

    fun effectiveScaleAt(progress01: Float): Float {
        val u = progress01.coerceIn(0f, 1f)
        return transformAt((u * 1_000_000f).toLong(), 1_000_000L).scale
    }

    fun speedAt(localMs: Long, durationMs: Long): Float {
        val keys = speedKeyframes.sortedBy { it.timeMs }
        if (keys.isEmpty()) return speed.toFloat().coerceIn(0.25f, 4f)
        val time = localMs.coerceIn(0L, durationMs.coerceAtLeast(0L))
        val before = keys.lastOrNull { it.timeMs <= time } ?: return keys.first().speed
        val after = keys.firstOrNull { it.timeMs >= time } ?: return before.speed
        if (before.timeMs == after.timeMs) return before.speed
        val u = (time - before.timeMs).toFloat() / (after.timeMs - before.timeMs).toFloat()
        return lerp(before.speed, after.speed, u).coerceIn(0.25f, 4f)
    }

    private fun TransformKeyframe.sample() = TransformSample(scale, panX, panY, rotationDeg)
    private fun lerp(a: Float, b: Float, u: Float) = a + (b - a) * u
}

internal data class TextOverlayItem(
    val id: String,
    val text: String,
    val startMs: Long,
    val durationMs: Long = 2500L,
    val x: Float = 0.5f,
    val y: Float = 0.80f,
    val scale: Float = 1.0f,
    val rotationDeg: Float = 0.0f,
    val fgColorHex: String = "#FFFFFF",
    val bgColorHex: String = "#B8000000",
    val style: String = "title",
    val bold: Boolean = true,
) {
    fun isVisibleAt(timelineMs: Long): Boolean =
        timelineMs >= startMs && timelineMs < startMs + durationMs.coerceAtLeast(1L)
}

internal data class SubtitleCueItem(
    val id: String,
    val startMs: Long,
    val endMs: Long,
    val text: String,
) {
    fun isVisibleAt(timelineMs: Long): Boolean = timelineMs in startMs until endMs
}

internal data class TimelineMarkerItem(
    val id: String,
    val positionMs: Long,
    val label: String,
    val colorHex: String = "#4F9CF9",
)

internal data class TimelineTrackInfo(
    val id: String,
    val kind: String,
    val muted: Boolean,
    val locked: Boolean,
    val clipCount: Int,
)

internal data class TimelineRichDocument(
    val snapEnabled: Boolean = true,
    val beatBpm: Double = 0.0,
    val beatOffsetMs: Long = 0L,
    /** Sequence output aspect ratio (width / height); null preserves source ratio. */
    val outputAspectRatio: Float? = null,
    /** Optional explicit even-pixel output dimensions, bounded to UHD. */
    val outputWidth: Int? = null,
    val outputHeight: Int? = null,
    val clipFx: Map<String, ClipRichFx> = emptyMap(),
    val overlays: List<TextOverlayItem> = emptyList(),
    val subtitles: List<SubtitleCueItem> = emptyList(),
    val markers: List<TimelineMarkerItem> = emptyList(),
) {
    fun fxFor(clipId: String): ClipRichFx = clipFx[clipId] ?: ClipRichFx(clipId = clipId)

    fun toJsonString(): String {
        val root = JSONObject()
        root.put("format", FORMAT_ID)
        root.put("version", 1)
        root.put("snap_enabled", snapEnabled)
        root.put("beat_bpm", beatBpm)
        root.put("beat_offset_ms", beatOffsetMs)
        outputAspectRatio?.takeIf { it.isFinite() && it in 0.3f..3.2f }?.let { root.put("output_aspect_ratio", it.toDouble()) }
        val resolutionValid = outputWidth != null && outputHeight != null &&
            outputWidth in 144..3840 && outputHeight in 144..3840 && outputWidth % 2 == 0 && outputHeight % 2 == 0 &&
            outputWidth.toLong() * outputHeight <= 8_294_400L
        if (resolutionValid) {
            root.put("output_width", outputWidth!!)
            root.put("output_height", outputHeight!!)
        }

        val fxArray = JSONArray()
        for (fx in clipFx.values.sortedBy { it.clipId }) {
            val obj = JSONObject()
            obj.put("track_id", fx.trackId)
            obj.put("clip_id", fx.clipId)
            obj.put("speed", fx.speed)
            obj.put("reversed", fx.reversed)
            obj.put("freeze_ms", fx.freezeMs)
            obj.put("volume", fx.volume.toDouble())
            obj.put("fade_in_ms", fx.fadeInMs)
            obj.put("fade_out_ms", fx.fadeOutMs)
            obj.put("muted", fx.muted)
            obj.put("ducking_db", fx.duckingDb.toDouble())
            obj.put("opacity", fx.opacity.toDouble())
            obj.put("blend_mode", fx.blendMode)
            obj.put("scale", fx.scale.toDouble())
            obj.put("pan_x", fx.panX.toDouble())
            obj.put("pan_y", fx.panY.toDouble())
            obj.put("rotation_deg", fx.rotationDeg.toDouble())
            obj.put("dynamic_zoom_start", fx.dynamicZoomStart.toDouble())
            obj.put("dynamic_zoom_end", fx.dynamicZoomEnd.toDouble())
            obj.put("transition_kind", fx.transitionIn)
            obj.put("transition_ms", fx.transitionMs)
            obj.put("chroma_key_hex", fx.chromaKeyHex)
            obj.put("chroma_tolerance", fx.chromaTolerance.toDouble())
            val peaksArr = JSONArray()
            for (p in fx.audioPeaks) peaksArr.put(p.toDouble())
            obj.put("audio_peaks", peaksArr)
            val keyframes = JSONArray()
            fx.transformKeyframes.sortedBy { it.timeMs }.forEach { key ->
                keyframes.put(JSONObject().apply {
                    put("time_ms", key.timeMs)
                    put("scale", key.scale.toDouble())
                    put("pan_x", key.panX.toDouble())
                    put("pan_y", key.panY.toDouble())
                    put("rotation_deg", key.rotationDeg.toDouble())
                })
            }
            obj.put("transform_keyframes", keyframes)
            val speedKeys = JSONArray()
            fx.speedKeyframes.sortedBy { it.timeMs }.forEach { key ->
                speedKeys.put(JSONObject().apply { put("time_ms", key.timeMs); put("speed", key.speed.toDouble()) })
            }
            obj.put("speed_keyframes", speedKeys)
            val eq = JSONArray()
            fx.eqBandsDb.take(6).forEach { eq.put(it.coerceIn(-18f, 18f).toDouble()) }
            while (eq.length() < 6) eq.put(0.0)
            obj.put("eq_bands_db", eq)
            obj.put("compressor_enabled", fx.compressorEnabled)
            obj.put("compressor_threshold_db", fx.compressorThresholdDb.toDouble())
            obj.put("compressor_ratio", fx.compressorRatio.toDouble())
            obj.put("limiter_enabled", fx.limiterEnabled)
            obj.put("limiter_ceiling_db", fx.limiterCeilingDb.toDouble())
            obj.put("gate_enabled", fx.gateEnabled)
            obj.put("gate_threshold_db", fx.gateThresholdDb.toDouble())
            fxArray.put(obj)
        }
        root.put("clip_fx", fxArray)

        val ovArray = JSONArray()
        for (ov in overlays) {
            val obj = JSONObject()
            obj.put("id", ov.id)
            obj.put("text", ov.text)
            obj.put("start_ms", ov.startMs)
            obj.put("duration_ms", ov.durationMs)
            obj.put("x", ov.x.toDouble())
            obj.put("y", ov.y.toDouble())
            obj.put("scale", ov.scale.toDouble())
            obj.put("rotation_deg", ov.rotationDeg.toDouble())
            obj.put("fg_hex", ov.fgColorHex)
            obj.put("bg_hex", ov.bgColorHex)
            obj.put("style", ov.style)
            obj.put("bold", ov.bold)
            ovArray.put(obj)
        }
        root.put("overlays", ovArray)

        val subArray = JSONArray()
        for (s in subtitles) {
            val obj = JSONObject()
            obj.put("id", s.id)
            obj.put("start_ms", s.startMs)
            obj.put("end_ms", s.endMs)
            obj.put("text", s.text)
            subArray.put(obj)
        }
        root.put("subtitles", subArray)

        val mkArray = JSONArray()
        for (m in markers) {
            val obj = JSONObject()
            obj.put("id", m.id)
            obj.put("position_ms", m.positionMs)
            obj.put("label", m.label)
            obj.put("color_hex", m.colorHex)
            mkArray.put(obj)
        }
        root.put("markers", mkArray)
        return root.toString()
    }

    companion object {
        const val FORMAT_ID = "owncut_tl_state_v1"

        fun sidecarFor(primaryPath: String): File = File("$primaryPath.tlstate")

        fun fromJsonString(json: String): TimelineRichDocument {
            val root = JSONObject(json)
            require(root.optString("format") == FORMAT_ID) { "Unsupported tlstate format" }
            val snap = root.optBoolean("snap_enabled", true)
            val bpm = root.optDouble("beat_bpm", 0.0).takeIf { it.isFinite() && it >= 0.0 } ?: 0.0
            val offsetMs = root.optLong("beat_offset_ms", 0L).coerceAtLeast(0L)
            val storedWidth = root.optInt("output_width", -1)
            val storedHeight = root.optInt("output_height", -1)
            val validResolution = storedWidth in 144..3840 && storedHeight in 144..3840 &&
                storedWidth % 2 == 0 && storedHeight % 2 == 0 && storedWidth.toLong() * storedHeight <= 8_294_400L
            val outputWidth = storedWidth.takeIf { validResolution }
            val outputHeight = storedHeight.takeIf { validResolution }
            val outputAspectRatio = if (validResolution) storedWidth.toFloat() / storedHeight.toFloat()
                else root.optDouble("output_aspect_ratio", Double.NaN)
                    .takeIf { it.isFinite() && it in 0.3..3.2 }?.toFloat()

            val fxMap = LinkedHashMap<String, ClipRichFx>()
            val fxArr = root.optJSONArray("clip_fx") ?: JSONArray()
            for (i in 0 until fxArr.length()) {
                val obj = fxArr.optJSONObject(i) ?: continue
                val id = obj.optString("clip_id")
                if (id.isEmpty()) continue
                val peaksJson = obj.optJSONArray("audio_peaks")
                val peaks = if (peaksJson != null) {
                    List(peaksJson.length()) { idx -> peaksJson.optDouble(idx, 0.0).toFloat().coerceIn(0f, 1f) }
                } else emptyList()
                val keyframeArray = obj.optJSONArray("transform_keyframes") ?: JSONArray()
                val keyframes = buildList {
                    for (keyIndex in 0 until minOf(keyframeArray.length(), 2048)) {
                        val key = keyframeArray.optJSONObject(keyIndex) ?: continue
                        val timeMs = key.optLong("time_ms", -1L)
                        if (timeMs < 0L) continue
                        add(TransformKeyframe(
                            timeMs = timeMs,
                            scale = key.optDouble("scale", 1.0).toFloat().takeIf { it.isFinite() }?.coerceIn(0.1f, 8f) ?: 1f,
                            panX = key.optDouble("pan_x", 0.0).toFloat().takeIf { it.isFinite() }?.coerceIn(-2f, 2f) ?: 0f,
                            panY = key.optDouble("pan_y", 0.0).toFloat().takeIf { it.isFinite() }?.coerceIn(-2f, 2f) ?: 0f,
                            rotationDeg = key.optDouble("rotation_deg", 0.0).toFloat().takeIf { it.isFinite() }?.coerceIn(-3600f, 3600f) ?: 0f,
                        ))
                    }
                }.distinctBy { it.timeMs }.sortedBy { it.timeMs }
                val speedKeyArray = obj.optJSONArray("speed_keyframes") ?: JSONArray()
                val speedKeys = buildList {
                    for (keyIndex in 0 until minOf(speedKeyArray.length(), 2048)) {
                        val key = speedKeyArray.optJSONObject(keyIndex) ?: continue
                        val timeMs = key.optLong("time_ms", -1L)
                        val speed = key.optDouble("speed", Double.NaN).toFloat()
                        if (timeMs >= 0L && speed.isFinite()) add(SpeedKeyframe(timeMs, speed.coerceIn(0.25f, 4f)))
                    }
                }.distinctBy { it.timeMs }.sortedBy { it.timeMs }
                val eqArray = obj.optJSONArray("eq_bands_db") ?: JSONArray()
                val eqBands = List(6) { index ->
                    eqArray.optDouble(index, 0.0).toFloat().takeIf { it.isFinite() }?.coerceIn(-18f, 18f) ?: 0f
                }
                fxMap[id] = ClipRichFx(
                    clipId = id,
                    trackId = obj.optString("track_id", "v1"),
                    speed = obj.optDouble("speed", 1.0).coerceIn(0.1, 8.0),
                    reversed = obj.optBoolean("reversed", false),
                    freezeMs = obj.optLong("freeze_ms", -1L),
                    volume = obj.optDouble("volume", 1.0).toFloat().coerceIn(0f, 2f),
                    fadeInMs = obj.optLong("fade_in_ms", 0L).coerceAtLeast(0L),
                    fadeOutMs = obj.optLong("fade_out_ms", 0L).coerceAtLeast(0L),
                    muted = obj.optBoolean("muted", false),
                    duckingDb = obj.optDouble("ducking_db", 0.0).toFloat(),
                    opacity = obj.optDouble("opacity", 1.0).toFloat().coerceIn(0f, 1f),
                    blendMode = obj.optString("blend_mode", "normal"),
                    scale = obj.optDouble("scale", 1.0).toFloat().coerceIn(0.1f, 8f),
                    panX = obj.optDouble("pan_x", 0.0).toFloat().coerceIn(-2f, 2f),
                    panY = obj.optDouble("pan_y", 0.0).toFloat().coerceIn(-2f, 2f),
                    rotationDeg = obj.optDouble("rotation_deg", 0.0).toFloat(),
                    dynamicZoomStart = obj.optDouble("dynamic_zoom_start", 1.0).toFloat().coerceIn(0.5f, 3f),
                    dynamicZoomEnd = obj.optDouble("dynamic_zoom_end", 1.0).toFloat().coerceIn(0.5f, 3f),
                    transitionIn = obj.optString("transition_kind", "none"),
                    transitionMs = obj.optLong("transition_ms", 0L).coerceAtLeast(0L),
                    chromaKeyHex = obj.optString("chroma_key_hex", ""),
                    chromaTolerance = obj.optDouble("chroma_tolerance", 0.0).toFloat().coerceIn(0f, 1f),
                    audioPeaks = peaks,
                    transformKeyframes = keyframes,
                    speedKeyframes = speedKeys,
                    eqBandsDb = eqBands,
                    compressorEnabled = obj.optBoolean("compressor_enabled", false),
                    compressorThresholdDb = obj.optDouble("compressor_threshold_db", -18.0).toFloat().takeIf { it.isFinite() }?.coerceIn(-60f, 0f) ?: -18f,
                    compressorRatio = obj.optDouble("compressor_ratio", 4.0).toFloat().takeIf { it.isFinite() }?.coerceIn(1f, 20f) ?: 4f,
                    limiterEnabled = obj.optBoolean("limiter_enabled", false),
                    limiterCeilingDb = obj.optDouble("limiter_ceiling_db", -1.0).toFloat().takeIf { it.isFinite() }?.coerceIn(-12f, 0f) ?: -1f,
                    gateEnabled = obj.optBoolean("gate_enabled", false),
                    gateThresholdDb = obj.optDouble("gate_threshold_db", -45.0).toFloat().takeIf { it.isFinite() }?.coerceIn(-80f, -6f) ?: -45f,
                )
            }

            val ovList = mutableListOf<TextOverlayItem>()
            val ovArr = root.optJSONArray("overlays") ?: JSONArray()
            for (i in 0 until ovArr.length()) {
                val obj = ovArr.optJSONObject(i) ?: continue
                val id = obj.optString("id")
                if (id.isEmpty()) continue
                ovList += TextOverlayItem(
                    id = id,
                    text = obj.optString("text", ""),
                    startMs = obj.optLong("start_ms", 0L).coerceAtLeast(0L),
                    durationMs = obj.optLong("duration_ms", 2000L).coerceAtLeast(100L),
                    x = obj.optDouble("x", 0.5).toFloat().coerceIn(0.05f, 0.95f),
                    y = obj.optDouble("y", 0.80).toFloat().coerceIn(0.05f, 0.95f),
                    scale = obj.optDouble("scale", 1.0).toFloat().coerceIn(0.4f, 3f),
                    rotationDeg = obj.optDouble("rotation_deg", 0.0).toFloat(),
                    fgColorHex = obj.optString("fg_hex", "#FFFFFF"),
                    bgColorHex = obj.optString("bg_hex", "#B8000000"),
                    style = obj.optString("style", "title"),
                    bold = obj.optBoolean("bold", true),
                )
            }

            val subList = mutableListOf<SubtitleCueItem>()
            val subArr = root.optJSONArray("subtitles") ?: JSONArray()
            for (i in 0 until subArr.length()) {
                val obj = subArr.optJSONObject(i) ?: continue
                val id = obj.optString("id")
                if (id.isEmpty()) continue
                val st = obj.optLong("start_ms", 0L).coerceAtLeast(0L)
                val en = obj.optLong("end_ms", st + 1000L).coerceAtLeast(st + 1L)
                subList += SubtitleCueItem(id, st, en, obj.optString("text", ""))
            }
            subList.sortBy { it.startMs }

            val mkList = mutableListOf<TimelineMarkerItem>()
            val mkArr = root.optJSONArray("markers") ?: JSONArray()
            for (i in 0 until mkArr.length()) {
                val obj = mkArr.optJSONObject(i) ?: continue
                val id = obj.optString("id")
                if (id.isEmpty()) continue
                mkList += TimelineMarkerItem(
                    id = id,
                    positionMs = obj.optLong("position_ms", 0L).coerceAtLeast(0L),
                    label = obj.optString("label", ""),
                    colorHex = obj.optString("color_hex", "#4F9CF9"),
                )
            }
            mkList.sortBy { it.positionMs }

            return TimelineRichDocument(
                snapEnabled = snap,
                beatBpm = bpm,
                beatOffsetMs = offsetMs,
                outputAspectRatio = outputAspectRatio,
                outputWidth = outputWidth,
                outputHeight = outputHeight,
                clipFx = fxMap,
                overlays = ovList,
                subtitles = subList,
                markers = mkList,
            )
        }

        fun writeAtomic(file: File, doc: TimelineRichDocument) {
            file.parentFile?.mkdirs()
            val tmp = File(file.parentFile, "${file.name}.tmp-${System.nanoTime()}")
            try {
                tmp.writeText(doc.toJsonString(), Charsets.UTF_8)
                if (!tmp.renameTo(file)) {
                    file.writeText(tmp.readText(Charsets.UTF_8), Charsets.UTF_8)
                }
            } finally {
                tmp.delete()
            }
        }

        fun readIfExists(file: File): TimelineRichDocument? {
            if (!file.exists() || !file.isFile || file.length() > 8L * 1024L * 1024L) return null
            return runCatching { fromJsonString(file.readText(Charsets.UTF_8)) }.getOrNull()
        }

        fun parseSrt(srt: String): List<SubtitleCueItem> {
            val cues = mutableListOf<SubtitleCueItem>()
            val blocks = srt.replace("\r\n", "\n").split(Regex("\\n\\s*\\n"))
            var idx = 1
            for (rawBlock in blocks) {
                val lines = rawBlock.lines().map { it.trim() }.filter { it.isNotEmpty() }
                if (lines.isEmpty()) continue
                val timeLineIdx = lines.indexOfFirst { it.contains("-->") }
                if (timeLineIdx < 0) continue
                val parts = lines[timeLineIdx].split("-->")
                if (parts.size < 2) continue
                val startMs = parseSrtTimestamp(parts[0].trim()) ?: continue
                val endMs = parseSrtTimestamp(parts[1].trim().substringBefore(' ')) ?: continue
                val body = lines.drop(timeLineIdx + 1).joinToString("\n").trim()
                if (body.isNotEmpty() && endMs > startMs) {
                    cues += SubtitleCueItem("sub-${idx++}", startMs, endMs, body)
                }
            }
            return cues.sortedBy { it.startMs }
        }

        fun formatSrt(cues: List<SubtitleCueItem>): String = buildString {
            cues.sortedBy { it.startMs }.forEachIndexed { index, cue ->
                append(index + 1).append('\n')
                append(formatSrtTimestamp(cue.startMs)).append(" --> ").append(formatSrtTimestamp(cue.endMs)).append('\n')
                append(cue.text).append("\n\n")
            }
        }

        private fun parseSrtTimestamp(ts: String): Long? {
            val clean = ts.replace(',', '.')
            val segs = clean.split(':')
            if (segs.size != 3) return null
            val hh = segs[0].toLongOrNull() ?: return null
            val mm = segs[1].toLongOrNull() ?: return null
            val secParts = segs[2].split('.')
            val ss = secParts[0].toLongOrNull() ?: return null
            val msRaw = secParts.getOrNull(1)?.padEnd(3, '0')?.take(3)?.toLongOrNull() ?: 0L
            return ((hh * 3600L + mm * 60L + ss) * 1000L + msRaw).coerceAtLeast(0L)
        }

        private fun formatSrtTimestamp(ms: Long): String {
            val safe = ms.coerceAtLeast(0L)
            val milli = safe % 1000L
            val sec = (safe / 1000L) % 60L
            val min = (safe / 60_000L) % 60L
            val hr = safe / 3_600_000L
            return String.format(Locale.US, "%02d:%02d:%02d,%03d", hr, min, sec, milli)
        }
    }
}
