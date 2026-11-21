package com.owncut.app

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.view.MotionEvent
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.*
import org.junit.Test

class TimelinePlayheadTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun invoke(activity: MainActivity, name: String, types: Array<Class<*>>, vararg args: Any?) =
        MainActivity::class.java.getDeclaredMethod(name, *types).apply { isAccessible = true }.invoke(activity, *args)

    @Test fun projectionUsesTimelinePlacementNotAbsoluteSourceTime() {
        val clip = ClipSnapshot("trim", "v1", "file:///test", 150, 30, 5000, 6000)
        assertEquals(150L, TimelineGeometry.frameForSource(clip, 0))
        assertEquals(165L, TimelineGeometry.frameForSource(clip, 5500))
        assertEquals(180L, TimelineGeometry.frameForSource(clip, 7000))
        assertEquals(152L, TimelineGeometry.frameForSource(clip, 5050))
        assertEquals("01:01:01:15", TimelineGeometry.timecode(3661L * 30 + 15))
        val end = 10L * 3600 * 30
        val scale = TimelineGeometry.pixelsPerFrame(end, 3f)
        assertTrue(end * scale <= TimelineGeometry.MAX_WIDTH_PX)
        assertEquals(end / 2, TimelineGeometry.frameAt((end / 2 * scale).toFloat(), scale, end))
    }

    @Test fun nativeLookupHandlesTrimBoundaryAndGapWithoutEditing() {
        EngineSession().use { session ->
            val first = session.addMediaClip("file:///first", 3000)
            val second = session.addMediaClip("file:///second", 3000)
            session.trimClip(second, true, 1000)
            val before = session.clips()
            assertEquals(first, session.resolvePlayhead("v1", 89)!!.clipId)
            assertNull(session.resolvePlayhead("v1", 90))
            val target = session.resolvePlayhead("v1", 135)!!
            assertEquals(second, target.clipId)
            assertEquals(1500L, target.sourcePositionMs)
            assertEquals(135L, target.timelineFrame)
            assertNull(session.resolvePlayhead("v1", 180))
            assertEquals(before, session.clips())
        }
    }

    @Test fun redLineRulerTouchScrubAndAccessibleClipUseOneCoordinateSpace() {
        instrumentation.runOnMainSync {
            val view = TimelineView(context)
            val clips = listOf(
                ClipSnapshot("first", "v1", "file:///first", 0, 90, 0, 3000),
                ClipSnapshot("second", "v1", "file:///second", 120, 60, 1000, 3000),
            )
            view.setTimeline(clips, "second")
            view.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
            view.layout(0, 0, view.measuredWidth, view.measuredHeight)
            view.setPlayhead(150)
            val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
            try {
                view.draw(Canvas(bitmap))
                val color = bitmap.getPixel(view.xForFrame(150).toInt(), view.height - 2)
                assertTrue(Color.red(color) > 180 && Color.green(color) < 150 && Color.blue(color) < 150)
            } finally { bitmap.recycle() }
            var sought = -1L
            view.onSeek = { track, frame -> assertEquals("v1", track); sought = frame }
            val y = 8f * context.resources.displayMetrics.density
            val now = android.os.SystemClock.uptimeMillis()
            fun send(action: Int, x: Float, time: Long) {
                MotionEvent.obtain(now, time, action, x, y, 0).also { event ->
                    try { view.onTouchEvent(event) } finally { event.recycle() }
                }
            }
            send(MotionEvent.ACTION_DOWN, view.xForFrame(90), now)
            send(MotionEvent.ACTION_UP, view.xForFrame(90), now + 50)
            assertEquals(90L, sought)
            var committed = -1L
            view.onScrub = { frame, commit -> if (commit) committed = frame }
            send(MotionEvent.ACTION_DOWN, view.xForFrame(150), now + 100)
            send(MotionEvent.ACTION_MOVE, view.xForFrame(160), now + 150)
            send(MotionEvent.ACTION_UP, view.xForFrame(160), now + 200)
            assertEquals(160L, committed)
            val nodes = view.getAccessibilityNodeProvider()
            assertEquals(2, nodes.createAccessibilityNodeInfo(View.NO_ID)!!.childCount)
            assertTrue(nodes.performAction(2, AccessibilityNodeInfo.ACTION_CLICK, null))
            assertEquals(120L, sought)
            view.isEnabled = false
            assertFalse(nodes.performAction(2, AccessibilityNodeInfo.ACTION_CLICK, null))
        }
    }

    @Test(timeout = 60_000L)
    fun timelineSeekMatchesRealPreviewThenSplitUndoAndGapStayAligned() {
        val file = File(context.cacheDir, "playhead-fixture-${System.nanoTime()}.mp4")
        try {
            instrumentation.context.assets.open("render-a.mp4").use { input -> file.outputStream().use { input.copyTo(it) } }
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                var before = emptyList<ClipSnapshot>()
                scenario.onActivity { activity ->
                    val session = field("engine").get(activity) as EngineSession
                    session.addMediaClip(android.net.Uri.fromFile(file).toString(), 3000)
                    val second = session.addMediaClip(android.net.Uri.fromFile(file).toString(), 3000)
                    session.trimClip(second, true, 1000)
                    before = session.clips()
                    invoke(activity, "refreshTimeline", emptyArray())
                    invoke(activity, "seekTimelineAt", arrayOf(String::class.java, Long::class.javaPrimitiveType!!), "v1", 135L)
                }
                awaitReady(scenario)
                val inspected = java.util.concurrent.CountDownLatch(1)
                val actualPosition = java.util.concurrent.atomic.AtomicInteger(-1)
                scenario.onActivity { activity ->
                    val handler = field("mediaHandler").get(activity) as android.os.Handler
                    handler.post {
                        try { actualPosition.set((field("player").get(activity) as android.media.MediaPlayer).currentPosition) }
                        finally { inspected.countDown() }
                    }
                }
                assertTrue(inspected.await(5, java.util.concurrent.TimeUnit.SECONDS))
                assertTrue("non-keyframe preview seek must not land at source zero", kotlin.math.abs(actualPosition.get() - 1500) <= 100)
                scenario.onActivity { activity ->
                    assertEquals(1500L, field("previewPositionMs").getLong(activity))
                    assertEquals(135L, (field("timelineView").get(activity) as TimelineView).playheadFrame)
                    invoke(activity, "splitAtPlayhead", emptyArray())
                }
                awaitReady(scenario)
                scenario.onActivity { activity ->
                    assertEquals(135L, (field("timelineView").get(activity) as TimelineView).playheadFrame)
                    assertEquals(3, (field("engine").get(activity) as EngineSession).clips().size)
                    invoke(activity, "runHistory", arrayOf(Boolean::class.javaPrimitiveType!!), true)
                }
                awaitReady(scenario)
                scenario.onActivity { activity ->
                    val session = field("engine").get(activity) as EngineSession
                    assertEquals(before, session.clips())
                    assertEquals(135L, (field("timelineView").get(activity) as TimelineView).playheadFrame)
                    invoke(activity, "togglePlayback", emptyArray())
                }
                val deadline = android.os.SystemClock.uptimeMillis() + 5000L
                var advanced = false
                while (!advanced && android.os.SystemClock.uptimeMillis() < deadline) {
                    scenario.onActivity { advanced = field("timelineFrame").getLong(it) > 137L }
                    if (!advanced) android.os.SystemClock.sleep(50)
                }
                assertTrue("playhead must advance with actual playback", advanced)
                scenario.onActivity { activity ->
                    invoke(activity, "seekTimelineAt", arrayOf(String::class.java, Long::class.javaPrimitiveType!!), "v1", 100L)
                    assertNull(field("selectedUri").get(activity))
                    assertEquals(100L, (field("timelineView").get(activity) as TimelineView).playheadFrame)
                    assertEquals(before, (field("engine").get(activity) as EngineSession).clips())
                    // Moving a non-selected clip must also prepare that clip's source, not retain the old preview.
                    invoke(activity, "moveClipToFreeSlot", arrayOf(ClipSnapshot::class.java, String::class.java, Long::class.javaPrimitiveType!!), before[1], "v1", 180L)
                }
                awaitReady(scenario)
                scenario.onActivity { activity ->
                    assertEquals(1000L, field("previewPositionMs").getLong(activity))
                    assertEquals(180L, (field("timelineView").get(activity) as TimelineView).playheadFrame)
                    assertEquals(android.net.Uri.fromFile(file), field("selectedUri").get(activity))
                }
            }
        } finally { file.delete() }
    }

    private fun awaitReady(scenario: ActivityScenario<MainActivity>) {
        val deadline = android.os.SystemClock.uptimeMillis() + 15_000L
        var ready = false
        while (!ready && android.os.SystemClock.uptimeMillis() < deadline) {
            scenario.onActivity { activity ->
                ready = field("playerPrepared").getBoolean(activity) && !field("previewSeeking").getBoolean(activity) &&
                    !field("selectedMediaLoading").getBoolean(activity)
            }
            if (!ready) android.os.SystemClock.sleep(50)
        }
        assertTrue("preview must prepare and complete exact seek", ready)
    }
}
