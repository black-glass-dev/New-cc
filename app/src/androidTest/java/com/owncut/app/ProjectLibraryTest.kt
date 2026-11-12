package com.owncut.app

import android.content.Context
import android.content.Intent
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.util.UUID

class ProjectLibraryTest {
    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun call(activity: MainActivity, name: String) = MainActivity::class.java.getDeclaredMethod(name).apply { isAccessible = true }.invoke(activity)
    private fun waitFor(condition: () -> Boolean) {
        val deadline = System.currentTimeMillis() + 15000
        while (System.currentTimeMillis() < deadline) { if (condition()) return; Thread.sleep(50) }
        fail("Timed out waiting for project operation")
    }
    @Test fun catalogIsIsolatedBoundedAndLegacyMigrationIsNonDestructive() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val dir = File(context.cacheDir, "library-test-${UUID.randomUUID()}").apply { mkdirs() }
        try {
            val library = ProjectLibrary(dir)
            val a = library.create("First")
            val b = library.create("Second")
            assertNotEquals(a.id, b.id)
            EngineSession().use {
                it.loadProject(library.projectFile(a.id).absolutePath)
                it.addMediaClip("content://test/library", 4000)
                it.saveProject(library.projectFile(a.id).absolutePath)
                it.saveAutosave(library.projectFile(a.id).absolutePath)
            }
            EngineSession().use { it.loadProject(library.projectFile(b.id).absolutePath); assertTrue(it.clips().isEmpty()) }
            assertTrue(library.read(a.id).recovery)
            assertEquals("Renamed", library.rename(a.id, " Renamed ").name)
            try { library.projectFile("../active"); fail("Path traversal accepted") } catch (_: IllegalArgumentException) { }
            try { library.create(" "); fail("Blank name accepted") } catch (_: IllegalArgumentException) { }
            val old = File(dir, "active.owncut")
            library.projectFile(a.id).copyTo(old)
            library.recoveryFile(a.id).copyTo(File(dir, "active.owncut.autosave"))
            val before = old.readBytes()
            library.migrateLegacy(); library.migrateLegacy()
            assertEquals(3, library.list().projects.size)
            assertArrayEquals(before, old.readBytes())
            assertTrue(library.list().projects.single { it.name == AppLanguage.text(R.string.message_b0ed78beaa) }.recovery)
            val catalogRoot = File(dir, "projects")
            val bad = File(catalogRoot, "bad-entry").apply { mkdirs() }
            File(bad, "metadata.json").writeText("broken")
            val malformed = File(catalogRoot, UUID.randomUUID().toString()).apply { mkdirs() }
            File(malformed, "metadata.json").writeText("broken")
            val oversized = File(catalogRoot, UUID.randomUUID().toString()).apply { mkdirs() }
            File(oversized, "metadata.json").writeBytes(ByteArray(16 * 1024 + 1))
            assertEquals(3, library.list().warnings.size)
            assertEquals(3, library.list().projects.size)
            assertTrue(bad.exists()); assertTrue(malformed.exists()); assertTrue(oversized.exists())
        } finally { dir.deleteRecursively() }
    }
    @Test fun launcherIsLibraryAndManagedEditorSavesReopensAndRetainsPage() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val launch = context.packageManager.getLaunchIntentForPackage(context.packageName)!!
        assertEquals(ProjectLibraryActivity::class.java.name, launch.component!!.className)
        ActivityScenario.launch(ProjectLibraryActivity::class.java).use { scenario ->
            scenario.onActivity { assertFalse(it.isFinishing) }
        }
        val library = ProjectLibrary(context.filesDir)
        val project = library.create("Workflow regression")
        val fixture = File(context.cacheDir, "library-media-${UUID.randomUUID()}.mp4")
        InstrumentationRegistry.getInstrumentation().context.assets.open("render-a.mp4").use { input -> fixture.outputStream().use { input.copyTo(it) } }
        val intent = Intent(context, MainActivity::class.java).putExtra(MainActivity.EXTRA_LIBRARY_ID, project.id)
        try {
            var owner: Any? = null
            ActivityScenario.launch<MainActivity>(intent).use { scenario ->
                waitFor { var ready = false; scenario.onActivity { ready = !field("projectIoBusy").getBoolean(it) }; ready }
                scenario.onActivity { activity ->
                    owner = field("managedStore").get(activity)
                    assertNotNull(owner)
                    val session = field("engine").get(activity) as EngineSession
                    session.addMediaClip(android.net.Uri.fromFile(fixture).toString(), 3000)
                    call(activity, "scheduleAutosave")
                    val preview = field("preview").get(activity)
                    MainActivity::class.java.getDeclaredMethod("showWorkspacePage", String::class.java)
                        .apply { isAccessible = true }.invoke(activity, "Deliver")
                    assertSame(preview, field("preview").get(activity))
                }
                scenario.recreate()
                scenario.onActivity { activity ->
                    assertSame(owner,field("managedStore").get(activity))
                    assertEquals("Deliver", field("workspacePage").get(activity))
                    assertEquals(1, (field("engine").get(activity) as EngineSession).clips().size)
                    call(activity, "returnToLibrary")
                }
                waitFor { scenario.state == androidx.lifecycle.Lifecycle.State.DESTROYED }
            }
            EngineSession().use { it.loadProject(library.projectFile(project.id).absolutePath); assertEquals(1, it.clips().size) }
            assertFalse(library.recoveryFile(project.id).exists())
            ActivityScenario.launch<MainActivity>(intent).use { scenario ->
                waitFor { var ready = false; scenario.onActivity { ready = !field("projectIoBusy").getBoolean(it) }; ready }
                scenario.onActivity { assertEquals(1, (field("engine").get(it) as EngineSession).clips().size) }
            }
        } finally { library.projectFile(project.id).parentFile!!.deleteRecursively(); fixture.delete() }
    }
}
