package com.owncut.app

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import androidx.media3.common.Effect
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.SonicAudioProcessor
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Clock
import androidx.media3.effect.DefaultVideoFrameProcessor
import androidx.media3.effect.GlEffect
import androidx.media3.effect.Presentation
import androidx.media3.effect.SpeedChangeEffect
import androidx.media3.transformer.Codec
import androidx.media3.transformer.Composition
import androidx.media3.transformer.DefaultEncoderFactory
import androidx.media3.transformer.DefaultDecoderFactory
import androidx.media3.transformer.DefaultAssetLoaderFactory
import androidx.media3.transformer.EditedMediaItem
import androidx.media3.transformer.EditedMediaItemSequence
import androidx.media3.transformer.Effects
import androidx.media3.transformer.ExportException
import androidx.media3.transformer.ExportResult
import androidx.media3.transformer.ProgressHolder
import androidx.media3.transformer.Transformer
import androidx.media3.transformer.VideoEncoderSettings
import java.io.File
import java.util.concurrent.CancellationException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

/** Android codec adapter. Timeline edits remain owned by the native engine. */
@UnstableApi
internal object TimelineRenderExporter {
    // A conservative first output preset for mid-range phones. Never silently fall back to remux.
    const val OUTPUT_HEIGHT = 720

    internal fun requireShippingExportSupported(plan: TimelineRemuxExporter.Plan) {
        require(plan.clips.none { it.gradeActive }) { AppLanguage.text(R.string.color_grade_export_pending) }
    }
    internal fun composition(plan: TimelineRemuxExporter.Plan): Composition {
        requireShippingExportSupported(plan)
        return buildComposition(plan,null)
    }

    /** Explicit opt-in experimental route only. Shipping composition remains guarded.
     * Same immutable color plan can prepare preview effects via GradedFramePipeline.
     * No output file/codec starts here; does not authorize automatic gap compaction.
     */
    data class Candidate(val colors: ColorRenderPlan, val composition: Composition,
                         val factory: DefaultVideoFrameProcessor.Factory) {
        fun configure(context: Context, builder: Transformer.Builder): Transformer.Builder = builder
            .setVideoFrameProcessorFactory(factory)
            .setAssetLoaderFactory(DefaultAssetLoaderFactory(context.applicationContext,
                GuardedDecoderFactory(DefaultDecoderFactory.Builder(context.applicationContext).build(),colors,preserveRawOutput=true),
                Clock.DEFAULT,null))
    }
    internal fun probeCandidate(context: Context, plan: TimelineRemuxExporter.Plan,
        revision: String, cancelled: AtomicBoolean, interpretMissing: Boolean=false): Candidate {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        val checkCancelled={ if (cancelled.get()) throw CancellationException("Color preparation cancelled") }
        return candidateComposition(plan,revision,checkCancelled,sourceReader(context,checkCancelled,interpretMissing))
    }
    internal fun candidateComposition(plan: TimelineRemuxExporter.Plan, revision: String,
        checkCancelled: () -> Unit, readSource: (String) -> VideoSourceMetadata): Candidate {
        val captured=plan.copy(clips=plan.clips.toList())
        val colors=ColorRenderPlan.prepare(revision,captured.clips,checkCancelled,readSource)
        val prepared=GradedFramePipeline.prepare(colors,checkCancelled)
        checkCancelled()
        val composition=buildComposition(captured,prepared.effects,checkCancelled)
        checkCancelled()
        return Candidate(colors,composition,prepared.factory)
    }

    private fun buildComposition(plan: TimelineRemuxExporter.Plan, grades: List<GlEffect>?,
        checkCancelled: () -> Unit = {}): Composition {
        val items = plan.clips.mapIndexed { index,clip ->
            checkCancelled()
            val media = MediaItem.Builder()
                .setUri(clip.mediaId)
                .setClippingConfiguration(MediaItem.ClippingConfiguration.Builder()
                    .setStartPositionMs(clip.sourceInMs)
                    .setEndPositionMs(clip.sourceOutMs)
                    .build())
                .build()
            EditedMediaItem.Builder(media).apply {
                if (grades != null) setEffects(Effects(emptyList(),listOf(grades[index])))
            }.build()
        }
        val sequence = EditedMediaItemSequence.Builder(items)
            .experimentalSetForceAudioTrack(true)
            .build()
        return Composition.Builder(sequence)
            .setTransmuxAudio(false)
            .setTransmuxVideo(false)
            .setHdrMode(if (grades == null) Composition.HDR_MODE_TONE_MAP_HDR_TO_SDR_USING_OPEN_GL
                else Composition.HDR_MODE_KEEP_HDR)
            .setEffects(Effects(emptyList(), listOf(Presentation.createForHeight(OUTPUT_HEIGHT))))
            .build()
    }

