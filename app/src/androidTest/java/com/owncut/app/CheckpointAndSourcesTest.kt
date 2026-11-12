package com.owncut.app

import android.app.AlertDialog
import android.content.Context
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/** Added coverage for an Android runner; not a claim of phone execution. */
class CheckpointAndSourcesTest {
    @Test fun checkpointReceiptRepairAndSharedSourceCatalog() {
        val context=ApplicationProvider.getApplicationContext<Context>()
        val dir=File(context.cacheDir,"checkpoint-${UUID.randomUUID()}").apply { mkdirs() }
        val file=File(dir,"project.owncut")
        try {
            EngineSession().use { it.saveProject(file.absolutePath) }
            EngineSession().use { session -> ManagedProjectStore.acquire(file).use { store ->
                store.load(session)
                val clip=session.addMediaClip("content://test/বাংলা/🎬",4000)
                session.splitClip(clip,60,"right")
                session.addMediaClip("content://test/b",2000)
                assertEquals(2,session.mediaAssets().size)
                assertEquals(2,session.mediaAssets()[0].clipIds.size)
                assertEquals("content://test/বাংলা/🎬",session.mediaAssets()[0].mediaId)
                assertEquals("content://test/বাংলা/🎬",session.clips()[0].mediaId)
                assertEquals(4000L,session.mediaAssets()[0].requiredSourceEndMs)
                store.checkpoint(session)
                val journal=File(file.absolutePath+".checkpoint")
                assertEquals("OWNCUTCK",String(journal.readBytes().copyOfRange(0,8),Charsets.UTF_8))
                assertEquals(2,journal.readBytes()[28].toInt())
                assertTrue(session.undo())
                journal.writeText("damaged")
                assertThrows(IllegalArgumentException::class.java) { store.checkpoint(session) }
                val primary=file.readBytes()
                val recovery=File(file.absolutePath+".autosave").readBytes()
                assertTrue(store.repairCheckpoint(session))
                assertArrayEquals(primary,file.readBytes())
                assertArrayEquals(recovery,File(file.absolutePath+".autosave").readBytes())
                assertEquals("damaged",dir.listFiles()!!.single { it.name.contains(".checkpoint.preserved-") }.readText())
                store.checkpoint(session)
                assertFalse(File(file.absolutePath+".autosave").exists())
            } }
        } finally { dir.deleteRecursively() }
    }
    @Test fun newProjectIdsAreUuidsAndRoundTripWithoutChangingIdentity() {
        val context=ApplicationProvider.getApplicationContext<Context>()
        val dir=File(context.cacheDir,"identity-${UUID.randomUUID()}").apply { mkdirs() }
        fun id(file: File): String {
            val bytes=file.readBytes()
            val length=ByteBuffer.wrap(bytes,12,4).order(ByteOrder.LITTLE_ENDIAN).int
            return String(bytes,16,length,Charsets.UTF_8)
        }
        try {
            val first=File(dir,"first.owncut"); val second=File(dir,"second.owncut"); val copy=File(dir,"copy.owncut")
            EngineSession().use { it.saveProject(first.absolutePath) }
            EngineSession().use { it.saveProject(second.absolutePath); it.loadProject(first.absolutePath); it.saveCopy(copy.absolutePath) }
            assertEquals(id(first),UUID.fromString(id(first)).toString())
            assertNotEquals(id(first),id(second)); assertEquals(id(first),id(copy))
        } finally { dir.deleteRecursively() }
    }
    @Test fun sourceBrowserPagesWithoutMutatingTimelineAndClosesWithActivity() {
        var dialog: AlertDialog?=null
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val engine=MainActivity::class.java.getDeclaredField("engine").apply { isAccessible=true }.get(activity) as EngineSession
                repeat(25) { engine.addMediaClip("content://test/asset-$it",1000) }
                MainActivity::class.java.getDeclaredMethod("showMediaSources",Int::class.javaPrimitiveType).apply { isAccessible=true }.invoke(activity,0)
                dialog=MainActivity::class.java.getDeclaredField("mediaSourcesDialog").apply { isAccessible=true }.get(activity) as AlertDialog
                assertEquals(20,dialog!!.listView.adapter.count)
                assertEquals(AppLanguage.text(R.string.message_ca21daf138),dialog!!.getButton(AlertDialog.BUTTON_NEGATIVE).text.toString())
                dialog!!.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                assertEquals(25,engine.clips().size)
            }
            androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().waitForIdleSync()
            scenario.onActivity { activity ->
                dialog=MainActivity::class.java.getDeclaredField("mediaSourcesDialog").apply { isAccessible=true }.get(activity) as AlertDialog
                assertEquals(5,dialog!!.listView.adapter.count)
                assertTrue(dialog!!.listView.adapter.getItem(0).toString().contains("asset-20"))
            }
        }
        assertFalse(dialog!!.isShowing)
    }
}
