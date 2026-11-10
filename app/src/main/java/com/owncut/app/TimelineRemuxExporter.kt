package com.owncut.app

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.media.MediaMuxer
import android.net.Uri
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.abs

/**
 * Lossless stream-copy export for a deliberately narrow, verifiable subset of the editor timeline.
 * No frame rendering or re-encoding is performed: clips must be non-overlapping, start on video sync
 * samples, and share compatible codec formats. Explicitly accepted empty gaps are concatenated.
 */
internal object TimelineRemuxExporter {
    private const val FRAME_RATE = 30L
    private const val FRAME_ALIGNMENT_TOLERANCE_US = 1_000_000L / (FRAME_RATE * 2L) + 1_000L
    private const val KEYFRAME_TOLERANCE_US = 1_000L
    private const val MAX_SAMPLE_BUFFER_BYTES = 32 * 1024 * 1024

    data class Plan(
        val clips: List<ClipSnapshot>,
        val trackId: String,
        val omittedGapFrames: Long,
    )

    fun makePlan(clips: List<ClipSnapshot>): Plan {
        require(clips.none { it.gradeActive }) { AppLanguage.text(R.string.color_grade_export_pending) }
        return timingPlan(clips)
    }

    /** Candidate graded composition only; shipping render/remux entry points keep guards. */
    internal fun candidateGradedTimingPlan(clips: List<ClipSnapshot>): Plan = timingPlan(clips)

    private fun timingPlan(clips: List<ClipSnapshot>): Plan {
        require(clips.isNotEmpty()) { AppLanguage.text(R.string.message_3df8193901) }
        val ordered = clips.sortedBy { it.startFrame }
        val track = ordered.first().trackId
        require(ordered.all { it.mediaId.isNotBlank() }) {
            AppLanguage.text(R.string.message_5656245819)
        }
        require(ordered.all { it.trackId == track }) {
            AppLanguage.text(R.string.message_7c02a3426b)
        }
        var previousTimelineEnd = 0L
        var outputFrame = 0L
        var omittedGapFrames = 0L
        val normalized = ArrayList<ClipSnapshot>(ordered.size)
        for (clip in ordered) {
            require(clip.startFrame >= previousTimelineEnd) {
                AppLanguage.text(R.string.message_8f2c8b3266)
            }
            omittedGapFrames = Math.addExact(omittedGapFrames, clip.startFrame - previousTimelineEnd)
            require(clip.durationFrames > 0L && clip.sourceOutMs > clip.sourceInMs) {
                AppLanguage.text(R.string.message_37b13021f8)
            }
            val sourceDurationUs = Math.multiplyExact(clip.sourceOutMs - clip.sourceInMs, 1_000L)
            val timelineDurationUs = Math.multiplyExact(clip.durationFrames, 1_000_000L) / FRAME_RATE
            require(abs(sourceDurationUs - timelineDurationUs) <= FRAME_ALIGNMENT_TOLERANCE_US) {
                AppLanguage.text(R.string.message_ac16e106a4)
            }
            normalized += clip.copy(startFrame = outputFrame)
            outputFrame = Math.addExact(outputFrame, clip.durationFrames)
            previousTimelineEnd = Math.addExact(clip.startFrame, clip.durationFrames)
        }
        return Plan(normalized, track, omittedGapFrames)
    }

