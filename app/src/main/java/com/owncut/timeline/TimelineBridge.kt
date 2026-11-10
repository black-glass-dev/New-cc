package com.owncut.timeline

import com.owncut.app.ClipSnapshot

internal data class SnapDecision(
    val finalFrame: Long,
    val snapped: Boolean,
    val snapGuideFrame: Long,
    val slotAdjusted: Boolean,
    val hapticPulse: Boolean,
)

internal data class AudioAnalysisResult(
    val detectedBpm: Double,
    val integratedLufs: Double,
    val peaks: List<Float>,
)

internal object TimelineBridge {
    @Volatile
    var nativeLoaded: Boolean = try {
        System.loadLibrary("owncut_android")
        true
    } catch (_: UnsatisfiedLinkError) {
        false
    } catch (_: SecurityException) {
        false
    }

    @JvmStatic external fun nativeCreate(fpsNum: Int, fpsDen: Int, width: Int, height: Int): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeAddSyntheticMedia(handle: Long, name: String, width: Int, height: Int, durationFrames: Long): Long
    @JvmStatic external fun nativePerformEdit(handle: Long, editMode: Int, mediaId: Long, srcInFrames: Long, srcOutFrames: Long): Boolean
    @JvmStatic external fun nativeBladeSplit(handle: Long, frameIndex: Long): Boolean
    @JvmStatic external fun nativeUndo(handle: Long): Boolean
    @JvmStatic external fun nativeRedo(handle: Long): Boolean
    @JvmStatic external fun nativeTwoFingerUndo(handle: Long): Boolean
    @JvmStatic external fun nativeThreeFingerRedo(handle: Long): Boolean
    @JvmStatic external fun nativeSetPlayheadFrames(handle: Long, frame: Long)
    @JvmStatic external fun nativeGetDurationFrames(handle: Long): Long

    @JvmStatic external fun nativeSyncClips(
        handle: Long,
        ids: Array<String>,
        trackIds: Array<String>,
        mediaIds: Array<String>,
        startFrames: LongArray,
        durationFrames: LongArray,
        sourceInMs: LongArray,
        sourceOutMs: LongArray,
        gradeActives: BooleanArray,
    ): Boolean

    @JvmStatic external fun nativeEvaluateSnap(
        handle: Long,
        trackId: String,
        clipId: String,
        proposedFrame: Long,
        durationFrames: Long,
        playheadFrame: Long,
        pixelsPerFrame: Double,
        snapEnabled: Boolean,
    ): LongArray?

    @JvmStatic external fun nativePinchZoom(handle: Long, scaleFactor: Double, focusXPx: Double): Double
    @JvmStatic external fun nativeResetZoom(handle: Long)
    @JvmStatic external fun nativeGetZoomScale(handle: Long): Double
    @JvmStatic external fun nativeGetCapabilitiesJson(handle: Long): String?

    @JvmStatic external fun nativeExportStateJson(handle: Long): String?
    @JvmStatic external fun nativeImportStateJson(handle: Long, json: String): Boolean
    @JvmStatic external fun nativeSetSnapEnabled(handle: Long, enabled: Boolean)
    @JvmStatic external fun nativeGetSnapEnabled(handle: Long): Boolean
    @JvmStatic external fun nativeSetBeatGrid(handle: Long, bpm: Double, offsetMs: Long)
    @JvmStatic external fun nativeUpsertMarker(handle: Long, id: String, positionMs: Long, label: String, colorHex: String)
    @JvmStatic external fun nativeRemoveMarker(handle: Long, id: String): Boolean
    @JvmStatic external fun nativeImportSrt(handle: Long, srtText: String): Boolean
    @JvmStatic external fun nativeExportSrt(handle: Long): String?
    @JvmStatic external fun nativeAnalyzePcm16(pcmInterleaved: ShortArray, channels: Int, sampleRate: Int, numPeaks: Int): DoubleArray?
    @JvmStatic external fun nativeDetectSceneCuts(rgbaFrames: ByteArray, numFrames: Int, width: Int, height: Int, threshold: Double): IntArray?
}

