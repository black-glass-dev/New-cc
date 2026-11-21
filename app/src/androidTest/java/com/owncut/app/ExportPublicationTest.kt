package com.owncut.app

import android.os.Build
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean

class ExportPublicationTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    private class FakePending : ExportPublication.PendingOutput {
        val bytes = ByteArrayOutputStream()
        var committed = false
        var discarded = false
        var badSize = false
        var failCommit = false
        var failWrite = false
        var failClose = false
        var refuseDiscard = false
        var onWrite: () -> Unit = {}
        override fun open(): OutputStream = object : OutputStream() {
            override fun write(b: Int) { bytes.write(b) }
            override fun write(b: ByteArray, off: Int, len: Int) {
                if (failWrite) throw IOException("simulated storage full")
                bytes.write(b, off, len)
                onWrite()
            }
            override fun close() { if (failClose) throw IOException("simulated close failure") }
        }
        override fun size(): Long = if (badSize) 0L else bytes.size().toLong()
        override fun commit() {
            if (failCommit) throw IOException("simulated publish failure")
            committed = true
        }
        override fun discard(): Boolean { discarded = true; return !refuseDiscard }
    }

    @Test fun exactCopyIsPublishedOnlyAfterCloseAndSizeCheck() = withSource { file ->
        val pending = FakePending()
        ExportPublication.publish(file, AtomicBoolean(false)) { pending }
        assertArrayEquals(file.readBytes(), pending.bytes.toByteArray())
        assertTrue(pending.committed)
        assertFalse(pending.discarded)
    }

    @Test fun storageCloseSizeAndPublishFailuresAllDiscardThePendingRow() = withSource { file ->
        for (configure in listOf<(FakePending) -> Unit>(
            { it.failWrite = true }, { it.failClose = true },
            { it.badSize = true }, { it.failCommit = true },
        )) {
            val pending = FakePending().also(configure)
            assertThrows(Exception::class.java) {
                ExportPublication.publish(file, AtomicBoolean(false)) { pending }
            }
            assertFalse(pending.committed)
            assertTrue(pending.discarded)
        }
    }

    @Test fun cancellationDuringCopyDiscardsAndDoesNotCommit() = withSource { file ->
        val cancel = AtomicBoolean(false)
        val pending = FakePending().apply { onWrite = { cancel.set(true) } }
        assertThrows(CancellationException::class.java) {
            ExportPublication.publish(file, cancel) { pending }
        }
        assertTrue(pending.discarded)
        assertFalse(pending.committed)
    }

    @Test fun deletionFailureIsReportedInsteadOfClaimingCleanupSucceeded() = withSource { file ->
        val pending = FakePending().apply { failWrite = true; refuseDiscard = true }
        val error = assertThrows(IllegalStateException::class.java) {
            ExportPublication.publish(file, AtomicBoolean(false)) { pending }
        }
        assertTrue(error.message!!.contains(if (AppLanguage.locale().language == "bn") "সরানো যায়নি" else "Could not remove the incomplete Gallery item"))
    }

    @Test fun cancelledOrEmptySourceDoesNotEvenCreateARow() = withSource { file ->
        assertThrows(CancellationException::class.java) {
            ExportPublication.publish(file, AtomicBoolean(true)) { error("must not create") }
        }
        file.writeBytes(byteArrayOf())
        assertThrows(IllegalArgumentException::class.java) {
            ExportPublication.publish(file, AtomicBoolean(false)) { error("must not create") }
        }
    }

    @Test fun realGalleryRowStoresExactBytesAndCanBeRemoved() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        val file = File(context.cacheDir, "gallery-fixture-${System.nanoTime()}.mp4")
        var pending: GalleryPendingOutput? = null
        try {
            InstrumentationRegistry.getInstrumentation().context.assets.open("render-a.mp4").use { input ->
                file.outputStream().use { input.copyTo(it) }
            }
            ExportPublication.publish(file, AtomicBoolean(false)) {
                GalleryPendingOutput.create(context).also { pending = it }
            }
            val row = pending!!
            assertEquals(file.length(), row.size())
            context.contentResolver.openInputStream(row.uri)!!.use {
                assertArrayEquals(file.readBytes(), it.readBytes())
            }
        } finally {
            pending?.discard()
            file.delete()
        }
    }

    private fun withSource(test: (File) -> Unit) {
        val file = File(context.cacheDir, "publication-test-${System.nanoTime()}.mp4")
        try { file.writeBytes(ByteArray(192 * 1024) { (it % 251).toByte() }); test(file) }
        finally { file.delete() }
    }
}
