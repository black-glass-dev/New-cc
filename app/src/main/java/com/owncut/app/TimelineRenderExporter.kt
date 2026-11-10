package com.owncut.app

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Clock
import androidx.media3.effect.DefaultVideoFrameProcessor
import androidx.media3.effect.GlEffect
import androidx.media3.effect.Presentation
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
        revision: String, cancelled: AtomicBoolean): Candidate {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        val checkCancelled={ if (cancelled.get()) throw CancellationException("Color preparation cancelled") }
        return candidateComposition(plan,revision,checkCancelled) { uri ->
            VideoSourceProbe.read(context,uri,checkCancelled)
        }
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

    /** Blocking worker entry point; Transformer and all its operations stay on one owned looper. */
    fun export(
        context: Context,
        plan: TimelineRemuxExporter.Plan,
        output: File,
        cancelled: AtomicBoolean,
        progress: (String) -> Unit,
    ) {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        exportComposition(context,plan,output,cancelled,progress,composition(plan),null)
    }

    /** Explicit test/consented experimental entry: REAL encoder path, not parity proof.
     * Does not change shipping export guards. Caller owns scratch output and gap consent.
     */
    internal fun exportGradedCandidate(context: Context, plan: TimelineRemuxExporter.Plan, revision: String,
        output: File, cancelled: AtomicBoolean, progress: (String) -> Unit) {
        check(android.os.Looper.myLooper() != android.os.Looper.getMainLooper())
        val captured=plan.copy(clips=plan.clips.toList())
        val candidate=probeCandidate(context,captured,revision,cancelled)
        exportComposition(context,captured,output,cancelled,progress,candidate.composition,candidate)
    }

    private fun exportComposition(context: Context, plan: TimelineRemuxExporter.Plan, output: File,
        cancelled: AtomicBoolean, progress: (String) -> Unit, composition: Composition, candidate: Candidate?) {
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
                val platform = DefaultEncoderFactory.Builder(context.applicationContext)
                    .setEnableFallback(candidate==null).build()
                val alwaysEncode = object : Codec.EncoderFactory by platform {
                    override fun videoNeedsEncoding() = true
                    override fun audioNeedsEncoding() = true
                }
                transformer = Transformer.Builder(context.applicationContext)
                    .setLooper(thread.looper)
                    .apply { candidate?.configure(context,this) }
                    .setEncoderFactory(alwaysEncode)
                    .setVideoMimeType(MimeTypes.VIDEO_H264)
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
