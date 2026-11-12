package com.owncut.app

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.UUID

/** Full Android/ART compilation/execution remains separate from host evidence. */
class ClipGradePersistenceTest {
    @Test fun managedCheckpointDraftAndHistoryKeepIndependentGrades() {
        val context=ApplicationProvider.getApplicationContext<Context>()
        val dir=File(context.cacheDir,"grade-${UUID.randomUUID()}").apply { mkdirs() }
        val file=File(dir,"project.owncut")
        try {
            EngineSession().use { it.saveProject(file.absolutePath) }
            EngineSession().use { session -> ManagedProjectStore.acquire(file).use { store ->
                store.load(session)
                val first=session.addMediaClip("content://shared",4000)
                val second=session.addMediaClip("content://shared",2000)
                val g=ColorGrade(exposureStops=0.12345678901234567,gamma=ColorChannels(0.8,1.2,1.5))
                session.setClipGrades(listOf(first),g)
                assertEquals(g,session.clips().first { it.id==first }.grade)
                assertFalse(session.clips().first { it.id==second }.gradeActive)
                session.splitClip(first,60,"right")
                assertEquals(g,session.clips().first { it.id=="right" }.grade)
                store.checkpoint(session)
                session.copyClipGrade(first,listOf(second))
                assertEquals(g,session.clips().first { it.id==second }.grade)
                assertTrue(session.undo()); assertFalse(session.clips().first { it.id==second }.gradeActive)
                assertTrue(session.redo())
                store.autosave(session)
                store.load(session)
                assertFalse(session.clips().first { it.id==second }.gradeActive)
                assertTrue(store.recover(session))
                assertEquals(g,session.clips().first { it.id==second }.grade)
                session.setClipGradeEnabled(first,false)
                assertFalse(session.clips().first { it.id==first }.gradeActive)
                session.setClipGradeEnabled(first,true)
                assertEquals(g,session.clips().first { it.id==first }.grade)
                session.resetClipGrades(listOf(first,second,"right"))
                assertTrue(session.clips().none { it.gradeActive })
                assertTrue(session.undo()); store.checkpoint(session)
            } }
            EngineSession().use { reopened ->
                reopened.loadProject(file.absolutePath)
                assertTrue(reopened.clips().all { it.gradeActive })
                assertThrows(IllegalArgumentException::class.java) { TimelineRemuxExporter.makePlan(reopened.clips()) }
                val plan=TimelineRemuxExporter.Plan(reopened.clips(),"v1",0)
                assertThrows(IllegalArgumentException::class.java) { TimelineRenderExporter.composition(plan) }
            }
        } finally { dir.deleteRecursively() }
    }
}
