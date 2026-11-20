@file:androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)

package com.owncut.app

import android.content.Context
import android.net.Uri
import android.os.Looper
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipFile

/** Plain timeline export at 16 or 32 bit through the FP32 frame loop.
 * Scope (fail closed, checked before any output is written): one video track as produced by the timeline
 * remux plan, SDR Rec.709 sources, the same frame size and frame rate for every clip, AAC audio (copied, not
 * re-encoded) that is either present in every clip with the same config or absent from every clip, and no
 * rich/overlay features (checked by the caller).
 * Each clip is graded in FP32 (native CPU reference) before it is written. Frames are not clamped before the sink.
 */
internal object TimelineFp32Exporter {
    private class ClipProbe(val clip: ClipSnapshot, val info: SourceVideoInfo, val fullRange: Boolean, val audio: AudioTrackInfo?)

    fun export(
        context: Context,
        plan: TimelineRemuxExporter.Plan,
        format: String,
        output: File,
        cancelled: AtomicBoolean,
        interpretMissing: Boolean,
        progress: (String) -> Unit,
    ) {
        check(Looper.myLooper() != Looper.getMainLooper()) { "FP32 export must run off the UI thread" }
        val depth = ExportVideoFormat.bitDepth(format)
        check(ExportBitDepthRoute.of(depth, ExportVideoFormat.container(format)) == BitDepthRoute.FP32_LOOP)
        val checkCancelled = { if (cancelled.get()) throw CancellationException(AppLanguage.text(R.string.message_1452bfda9c)) }

        require(plan.clips.isNotEmpty()) { AppLanguage.text(R.string.export_fp32_no_clips) }
        val readSource = TimelineRenderExporter.sourceReader(context, checkCancelled, interpretMissing)
        val probes = plan.clips.map { clip ->
            checkCancelled()
            val color = readSource(clip.mediaId).color
            require(color.primaries == ColorEngine.VideoPrimaries.REC709 &&
                color.transfer == ColorEngine.VideoTransfer.SDR_VIDEO &&
                color.range != ColorEngine.VideoRange.UNKNOWN &&
                !color.hasHdrStaticMetadata) { AppLanguage.text(R.string.color_graded_sdr_only) }
            val uri = Uri.parse(clip.mediaId)
            val info = SourceFrameDecoder.probe(context, uri)
            require(info.frameRate > 0) { AppLanguage.text(R.string.export_fp32_frame_rate_missing) }
            val audio = AacTrackSource.probe(context, uri)
            if (audio != null) {
                require(audio.mime == AacTrackSource.AAC_MIME && audio.asc != null) {
                    AppLanguage.text(R.string.export_fp32_audio_codec, audio.mime)
                }
            }
            ClipProbe(clip, info, color.range == ColorEngine.VideoRange.FULL, audio)
        }
        val width = probes.first().info.outputWidth
        val height = probes.first().info.outputHeight
        val fps = probes.first().info.frameRate
        require(probes.all { it.info.outputWidth == width && it.info.outputHeight == height && it.info.frameRate == fps }) {
            AppLanguage.text(R.string.export_fp32_mixed_clips)
        }
        require(width % 2 == 0 && height % 2 == 0) { AppLanguage.text(R.string.export_fp32_odd_size) }

        // One audio stream for the whole output: every clip carries the same AAC config, or none carries audio.
        val audioConfig = probes.firstNotNullOfOrNull { it.audio }
        if (audioConfig != null) {
            val asc = checkNotNull(audioConfig.asc)
            require(probes.all { p ->
                val a = p.audio
                a != null && a.asc != null && a.asc.contentEquals(asc) && a.sampleRate == audioConfig.sampleRate && a.channels == audioConfig.channels
            }) { AppLanguage.text(R.string.export_fp32_audio_mixed) }
        }

        if (depth == OutputBitDepth.THIRTY_TWO && audioConfig != null) {
            require(AdtsHeader.supports(checkNotNull(audioConfig.asc))) { AppLanguage.text(R.string.export_fp32_audio_profile) }
        }

        val spoolFile = audioConfig?.let { File.createTempFile("owncut-fp32-audio-", ".aac", context.cacheDir) }
        val grades = HashMap<ColorGrade, GradeEngine>()
        var currentGrade: GradeEngine? = null
        val sink: Fp32FrameSink = when (depth) {
            OutputBitDepth.SIXTEEN -> Ffv1FrameSink(Ffv1Writer.open(output, width, height, fps, 1,
                audioConfig?.let { Ffv1AudioConfig(it.sampleRate, it.channels, checkNotNull(it.asc)) }))
            OutputBitDepth.THIRTY_TWO -> ExrZipSink(FileOutputStream(output), width, height,
                spoolFile?.let { AdtsAudioSpool(checkNotNull(audioConfig?.asc), it) })
            else -> error("not an FP32 depth")
        }
        var audioPackets = 0
        val frames = try {
            val loop = Fp32FrameLoop(width, height, { rgba8, out ->
                checkNotNull(currentGrade) { "no grade selected" }.apply(rgba8, out, width * height)
            }, sink)
            probes.forEachIndexed { index, probe ->
                checkCancelled()
                progress(AppLanguage.text(R.string.export_fp32_progress, index + 1, probes.size))
                currentGrade = grades.getOrPut(probe.clip.grade) { GradeEngine.open(probe.clip.grade) }
                val uri = Uri.parse(probe.clip.mediaId)
                // Audio first for this clip: packets are small, and the muxer then holds at most one clip of audio.
                if (probe.audio != null) {
                    val offsetUs = loop.framesWritten.toLong() * 1_000_000L / fps
                    audioPackets += AacTrackSource.copyRange(context, uri, probe.clip.sourceInMs * 1000L,
                        probe.clip.sourceOutMs * 1000L, offsetUs, loop) { cancelled.get() }
                }
                SourceFrameDecoder.decodeRange(context, uri, probe.clip.sourceInMs, probe.clip.sourceOutMs, probe.fullRange,
                    { cancelled.get() }) { frame -> loop.submit(frame) }
            }
            loop.finish()
        } finally {
            sink.close()
            grades.values.forEach { it.close() }
            spoolFile?.delete()
        }
        checkCancelled()
        check(output.length() > 0L) { "FP32 export wrote no bytes" }
        if (depth == OutputBitDepth.THIRTY_TWO) {
            // One entry per frame, plus audio.aac only when at least one audio packet was written to the spool.
            val expected = frames + if (audioConfig != null && audioPackets > 0) 1 else 0
            ZipFile(output).use { zip -> check(zip.size() == expected) { "EXR zip has ${zip.size()} entries, expected $expected" } }
        }
    }
}