internal class TimelineEngineBridge(
    fpsNum: Int = 30,
    fpsDen: Int = 1,
    width: Int = 1920,
    height: Int = 1080,
) : AutoCloseable {
    private var handle: Long = if (TimelineBridge.nativeLoaded) {
        TimelineBridge.nativeCreate(fpsNum, fpsDen, width, height)
    } else 0L

    private var cachedZoomScale: Double = 1.0

    val isActive: Boolean
        @Synchronized get() = handle != 0L

    @Synchronized
    fun syncClips(clips: List<ClipSnapshot>) {
        val current = handle
        if (current == 0L) return
        val count = clips.size
        val ids = Array(count) { clips[it].id }
        val trackIds = Array(count) { clips[it].trackId }
        val mediaIds = Array(count) { clips[it].mediaId }
        val startFrames = LongArray(count) { clips[it].startFrame }
        val durationFrames = LongArray(count) { clips[it].durationFrames }
        val sourceInMs = LongArray(count) { clips[it].sourceInMs }
        val sourceOutMs = LongArray(count) { clips[it].sourceOutMs }
        val gradeActives = BooleanArray(count) { clips[it].gradeActive }
        TimelineBridge.nativeSyncClips(
            current, ids, trackIds, mediaIds,
            startFrames, durationFrames, sourceInMs, sourceOutMs, gradeActives
        )
    }

    @Synchronized
    fun evaluateSnap(
        trackId: String,
        clipId: String,
        proposedFrame: Long,
        durationFrames: Long,
        playheadFrame: Long,
        pixelsPerFrame: Double,
        snapEnabled: Boolean,
    ): SnapDecision {
        val current = handle
        if (current == 0L) {
            return SnapDecision(proposedFrame.coerceAtLeast(0L), false, -1L, false, false)
        }
        val res = TimelineBridge.nativeEvaluateSnap(
            current, trackId, clipId, proposedFrame, durationFrames,
            playheadFrame, pixelsPerFrame, snapEnabled
        ) ?: return SnapDecision(proposedFrame.coerceAtLeast(0L), false, -1L, false, false)

        return if (res.size >= 5) {
            SnapDecision(
                finalFrame = res[0],
                snapped = res[1] != 0L,
                snapGuideFrame = res[2],
                slotAdjusted = res[3] != 0L,
                hapticPulse = res[4] != 0L,
            )
        } else {
            SnapDecision(proposedFrame.coerceAtLeast(0L), false, -1L, false, false)
        }
    }

    @Synchronized
    fun setPlayheadFrames(frame: Long) {
        val current = handle
        if (current != 0L) TimelineBridge.nativeSetPlayheadFrames(current, frame.coerceAtLeast(0L))
    }

    @Synchronized
    fun pinchZoom(scaleFactor: Double, focusXPx: Double): Double {
        val current = handle
        if (current == 0L) {
            if (scaleFactor > 0.0 && scaleFactor.isFinite()) {
                cachedZoomScale = (cachedZoomScale * scaleFactor).coerceIn(0.125, 15.0)
            }
            return cachedZoomScale
        }
        cachedZoomScale = TimelineBridge.nativePinchZoom(current, scaleFactor, focusXPx)
        return cachedZoomScale
    }

    @Synchronized
    fun resetZoom() {
        cachedZoomScale = 1.0
        val current = handle
        if (current != 0L) TimelineBridge.nativeResetZoom(current)
    }

    @Synchronized
    fun zoomScale(): Double {
        val current = handle
        return if (current != 0L) TimelineBridge.nativeGetZoomScale(current) else cachedZoomScale
    }

    @Synchronized
    fun capabilitiesJson(): String {
        val current = handle
        if (current == 0L) return "{\"engine\":\"owncut_timeline_engine\",\"active\":false}"
        return TimelineBridge.nativeGetCapabilitiesJson(current) ?: "{}"
    }

    @Synchronized
    fun exportRichStateJson(): String? {
        val current = handle
        if (current == 0L) return null
        return TimelineBridge.nativeExportStateJson(current)
    }

    @Synchronized
    fun importRichStateJson(json: String): Boolean {
        val current = handle
        if (current == 0L) return false
        return TimelineBridge.nativeImportStateJson(current, json)
    }

    @Synchronized
    fun setSnapEnabled(enabled: Boolean) {
        val current = handle
        if (current != 0L) TimelineBridge.nativeSetSnapEnabled(current, enabled)
    }

    @Synchronized
    fun snapEnabled(): Boolean {
        val current = handle
        return if (current == 0L) true else TimelineBridge.nativeGetSnapEnabled(current)
    }

    @Synchronized
    fun setBeatGrid(bpm: Double, offsetMs: Long = 0L) {
        val current = handle
        if (current != 0L) TimelineBridge.nativeSetBeatGrid(current, bpm, offsetMs)
    }

    @Synchronized
    fun upsertMarker(id: String, positionMs: Long, label: String, colorHex: String = "#4F9CF9") {
        val current = handle
        if (current != 0L) TimelineBridge.nativeUpsertMarker(current, id, positionMs, label, colorHex)
    }

    @Synchronized
    fun removeMarker(id: String): Boolean {
        val current = handle
        return if (current == 0L) false else TimelineBridge.nativeRemoveMarker(current, id)
    }

    @Synchronized
    fun importSrt(srtText: String): Boolean {
        val current = handle
        return if (current == 0L) false else TimelineBridge.nativeImportSrt(current, srtText)
    }

    @Synchronized
    fun exportSrt(): String {
        val current = handle
        if (current == 0L) return ""
        return TimelineBridge.nativeExportSrt(current).orEmpty()
    }

    fun analyzePcm16(pcmInterleaved: ShortArray, channels: Int, sampleRate: Int, numPeaks: Int = 64): AudioAnalysisResult? {
        if (!TimelineBridge.nativeLoaded || pcmInterleaved.isEmpty() || channels <= 0 || sampleRate <= 0) return null
        val raw = TimelineBridge.nativeAnalyzePcm16(pcmInterleaved, channels, sampleRate, numPeaks) ?: return null
        if (raw.size < 2) return null
        val bpm = raw[0]
        val lufs = raw[1]
        val peaks = List(raw.size - 2) { idx -> raw[idx + 2].toFloat().coerceIn(0f, 1f) }
        return AudioAnalysisResult(bpm, lufs, peaks)
    }

    fun detectSceneCuts(rgbaFrames: ByteArray, numFrames: Int, width: Int, height: Int, threshold: Double = 0.35): IntArray {
        if (!TimelineBridge.nativeLoaded || numFrames < 2 || width <= 0 || height <= 0) return IntArray(0)
        return TimelineBridge.nativeDetectSceneCuts(rgbaFrames, numFrames, width, height, threshold) ?: IntArray(0)
    }

    @Synchronized
    override fun close() {
        val current = handle
        if (current != 0L) {
            handle = 0L
            TimelineBridge.nativeDestroy(current)
        }
    }
}
