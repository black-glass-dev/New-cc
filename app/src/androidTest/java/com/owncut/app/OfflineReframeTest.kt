package com.owncut.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Runs the NDK tracker directly; this checks bounds/contract, not device-camera semantic quality. */
class OfflineReframeTest {
    @Test fun nativeStabilizerReturnsBoundedTransformSamplesOffline() {
        val width = 96
        val height = 54
        val frameCount = 3
        val rgba = ByteArray(frameCount * width * height * 4)
        for (index in rgba.indices step 4) {
            rgba[index] = 96.toByte()
            rgba[index + 1] = 112.toByte()
            rgba[index + 2] = 128.toByte()
            rgba[index + 3] = 0xff.toByte()
        }
        val result = NativeReframe.nativeAnalyzeStabilization(
            rgba, width, height, frameCount, 0.65f, false, 1.35f,
        )
        assertEquals(frameCount * 4, result.size)
        for (index in 0 until frameCount) {
            val at = index * 4
            assertTrue(result[at].isFinite() && result[at] in -0.5f..0.5f)
            assertTrue(result[at + 1].isFinite() && result[at + 1] in -0.5f..0.5f)
            assertTrue(result[at + 2].isFinite())
            assertTrue(result[at + 3].isFinite() && result[at + 3] in 1f..1.35f)
        }
    }

    @Test fun nativeTrackerReturnsBoundedPortraitCropTrajectoryOffline() {
        val width = 160
        val height = 90
        val frameCount = 5
        val pixelsPerFrame = width * height
        val rgba = ByteArray(frameCount * pixelsPerFrame * 4)
        for (frame in 0 until frameCount) {
            val centerX = 30 + frame * 20
            for (y in 0 until height) for (x in 0 until width) {
                val index = (frame * pixelsPerFrame + y * width + x) * 4
                val isSubject = x in (centerX - 9)..(centerX + 9) && y in 34..56
                val value = if (isSubject) 240 else 24
                rgba[index] = value.toByte()
                rgba[index + 1] = value.toByte()
                rgba[index + 2] = value.toByte()
                rgba[index + 3] = 0xff.toByte()
            }
        }
        val result = NativeReframe.nativeAnalyzeRgba(rgba, width, height, frameCount, 9, 16, 0.6f)
        assertEquals(frameCount * 4, result.size)
        for (index in 0 until frameCount) {
            val at = index * 4
            assertTrue(result[at].isFinite() && result[at] in 0f..1f)
            assertTrue(result[at + 1].isFinite() && result[at + 1] in 0f..1f)
            assertEquals(9f / 16f / (width.toFloat() / height), result[at + 2], 0.02f)
            assertEquals(1f, result[at + 3], 0.02f)
        }
        assertTrue("tracked center should respond to subject motion", result[16] > result[0])
    }
}
