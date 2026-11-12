package com.owncut.app

import android.media.MediaFormat
import androidx.media3.common.C
import androidx.media3.common.ColorInfo
import androidx.media3.common.util.UnstableApi
import org.junit.Assert.*
import org.junit.Test

/** Added source only; Android compilation/execution pending. No codec/device claim. */
@UnstableApi
class ColorSourcePlanTest {
    @Test fun platformTagsNeverDefaultUnknownToSdr() {
        val unknown=VideoSourceProbe.color(null,null,null)
        assertEquals(ColorEngine.VideoPrimaries.UNKNOWN,unknown.primaries)
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.validateVideoSource(unknown) }
        for(unset in listOf(0,-1)) {
            val untagged=VideoSourceProbe.color(unset,unset,unset)
            assertEquals(unknown,untagged)
            assertThrows(IllegalArgumentException::class.java) { ColorEngine.validateVideoSource(untagged) }
        }
        val good=VideoSourceProbe.color(MediaFormat.COLOR_STANDARD_BT709,
            MediaFormat.COLOR_TRANSFER_SDR_VIDEO,MediaFormat.COLOR_RANGE_LIMITED)
        ColorEngine.validateVideoSource(good)
        for (transfer in listOf(null,MediaFormat.COLOR_TRANSFER_HLG,MediaFormat.COLOR_TRANSFER_ST2084,999)) {
            val metadata=VideoSourceProbe.color(MediaFormat.COLOR_STANDARD_BT709,transfer,MediaFormat.COLOR_RANGE_FULL)
            assertThrows(IllegalArgumentException::class.java) { ColorEngine.validateVideoSource(metadata) }
        }
        val conflict=ColorInfo.Builder().setColorSpace(C.COLOR_SPACE_BT709).setColorTransfer(C.COLOR_TRANSFER_SDR)
            .setColorRange(C.COLOR_RANGE_FULL).setHdrStaticInfo(byteArrayOf(1)).build()
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.validateVideoSource(VideoSourceProbe.color(conflict)) }
    }

    @Test fun candidateBindsPerClipLooksWithoutOpeningShippingGuard() {
        val clips=EngineSession().use { session ->
            val first=session.addMediaClip("content://synthetic-source",1000)
            session.addMediaClip("content://synthetic-source",1000)
            session.setClipGrades(listOf(first),ColorGrade(exposureStops=1.0))
            session.clips()
        }
        assertThrows(IllegalArgumentException::class.java) { TimelineRemuxExporter.makePlan(clips) }
        val timing=TimelineRemuxExporter.candidateGradedTimingPlan(clips)
        val metadata=VideoSourceMetadata(clips.first().mediaId,0,1920,1080,0,
            VideoSourceProbe.color(MediaFormat.COLOR_STANDARD_BT709,MediaFormat.COLOR_TRANSFER_SDR_VIDEO,MediaFormat.COLOR_RANGE_LIMITED))
        var reads=0
        val candidate=TimelineRenderExporter.candidateComposition(timing,"snapshot-test",{}) { reads++;metadata }
        assertEquals(1,reads)
        assertEquals(clips.map { it.grade },candidate.colors.clips.map { it.clip.grade })
        val items=candidate.composition.sequences.single().editedMediaItems
        assertEquals(2,items.size)
        assertTrue(items.all { it.effects.videoEffects.size==1 })
        assertEquals(1,candidate.composition.effects.videoEffects.size) // presentation, not another grade
        assertEquals(1000L,items.first().mediaItem.clippingConfiguration.endPositionMs)
        assertThrows(IllegalArgumentException::class.java) { TimelineRenderExporter.composition(timing) }
    }
}
