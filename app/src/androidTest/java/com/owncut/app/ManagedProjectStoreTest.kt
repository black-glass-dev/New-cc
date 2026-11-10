package com.owncut.app

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.UUID

class ManagedProjectStoreTest {
    @Test fun leaseConflictAndCorruptRecoveryNeverOverwritePrimaryOrSession() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val dir = File(context.cacheDir,"store-${UUID.randomUUID()}").apply { mkdirs() }
        val file = File(dir,"project.owncut")
        try {
            EngineSession().use { it.addMediaClip("content://test/original",4000); it.saveProject(file.absolutePath) }
            EngineSession().use { session -> ManagedProjectStore.acquire(file).use { store ->
                store.load(session)
                assertThrows(IllegalStateException::class.java) { ManagedProjectStore.acquire(file) }
                val before = session.clips()
                File(file.absolutePath+".autosave").writeText("corrupt")
                assertThrows(Exception::class.java) { store.recover(session) }
                assertEquals(before,session.clips())
                java.io.RandomAccessFile(File(file.absolutePath+".autosave"),"rw").use { it.setLength(ProjectDocumentIo.MAX_PROJECT_BYTES+1) }
                assertThrows(IllegalArgumentException::class.java) { store.recover(session) }
                assertEquals(before,session.clips())
                assertFalse(dir.listFiles()!!.any { it.name.startsWith("read-") })
                store.autosave(session)
                val recovery = File(file.absolutePath+".autosave").readBytes()
                EngineSession().use { foreign -> foreign.addMediaClip("content://test/foreign",2000); foreign.saveProject(file.absolutePath) }
                val foreignBytes = file.readBytes()
                assertThrows(IllegalStateException::class.java) { store.checkpoint(session) }
                assertArrayEquals(foreignBytes,file.readBytes())
                assertArrayEquals(recovery,File(file.absolutePath+".autosave").readBytes())
            } }
            EngineSession().use { session -> ManagedProjectStore.acquire(file).use { store ->
                store.load(session); store.autosave(session); store.checkpoint(session)
                assertFalse(File(file.absolutePath+".autosave").exists())
            } }
        } finally { dir.deleteRecursively() }
    }
    @Test fun catalogRepairValidatesNativeDocumentAndPreservesDamagedMetadata() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val dir = File(context.cacheDir,"repair-${UUID.randomUUID()}").apply { mkdirs() }
        try {
            val library = ProjectLibrary(dir)
            val project = library.create("Original")
            val metadata = File(library.projectFile(project.id).parentFile,"metadata.json")
            metadata.writeText("broken")
            assertEquals(listOf(project.id),library.list().repairable)
            val bytes = library.projectFile(project.id).readBytes()
            assertEquals("Recovered",library.repair(project.id,"Recovered").name)
            assertArrayEquals(bytes,library.projectFile(project.id).readBytes())
            assertEquals("broken",metadata.parentFile!!.listFiles()!!.single { it.name.startsWith("metadata.json.preserved-") }.readText())
            metadata.writeText("broken again"); library.projectFile(project.id).writeText("invalid native document")
            assertThrows(Exception::class.java) { library.repair(project.id,"Must fail") }
            assertEquals("broken again",metadata.readText())
        } finally { dir.deleteRecursively() }
    }
}
