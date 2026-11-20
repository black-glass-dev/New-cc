package com.owncut.app

import java.nio.ByteBuffer
import java.nio.FloatBuffer

/** FP32 CPU grade for one clip grade (native: grade_fp32_jni.cpp, editor::color::fp32::Rec709Grader).
 * RGBA8 Rec.709 in (direct buffer, pixels*4 bytes), RGBA32F out (direct buffer, pixels*4 floats). No clamp.
 * The grade parameters are the same 17 values as the preview's shader parameters.
 */
internal class GradeEngine private constructor(private var handle: Long) : AutoCloseable {
    fun grade(rgba8: ByteBuffer, out: FloatBuffer, pixels: Int) {
        check(handle != 0L) { "grade is closed" }
        require(rgba8.isDirect && out.isDirect) { "grade buffers must be direct" }
        require(pixels > 0 && rgba8.capacity() >= pixels * 4 && out.capacity() >= pixels * 4) {
            "grade buffers are too small for $pixels pixels"
        }
        nativeApply(handle, rgba8, out, pixels)
    }

    override fun close() {
        if (handle == 0L) return
        val closing = handle
        handle = 0L
        nativeClose(closing)
    }

    companion object {
        init { System.loadLibrary("owncut_android") }

        fun open(grade: ColorGrade): GradeEngine {
            val handle = nativeOpen(grade.parametersV1(), grade.enabled)
            check(handle != 0L) { "grade did not open" }
            return GradeEngine(handle)
        }

        @JvmStatic private external fun nativeOpen(parameters: DoubleArray, enabled: Boolean): Long
        @JvmStatic private external fun nativeApply(handle: Long, rgba8: ByteBuffer, out: FloatBuffer, pixels: Int)
        @JvmStatic private external fun nativeClose(handle: Long)
    }
}
