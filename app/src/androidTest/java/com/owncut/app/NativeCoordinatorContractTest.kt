package com.owncut.app

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.UUID

/** Android execution remains mandatory: host native tests do not replace this bridge gate. */
class NativeCoordinatorContractTest {
    @Test fun pagedSerializationReturnsEveryClipAndPreservesIsolation() {
        EngineSession().use { first -> EngineSession().use { second ->
            val ids = (0 until 280).map { first.addMediaClip("content://test/quoted\"source",1000) }
            assertEquals(280,first.clips().size)
            assertEquals(ids.toSet(),first.clips().map { it.id }.toSet())
            assertTrue(first.clips().all { it.mediaId == "content://test/quoted\"source" })
            assertTrue(second.clips().isEmpty())
            assertTrue(first.undo()); assertEquals(279,first.clips().size)
            assertTrue(first.redo()); assertEquals(280,first.clips().size)
        } }
    }
    @Test fun copyAndDraftBridgePreserveDocumentsAndRejectCorruptReplacement() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val dir = File(context.cacheDir,"coordinator-${UUID.randomUUID()}").apply { mkdirs() }
        try {
            EngineSession().use { session ->
                val id = session.addMediaClip("content://test/original",4000)
                session.saveProject(File(dir,"primary.owncut").absolutePath)
                session.splitClip(id,60,"right")
                session.saveCopy(File(dir,"copy.owncut").absolutePath)
                session.restoreDraft(File(dir,"copy.owncut").absolutePath)
                assertEquals(2,session.clips().size)
                assertFalse(session.undo())
                val bad = File(dir,"bad.owncut").apply { writeText("invalid") }
                assertThrows(IllegalArgumentException::class.java) { session.restoreDraft(bad.absolutePath) }
                assertEquals(2,session.clips().size)
                EngineSession().use { original ->
                    original.loadProject(File(dir,"primary.owncut").absolutePath)
                    assertEquals(1,original.clips().size)
                    assertNotEquals(id,original.addMediaClip("content://test/next",1000))
                }
            }
        } finally { dir.deleteRecursively() }
    }
}
