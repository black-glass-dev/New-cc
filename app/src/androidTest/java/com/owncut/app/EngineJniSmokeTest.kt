package com.owncut.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class EngineJniSmokeTest {
    @Test
    fun nativeEditingUndoRedoProjectAndAutosaveRoundTrip() {
        val engine = EngineSession()
        val projectFile = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "jni-roundtrip.owncut")
        val projectPath = projectFile.toString()
        val autosaveFile = File("$projectPath.autosave")
        try {
            val mediaIdWithJsonEscapes = "content://test.local/video/quote\"slash\\1"
            val clipId = engine.addMediaClip(mediaIdWithJsonEscapes, 10_000L)
            requireTest(engine.status().contains("clips=1"), "media insert should add one clip")
            requireTestEquals(clipId, engine.clips().single().id)
            requireTestEquals(mediaIdWithJsonEscapes, engine.clips().single().mediaId)
            requireTestEquals(300L, engine.clips().single().durationFrames)

            engine.relinkClipMedia(clipId, "content://test.local/video/relinked", 10_000L)
            requireTestEquals("content://test.local/video/relinked", engine.clips().single().mediaId)
            requireTest(engine.undo(), "media relink undo should succeed")
            requireTestEquals(mediaIdWithJsonEscapes, engine.clips().single().mediaId)
            requireTest(engine.redo(), "media relink redo should succeed")
            requireTest(engine.undo(), "media relink redo should be undoable")

            engine.moveClip(clipId, "v1", 30L)
            requireTestEquals(30L, engine.clips().single().startFrame)
            requireTest(engine.undo(), "move undo should succeed")
            requireTestEquals(0L, engine.clips().single().startFrame)
            requireTest(engine.redo(), "move redo should succeed")
            requireTestEquals(30L, engine.clips().single().startFrame)
            requireTest(engine.undo(), "move redo should be undoable")

            engine.splitClip(clipId, 150L, "right-test-clip")
            requireTest(engine.status().contains("clips=2"), "split should add a clip")
            val right = engine.clips().single { it.id == "right-test-clip" }
            requireTestEquals(150L, right.startFrame)
            requireTestEquals(5_000L, right.sourceInMs)
            requireTest(engine.undo(), "split undo should succeed")
            requireTest(engine.status().contains("clips=1"), "expected one clip")
            requireTest(engine.redo(), "split redo should succeed")
            requireTest(engine.status().contains("clips=2"), "expected two clips")

            engine.trimClip("right-test-clip", true, 6_000L)
            requireTest(engine.undo(), "trim undo should succeed")
            engine.deleteClip("right-test-clip", ripple = true)
            requireTest(engine.status().contains("clips=1"), "expected one clip")
            engine.saveProject(projectPath)
            engine.addMediaClip("content://test.local/video/2", 2_000L)
            requireTest(engine.status().contains("clips=2"), "expected two clips")
            engine.saveAutosave(projectPath)
            engine.loadProject(projectPath)
            requireTest(engine.status().contains("clips=1"), "expected one clip")
            requireTest(!engine.undo(), "loading a project should reset transient undo history")

            requireTest(engine.loadAutosave(projectPath), "autosave snapshot should be available")
            requireTest(engine.status().contains("clips=2"), "expected two clips")
            engine.clearAutosave(projectPath)
            requireTest(!engine.loadAutosave(projectPath), "cleared autosave should not be offered")
        } finally {
            engine.close()
            projectFile.delete()
            autosaveFile.delete()
        }
    }

    @Test
    fun splitGroupRelinkValidatesLongestRangeAndSurvivesReopen() {
        val engine = EngineSession()
        val file = File(InstrumentationRegistry.getInstrumentation().targetContext.cacheDir,
            "group-roundtrip-${System.nanoTime()}.owncut")
        try {
            val left = engine.addMediaClip("content://test/original", 10_000L)
            engine.splitClip(left, 150L, "right")
            val before = engine.clips()
            org.junit.Assert.assertThrows(IllegalArgumentException::class.java) {
                engine.relinkClipMedia(left, "content://test/too-short", 5_000L)
            }
            requireTestEquals(before, engine.clips())
            engine.relinkClipMedia(left, "content://test/replacement", 10_000L)
            val linked = before.map { it.copy(mediaId = "content://test/replacement") }
            requireTestEquals(linked, engine.clips())
            requireTest(engine.undo(), "group relink undo")
            requireTestEquals(before, engine.clips())
            requireTest(engine.redo(), "group relink redo")
            engine.moveClip("right", "v1", 300L)
            val moved = engine.clips()
            requireTest(engine.undo(), "move undo")
            requireTest(engine.redo(), "move redo")
            requireTestEquals(moved, engine.clips())
            engine.saveProject(file.absolutePath)
            EngineSession().use { reopened ->
                reopened.loadProject(file.absolutePath)
                requireTestEquals(moved, reopened.clips())
                requireTest(!reopened.undo(), "reopen resets transient history")
            }
            file.writeText("invalid/truncated project")
            org.junit.Assert.assertThrows(Exception::class.java) { engine.loadProject(file.absolutePath) }
            requireTestEquals(moved, engine.clips())
        } finally { engine.close(); file.delete() }
    }

    private fun requireTest(condition: Boolean, message: String) {
        if (!condition) throw AssertionError(message)
    }

    private fun requireTestEquals(expected: Any?, actual: Any?) {
        if (expected != actual) throw AssertionError("Expected <$expected>, got <$actual>")
    }
}