    fun export(
        context: Context,
        plan: Plan,
        output: File,
        cancelled: AtomicBoolean,
        progress: (String) -> Unit,
    ) {
        require(plan.clips.none { it.gradeActive }) { AppLanguage.text(R.string.color_grade_export_pending) }
        val firstMediaId = plan.clips.first().mediaId
        val probe = newExtractor(context, Uri.parse(firstMediaId))
        val tracksByMediaId = linkedMapOf<String, Tracks>()
        var muxer: MediaMuxer? = null
        var muxerStarted = false
        var expectedAudioTrack = false
        try {
            val primaryTracks = findTracks(probe)
            require(primaryTracks.videoIndex >= 0) { AppLanguage.text(R.string.message_4087ee8fbc) }
            tracksByMediaId[firstMediaId] = primaryTracks
            for (mediaId in plan.clips.asSequence().map { it.mediaId }.distinct().drop(1)) {
                ensureNotCancelled(cancelled)
                val sourceProbe = newExtractor(context, Uri.parse(mediaId))
                try {
                    val tracks = findTracks(sourceProbe)
                    require(tracks.videoIndex >= 0) { AppLanguage.text(R.string.message_4087ee8fbc) }
                    requireCompatibleTracks(primaryTracks, tracks)
                    tracksByMediaId[mediaId] = tracks
                } finally {
                    sourceProbe.release()
                }
            }
            expectedAudioTrack = primaryTracks.audioIndex >= 0

            for (clip in plan.clips) {
                ensureNotCancelled(cancelled)
                verifySyncBoundary(
                    context,
                    Uri.parse(clip.mediaId),
                    tracksByMediaId.getValue(clip.mediaId).videoIndex,
                    clip.sourceInMs * 1000L,
                )
            }

            muxer = MediaMuxer(output.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
            val outputVideo = muxer.addTrack(primaryTracks.videoFormat)
            val outputAudio = if (expectedAudioTrack) muxer.addTrack(primaryTracks.audioFormat!!) else -1
            val rotation = rotationHint(primaryTracks.videoFormat)
            if (rotation == 90 || rotation == 180 || rotation == 270) muxer.setOrientationHint(rotation)
            muxer.start()
            muxerStarted = true

            val maxInput = tracksByMediaId.values.flatMap { tracks ->
                listOfNotNull(
                    tracks.videoFormat.maxInputSizeOrNull(),
                    tracks.audioFormat?.maxInputSizeOrNull(),
                )
            }.maxOrNull()?.coerceIn(64 * 1024, MAX_SAMPLE_BUFFER_BYTES) ?: 1024 * 1024
            val sampleBuffer = ByteBuffer.allocateDirect(maxInput)
            val info = MediaCodec.BufferInfo()
            var lastVideoPtsUs = Long.MIN_VALUE
            var lastAudioPtsUs = Long.MIN_VALUE

            for ((index, clip) in plan.clips.withIndex()) {
                ensureNotCancelled(cancelled)
                progress(AppLanguage.text(R.string.message_7d95b4d830, index + 1, plan.clips.size))
                val sourceTracks = tracksByMediaId.getValue(clip.mediaId)
                val extractor = newExtractor(context, Uri.parse(clip.mediaId))
                try {
                    extractor.selectTrack(sourceTracks.videoIndex)
                    if (sourceTracks.audioIndex >= 0) extractor.selectTrack(sourceTracks.audioIndex)
                    val sourceInUs = clip.sourceInMs * 1000L
                    val sourceOutUs = clip.sourceOutMs * 1000L
                    val timelineStartUs = Math.multiplyExact(clip.startFrame, 1_000_000L) / FRAME_RATE
                    val timelineDurationUs = Math.multiplyExact(clip.durationFrames, 1_000_000L) / FRAME_RATE
                    extractor.seekTo(sourceInUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
                    var firstVideoSampleSeen = false
                    while (extractor.sampleTrackIndex >= 0 && extractor.sampleTime < sourceOutUs) {
                        ensureNotCancelled(cancelled)
                        val sourceTrack = extractor.sampleTrackIndex
                        val sampleTime = extractor.sampleTime
                        val clipRelativeUs = sampleTime - sourceInUs
                        if (clipRelativeUs >= 0L && clipRelativeUs < timelineDurationUs &&
                            (sourceTrack == sourceTracks.videoIndex || sourceTrack == sourceTracks.audioIndex)) {
                            if (sourceTrack == sourceTracks.videoIndex && !firstVideoSampleSeen) {
                                val isSync = extractor.sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC != 0
                                require(isSync && abs(sampleTime - sourceInUs) <= KEYFRAME_TOLERANCE_US) {
                                    AppLanguage.text(R.string.message_28ccb137ab)
                                }
                                firstVideoSampleSeen = true
                            }
                            sampleBuffer.clear()
                            val size = extractor.readSampleData(sampleBuffer, 0)
                            require(size > 0 && size <= sampleBuffer.capacity()) {
                                AppLanguage.text(R.string.message_411b41bacc)
                            }
                            val outputPtsUs = Math.addExact(timelineStartUs, clipRelativeUs)
                            val destinationTrack = if (sourceTrack == sourceTracks.videoIndex) {
                                require(outputPtsUs > lastVideoPtsUs) {
                                    AppLanguage.text(R.string.message_71dafd7a01)
                                }
                                lastVideoPtsUs = outputPtsUs
                                outputVideo
                            } else {
                                require(outputPtsUs > lastAudioPtsUs) {
                                    AppLanguage.text(R.string.message_1f7073c78b)
                                }
                                lastAudioPtsUs = outputPtsUs
                                outputAudio
                            }
                            info.set(0, size, outputPtsUs, toMuxerFlags(extractor.sampleFlags))
                            muxer.writeSampleData(destinationTrack, sampleBuffer, info)
                        }
                        if (!extractor.advance()) break
                    }
                    require(firstVideoSampleSeen) { AppLanguage.text(R.string.message_0582bad777) }
                } finally {
                    extractor.release()
                }
            }
            ensureNotCancelled(cancelled)
            muxer.stop()
            muxerStarted = false
        } catch (cancel: CancellationException) {
            output.delete()
            throw cancel
        } catch (error: Exception) {
            output.delete()
            throw error
        } finally {
            if (muxerStarted) {
                try { muxer?.stop() } catch (_: RuntimeException) { }
            }
            muxer?.release()
            probe.release()
        }
        try {
            progress(AppLanguage.text(R.string.message_65a0c3462d))
            verifyMuxedOutput(output, expectedAudioTrack, cancelled)
            progress(AppLanguage.text(R.string.message_e78d6626c8))
        } catch (error: Exception) {
            output.delete()
            throw error
        }
    }

    private data class Tracks(
        val videoIndex: Int,
        val videoFormat: MediaFormat,
        val audioIndex: Int,
        val audioFormat: MediaFormat?,
    )

    private fun findTracks(extractor: MediaExtractor): Tracks {
        var videoIndex = -1
        var videoFormat: MediaFormat? = null
        var audioIndex = -1
        var audioFormat: MediaFormat? = null
        for (index in 0 until extractor.trackCount) {
            val format = extractor.getTrackFormat(index)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: continue
            when {
                mime.startsWith("video/") -> {
                    require(videoIndex < 0) { AppLanguage.text(R.string.message_545b6f2b51) }
                    videoIndex = index
                    videoFormat = format
                }
                mime.startsWith("audio/") -> {
                    require(audioIndex < 0) { AppLanguage.text(R.string.message_7fdc670a3b) }
                    audioIndex = index
                    audioFormat = format
                }
            }
        }
        return Tracks(
            videoIndex,
            videoFormat ?: MediaFormat(),
            audioIndex,
            audioFormat,
        )
    }

    private fun requireCompatibleTracks(reference: Tracks, candidate: Tracks) {
        require((reference.audioIndex >= 0) == (candidate.audioIndex >= 0)) {
            AppLanguage.text(R.string.message_4bb961786b)
        }
        requireCompatibleFormat(reference.videoFormat, candidate.videoFormat, "video")
        if (reference.audioIndex >= 0) {
            requireCompatibleFormat(reference.audioFormat!!, candidate.audioFormat!!, "audio")
        }
    }

    internal fun formatsCompatible(reference: MediaFormat, candidate: MediaFormat, kind: String): Boolean {
        if (reference.getString(MediaFormat.KEY_MIME) != candidate.getString(MediaFormat.KEY_MIME)) return false
        val keys = if (kind == "video") {
            listOf(
                MediaFormat.KEY_WIDTH,
                MediaFormat.KEY_HEIGHT,
                MediaFormat.KEY_PROFILE,
                MediaFormat.KEY_LEVEL,
                MediaFormat.KEY_COLOR_STANDARD,
                MediaFormat.KEY_COLOR_RANGE,
                MediaFormat.KEY_COLOR_TRANSFER,
            )
        } else {
            listOf(
                MediaFormat.KEY_SAMPLE_RATE,
                MediaFormat.KEY_CHANNEL_COUNT,
                MediaFormat.KEY_PROFILE,
                MediaFormat.KEY_LEVEL,
            )
        }
        for (key in keys) {
            val first = if (reference.containsKey(key)) reference.getInteger(key) else null
            val next = if (candidate.containsKey(key)) candidate.getInteger(key) else null
            if (first != next) return false
        }
        for (index in 0..3) {
            val key = "csd-${index}"
            val first = reference.getByteBuffer(key)?.copyRemainingBytes()
            val next = candidate.getByteBuffer(key)?.copyRemainingBytes()
            if (!byteArraysEqual(first, next)) return false
        }
        return kind != "video" || rotationHint(reference) == rotationHint(candidate)
    }

    private fun requireCompatibleFormat(reference: MediaFormat, candidate: MediaFormat, kind: String) {
        require(formatsCompatible(reference, candidate, kind)) {
            AppLanguage.text(R.string.message_94375196f5, kind)
        }
    }

    private fun rotationHint(format: MediaFormat): Int =
        if (format.containsKey(MediaFormat.KEY_ROTATION)) format.getInteger(MediaFormat.KEY_ROTATION) else 0

    private fun ByteBuffer.copyRemainingBytes(): ByteArray {
        val duplicate = duplicate()
        val bytes = ByteArray(duplicate.remaining())
        duplicate.get(bytes)
        return bytes
    }

    private fun byteArraysEqual(first: ByteArray?, second: ByteArray?): Boolean = when {
        first == null -> second == null
        second == null -> false
        else -> first.contentEquals(second)
    }

    internal fun verifyMuxedOutput(
        output: File,
        expectedAudioTrack: Boolean,
        cancelled: AtomicBoolean,
        expectedDurationUs: Long? = null,
    ) {
        require(output.isFile && output.length() > 0L) { AppLanguage.text(R.string.message_8ccdf8c183) }
        val extractor = MediaExtractor()
        var maximumVideoIntervalUs = 33_334L
        try {
            extractor.setDataSource(output.absolutePath)
            val tracks = findTracks(extractor)
            require(tracks.videoIndex >= 0) { AppLanguage.text(R.string.message_36eb3c2094) }
            require((tracks.audioIndex >= 0) == expectedAudioTrack) {
                AppLanguage.text(R.string.message_f118417f6a)
            }
            extractor.selectTrack(tracks.videoIndex)
            if (tracks.audioIndex >= 0) extractor.selectTrack(tracks.audioIndex)
            var videoSamples = 0
            var audioSamples = 0
            var lastVideoTime = Long.MIN_VALUE
            var lastAudioTime = Long.MIN_VALUE
            // Negative AAC pre-roll PTS is not EOF; track index is the unambiguous EOS signal.
            while (extractor.sampleTrackIndex >= 0) {
                ensureNotCancelled(cancelled)
                when (extractor.sampleTrackIndex) {
                    tracks.videoIndex -> {
                        require(extractor.sampleTime > lastVideoTime) { AppLanguage.text(R.string.message_0a8e43ec87) }
                        if (lastVideoTime != Long.MIN_VALUE)
                            maximumVideoIntervalUs = maxOf(maximumVideoIntervalUs, extractor.sampleTime - lastVideoTime)
                        lastVideoTime = extractor.sampleTime
                        videoSamples++
                    }
                    tracks.audioIndex -> {
                        require(extractor.sampleTime > lastAudioTime) { AppLanguage.text(R.string.message_fac2a9f7ec) }
                        lastAudioTime = extractor.sampleTime
                        audioSamples++
                    }
                }
                if (!extractor.advance()) break
            }
            require(videoSamples > 0) { AppLanguage.text(R.string.message_a82ee6e5ca) }
            require(!expectedAudioTrack || audioSamples > 0) { AppLanguage.text(R.string.message_831513c165) }
        } finally {
            extractor.release()
        }

        ensureNotCancelled(cancelled)
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(output.absolutePath)
            val durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                ?.toLongOrNull() ?: throw IllegalArgumentException(AppLanguage.text(R.string.message_4983402399))
            require(durationMs > 0L) { AppLanguage.text(R.string.message_5a994fce3b) }
            expectedDurationUs?.let { expected ->
                val tolerance = maxOf(250_000L, maximumVideoIntervalUs * 2L)
                require(abs(durationMs * 1000L - expected) <= tolerance) {
                    AppLanguage.text(R.string.message_896bcfa0b6)
                }
            }
            for (timeUs in listOf(0L, ((durationMs - 50L).coerceAtLeast(0L)) * 1000L).distinct()) {
                ensureNotCancelled(cancelled)
                val frame = retriever.getFrameAtTime(timeUs, MediaMetadataRetriever.OPTION_CLOSEST)
                    ?: throw IllegalArgumentException(AppLanguage.text(R.string.message_89fb9c0b30))
                try { ensureNotCancelled(cancelled) } finally { frame.recycle() }
            }
        } finally {
            retriever.release()
        }
    }

    private fun verifySyncBoundary(context: Context, uri: Uri, videoTrack: Int, sourceInUs: Long) {
        val extractor = newExtractor(context, uri)
        try {
            extractor.selectTrack(videoTrack)
            extractor.seekTo(sourceInUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            while (extractor.sampleTime >= 0L) {
                val time = extractor.sampleTime
                if (time >= sourceInUs && extractor.sampleTrackIndex == videoTrack &&
                    extractor.sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC != 0) {
                    require(abs(time - sourceInUs) <= KEYFRAME_TOLERANCE_US) {
                        AppLanguage.text(R.string.message_a038b9bbf4)
                    }
                    return
                }
                if (!extractor.advance()) break
            }
            throw IllegalArgumentException(AppLanguage.text(R.string.message_cb75968fda))
        } finally {
            extractor.release()
        }
    }

    private fun toMuxerFlags(sampleFlags: Int): Int {
        require(sampleFlags and MediaExtractor.SAMPLE_FLAG_ENCRYPTED == 0) {
            AppLanguage.text(R.string.message_bf30911ad7)
        }
        require(sampleFlags and MediaExtractor.SAMPLE_FLAG_PARTIAL_FRAME == 0) {
            AppLanguage.text(R.string.message_b4ff35a2fa)
        }
        return if (sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC != 0) {
            MediaCodec.BUFFER_FLAG_KEY_FRAME
        } else {
            0
        }
    }

    private fun newExtractor(context: Context, uri: Uri): MediaExtractor {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(context, uri, null)
            return extractor
        } catch (error: Exception) {
            extractor.release()
            throw error
        }
    }

    private fun MediaFormat.maxInputSizeOrNull(): Int? =
        if (containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)) getInteger(MediaFormat.KEY_MAX_INPUT_SIZE) else null

    private fun ensureNotCancelled(cancelled: AtomicBoolean) {
        if (cancelled.get() || Thread.currentThread().isInterrupted) throw CancellationException(AppLanguage.text(R.string.message_2b042e5a6c))
    }
}