    internal data class SpeedSegment(val startMs: Long, val endMs: Long, val speed: Float)

    /** Approximate a linear ramp with short, constant-rate codec segments. Splits at every
     * authored key so no segment straddles a discontinuity; 200ms caps rate-curve error.
     */
    internal fun speedSegments(fx: ClipRichFx, durationMs: Long): List<SpeedSegment> {
        val duration = durationMs.coerceAtLeast(1L)
        if (fx.speedKeyframes.isEmpty()) return listOf(SpeedSegment(0L, duration, fx.speed.coerceIn(0.25, 4.0).toFloat()))
        val keyTimes = fx.speedKeyframes.map { it.timeMs.coerceIn(0L, duration) }.distinct().sorted()
        val boundaries = sortedSetOf(0L, duration)
        keyTimes.forEach { boundaries += it }
        val authored = boundaries.toList()
        for (i in 0 until authored.lastIndex) {
            var point = authored[i] + 200L
            while (point < authored[i + 1]) { boundaries += point; point += 200L }
        }
        return boundaries.toList().zipWithNext().mapNotNull { (start, end) ->
            if (end <= start) null else SpeedSegment(start, end, fx.speedAt(start + (end - start) / 2L, duration))
        }
    }

    internal fun richComposition(
        plan: TimelineRemuxExporter.Plan,
        richDoc: TimelineRichDocument,
        allClips: List<ClipSnapshot>,
        tracks: List<TimelineTrackInfo>,
        grades: List<GlEffect>? = null,
        checkCancelled: () -> Unit = {},
    ): Composition {
        val trackStates = tracks.associateBy { it.id }
        val v1TrackMuted = trackStates["v1"]?.muted == true
        val videoItems = plan.clips.flatMapIndexed { index, clip ->
            checkCancelled()
            val fx = richDoc.fxFor(clip.id)
            val durMs = (clip.sourceOutMs - clip.sourceInMs).coerceAtLeast(1L)
            speedSegments(fx, durMs).map { segment ->
                checkCancelled()
                val media = MediaItem.Builder()
                    .setUri(clip.mediaId)
                    .setClippingConfiguration(
                        MediaItem.ClippingConfiguration.Builder()
                            .setStartPositionMs(clip.sourceInMs + segment.startMs)
                            .setEndPositionMs((clip.sourceInMs + segment.endMs).coerceAtMost(clip.sourceOutMs))
                            .build()
                    ).build()
                val videoFx = mutableListOf<Effect>()
                if (fx.hasVideoTransformOrFx()) videoFx += TimelineClipGlEffect(fx, durMs * 1000L, segment.startMs * 1000L)
                if (segment.speed != 1f) videoFx += SpeedChangeEffect(segment.speed)
                if (grades != null && index < grades.size) videoFx += grades[index]
                val effectiveFx = if (v1TrackMuted) fx.copy(muted = true) else fx
                val audioFx = mutableListOf<AudioProcessor>()
                if (segment.speed != 1f) audioFx += SonicAudioProcessor().apply { setSpeed(segment.speed) }
                if (effectiveFx.hasAudioEnvelope()) audioFx += ClipVolumeAudioProcessor(effectiveFx, durMs, localOffsetMs = segment.startMs)
                EditedMediaItem.Builder(media).apply {
                    if (audioFx.isNotEmpty() || videoFx.isNotEmpty()) setEffects(Effects(audioFx, videoFx))
                }.build()
            }
        }

        val sequences = mutableListOf<EditedMediaItemSequence>()
        sequences += EditedMediaItemSequence.Builder(videoItems)
            .experimentalSetForceAudioTrack(true)
            .build()

        // Extra audio tracks (a1, a2, ...) mixed cleanly alongside v1
        val audioTracks = allClips
            .filter { it.trackId.startsWith("a", ignoreCase = true) }
            .groupBy { it.trackId }
        for ((trackId, trackClips) in audioTracks) {
            if (trackStates[trackId]?.muted == true) continue
            val sorted = trackClips.sortedBy { it.startFrame }
            val seqBuilder = EditedMediaItemSequence.Builder().experimentalSetForceAudioTrack(true)
            var cursorFrame = 0L
            var hasItems = false
            for (aClip in sorted) {
                checkCancelled()
                val gapFrames = (aClip.startFrame - cursorFrame).coerceAtLeast(0L)
                if (gapFrames > 0L) {
                    seqBuilder.addGap((gapFrames * 1_000_000L) / TimelineGeometry.FPS)
                }
                val fx = richDoc.fxFor(aClip.id)
                val durMs = (aClip.sourceOutMs - aClip.sourceInMs).coerceAtLeast(1L)
                val duckFactor = if (fx.duckingDb < -0.5f) {
                    Math.pow(10.0, fx.duckingDb / 20.0).toFloat().coerceIn(0.1f, 1f)
                } else 1.0f
                for (segment in speedSegments(fx, durMs)) {
                    val aMedia = MediaItem.Builder()
                        .setUri(aClip.mediaId)
                        .setClippingConfiguration(
                            MediaItem.ClippingConfiguration.Builder()
                                .setStartPositionMs(aClip.sourceInMs + segment.startMs)
                                .setEndPositionMs((aClip.sourceInMs + segment.endMs).coerceAtMost(aClip.sourceOutMs))
                                .build()
                        ).build()
                    val audioProcessors = mutableListOf<AudioProcessor>()
                    if (segment.speed != 1f) audioProcessors += SonicAudioProcessor().apply { setSpeed(segment.speed) }
                    if (fx.hasAudioEnvelope() || duckFactor < 0.99f) {
                        audioProcessors += ClipVolumeAudioProcessor(fx, durMs, duckFactor, segment.startMs)
                    }
                    val item = EditedMediaItem.Builder(aMedia)
                        .setRemoveVideo(true)
                        .apply { if (audioProcessors.isNotEmpty()) setEffects(Effects(audioProcessors, emptyList())) }
                        .build()
                    seqBuilder.addItem(item)
                    hasItems = true
                }
                cursorFrame = maxOf(cursorFrame, aClip.startFrame + aClip.durationFrames)
            }
            if (hasItems) {
                sequences += seqBuilder.build()
            }
        }

        val globalVideoEffects = mutableListOf<Effect>()
        if (richDoc.overlays.isNotEmpty() || richDoc.subtitles.isNotEmpty()) {
            globalVideoEffects += TextBurnGlEffect(richDoc)
        }
        val explicitWidth = richDoc.outputWidth
        val explicitHeight = richDoc.outputHeight
        require((explicitWidth == null) == (explicitHeight == null)) { "Custom resolution needs both dimensions" }
        if (explicitWidth != null && explicitHeight != null) {
            require(explicitWidth in 144..3840 && explicitHeight in 144..3840 &&
                explicitWidth % 2 == 0 && explicitHeight % 2 == 0 &&
                explicitWidth.toLong() * explicitHeight <= 8_294_400L) { "Invalid custom output resolution" }
            globalVideoEffects += Presentation.createForWidthAndHeight(
                explicitWidth, explicitHeight, Presentation.LAYOUT_SCALE_TO_FIT_WITH_CROP,
            )
        } else {
            richDoc.outputAspectRatio?.let { aspect ->
                globalVideoEffects += Presentation.createForAspectRatio(
                    aspect, Presentation.LAYOUT_SCALE_TO_FIT_WITH_CROP,
                )
            }
            globalVideoEffects += Presentation.createForHeight(OUTPUT_HEIGHT)
        }

        return Composition.Builder(sequences)
            .setTransmuxAudio(false)
            .setTransmuxVideo(false)
            .setHdrMode(
                if (grades == null) Composition.HDR_MODE_TONE_MAP_HDR_TO_SDR_USING_OPEN_GL
                else Composition.HDR_MODE_KEEP_HDR
            )
            .setEffects(Effects(emptyList(), globalVideoEffects))
            .build()
    }

