package com.owncut.app

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/** 16-bit lossless FFV1 in MKV, fed with FP32 RGBA frames (native: ffv1_writer_jni.cpp).
 * Values are clamped to [0,1] and quantised to 16 bits on the native side. Values above display white are
 * NOT kept in this format; the 32-bit EXR path keeps them. Errors throw IllegalStateException.
 */
internal class Ffv1Writer private constructor(private var handle: Long, width: Int, height: Int) : AutoCloseable {
    private val frameFloats = Math.multiplyExact(Math.multiplyExact(width, height), 4)
    private val frame: FloatBuffer = ByteBuffer.allocateDirect(frameFloats * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()

    /** rgba: width*height*4 floats, row-major RGBA. Called once per output frame, in order. */
    fun write(rgba: FloatArray) {
        check(handle != 0L) { "16-bit FFV1 writer is closed" }
        require(rgba.size == frameFloats) { "frame must hold width*height*4 floats" }
        require(rgba.all { it.isFinite() }) { "frame values must be finite" }
        frame.clear()
        frame.put(rgba)
        frame.flip()
        nativeWrite(handle, frame, frameFloats)
    }

    override fun close() {
        if (handle == 0L) return
        val finishing = handle
        handle = 0L
        nativeClose(finishing)
    }

    companion object {
        init { System.loadLibrary("owncut_android") }

        fun open(output: File, width: Int, height: Int, fpsNum: Int, fpsDen: Int): Ffv1Writer {
            require(width > 0 && height > 0) { "frame size must be positive" }
            require(fpsNum > 0 && fpsDen > 0) { "frame rate must be positive" }
            val handle = nativeOpen(output.absolutePath, width, height, fpsNum, fpsDen)
            check(handle != 0L) { "16-bit FFV1 writer did not open" }
            return Ffv1Writer(handle, width, height)
        }

        @JvmStatic private external fun nativeOpen(path: String, width: Int, height: Int, fpsNum: Int, fpsDen: Int): Long
        @JvmStatic private external fun nativeWrite(handle: Long, pixels: FloatBuffer, count: Int)
        @JvmStatic private external fun nativeClose(handle: Long)
    }
}
