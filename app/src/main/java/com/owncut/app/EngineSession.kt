package com.owncut.app

import com.owncut.timeline.AudioAnalysisResult
import com.owncut.timeline.SnapDecision
import com.owncut.timeline.TimelineEngineBridge
import org.json.JSONObject
import org.json.JSONArray
import java.io.File

internal data class MediaAssetSummary(val mediaId: String, val clipIds: List<String>, val requiredSourceEndMs: Long)

internal data class PlayheadPosition(val clipId: String, val timelineFrame: Long, val sourcePositionMs: Long)

/**
 * Owns one native engine session. Native work is intentionally batched at this boundary.
 * Project and autosave file methods are blocking; call them from a worker, not the UI thread.
 */
internal class EngineSession : AutoCloseable {
    private enum class HistorySlot { NATIVE, RICH }

    private var handle: Long = nativeCreate(java.util.UUID.randomUUID().toString())
    private val timelineBridge = TimelineEngineBridge()
    private var syncedTimelineRevision: String? = null
    private var richDocument: TimelineRichDocument = TimelineRichDocument()
    private val undoSlots = ArrayDeque<HistorySlot>()
    private val redoSlots = ArrayDeque<HistorySlot>()
    private val richUndoStack = ArrayDeque<TimelineRichDocument>()
    private val richRedoStack = ArrayDeque<TimelineRichDocument>()

    private val snapshotCache = TimelineSnapshotCache()

    init {
        check(handle != 0L) { AppLanguage.text(R.string.message_22b4d27a73) }
    }

    private fun recordNativeEdit() {
        undoSlots.addLast(HistorySlot.NATIVE)
        if (undoSlots.size > 256) undoSlots.removeFirst()
        redoSlots.clear()
        richRedoStack.clear()
    }

    private fun applyLoadedRichDocument(loaded: TimelineRichDocument) {
        richDocument = loaded
        undoSlots.clear()
        redoSlots.clear()
        richUndoStack.clear()
        richRedoStack.clear()
        timelineBridge.importRichStateJson(richDocument.toJsonString())
    }

    private fun syncRichBridgeLocked() {
        timelineBridge.importRichStateJson(richDocument.toJsonString())
    }

    @Synchronized
    fun richState(): TimelineRichDocument = richDocument

    @Synchronized
    fun clipFx(clipId: String): ClipRichFx = richDocument.fxFor(clipId)

