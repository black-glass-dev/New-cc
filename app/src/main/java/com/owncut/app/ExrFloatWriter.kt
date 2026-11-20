package com.owncut.app

import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Uncompressed OpenEXR scanline writer, 32-bit float RGBA, single part, INCREASING_Y.
 * Used for the 32-bit output (image sequence, one .exr per frame). No compression, so no zlib dependency.
 * Channels are written in alphabetical order (A, B, G, R) as the OpenEXR spec requires.
 * Input is interleaved RGBA float values, row-major, length width*height*4. Values are written unclamped.
 */
internal object ExrFloatWriter {
    private const val MAGIC = 20000630
    private const val VERSION = 2
    private const val PIXEL_TYPE_FLOAT = 2
    private const val COMPRESSION_NONE: Byte = 0
    private const val LINE_ORDER_INCREASING_Y: Byte = 0

    fun write(out: OutputStream, width: Int, height: Int, rgba: FloatArray) {
        require(width > 0 && height > 0) { "EXR size must be positive" }
        require(rgba.size.toLong() == width.toLong() * height * 4) { "EXR input must be width*height*4 floats" }
        require(rgba.all { it.isFinite() }) { "EXR input must be finite" }

        val header = buildHeader(width, height)
        val lineBytes = 4 * 4 * width // A,B,G,R floats per scanline
        val blockBytes = 8 + lineBytes
        val firstBlock = header.size.toLong() + 8L * height
        val offsets = ByteBuffer.allocate(8 * height).order(ByteOrder.LITTLE_ENDIAN)
        for (y in 0 until height) offsets.putLong(firstBlock + y.toLong() * blockBytes)

        out.write(header)
        out.write(offsets.array())
        val block = ByteBuffer.allocate(blockBytes).order(ByteOrder.LITTLE_ENDIAN)
        // EXR channel order is alphabetical: A (index 3), B (2), G (1), R (0).
        val channelIndex = intArrayOf(3, 2, 1, 0)
        for (y in 0 until height) {
            block.clear()
            block.putInt(y)
            block.putInt(lineBytes)
            for (c in channelIndex) {
                for (x in 0 until width) block.putFloat(rgba[(y * width + x) * 4 + c])
            }
            out.write(block.array(), 0, blockBytes)
        }
        out.flush()
    }

    private fun buildHeader(width: Int, height: Int): ByteArray {
        val bytes = ByteArrayOutputStream()
        bytes.write(le32(MAGIC))
        bytes.write(le32(VERSION))

        attribute(bytes, "channels", "chlist") { v ->
            for (name in listOf("A", "B", "G", "R")) {
                cstring(v, name)
                v.write(le32(PIXEL_TYPE_FLOAT))
                v.write(0)            // pLinear
                v.write(byteArrayOf(0, 0, 0)) // reserved
                v.write(le32(1))      // xSampling
                v.write(le32(1))      // ySampling
            }
            v.write(0)                // end of channel list
        }
        attribute(bytes, "compression", "compression") { v -> v.write(COMPRESSION_NONE.toInt()) }
        val box = ByteArrayOutputStream().apply {
            write(le32(0)); write(le32(0)); write(le32(width - 1)); write(le32(height - 1))
        }.toByteArray()
        attribute(bytes, "dataWindow", "box2i") { v -> v.write(box) }
        attribute(bytes, "displayWindow", "box2i") { v -> v.write(box) }
        attribute(bytes, "lineOrder", "lineOrder") { v -> v.write(LINE_ORDER_INCREASING_Y.toInt()) }
        attribute(bytes, "pixelAspectRatio", "float") { v -> v.write(le32(java.lang.Float.floatToIntBits(1f))) }
        attribute(bytes, "screenWindowCenter", "v2f") { v ->
            v.write(le32(0)); v.write(le32(0))
        }
        attribute(bytes, "screenWindowWidth", "float") { v -> v.write(le32(java.lang.Float.floatToIntBits(1f))) }

        bytes.write(0) // end of header
        return bytes.toByteArray()
    }

    private inline fun attribute(out: ByteArrayOutputStream, name: String, type: String, body: (ByteArrayOutputStream) -> Unit) {
        cstring(out, name)
        cstring(out, type)
        val value = ByteArrayOutputStream().also(body).toByteArray()
        out.write(le32(value.size))
        out.write(value)
    }

    private fun cstring(out: ByteArrayOutputStream, text: String) {
        out.write(text.toByteArray(Charsets.US_ASCII))
        out.write(0)
    }

    private fun le32(value: Int): ByteArray =
        ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(value).array()
}
