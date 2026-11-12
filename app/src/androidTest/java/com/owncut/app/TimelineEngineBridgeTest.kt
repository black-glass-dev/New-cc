package com.owncut.app

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.owncut.timeline.TimelineBridge
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import kotlin.math.sin

@RunWith(AndroidJUnit4::class)
class TimelineEngineBridgeTest {
    @Test
    fun sessionSyncsModularTimelineEngineAndEvaluatesMagneticSnapAndPinchZoom() {
        EngineSession().use { session ->
            val clip1 = session.addMediaClip("content://test/clip1.mp4", 3_000L) // 0..90
            val clip2 = session.addMediaClip("content://test/clip2.mp4", 2_000L) // 90..150
            session.moveClip(clip2, "v1", 120L)

            val snap = session.evaluateTimelineSnap(
                trackId = "v1",
                clipId = clip2,
                proposedFrame = 92L,
                durationFrames = 60L,
                playheadFrame = 0L,
                pixelsPerFrame = 1.6,
                snapEnabled = true
            )
            assertTrue(snap.snapped)
            assertEquals(90L, snap.finalFrame)
            assertEquals(90L, snap.snapGuideFrame)
            assertTrue(snap.hapticPulse)

            val initialZoom = session.timelineZoomScale()
            assertEquals(1.0, initialZoom, 1e-4)
            val zoomed = session.pinchTimelineZoom(2.0, 120.0)
            assertTrue(zoomed > 1.9)
            session.resetTimelineZoom()
            assertEquals(1.0, session.timelineZoomScale(), 1e-4)

            val caps = session.timelineCapabilitiesJson()
            assertTrue(caps.contains("owncut_timeline_engine"))
            assertTrue(caps.contains("\"clips\":2"))
            assertTrue(caps.contains("magnetic_snap"))
            assertNotNull(clip1)
        }
    }

    @Test
    fun directTimelineBridgeExecutesSevenModeEditsSplitAndUndoRedo() {
        assertTrue(TimelineBridge.nativeLoaded)
        val handle = TimelineBridge.nativeCreate(30, 1, 1920, 1080)
        assertTrue(handle != 0L)
        try {
            val mediaId = TimelineBridge.nativeAddSyntheticMedia(handle, "Sample", 1920, 1080, 180L)
            assertTrue(mediaId > 0L)
            assertTrue(TimelineBridge.nativePerformEdit(handle, 1, mediaId, 0L, 120L)) // AppendAtEnd
            assertEquals(120L, TimelineBridge.nativeGetDurationFrames(handle))
            assertTrue(TimelineBridge.nativeBladeSplit(handle, 60L))
            assertTrue(TimelineBridge.nativeUndo(handle))
            assertTrue(TimelineBridge.nativeRedo(handle))

            // Rich state export / import & SRT roundtrip
            assertTrue(TimelineBridge.nativeImportSrt(handle, "1\n00:00:00,000 --> 00:00:01,500\nবাংলা টেস্ট Hello\n"))
            val srt = TimelineBridge.nativeExportSrt(handle) ?: ""
            assertTrue(srt.contains("বাংলা টেস্ট Hello"))
            TimelineBridge.nativeSetBeatGrid(handle, 124.0, 100L)
            TimelineBridge.nativeUpsertMarker(handle, "m1", 500L, "Beat1", "#4F9CF9")
            val json = TimelineBridge.nativeExportStateJson(handle) ?: ""
            assertTrue(json.contains("owncut_tl_state_v1"))
            assertTrue(json.contains("124"))
            assertTrue(TimelineBridge.nativeImportStateJson(handle, json))
        } finally {
            TimelineBridge.nativeDestroy(handle)
        }
    }

    @Test
    fun multiTrackUnifiedUndoAndSidecarPersistencePreservePrimaryProject() {
        val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
        val dir = File(ctx.cacheDir, "tlstate-test-${System.nanoTime()}").apply { mkdirs() }
        val projFile = File(dir, "demo.owncut")
        try {
            EngineSession().use { s ->
                val v1 = s.addMediaClip("content://test/main.mp4", 4_000L)
                val v2 = s.addMediaClipToTrack("v2", "content://test/pip.mp4", 2_000L, 15L)
                val a1 = s.addMediaClipToTrack("a1", "content://test/bgm.m4a", 5_000L, 0L)
                assertEquals(3, s.clips().size)
                assertTrue(s.tracks().map { it.id }.containsAll(listOf("v1", "v2", "a1")))

                s.setClipFx(v1) { it.copy(speed = 1.5, transitionIn = "dissolve", transitionMs = 500L) }
                s.upsertOverlay(TextOverlayItem("ov1", "বাংলা শিরোনাম", 100L, 2000L))
                s.upsertSubtitle(SubtitleCueItem("sub1", 0L, 1200L, "Caption One"))
                s.setBeatGrid(120.0, 0L)
                assertEquals(1.5, s.clipFx(v1).speed, 1e-4)

                // Coordinated undo/redo across rich & native layers
                assertTrue(s.undo())
                assertEquals(0.0, s.richState().beatBpm, 1e-4)
                assertTrue(s.redo())
                assertEquals(120.0, s.richState().beatBpm, 1e-4)

                val dup = s.duplicateClip(v2)
                assertEquals(4, s.clips().size)
                assertNotEquals(v2, dup)

                val pcm = ShortArray(48000) { i -> (sin(i * 0.05) * 12000.0).toInt().toShort() }
                val analysis = s.analyzeAudioPcm16(pcm, 1, 48000, 16)
                assertNotNull(analysis)
                assertEquals(16, analysis!!.peaks.size)

                s.saveProject(projFile.absolutePath)
                assertTrue(TimelineRichDocument.sidecarFor(projFile.absolutePath).exists())
                assertTrue(a1.isNotEmpty())
            }

            EngineSession().use { reloaded ->
                reloaded.loadProject(projFile.absolutePath)
                assertFalse(reloaded.undo())
                assertEquals(4, reloaded.clips().size)
                assertEquals(120.0, reloaded.richState().beatBpm, 1e-4)
                assertEquals("বাংলা শিরোনাম", reloaded.richState().overlays.single().text)
                assertEquals("Caption One", reloaded.richState().subtitles.single().text)
                val v1Clip = reloaded.clips().first { it.trackId == "v1" }
                assertEquals(1.5, reloaded.clipFx(v1Clip.id).speed, 1e-4)
            }
        } finally {
            dir.deleteRecursively()
        }
    }
}
