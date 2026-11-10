package com.owncut.app

import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.atomic.AtomicBoolean

/** Regression tests inspect private wiring without adding a production editor-testing API. */
class EditorLifecycleTest {
    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun engine(activity: MainActivity) = field("engine").get(activity) as EngineSession

    @Test fun recreateKeepsEditedSessionAndUndoHistory() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            var before = emptyList<ClipSnapshot>()
            scenario.onActivity { activity ->
                val session = engine(activity)
                val clip = session.addMediaClip("content://test/source", 6000L)
                session.splitClip(clip, 90L, "right")
                session.moveClip("right", "v1", 120L)
                before = session.clips()
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                val session = engine(activity)
                assertEquals(before, session.clips())
                assertTrue(session.undo())
                assertEquals(90L, session.clips().single { it.id == "right" }.startFrame)
                assertTrue(session.redo())
                assertEquals(before, session.clips())
            }
        }
    }

    @Test fun editingIsBlockedDuringExportAndRotationCancelsJobButKeepsProject() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            var before = emptyList<ClipSnapshot>()
            val cancel = AtomicBoolean(false)
            scenario.onActivity { activity ->
                val session = engine(activity)
                val id = session.addMediaClip("content://test/source", 6000L)
                field("selectedClipId").set(activity, id)
                before = session.clips()
                field("projectIoBusy").setBoolean(activity, true)
                val operation = field("projectIoOperation")
                operation.set(activity, operation.type.enumConstants.single { (it as Enum<*>).name == "EXPORT" })
                field("exportCancellation").set(activity, cancel)
                val delete = MainActivity::class.java.getDeclaredMethod("deleteSelected", Boolean::class.javaPrimitiveType)
                    .apply { isAccessible = true }
                delete.invoke(activity, false)
                assertEquals(before, session.clips())
                val history = MainActivity::class.java.getDeclaredMethod("runHistory", Boolean::class.javaPrimitiveType)
                    .apply { isAccessible = true }
                history.invoke(activity, true)
                assertEquals(before, session.clips())
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertTrue(cancel.get())
                assertEquals(before, engine(activity).clips())
                assertFalse(field("projectIoBusy").getBoolean(activity))
            }
        }
    }

    @Test fun undoOfLastImportClearsSelectedMediaAndPreviewRange() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val session = engine(activity)
                val id = session.addMediaClip("content://test/source", 6000L)
                field("selectedClipId").set(activity, id)
                field("selectedUri").set(activity, android.net.Uri.parse("content://test/source"))
                field("previewStartMs").setLong(activity, 1000L)
                field("previewEndMs").setLong(activity, 6000L)
                MainActivity::class.java.getDeclaredMethod("runHistory", Boolean::class.javaPrimitiveType)
                    .apply { isAccessible = true }.invoke(activity, true)
                assertTrue(session.clips().isEmpty())
                assertNull(field("selectedUri").get(activity))
                assertEquals(0L, field("previewEndMs").getLong(activity))
            }
        }
    }

    @Test fun previewPositionAndTrimmedRangeSurviveRecreation() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val file = java.io.File(instrumentation.targetContext.cacheDir, "lifecycle-${System.nanoTime()}.mp4")
        try {
            instrumentation.context.assets.open("render-a.mp4").use { input ->
                file.outputStream().use { input.copyTo(it) }
            }
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    val session = engine(activity)
                    val id = session.addMediaClip(android.net.Uri.fromFile(file).toString(), 3000L)
                    session.trimClip(id, true, 1000L)
                    field("selectedClipId").set(activity, id)
                    field("selectedUri").set(activity, android.net.Uri.fromFile(file))
                    field("previewPositionMs").setLong(activity, 1800L)
                    field("previewStartMs").setLong(activity, 1000L)
                    field("previewEndMs").setLong(activity, 3000L)
                }
                scenario.recreate()
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    assertEquals(1800L, field("previewPositionMs").getLong(activity))
                    assertEquals(1000L, field("previewStartMs").getLong(activity))
                    assertEquals(3000L, field("previewEndMs").getLong(activity))
                    assertEquals(1, engine(activity).clips().size)
                }
            }
        } finally { file.delete() }
    }
}