    /** Blocking worker entry point; Transformer and all its operations stay on one owned looper. */
    fun export(
        context: Context,
        plan: TimelineRemuxExporter.Plan,
        output: File,
        cancelled: AtomicBoolean,
        videoMime: String = MimeTypes.VIDEO_H264,
        progress: (String) -> Unit,
    ) {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        exportComposition(context,plan,output,cancelled,progress,composition(plan),null,videoMime)
    }

    fun exportRich(
        context: Context,
        plan: TimelineRemuxExporter.Plan,
        richDoc: TimelineRichDocument,
        allClips: List<ClipSnapshot>,
        tracks: List<TimelineTrackInfo>,
        output: File,
        cancelled: AtomicBoolean,
        videoMime: String = MimeTypes.VIDEO_H264,
        progress: (String) -> Unit,
    ) {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        requireShippingExportSupported(plan)
        val comp = richComposition(plan, richDoc, allClips, tracks, null) {
            if (cancelled.get()) throw CancellationException("Export cancelled")
        }
        exportComposition(context, plan, output, cancelled, progress, comp, null, videoMime)
    }

    /** Missing tags are interpreted only with explicit consent; otherwise the export stops before encoding. */
    internal fun sourceReader(context: Context, checkCancelled: () -> Unit, interpretMissing: Boolean): (String) -> VideoSourceMetadata =
        { uri ->
            val source = VideoSourceProbe.read(context, uri, checkCancelled)
            if (PreviewColorPolicy.needsInterpretation(source.color)) {
                if (!interpretMissing) throw ColorTagsInterpretationRequired()
                source.copy(color = PreviewColorPolicy.resolve(source.color, true))
            } else source
        }

