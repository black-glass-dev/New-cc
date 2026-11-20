package com.owncut.app

import java.io.File
import java.nio.ByteBuffer
import java.nio.FloatBuffer

/** Audio stream for the MKV: AAC access units are copied from the source (no decode). [asc] is the AudioSpecificConfig. */
internal class Ffv1AudioConfig(val sampleRate: Int, val channels: Int, val asc: ByteArray) {
    init {
        require(sampleRate > 0 && channels > 0) { "audio needs a sample rate and channel count" }
        require(asc.size >= 2) { "AAC needs an AudioSpecificConfig" }
    }
}

/** 16-bit lossless FFV1 in MKV (native: ffv1_writer_jni.cpp), with an optional AAC stream copied from the source.
 * FP32 frames are passed as direct buffers and quantised to 16 bits on the native side (clamped to [0,1]).
 * Values above display white are NOT kept in this format; the 32-bit EXR path keeps them. Errors throw IllegalStateException.
 */
internal class Ffv1Writer private constructor(private var handle: Long, private val frameFloats: Int) : AutoCloseable {
    /** rgba: width*height*4 floats in a direct buffer, read from position 0. Called once per output frame, in order. */
    fun write(rgba: FloatBuffer) {
        check(handle != 0L) { "16-bit FFV1 writer is closed" }
        require(rgba.isDirect && rgba.remaining() >= frameFloats) { "frame must be a direct buffer of width*height*4 floats" }
        nativeWrite(handle, rgba, frameFloats)
    }

    /** One AAC access unit: the first [size] bytes of a direct [packet] buffer, at [ptsUs] on the output timeline. */
    fun writeAudio(packet: ByteBuffer, size: Int, ptsUs: Long) {
        check(handle != 0L) { "16-bit FFV1 writer is closed" }
        require(packet.isDirect && size > 0 && size <= packet.capacity()) { "audio packet must be a direct buffer" }
        require(ptsUs >= 0L) { "audio timestamp must not be negative" }
        nativeWriteAudio(handle, packet, size, ptsUs)
    }

    override fun close() {
        if (handle == 0L) return
        val finishing = handle
        handle = 0L
        nativeClose(finishing)
    }

    companion object {
        init { System.loadLibrary("owncut_android") }

        fun open(output: File, width: Int, height: Int, fpsNum: Int, fpsDen: Int, audio: Ffv1AudioConfig?): Ffv1Writer {
            require(width > 0 && height > 0) { "frame size must be positive" }
            require(fpsNum > 0 && fpsDen > 0) { "frame rate must be positive" }
            val frameFloats = Math.multiplyExact(Math.multiplyExact(width, height), 4)
            val handle = nativeOpen(
                output.absolutePath, width, height, fpsNum, fpsDen,
                audio?.sampleRate ?: 0, audio?.channels ?: 0, audio?.asc,
            )
            check(handle != 0L) { "16-bit FFV1 writer did not open" }
            return Ffv1Writer(handle, frameFloats)
        }

        @JvmStatic private external fun nativeOpen(
            path: String, width: Int, height: Int, fpsNum: Int, fpsDen: Int,
            sampleRate: Int, channels: Int, extradata: ByteArray?,
        ): Long
        @JvmStatic private external fun nativeWrite(handle: Long, pixels: FloatBuffer, count: Int)
        @JvmStatic private external fun nativeWriteAudio(handle: Long, packet: ByteBuffer, size: Int, ptsUs: Long)
        @JvmStatic private external fun nativeClose(handle: Long)
    }
}
