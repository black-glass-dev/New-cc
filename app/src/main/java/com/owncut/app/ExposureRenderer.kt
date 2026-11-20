package com.owncut.app

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/** FP32 Rec.709 exposure for the 16/32-bit export loop (native: fp32_render_jni.cpp).
 * Uses Vulkan compute when the phone has a suitable device, otherwise the CPU FP32 reference.
 * Both give the same float layout; only speed differs. [backend] says which one is in use.
 */
internal class ExposureRenderer private constructor(private var handle: Long, val backend: String) : AutoCloseable {

    /** rgba8: direct buffer with width*height*4 bytes (R,G,B,A). out: direct float buffer, width*height*4 floats. */
    fun render(rgba8: ByteBuffer, width: Int, height: Int, stops: Float, out: FloatBuffer) {
        check(handle != 0L) { "exposure renderer is closed" }
        require(rgba8.isDirect && out.isDirect) { "exposure buffers must be direct" }
        require(width > 0 && height > 0) { "frame size must be positive" }
        require(stops.isFinite() && stops >= -8f && stops <= 8f) { "exposure must be finite within [-8, 8]" }
        val pixels = Math.multiplyExact(width, height)
        require(rgba8.capacity() >= pixels * 4) { "input buffer is smaller than width*height*4" }
        require(out.capacity() >= pixels * 4) { "output buffer is smaller than width*height*4 floats" }
        nativeRender(handle, rgba8, width, height, stops, out)
    }

    override fun close() {
        if (handle == 0L) return
        val closing = handle
        handle = 0L
        nativeClose(closing)
    }

    companion object {
        init { System.loadLibrary("owncut_android") }

        /** allowVulkan=false forces the CPU reference (used by tests and for comparison). */
        fun open(allowVulkan: Boolean = true): ExposureRenderer {
            val handle = nativeCreate(allowVulkan)
            check(handle != 0L) { "exposure renderer did not open" }
            return ExposureRenderer(handle, nativeBackend(handle))
        }

        /** Direct float buffer sized for one frame, in native byte order. */
        fun allocateOutput(width: Int, height: Int): FloatBuffer =
            ByteBuffer.allocateDirect(Math.multiplyExact(Math.multiplyExact(width, height), 16))
                .order(ByteOrder.nativeOrder()).asFloatBuffer()

        @JvmStatic private external fun nativeCreate(allowVulkan: Boolean): Long
        @JvmStatic private external fun nativeBackend(handle: Long): String
        @JvmStatic private external fun nativeRender(handle: Long, rgba8: ByteBuffer, width: Int, height: Int, stops: Float, out: FloatBuffer)
        @JvmStatic private external fun nativeClose(handle: Long)
    }
}
