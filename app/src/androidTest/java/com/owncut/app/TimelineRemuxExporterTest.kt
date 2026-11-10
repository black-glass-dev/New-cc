package com.owncut.app

import android.media.MediaFormat
import java.nio.ByteBuffer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class TimelineRemuxExporterTest {
    @Test
    fun acceptsThreeClipsFromSeveralMediaSourcesAtPlanningStage() {
        val plan = TimelineRemuxExporter.makePlan(listOf(
            clip("left", start = 0, duration = 120, sourceIn = 0, sourceOut = 4_000),
            clip("middle", start = 120, duration = 150, sourceIn = 0, sourceOut = 5_000, media = "content://media/second"),
            clip("right", start = 270, duration = 150, sourceIn = 0, sourceOut = 5_000, media = "content://media/third"),
        ))
        assertEquals(listOf("left", "middle", "right"), plan.clips.map { it.id })
        assertEquals(listOf("content://media/original", "content://media/second", "content://media/third"), plan.clips.map { it.mediaId })
    }

    @Test
    fun acceptsMatchingVideoAndAudioCodecConfigurationsAcrossSources() {
        val firstVideo = videoFormat(width = 1920, height = 1080, csd = byteArrayOf(1, 2, 3))
        val secondVideo = videoFormat(width = 1920, height = 1080, csd = byteArrayOf(1, 2, 3))
        assertTrue(TimelineRemuxExporter.formatsCompatible(firstVideo, secondVideo, "video"))
        assertFalse(
            TimelineRemuxExporter.formatsCompatible(
                firstVideo,
                videoFormat(width = 1280, height = 720, csd = byteArrayOf(1, 2, 3)),
                "video",
            ),
        )
        assertFalse(
            TimelineRemuxExporter.formatsCompatible(
                firstVideo,
                videoFormat(width = 1920, height = 1080, csd = byteArrayOf(1, 9, 3)),
                "video",
            ),
        )

        val firstAudio = MediaFormat.createAudioFormat("audio/mp4a-latm", 48_000, 2).apply {
            setInteger(MediaFormat.KEY_PROFILE, 2)
            setByteBuffer("csd-0", ByteBuffer.wrap(byteArrayOf(0x11, 0x90.toByte())))
        }
        val secondAudio = MediaFormat.createAudioFormat("audio/mp4a-latm", 48_000, 2).apply {
            setInteger(MediaFormat.KEY_PROFILE, 2)
            setByteBuffer("csd-0", ByteBuffer.wrap(byteArrayOf(0x11, 0x90.toByte())))
        }
        assertTrue(TimelineRemuxExporter.formatsCompatible(firstAudio, secondAudio, "audio"))
    }

    @Test
    fun roundsSubframeDurationDifferenceToNearestVideoFrame() {
        val plan = TimelineRemuxExporter.makePlan(listOf(
            clip("a", start = 0, duration = 180, sourceIn = 0, sourceOut = 6_007),
        ))
        assertEquals(180L, plan.clips.single().durationFrames)
        assertEquals(0L, plan.omittedGapFrames)

        assertThrows(IllegalArgumentException::class.java) {
            TimelineRemuxExporter.makePlan(listOf(
                clip("a", start = 0, duration = 180, sourceIn = 0, sourceOut = 6_020),
            ))
        }
    }

    @Test
    fun compactsLeadingAndInteriorEmptyTimelineRanges() {
        val plan = TimelineRemuxExporter.makePlan(listOf(
            clip("a", start = 16, duration = 120, sourceIn = 0, sourceOut = 4_000),
            clip("b", start = 150, duration = 120, sourceIn = 4_000, sourceOut = 8_000),
        ))
        assertEquals(30L, plan.omittedGapFrames)
        assertEquals(listOf(0L, 120L), plan.clips.map { it.startFrame })
    }

    @Test
    fun rejectsTimelineOverlap() {
        assertThrows(IllegalArgumentException::class.java) {
            TimelineRemuxExporter.makePlan(listOf(
                clip("a", start = 0, duration = 120, sourceIn = 0, sourceOut = 4_000),
                clip("b", start = 119, duration = 120, sourceIn = 4_000, sourceOut = 8_000),
            ))
        }
    }

    @Test
    fun rejectsMixedTracksAndInconsistentDuration() {
        assertThrows(IllegalArgumentException::class.java) {
            TimelineRemuxExporter.makePlan(listOf(
                clip("a", start = 0, duration = 120, sourceIn = 0, sourceOut = 4_000),
                clip("b", start = 120, duration = 120, sourceIn = 4_000, sourceOut = 8_000, track = "a1"),
            ))
        }
        assertThrows(IllegalArgumentException::class.java) {
            TimelineRemuxExporter.makePlan(listOf(
                clip("a", start = 0, duration = 120, sourceIn = 0, sourceOut = 4_100),
            ))
        }
    }

    private fun videoFormat(width: Int, height: Int, csd: ByteArray) =
        MediaFormat.createVideoFormat("video/avc", width, height).apply {
            setInteger(MediaFormat.KEY_PROFILE, 1)
            setInteger(MediaFormat.KEY_LEVEL, 1)
            setByteBuffer("csd-0", ByteBuffer.wrap(csd))
        }

    private fun clip(
        id: String,
        start: Long,
        duration: Long,
        sourceIn: Long,
        sourceOut: Long,
        media: String = "content://media/original",
        track: String = "v1",
    ) = ClipSnapshot(id, track, media, start, duration, sourceIn, sourceOut)
}