    /** Graded export accepts only SDR Rec.709 sources with known range and no HDR static metadata. */
    internal fun requireSdrRec709Sources(colors: ColorRenderPlan) {
        for (prepared in colors.clips) {
            val color = prepared.source.color
            require(color.primaries == ColorEngine.VideoPrimaries.REC709 &&
                color.transfer == ColorEngine.VideoTransfer.SDR_VIDEO &&
                color.range != ColorEngine.VideoRange.UNKNOWN &&
                !color.hasHdrStaticMetadata) { AppLanguage.text(R.string.color_graded_sdr_only) }
        }
    }

    internal fun exportRichGraded(
        context: Context,
        plan: TimelineRemuxExporter.Plan,
        revision: String,
        richDoc: TimelineRichDocument,
        allClips: List<ClipSnapshot>,
        tracks: List<TimelineTrackInfo>,
        output: File,
        cancelled: AtomicBoolean,
        interpretMissing: Boolean = false,
        videoMime: String = MimeTypes.VIDEO_H264,
        progress: (String) -> Unit,
    ) {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        val captured = plan.copy(clips = plan.clips.toList())
        val checkCancelled = { if (cancelled.get()) throw CancellationException("Color preparation cancelled") }
        val colors = ColorRenderPlan.prepare(revision, captured.clips, checkCancelled,
            sourceReader(context, checkCancelled, interpretMissing))
        requireSdrRec709Sources(colors)
        val prepared = GradedFramePipeline.prepare(colors, checkCancelled)
        val comp = richComposition(captured, richDoc, allClips, tracks, prepared.effects, checkCancelled)
        val candidate = Candidate(colors, comp, prepared.factory)
        exportComposition(context, captured, output, cancelled, progress, comp, candidate, videoMime)
    }

    /** Explicit test/consented experimental entry: REAL encoder path, not parity proof.
     * Does not change shipping export guards. Caller owns scratch output and gap consent.
     */
    internal fun exportGradedCandidate(context: Context, plan: TimelineRemuxExporter.Plan, revision: String,
        output: File, cancelled: AtomicBoolean, interpretMissing: Boolean=false, videoMime: String=MimeTypes.VIDEO_H264,
        progress: (String) -> Unit) {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        val captured=plan.copy(clips=plan.clips.toList())
        val candidate=probeCandidate(context,captured,revision,cancelled,interpretMissing)
        requireSdrRec709Sources(candidate.colors)
        exportComposition(context,captured,output,cancelled,progress,candidate.composition,candidate,videoMime)
    }

