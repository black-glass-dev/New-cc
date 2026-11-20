package com.owncut.app

import android.content.Context
import android.net.Uri
import android.os.Looper
import androidx.media3.common.util.UnstableApi
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipFile

/** Plain timeline export at 16 or 32 bit through the FP32 frame loop.
 * Scope (fail closed, checked before any output is written): video only, no audio tracks, no grades,
 * SDR Rec.709 sources, the same frame size and frame rate for every clip, and no rich/overlay features (caller).
 * The decoded frames are not graded: the path has no grade stage yet, so grades stay on the 8/10-bit route.
 */
@UnstableApi
internal object TimelineFp32Exporter {
    private class ClipProbe(val clip: ClipSnapshot, val info: SourceVideoInfo, val fullRange: Boolean)

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
        require(plan.clips.none { it.gradeActive }) { AppLanguage.text(R.string.export_fp32_graded_unsupported) }
        val readSource = TimelineRenderExporter.sourceReader(context, checkCancelled, interpretMissing)
        val probes = plan.clips.map { clip ->
            checkCancelled()
            val color = readSource(clip.mediaId).color
            require(color.primaries == ColorEngine.VideoPrimaries.REC709 &&
                color.transfer == ColorEngine.VideoTransfer.SDR_VIDEO &&
                color.range != ColorEngine.VideoRange.UNKNOWN &&
                !color.hasHdrStaticMetadata) { AppLanguage.text(R.string.color_graded_sdr_only) }
            val info = SourceFrameDecoder.probe(context, Uri.parse(clip.mediaId))
            require(!info.hasAudio) { AppLanguage.text(R.string.export_fp32_audio_unsupported) }
            require(info.frameRate > 0) { AppLanguage.text(R.string.export_fp32_frame_rate_missing) }
            ClipProbe(clip, info, color.range == ColorEngine.VideoRange.FULL)
        }
        val width = probes.first().info.outputWidth
        val height = probes.first().info.outputHeight
        val fps = probes.first().info.frameRate
        require(probes.all { it.info.outputWidth == width && it.info.outputHeight == height && it.info.frameRate == fps }) {
            AppLanguage.text(R.string.export_fp32_mixed_clips)
        }
        require(width % 2 == 0 && height % 2 == 0) { AppLanguage.text(R.string.export_fp32_odd_size) }

        val sink: Fp32FrameSink = when (depth) {
            OutputBitDepth.SIXTEEN -> Ffv1FrameSink(Ffv1Writer.open(output, width, height, fps, 1))
            OutputBitDepth.THIRTY_TWO -> ExrZipSink(FileOutputStream(output), width, height)
            else -> error("not an FP32 depth")
        }
        val renderer = ExposureRenderer.open(allowVulkan = true)
        val input = ByteBuffer.allocateDirect(width * height * 4)
        val floats = ExposureRenderer.allocateOutput(width, height)
        val frames = try {
            val loop = Fp32FrameLoop(width, height, 0f, { rgba8, stops, out ->
                input.clear()
                input.put(rgba8)
                input.flip()
                renderer.render(input, width, height, stops, floats)
                floats.clear()
                floats.get(out)
            }, sink)
            probes.forEachIndexed { index, probe ->
                progress(AppLanguage.text(R.string.export_fp32_progress, index + 1, probes.size))
                SourceFrameDecoder.decodeRange(context, Uri.parse(probe.clip.mediaId), probe.clip.sourceInMs,
                    probe.clip.sourceOutMs, probe.fullRange, { cancelled.get() }) { frame -> loop.submit(frame) }
            }
            loop.finish()
        } finally {
            sink.close()
            renderer.close()
        }
        checkCancelled()
        check(output.length() > 0L) { "FP32 export wrote no bytes" }
        if (depth == OutputBitDepth.THIRTY_TWO) {
            ZipFile(output).use { zip -> check(zip.size() == frames) { "EXR zip has ${zip.size()} entries, expected $frames" } }
        }
    }
}
