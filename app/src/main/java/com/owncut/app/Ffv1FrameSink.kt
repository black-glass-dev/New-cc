package com.owncut.app

/** 16-bit: FFV1 MKV through the native writer (values clamped to [0,1] by the native converter).
 * Kept apart from Fp32FrameLoop.kt because it needs the native library, which host tests do not load.
 */
internal class Ffv1FrameSink(private val writer: Ffv1Writer) : Fp32FrameSink {
    override fun writeFrame(index: Int, rgba: FloatArray) = writer.write(rgba)
    override fun close() = writer.close()
}