    private fun exportComposition(context: Context, plan: TimelineRemuxExporter.Plan, output: File,
        cancelled: AtomicBoolean, progress: (String) -> Unit, composition: Composition, candidate: Candidate?,
        videoMime: String) {
        val thread = HandlerThread("OwncutRender").apply { start() }
        val handler = Handler(thread.looper)
        val finished = CountDownLatch(1)
        val failure = AtomicReference<Throwable?>()
        var transformer: Transformer? = null // accessed only on the render looper
        val ticker = object : Runnable {
            override fun run() {
                if (finished.count == 0L) return
                try {
                    if (cancelled.get()) {
                        transformer?.cancel()
                        failure.set(CancellationException(AppLanguage.text(R.string.message_1452bfda9c)))
                        finished.countDown()
                        return
                    }
                    val holder = ProgressHolder()
                    val state = transformer?.getProgress(holder)
                    progress(if (state == Transformer.PROGRESS_STATE_AVAILABLE)
                        AppLanguage.text(R.string.message_34d403c213, holder.progress) else AppLanguage.text(R.string.message_0f4c747ce7))
                    handler.postDelayed(this, 250L)
                } catch (error: Throwable) {
                    failure.set(error)
                    finished.countDown()
                }
            }
        }
        handler.post {
            try {
                if (cancelled.get()) throw CancellationException(AppLanguage.text(R.string.message_1452bfda9c))
                // 10-bit HEVC: request Main 10 explicitly and never fall back to another codec.
                val hevc10 = Hevc10Export.isHevc10(videoMime)
                val platform = DefaultEncoderFactory.Builder(context.applicationContext)
                    .setEnableFallback(candidate==null && !hevc10)
                    .apply {
                        if (hevc10) {
                            val (profile, level) = Hevc10Export.requireEncoderLevel()
                            setRequestedVideoEncoderSettings(
                                VideoEncoderSettings.Builder().setEncodingProfileLevel(profile, level).build()
                            )
                        }
                    }.build()
                val alwaysEncode = object : Codec.EncoderFactory by platform {
                    override fun videoNeedsEncoding() = true
                    override fun audioNeedsEncoding() = true
                }
                transformer = Transformer.Builder(context.applicationContext)
                    .setLooper(thread.looper)
                    .apply { candidate?.configure(context,this) }
                    .setEncoderFactory(alwaysEncode)
                    .setVideoMimeType(Hevc10Export.transformerMime(videoMime))
                    .setAudioMimeType(MimeTypes.AUDIO_AAC)
                    .addListener(object : Transformer.Listener {
                        override fun onCompleted(composition: Composition, exportResult: ExportResult) {
                            finished.countDown()
                        }
                        override fun onError(
                            composition: Composition,
                            exportResult: ExportResult,
                            exportException: ExportException,
                        ) {
                            failure.set(exportException)
                            finished.countDown()
                        }
                    }).build()
                transformer!!.start(composition, output.absolutePath)
                handler.post(ticker)
            } catch (error: Throwable) {
                failure.set(error)
                finished.countDown()
            }
        }
        var succeeded = false
        try {
            // Codec hangs must not leave an indefinitely busy editor. Device runs still need validation.
            check(finished.await(30, TimeUnit.MINUTES)) { AppLanguage.text(R.string.message_493010d9f8) }
            failure.get()?.let { throw it }
            if (cancelled.get()) throw CancellationException(AppLanguage.text(R.string.message_1452bfda9c))
            progress(AppLanguage.text(R.string.message_485965e2a5))
            val expectedDurationUs = plan.clips.fold(0L) { total, clip ->
                Math.addExact(total, Math.multiplyExact(clip.sourceOutMs - clip.sourceInMs, 1000L))
            }
            TimelineRemuxExporter.verifyMuxedOutput(output, true, cancelled, expectedDurationUs)
            if (Hevc10Export.isHevc10(videoMime)) Hevc10Export.requireMain10Output(output)
            succeeded = true
        } catch (interrupted: InterruptedException) {
            cancelled.set(true)
            throw CancellationException(AppLanguage.text(R.string.message_5714916d16)).apply { initCause(interrupted) }
        } finally {
            val stopped = CountDownLatch(1)
            handler.removeCallbacks(ticker)
            handler.post {
                try { transformer?.cancel() } finally { stopped.countDown() }
            }
            // Never delete a file while the codec still has an open muxer.
            var interrupted = false
            while (true) {
                try { stopped.await(); break } catch (_: InterruptedException) { interrupted = true }
            }
            thread.quitSafely()
            while (thread.isAlive) {
                try { thread.join() } catch (_: InterruptedException) { interrupted = true }
            }
            if (interrupted) Thread.currentThread().interrupt()
            if (!succeeded) output.delete()
        }
    }
}
