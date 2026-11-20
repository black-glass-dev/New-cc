package com.owncut.app

import java.nio.ByteBuffer
import java.nio.FloatBuffer

/** FP32 grade for one clip grade (native: grade_fp32_jni.cpp).
 * RGBA8 Rec.709 in (direct buffer, width*height*4 bytes), RGBA32F out (direct buffer, width*height*4 floats). No clamp.
 * Backend is chosen once in [open]: Vulkan compute when the phone has a suitable device, otherwise the CPU FP32
 * reference (editor::color::fp32::Rec709Grader). Both implement the same grade arithmetic; only speed differs.
 * [backend] names the one in use, e.g. "vulkan:<device>" or "cpu-fp32". A device error during [grade] throws.
 * The grade parameters are the same 17 values as the preview's shader parameters.
 */
internal class GradeEngine private constructor(private var handle: Long, val backend: String) : AutoCloseable {
    fun grade(rgba8: ByteBuffer, out: FloatBuffer, width: Int, height: Int) {
        check(handle != 0L) { "grade is closed" }
        require(width > 0 && height > 0) { "grade frame size must be positive" }
        require(rgba8.isDirect && out.isDirect) { "grade buffers must be direct" }
        val pixels = Math.multiplyExact(width, height)
        require(rgba8.capacity() >= pixels * 4 && out.capacity() >= pixels * 4) {
            "grade buffers are too small for ${width}x$height"
        }
        nativeApply(handle, rgba8, out, width, height)
    }

    override fun close() {
        if (handle == 0L) return
        val closing = handle
        handle = 0L
        nativeClose(closing)
    }

    companion object {
        init { System.loadLibrary("owncut_android") }

        /** allowVulkan=false forces the CPU reference (tests and comparison). */
        fun open(grade: ColorGrade, allowVulkan: Boolean = true): GradeEngine {
            val handle = nativeOpenBackend(grade.parametersV1(), grade.enabled, allowVulkan)
            check(handle != 0L) { "grade did not open" }
            return GradeEngine(handle, nativeBackend(handle))
        }

        @JvmStatic private external fun nativeOpenBackend(parameters: DoubleArray, enabled: Boolean, allowVulkan: Boolean): Long
        @JvmStatic private external fun nativeBackend(handle: Long): String
        @JvmStatic private external fun nativeApply(handle: Long, rgba8: ByteBuffer, out: FloatBuffer, width: Int, height: Int)
        @JvmStatic private external fun nativeClose(handle: Long)
    }
}
