package com.owncut.app

import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import androidx.media3.common.util.UnstableApi
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** REAL candidate encoder/decode test SOURCE, UNCOMPILED/UNRUN here. Proposed codec
 * thresholds are not measured phone results. Does not prove live-display or all-color parity.
 * No skips/fallback to ungraded export: unsupported raw output must surface as failure.
 */
@UnstableApi
class GradedExportParityTest {
    private val context=InstrumentationRegistry.getInstrumentation().targetContext
    @Test(timeout=240_000L) fun twoClipExposureAndBypassMatchNativeReferenceAcrossCut() {
        val dir=File(context.cacheDir,"grade-parity-${System.nanoTime()}").apply { mkdirs() }
        try {
            val source=File(dir,"gray.mp4");GrayVideoFixture.create(source)
            val neutral=File(dir,"neutral.mp4");val graded=File(dir,"graded.mp4");val bypassed=File(dir,"bypass.mp4")
            val base=EngineSession().use { session ->
                session.addMediaClip(Uri.fromFile(source).toString(),1000)
                session.addMediaClip(Uri.fromFile(source).toString(),1000)
                session.clips()
            }
            fun encode(clips: List<ClipSnapshot>,output: File) {
                TimelineRenderExporter.exportGradedCandidate(context,
                    TimelineRemuxExporter.candidateGradedTimingPlan(clips),"parity-fixture",output,AtomicBoolean(false)) { }
                assertTrue(output.length()>1024)
                TimelineRemuxExporter.verifyMuxedOutput(output,true,AtomicBoolean(false),2_000_000L)
            }
            encode(base,neutral)
            val settings=listOf(ColorGrade(exposureStops=1.0),ColorGrade(exposureStops= -1.0))
            // Obtain native gradeActive and all settings through real session snapshots, not fabricated flags.
            fun clips(enabled: Boolean)=EngineSession().use { session ->
                for(grade in settings) {
                    val id=session.addMediaClip(Uri.fromFile(source).toString(),1000)
                    session.setClipGrades(listOf(id),grade.copy(enabled=enabled))
                }
                session.clips()
            }
            val active=clips(true);encode(active,graded);encode(clips(false),bypassed)
            assertThrows(IllegalArgumentException::class.java) { TimelineRemuxExporter.makePlan(active) }
            val readers=mutableListOf<MediaMetadataRetriever>()
            try {
                for(file in listOf(neutral,graded,bypassed)) {
                    val reader=MediaMetadataRetriever();readers.add(reader);reader.setDataSource(file.absolutePath)
                }
                for(timeUs in listOf(250_000L,750_000L,950_000L,1_050_000L,1_250_000L,1_750_000L)) {
                    val arrays=readers.map { sample(it,timeUs) }
                    val grade=settings[if(timeUs<1_000_000L) 0 else 1]
                    val oracle=ColorFrameBuffers.render(arrays[0],32,32,grade)
                    val stats=ColorFrameBuffers.compare(oracle,arrays[1],32,32)
                    val change=ColorFrameBuffers.compare(arrays[0],arrays[1],32,32)
                    val bypass=ColorFrameBuffers.compare(arrays[0],arrays[2],32,32)
                    android.util.Log.i("OwncutColorParity","timeUs=$timeUs reference=$stats change=$change bypass=$bypass")
                    // Lossy 8-bit codec test gates, NOT shader tolerance or full color-calibration guarantees.
                    assertTrue("native reference at $timeUs: $stats",stats.meanAbsoluteRgbError<=8.0 &&
                        maxOf(stats.maximumRed,stats.maximumGreen,stats.maximumBlue)<=16)
                    assertEquals("alpha at $timeUs",0L,stats.alphaMismatches)
                    assertTrue("bypass at $timeUs: $bypass",bypass.meanAbsoluteRgbError<=3.0 &&
                        maxOf(bypass.maximumRed,bypass.maximumGreen,bypass.maximumBlue)<=8)
                    assertTrue("grade missing/wrong segment at $timeUs: $change",
                        if(timeUs<1_000_000L) change.meanSignedRgbError>12 else change.meanSignedRgbError< -12)
                }
            } finally {
                var releaseError: Exception?=null
                readers.forEach { reader -> try { reader.release() } catch(error: Exception) {
                    if(releaseError==null) releaseError=error else releaseError?.addSuppressed(error)
                } }
                releaseError?.let { throw it }
            }
        } finally { dir.deleteRecursively() }
    }
    private fun sample(reader: MediaMetadataRetriever,timeUs: Long): IntArray {
        val frame=requireNotNull(reader.getFrameAtTime(timeUs,MediaMetadataRetriever.OPTION_CLOSEST))
        try {
            check(frame.config==Bitmap.Config.ARGB_8888 && frame.colorSpace?.isSrgb==true)
            check(frame.width>=32 && frame.height>=32)
            return IntArray(32*32).also { frame.getPixels(it,0,32,(frame.width-32)/2,(frame.height-32)/2,32,32) }
        } finally { frame.recycle() }
    }
}