    @Synchronized
    fun mutateRichState(recordUndo: Boolean = true, transform: (TimelineRichDocument) -> TimelineRichDocument) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val updated = transform(richDocument)
        if (updated == richDocument) return
        if (recordUndo) {
            richUndoStack.addLast(richDocument)
            undoSlots.addLast(HistorySlot.RICH)
            while (undoSlots.size > 256) undoSlots.removeFirst()
            while (richUndoStack.size > 256) richUndoStack.removeFirst()
            redoSlots.clear()
            richRedoStack.clear()
        }
        richDocument = updated
        syncRichBridgeLocked()
    }

    @Synchronized
    fun setClipFx(clipId: String, updater: (ClipRichFx) -> ClipRichFx) {
        val trackId = clips().firstOrNull { it.id == clipId }?.trackId ?: "v1"
        mutateRichState(recordUndo = true) { doc ->
            val current = doc.clipFx[clipId] ?: ClipRichFx(clipId = clipId, trackId = trackId)
            val next = updater(current.copy(trackId = trackId))
            val map = LinkedHashMap(doc.clipFx)
            map[clipId] = next
            doc.copy(clipFx = map)
        }
    }

    @Synchronized
    fun setClipWaveformPeaks(clipId: String, peaks: List<Float>) {
        if (peaks.isEmpty()) return
        val trackId = clips().firstOrNull { it.id == clipId }?.trackId ?: "v1"
        mutateRichState(recordUndo = false) { doc ->
            val current = doc.clipFx[clipId] ?: ClipRichFx(clipId = clipId, trackId = trackId)
            if (current.audioPeaks == peaks) return@mutateRichState doc
            val map = LinkedHashMap(doc.clipFx)
            map[clipId] = current.copy(audioPeaks = peaks)
            doc.copy(clipFx = map)
        }
    }

    @Synchronized
    fun setSnapEnabled(enabled: Boolean) {
        mutateRichState(recordUndo = false) { it.copy(snapEnabled = enabled) }
        timelineBridge.setSnapEnabled(enabled)
    }

    @Synchronized
    fun isSnapEnabled(): Boolean = richDocument.snapEnabled

    @Synchronized
    fun setBeatGrid(bpm: Double, offsetMs: Long = 0L) {
        mutateRichState(recordUndo = true) { it.copy(beatBpm = bpm.coerceAtLeast(0.0), beatOffsetMs = offsetMs.coerceAtLeast(0L)) }
        timelineBridge.setBeatGrid(bpm, offsetMs)
    }

    @Synchronized
    fun upsertOverlay(item: TextOverlayItem) {
        mutateRichState(recordUndo = true) { doc ->
            val list = doc.overlays.filterNot { it.id == item.id } + item
            doc.copy(overlays = list.sortedBy { it.startMs })
        }
    }

    @Synchronized
    fun removeOverlay(overlayId: String) {
        mutateRichState(recordUndo = true) { doc ->
            doc.copy(overlays = doc.overlays.filterNot { it.id == overlayId })
        }
    }

    @Synchronized
    fun upsertSubtitle(cue: SubtitleCueItem) {
        mutateRichState(recordUndo = true) { doc ->
            val list = doc.subtitles.filterNot { it.id == cue.id } + cue
            doc.copy(subtitles = list.sortedBy { it.startMs })
        }
    }

    @Synchronized
    fun removeSubtitle(cueId: String) {
        mutateRichState(recordUndo = true) { doc ->
            doc.copy(subtitles = doc.subtitles.filterNot { it.id == cueId })
        }
    }

    @Synchronized
    fun importSrtSubtitles(srtText: String): Int {
        val parsed = TimelineRichDocument.parseSrt(srtText)
        if (parsed.isEmpty()) return 0
        mutateRichState(recordUndo = true) { doc -> doc.copy(subtitles = parsed) }
        return parsed.size
    }

    @Synchronized
    fun exportSrtSubtitles(): String = TimelineRichDocument.formatSrt(richDocument.subtitles)

    @Synchronized
    fun upsertMarker(marker: TimelineMarkerItem) {
        mutateRichState(recordUndo = true) { doc ->
            val list = doc.markers.filterNot { it.id == marker.id } + marker
            doc.copy(markers = list.sortedBy { it.positionMs })
        }
    }

    @Synchronized
    fun removeMarker(markerId: String) {
        mutateRichState(recordUndo = true) { doc ->
            doc.copy(markers = doc.markers.filterNot { it.id == markerId })
        }
    }

    fun analyzeAudioPcm16(pcmInterleaved: ShortArray, channels: Int, sampleRate: Int, numPeaks: Int = 64): AudioAnalysisResult? =
        timelineBridge.analyzePcm16(pcmInterleaved, channels, sampleRate, numPeaks)

    fun detectVideoSceneCuts(rgbaFrames: ByteArray, numFrames: Int, width: Int, height: Int, threshold: Double = 0.35): IntArray =
        timelineBridge.detectSceneCuts(rgbaFrames, numFrames, width, height, threshold)

    @Synchronized
    fun loadRichSidecarFromFile(file: File) {
        val doc = TimelineRichDocument.readIfExists(file) ?: TimelineRichDocument()
        applyLoadedRichDocument(doc)
    }

    @Synchronized
    fun saveRichSidecarToFile(file: File) {
        TimelineRichDocument.writeAtomic(file, richDocument)
    }

    @Synchronized
    fun status(): String {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        return nativeGetStatus(handle)
    }

    @Synchronized
    fun clips(): List<ClipSnapshot> {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val revision = nativeGetRevision(handle)
        val result = snapshotCache.read(revision) { offset, limit, expected ->
            clipPage(offset, limit, expected)
        }
        if (syncedTimelineRevision != revision) {
            timelineBridge.syncClips(result)
            syncedTimelineRevision = revision
        }
        return result
    }

    @Synchronized
    fun evaluateTimelineSnap(
        trackId: String,
        clipId: String,
        proposedFrame: Long,
        durationFrames: Long,
        playheadFrame: Long,
        pixelsPerFrame: Double,
        snapEnabled: Boolean = true,
    ): SnapDecision {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        clips()
        return timelineBridge.evaluateSnap(
            trackId, clipId, proposedFrame, durationFrames,
            playheadFrame, pixelsPerFrame, snapEnabled
        )
    }

    @Synchronized
    fun pinchTimelineZoom(scaleFactor: Double, focusXPx: Double): Double {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        return timelineBridge.pinchZoom(scaleFactor, focusXPx)
    }

    @Synchronized
    fun resetTimelineZoom() {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        timelineBridge.resetZoom()
    }

    @Synchronized
    fun timelineZoomScale(): Double {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        return timelineBridge.zoomScale()
    }

    @Synchronized
    fun timelineCapabilitiesJson(): String {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        clips()
        return timelineBridge.capabilitiesJson()
    }

    @Synchronized
    fun clipPage(offset: Int, limit: Int = 256, expectedRevision: String? = null): ClipSnapshotPage {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        require(offset >= 0 && limit in 1..256)
        val json = JSONObject(nativeGetClipPage(handle, offset, limit, expectedRevision))
        return ClipSnapshotPage(parseClips(json.getJSONArray("clips")), json.getString("revision"),
            json.getInt("total"), json.getInt("nextOffset"))
    }

    private fun parseClips(array: JSONArray): List<ClipSnapshot> = List(array.length()) { index ->
        val clip = array.getJSONObject(index)
        val grade = clip.getJSONObject("grade")
        val values = grade.getJSONArray("parameters")
        ClipSnapshot(clip.getString("id"), clip.getString("trackId"), clip.getString("mediaId"),
            clip.getLong("startFrame"), clip.getLong("durationFrames"),
            clip.getLong("sourceInMs"), clip.getLong("sourceOutMs"),
            ColorGrade.fromParametersV1(DoubleArray(values.length()) { values.getDouble(it) },grade.getBoolean("enabled")),
            clip.getBoolean("gradeActive"))
    }
    private external fun nativeGetRevision(handle: Long): String
    private external fun nativeGetClipPage(handle: Long, offset: Int, limit: Int, expectedRevision: String?): String

    @Synchronized
    fun setClipGrades(clipIds: List<String>, grade: ColorGrade) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSetClipGrades(handle,clipIds.toTypedArray(),grade.parametersV1(),grade.enabled)
        recordNativeEdit()
    }
    @Synchronized
    fun resetClipGrades(clipIds: List<String>) = setClipGrades(clipIds,ColorGrade())
    @Synchronized
    fun setClipGradeEnabled(clipId: String, enabled: Boolean) {
        val grade = clips().firstOrNull { it.id == clipId }?.grade
            ?: error(AppLanguage.text(R.string.message_3d1d8c290a))
        setClipGrades(listOf(clipId),grade.copy(enabled=enabled))
    }
    @Synchronized
    fun copyClipGrade(sourceId: String, targetIds: List<String>) {
        val grade = clips().firstOrNull { it.id == sourceId }?.grade
            ?: error(AppLanguage.text(R.string.message_3d1d8c290a))
        setClipGrades(targetIds,grade)
    }
    private external fun nativeSetClipGrades(handle: Long, targets: Array<String>, parameters: DoubleArray, enabled: Boolean)

    @Synchronized
    fun mediaAssets(): List<MediaAssetSummary> {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val array = JSONObject(nativeGetMediaAssets(handle)).getJSONArray("assets")
        return List(array.length()) { index ->
            val item = array.getJSONObject(index)
            val ids = item.getJSONArray("clipIds")
            MediaAssetSummary(item.getString("mediaId"), List(ids.length()) { ids.getString(it) }, item.getLong("requiredSourceEndMs"))
        }
    }
    private external fun nativeGetMediaAssets(handle: Long): String

    @Synchronized
    fun resolvePlayhead(trackId: String, timelineFrame: Long): PlayheadPosition? {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val json = nativeResolvePlayhead(handle, trackId, timelineFrame) ?: return null
        val value = JSONObject(json)
        return PlayheadPosition(value.getString("clipId"), value.getLong("timelineFrame"), value.getLong("sourcePositionMs"))
    }

    @Synchronized
    fun ensureTrack(trackId: String, isAudio: Boolean) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeEnsureTrack(handle, trackId, isAudio)
    }

    @Synchronized
    fun setTrackState(trackId: String, muted: Boolean, locked: Boolean) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSetTrackState(handle, trackId, muted, locked)
    }

    @Synchronized
    fun tracks(): List<TimelineTrackInfo> {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val array = JSONObject(nativeGetTracksJson(handle)).getJSONArray("tracks")
        return List(array.length()) { index ->
            val t = array.getJSONObject(index)
            TimelineTrackInfo(
                id = t.getString("id"),
                kind = t.getString("kind"),
                muted = t.getBoolean("muted"),
                locked = t.getBoolean("locked"),
                clipCount = t.getInt("clipCount"),
            )
        }
    }

    @Synchronized
    fun addMediaClip(mediaId: String, durationMs: Long): String {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val id = nativeAddMediaClip(handle, mediaId, durationMs)
        recordNativeEdit()
        return id
    }

    @Synchronized
    fun addMediaClipToTrack(trackId: String, mediaId: String, durationMs: Long, startFrame: Long = -1L): String {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val isAudio = trackId.startsWith("a", ignoreCase = true)
        nativeEnsureTrack(handle, trackId, isAudio)
        val effectiveStart = if (startFrame >= 0L) {
            val durFrames = ((durationMs * 30L) + 999L) / 1000L
            val existing = clips().filter { it.trackId == trackId }
            val overlaps = existing.any { c ->
                startFrame < c.startFrame + c.durationFrames && c.startFrame < startFrame + durFrames
            }
            if (overlaps) -1L else startFrame
        } else -1L
        val id = nativeAddMediaToTrack(handle, trackId, mediaId, durationMs, effectiveStart)
        recordNativeEdit()
        return id
    }

    @Synchronized
    fun relinkClipMedia(clipId: String, replacementMediaId: String, replacementDurationMs: Long) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val all = clips()
        val target = all.firstOrNull { it.id == clipId } ?: error(AppLanguage.text(R.string.message_3d1d8c290a))
        val required = all.filter { it.mediaId == target.mediaId }.maxOf { it.sourceOutMs }
        require(replacementDurationMs >= required) {
            AppLanguage.text(R.string.message_02a0ab04a0, required)
        }
        nativeRelinkClipMedia(handle, clipId, replacementMediaId)
        recordNativeEdit()
    }

    @Synchronized
    fun moveClip(clipId: String, destinationTrackId: String, destinationStartFrame: Long) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeMoveClip(handle, clipId, destinationTrackId, destinationStartFrame)
        recordNativeEdit()
    }

    /** Copies a clip's source range to an audio-only track and mutes its embedded source audio. */
    @Synchronized
    fun detachAudio(clipId: String): String {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val source = clips().firstOrNull { it.id == clipId } ?: error(AppLanguage.text(R.string.message_3d1d8c290a))
        require(!source.trackId.startsWith("a", ignoreCase = true)) { "Select a video clip to detach audio" }
        require(tracks().firstOrNull { it.id == source.trackId }?.locked != true) { "Unlock the source track before detaching audio" }
        require(source.sourceOutMs > source.sourceInMs) { "Clip has no usable source range" }
        val start = source.startFrame
        val end = Math.addExact(start, source.durationFrames)
        val allClips = clips()
        val audioTracks = tracks().filter { it.kind.equals("audio", ignoreCase = true) || it.id.startsWith("a", true) }
        var target = audioTracks.firstOrNull { candidate ->
            allClips.none { it.trackId == candidate.id && start < Math.addExact(it.startFrame, it.durationFrames) && it.startFrame < end }
        }?.id
        if (target == null) {
            var index = 1
            while (index < 1000) {
                val candidate = "a$index"
                if (audioTracks.none { it.id == candidate }) { target = candidate; break }
                index++
            }
        }
        val audioTrack = target ?: error("No available audio track")
        ensureTrack(audioTrack, true)
        var detachedId: String? = null
        try {
            detachedId = addMediaClipToTrack(audioTrack, source.mediaId, source.sourceOutMs, start)
            if (source.sourceInMs > 0L) {
                nativeTrimClip(handle, detachedId, true, source.sourceInMs)
                recordNativeEdit()
                nativeMoveClip(handle, detachedId, audioTrack, start)
                recordNativeEdit()
            }
            val sourceFx = clipFx(clipId)
            setClipFx(detachedId) {
                it.copy(
                    speed = sourceFx.speed,
                    reversed = sourceFx.reversed,
                    freezeMs = sourceFx.freezeMs,
                    volume = sourceFx.volume,
                    fadeInMs = sourceFx.fadeInMs,
                    fadeOutMs = sourceFx.fadeOutMs,
                    muted = false,
                    duckingDb = sourceFx.duckingDb,
                )
            }
            setClipFx(clipId) { it.copy(muted = true) }
            return detachedId
        } catch (error: Throwable) {
            detachedId?.let { runCatching { deleteClip(it) } }
            throw error
        }
    }

    @Synchronized
    fun splitClip(clipId: String, cutFrame: Long, rightClipId: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSplitClip(handle, clipId, cutFrame, rightClipId)
        // Propagate rich FX from left clip to split right segment cleanly
        richDocument.clipFx[clipId]?.let { leftFx ->
            val rightFx = leftFx.copy(clipId = rightClipId, transitionIn = "none", transitionMs = 0L)
            val map = LinkedHashMap(richDocument.clipFx)
            map[rightClipId] = rightFx
            richDocument = richDocument.copy(clipFx = map)
            syncRichBridgeLocked()
        }
        recordNativeEdit()
    }

    @Synchronized
    fun slipClip(clipId: String, sourceDeltaMs: Long) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSlipClip(handle, clipId, sourceDeltaMs)
        recordNativeEdit()
    }

    @Synchronized
    fun rollBoundary(leftClipId: String, rightClipId: String, sourceDeltaMs: Long) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeRollBoundary(handle, leftClipId, rightClipId, sourceDeltaMs)
        recordNativeEdit()
    }

    @Synchronized
    fun slideClips(leftClipId: String, movingClipId: String, rightClipId: String, deltaMs: Long) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSlideClips(handle, leftClipId, movingClipId, rightClipId, deltaMs)
        recordNativeEdit()
    }

    @Synchronized
    fun trimClip(clipId: String, leftEdge: Boolean, sourceBoundaryMs: Long) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeTrimClip(handle, clipId, leftEdge, sourceBoundaryMs)
        recordNativeEdit()
    }

    @Synchronized
    fun deleteClip(clipId: String, ripple: Boolean = false) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeDeleteClip(handle, clipId, ripple)
        recordNativeEdit()
    }

    @Synchronized
    fun duplicateClip(clipId: String): String {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val src = clips().firstOrNull { it.id == clipId } ?: error(AppLanguage.text(R.string.message_3d1d8c290a))
        val destFrame = src.startFrame + src.durationFrames
        val newId = addMediaClipToTrack(src.trackId, src.mediaId, src.sourceOutMs, destFrame)
        if (src.sourceInMs > 0L) {
            nativeTrimClip(handle, newId, true, src.sourceInMs)
            recordNativeEdit()
            nativeMoveClip(handle, newId, src.trackId, destFrame)
            recordNativeEdit()
        }
        if (src.gradeActive && !src.trackId.startsWith("a", ignoreCase = true)) {
            nativeSetClipGrades(handle, arrayOf(newId), src.grade.parametersV1(), src.grade.enabled)
            recordNativeEdit()
        }
        richDocument.clipFx[clipId]?.let { srcFx ->
            val map = LinkedHashMap(richDocument.clipFx)
            map[newId] = srcFx.copy(clipId = newId)
            richDocument = richDocument.copy(clipFx = map)
            syncRichBridgeLocked()
        }
        return newId
    }

    @Synchronized
    fun undo(): Boolean {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        if (undoSlots.isEmpty()) return nativeUndo(handle)
        return when (undoSlots.removeLast()) {
            HistorySlot.RICH -> {
                if (richUndoStack.isEmpty()) return false
                richRedoStack.addLast(richDocument)
                richDocument = richUndoStack.removeLast()
                redoSlots.addLast(HistorySlot.RICH)
                syncRichBridgeLocked()
                true
            }
            HistorySlot.NATIVE -> {
                val ok = nativeUndo(handle)
                if (ok) redoSlots.addLast(HistorySlot.NATIVE)
                ok
            }
        }
    }

    @Synchronized
    fun redo(): Boolean {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        if (redoSlots.isEmpty()) return nativeRedo(handle)
        return when (redoSlots.removeLast()) {
            HistorySlot.RICH -> {
                if (richRedoStack.isEmpty()) return false
                richUndoStack.addLast(richDocument)
                richDocument = richRedoStack.removeLast()
                undoSlots.addLast(HistorySlot.RICH)
                syncRichBridgeLocked()
                true
            }
            HistorySlot.NATIVE -> {
                val ok = nativeRedo(handle)
                if (ok) undoSlots.addLast(HistorySlot.NATIVE)
                ok
            }
        }
    }

    @Synchronized
    fun saveProject(path: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSaveProject(handle, path)
        TimelineRichDocument.writeAtomic(TimelineRichDocument.sidecarFor(path), richDocument)
    }

    @Synchronized
    fun loadProject(path: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeLoadProject(handle, path)
        applyLoadedRichDocument(TimelineRichDocument.readIfExists(TimelineRichDocument.sidecarFor(path)) ?: TimelineRichDocument())
    }

    @Synchronized
    fun saveAutosave(path: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSaveAutosave(handle, path)
        TimelineRichDocument.writeAtomic(TimelineRichDocument.sidecarFor("$path.autosave"), richDocument)
    }

    @Synchronized
    fun loadAutosave(path: String): Boolean {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        val ok = nativeLoadAutosave(handle, path)
        if (ok) {
            applyLoadedRichDocument(TimelineRichDocument.readIfExists(TimelineRichDocument.sidecarFor("$path.autosave")) ?: TimelineRichDocument())
        }
        return ok
    }

    @Synchronized
    fun clearAutosave(path: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeClearAutosave(handle, path)
        TimelineRichDocument.sidecarFor("$path.autosave").delete()
    }

    @Synchronized
    override fun close() {
        val current = handle
        if (current != 0L) {
            handle = 0L
            syncedTimelineRevision = null
            snapshotCache.clear()
            undoSlots.clear()
            redoSlots.clear()
            richUndoStack.clear()
            richRedoStack.clear()
            timelineBridge.close()
            nativeDestroy(current)
        }
    }

    @Synchronized
    fun saveCopy(path: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeSaveCopy(handle,path)
        TimelineRichDocument.writeAtomic(TimelineRichDocument.sidecarFor(path), richDocument)
    }
    @Synchronized
    fun restoreDraft(path: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeRestoreDraft(handle,path)
        applyLoadedRichDocument(TimelineRichDocument.readIfExists(TimelineRichDocument.sidecarFor(path)) ?: TimelineRichDocument())
    }
    private external fun nativeSaveCopy(handle: Long, path: String)
    private external fun nativeRestoreDraft(handle: Long, path: String)
    @Synchronized
    fun checkpointProject(path: String) {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        nativeCheckpointProject(handle,path)
        TimelineRichDocument.writeAtomic(TimelineRichDocument.sidecarFor(path), richDocument)
    }
    @Synchronized
    fun repairCheckpoint(path: String): Boolean {
        check(handle != 0L) { AppLanguage.text(R.string.message_bf5f0d21b6) }
        return nativeRepairCheckpoint(handle,path)
    }
    private external fun nativeRepairCheckpoint(handle: Long, path: String): Boolean
    private external fun nativeCheckpointProject(handle: Long, path: String)
    private external fun nativeCreate(projectId: String): Long
    private external fun nativeGetStatus(handle: Long): String
    private external fun nativeGetTimelineSnapshot(handle: Long): String
    private external fun nativeResolvePlayhead(handle: Long, trackId: String, timelineFrame: Long): String?
    private external fun nativeEnsureTrack(handle: Long, trackId: String, isAudio: Boolean)
    private external fun nativeSetTrackState(handle: Long, trackId: String, muted: Boolean, locked: Boolean)
    private external fun nativeGetTracksJson(handle: Long): String
    private external fun nativeAddMediaClip(handle: Long, mediaId: String, durationMs: Long): String
    private external fun nativeAddMediaToTrack(handle: Long, trackId: String, mediaId: String, durationMs: Long, startFrame: Long): String
    private external fun nativeRelinkClipMedia(handle: Long, clipId: String, replacementMediaId: String)
    private external fun nativeMoveClip(handle: Long, clipId: String, destinationTrackId: String, destinationStartFrame: Long)
    private external fun nativeSplitClip(handle: Long, clipId: String, cutFrame: Long, rightClipId: String)
    private external fun nativeSlipClip(handle: Long, clipId: String, sourceDeltaMs: Long)
    private external fun nativeRollBoundary(handle: Long, leftClipId: String, rightClipId: String, sourceDeltaMs: Long)
    private external fun nativeSlideClips(handle: Long, leftClipId: String, movingClipId: String, rightClipId: String, deltaMs: Long)
    private external fun nativeTrimClip(handle: Long, clipId: String, leftEdge: Boolean, sourceBoundaryMs: Long)
    private external fun nativeDeleteClip(handle: Long, clipId: String, ripple: Boolean)
    private external fun nativeUndo(handle: Long): Boolean
    private external fun nativeRedo(handle: Long): Boolean
    private external fun nativeSaveProject(handle: Long, path: String)
    private external fun nativeLoadProject(handle: Long, path: String)
    private external fun nativeSaveAutosave(handle: Long, path: String)
    private external fun nativeLoadAutosave(handle: Long, path: String): Boolean
    private external fun nativeClearAutosave(handle: Long, path: String)
    private external fun nativeDestroy(handle: Long)

    companion object {
        init { System.loadLibrary("owncut_android") }
    }
}
