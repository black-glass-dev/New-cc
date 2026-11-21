package com.owncut.app

import java.io.InputStream
import java.io.OutputStream

/** Bounded stream copy used by the SAF adapter; the native project codec itself is path-based. */
internal object ProjectDocumentIo {
    const val MAX_PROJECT_BYTES = 96L * 1024L * 1024L

    fun copy(input: InputStream, output: OutputStream, maxBytes: Long = MAX_PROJECT_BYTES) {
        require(maxBytes >= 0L) { "maxBytes must not be negative" }
        val buffer = ByteArray(64 * 1024)
        var total = 0L
        while (true) {
            var count = input.read(buffer)
            // Defensive providers can return a zero-length read without EOF. Ensure progress.
            if (count == 0) {
                val next = input.read()
                if (next < 0) break
                buffer[0] = next.toByte()
                count = 1
            }
            if (count < 0) break
            total += count
            require(total <= maxBytes) { AppLanguage.text(R.string.message_f019870b11, maxBytes / (1024 * 1024)) }
            output.write(buffer, 0, count)
        }
        output.flush()
    }
}
