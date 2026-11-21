package com.owncut.app

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class ProjectDocumentIoTest {
    @Test
    fun copiesProjectBytesExactly() {
        val source = "OWNCUTPJ\u0001project-data".toByteArray(Charsets.UTF_8)
        val destination = ByteArrayOutputStream()

        ProjectDocumentIo.copy(ByteArrayInputStream(source), destination, maxBytes = source.size.toLong())

        assertArrayEquals(source, destination.toByteArray())
    }

    @Test
    fun rejectsInputBeyondBound() {
        val destination = ByteArrayOutputStream()
        var rejected = false
        try {
            ProjectDocumentIo.copy(ByteArrayInputStream(byteArrayOf(1, 2, 3, 4, 5)), destination, maxBytes = 4)
        } catch (_: IllegalArgumentException) {
            rejected = true
        }

        assertEquals(true, rejected)
        assertEquals(0, destination.size())
    }
    @Test
    fun zeroLengthReadsDoNotHangOrLoseBytes() {
        val stream = object : java.io.InputStream() {
            var value = 0
            override fun read(b: ByteArray, off: Int, len: Int): Int = if (value >= 4) -1 else 0
            override fun read(): Int = if (value >= 4) -1 else value++
        }
        val output = ByteArrayOutputStream()
        ProjectDocumentIo.copy(stream, output, 4)
        assertArrayEquals(byteArrayOf(0, 1, 2, 3), output.toByteArray())
    }

    @Test
    fun writeAndFlushFailuresArePropagated() {
        for (failOnFlush in listOf(false, true)) {
            val output = object : java.io.OutputStream() {
                override fun write(b: Int) { if (!failOnFlush) throw java.io.IOException("storage full") }
                override fun flush() { if (failOnFlush) throw java.io.IOException("flush failed") }
            }
            org.junit.Assert.assertThrows(java.io.IOException::class.java) {
                ProjectDocumentIo.copy(ByteArrayInputStream(byteArrayOf(1, 2, 3)), output, 3)
            }
        }
    }

}
