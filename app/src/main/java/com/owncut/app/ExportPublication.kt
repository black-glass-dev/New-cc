package com.owncut.app

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import java.io.File
import java.io.OutputStream
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean

/** Storage transaction for an already rendered/validated file; never makes partial bytes visible. */
internal object ExportPublication {
    interface PendingOutput {
        fun open(): OutputStream
        /** null means the provider cannot report a meaningful size. */
        fun size(): Long?
        fun commit()
        fun discard(): Boolean
    }

    fun publish(file: File, cancelled: AtomicBoolean, create: () -> PendingOutput) {
        checkCancellation(cancelled)
        require(file.isFile && file.length() > 0L) { AppLanguage.text(R.string.message_19c5867f0e) }
        val expectedSize = file.length()
        val pending = create()
        try {
            var copied = 0L
            pending.open().buffered().use { output ->
                file.inputStream().buffered().use { input ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        checkCancellation(cancelled)
                        val count = input.read(buffer)
                        if (count < 0) break
                        output.write(buffer, 0, count)
                        copied += count
                    }
                    output.flush()
                }
            } // close/flush errors must also abort the pending row
            check(copied == expectedSize) { AppLanguage.text(R.string.message_2b7a3b470e) }
            pending.size()?.let { check(it == expectedSize) { AppLanguage.text(R.string.message_0463a65e49) } }
            checkCancellation(cancelled)
            pending.commit()
        } catch (error: Exception) {
            val removed = try { pending.discard() } catch (cleanup: Exception) {
                error.addSuppressed(cleanup)
                false
            }
            if (!removed) {
                val message = AppLanguage.text(R.string.message_0d4c2e3e06, error.message ?: AppLanguage.text(R.string.message_29b7d7e952))
                if (error is CancellationException) throw CancellationException(message).apply { initCause(error) }
                throw IllegalStateException(message, error)
            }
            throw error
        }
    }

    private fun checkCancellation(cancelled: AtomicBoolean) {
        if (cancelled.get() || Thread.currentThread().isInterrupted)
            throw CancellationException(AppLanguage.text(R.string.message_df54d9e4da))
    }
}

/** Scoped-storage adapter: only our newly inserted pending row is eligible for cleanup. */
@android.annotation.TargetApi(Build.VERSION_CODES.Q)
internal class GalleryPendingOutput private constructor(
    private val context: Context,
    internal val uri: Uri,
) : ExportPublication.PendingOutput {
    override fun open(): OutputStream = context.contentResolver.openOutputStream(uri, "wt")
        ?: throw IllegalStateException(AppLanguage.text(R.string.message_3f5bcb7685))

    override fun size(): Long? = context.contentResolver.openFileDescriptor(uri, "r")?.use {
        it.statSize.takeIf { size -> size >= 0L }
    }

    override fun commit() {
        check(context.contentResolver.update(uri, ContentValues().apply {
            put(MediaStore.Video.Media.IS_PENDING, 0)
        }, null, null) == 1) { AppLanguage.text(R.string.message_392a697579) }
    }

    override fun discard(): Boolean = context.contentResolver.delete(uri, null, null) == 1

    companion object {
        fun create(context: Context): GalleryPendingOutput {
            val app = context.applicationContext
            val values = ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, "Owncut-edited-${System.currentTimeMillis()}.mp4")
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(MediaStore.Video.Media.RELATIVE_PATH, "${Environment.DIRECTORY_MOVIES}/Owncut")
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
            val uri = app.contentResolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)
                ?: throw IllegalStateException(AppLanguage.text(R.string.message_fbb1b3c9d7))
            return GalleryPendingOutput(app, uri)
        }
    }
}
