package com.owncut.app

import android.media.MediaExtractor
import android.media.MediaMetadataRetriever
import android.media.MediaFormat
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean
import org.junit.Assert.*
import org.junit.Test

/** Real decoder/GPU/encoder tests, not a planner-only proxy for export. */
@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
class TimelineRenderExporterTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    @Test fun rendersUntouchedSourceInsteadOfRemuxing() = withFixtures { a, _ ->
        renderAndCheck(listOf(clip("a", a, 0, 0, 3000)), 3000L)
    }

    @Test fun rendersNonKeyframeTrim() = withFixtures { a, _ ->
        // Fixture GOP is 3 seconds: 500ms is deliberately not a sync sample.
        renderAndCheck(listOf(clip("trim", a, 0, 500, 1500)), 1000L)
    }

    @Test fun rendersThreeClipsWithMixedDimensionsAndMissingAudio() = withFixtures { a, b ->
        renderAndCheck(listOf(
            clip("first", a, 0, 500, 1500),
            clip("silent", b, 30, 1000, 2000),
            clip("last", a, 60, 1500, 2500),
        ), 3000L)
    }

    @Test fun cancelledRenderLeavesNoOutput() = withFixtures { a, _ ->
        val output = File(context.cacheDir, "cancel-render-${System.nanoTime()}.mp4")
        try {
            assertThrows(CancellationException::class.java) {
                TimelineRenderExporter.export(context,
                    TimelineRemuxExporter.makePlan(listOf(clip("a", a, 0, 0, 3000))),
                    output, AtomicBoolean(true)) { }
            }
            assertFalse(output.exists())
        } finally { output.delete() }
    }

    @Test fun preCancelledGradedCandidateNeverStartsEncoderOrPublishesOutput() = withFixtures { a, _ ->
        val output=File(context.cacheDir,"graded-cancel-${System.nanoTime()}.mp4")
        val source=clip("grade",a,0,0,3000).copy(grade=ColorGrade(exposureStops=1.0),gradeActive=true)
        val plan=TimelineRemuxExporter.candidateGradedTimingPlan(listOf(source))
        try {
            assertThrows(CancellationException::class.java) {
                TimelineRenderExporter.exportGradedCandidate(context,plan,"test-snapshot",output,AtomicBoolean(true)) { }
            }
            assertFalse(output.exists())
            assertFalse(Thread.getAllStackTraces().keys.any { it.isAlive && it.name=="OwncutRender" })
            assertThrows(IllegalArgumentException::class.java) { TimelineRenderExporter.requireShippingExportSupported(plan) }
            assertThrows(IllegalArgumentException::class.java) { TimelineRenderExporter.composition(plan) }
        } finally { output.delete() }
    }

    @Test(timeout = 120_000L)
    fun cancelsAnActiveRenderThenRendersAgainWithoutLeakingWorker() = withFixtures { a, _ ->
        val output = File(context.cacheDir, "active-cancel-${System.nanoTime()}.mp4")
        val cancel = AtomicBoolean(false)
        var started = false
        try {
            assertThrows(CancellationException::class.java) {
                TimelineRenderExporter.export(context,
                    TimelineRemuxExporter.makePlan(listOf(clip("a", a, 0, 0, 3000))),
                    output, cancel) { started = true; cancel.set(true) }
            }
            assertTrue(started)
            assertFalse(output.exists())
            assertFalse(Thread.getAllStackTraces().keys.any { it.isAlive && it.name == "OwncutRender" })
            renderAndCheck(listOf(clip("retry", a, 0, 500, 1500)), 1000L)
        } finally { output.delete() }
    }

    @Test(timeout = 120_000L)
    fun missingMediaAndInvalidOutputPathFailWithoutLeavingOutput() = withFixtures { a, _ ->
        val output = File(context.cacheDir, "missing-render-${System.nanoTime()}.mp4")
        val missing = File(context.cacheDir, "does-not-exist-${System.nanoTime()}.mp4")
        try {
            assertThrows(Exception::class.java) {
                TimelineRenderExporter.export(context,
                    TimelineRemuxExporter.makePlan(listOf(clip("missing", missing, 0, 0, 3000))),
                    output, AtomicBoolean(false)) { }
            }
            assertFalse(output.exists())
            val invalidOutput = File(missing, "output.mp4")
            assertThrows(Exception::class.java) {
                TimelineRenderExporter.export(context,
                    TimelineRemuxExporter.makePlan(listOf(clip("source", a, 0, 0, 3000))),
                    invalidOutput, AtomicBoolean(false)) { }
            }
            assertFalse(invalidOutput.exists())
        } finally { output.delete() }
    }

    @Test fun outputValidatorRejectsWrongTimelineDuration() = withFixtures { a, _ ->
        assertThrows(IllegalArgumentException::class.java) {
            TimelineRemuxExporter.verifyMuxedOutput(a, true, AtomicBoolean(false), 20_000_000L)
        }
        TimelineRemuxExporter.verifyMuxedOutput(a, true, AtomicBoolean(false), 3_000_000L)
    }

    private fun renderAndCheck(clips: List<ClipSnapshot>, expectedDurationMs: Long) {
        val output = File(context.cacheDir, "render-${System.nanoTime()}.mp4")
        try {
            TimelineRenderExporter.export(context, TimelineRemuxExporter.makePlan(clips),
                output, AtomicBoolean(false)) { }
            assertTrue(output.length() > 1024L)
            val extractor = MediaExtractor()
            try {
                extractor.setDataSource(output.absolutePath)
                val formats = (0 until extractor.trackCount).map(extractor::getTrackFormat)
                val video = formats.single { it.getString(MediaFormat.KEY_MIME) == "video/avc" }
                assertEquals(720, video.getInteger(MediaFormat.KEY_HEIGHT))
                assertTrue(formats.any { it.getString(MediaFormat.KEY_MIME) == "audio/mp4a-latm" })
            } finally { extractor.release() }
            val retriever = MediaMetadataRetriever()
            try {
                retriever.setDataSource(output.absolutePath)
                val duration = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)!!.toLong()
                assertTrue("duration $duration != $expectedDurationMs", kotlin.math.abs(duration - expectedDurationMs) < 150)
                for (time in listOf(0L, expectedDurationMs * 500L, (expectedDurationMs - 150L) * 1000L)) {
                    val frame = retriever.getFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST)
                    assertNotNull("frame at $time", frame)
                    frame?.let { bitmap ->
                        try {
                            val colors = mutableSetOf<Int>()
                            for (x in 1..7) for (y in 1..7)
                                colors.add(bitmap.getPixel(bitmap.width * x / 8, bitmap.height * y / 8))
                            assertTrue("rendered fixture must contain visible image content", colors.size > 4)
                        } finally { bitmap.recycle() }
                    }
                }
            } finally { retriever.release() }
        } finally { output.delete() }
    }

    private fun clip(id: String, file: File, start: Long, sourceIn: Long, sourceOut: Long) =
        ClipSnapshot(id, "v1", android.net.Uri.fromFile(file).toString(), start,
            (sourceOut - sourceIn) * 30 / 1000, sourceIn, sourceOut)

    private fun withFixtures(test: (File, File) -> Unit) {
        val a = File(context.cacheDir, "fixture-a-${System.nanoTime()}.mp4")
        val b = File(context.cacheDir, "fixture-b-${System.nanoTime()}.mp4")
        try {
            listOf("render-a.mp4" to a, "render-b.mp4" to b).forEach { (name, file) ->
                instrumentation.context.assets.open(name).use { source -> file.outputStream().use { source.copyTo(it) } }
            }
            test(a, b)
        } finally { a.delete(); b.delete() }
    }
}
