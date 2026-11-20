package com.owncut.app

import java.io.OutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** User-selected output bit depth. 8 and 10 keep the Media3/HEVC Transformer path; 16 and 32 use the FP32 loop. */
internal enum class OutputBitDepth(val bits: Int) {
    EIGHT(8), TEN(10), SIXTEEN(16), THIRTY_TWO(32);

    companion object {
        fun fromBits(bits: Int): OutputBitDepth =
            entries.firstOrNull { it.bits == bits } ?: throw IllegalArgumentException("Unsupported bit depth: $bits")
    }
}

/** Which writer each bit depth needs. Pure policy, host-tested in tests/kotlin. */
internal enum class Fp32Writer { NONE, FFV1_MKV, EXR_SEQUENCE }

internal object OutputBitDepthPolicy {
    /** The FP32 loop is used only for 16 and 32 bit output. */
    fun writerFor(depth: OutputBitDepth): Fp32Writer = when (depth) {
        OutputBitDepth.EIGHT, OutputBitDepth.TEN -> Fp32Writer.NONE
        OutputBitDepth.SIXTEEN -> Fp32Writer.FFV1_MKV
        OutputBitDepth.THIRTY_TWO -> Fp32Writer.EXR_SEQUENCE
    }

    /** 16-bit must be an MKV file; ordinary MP4/MOV stays 8/10-bit. 32-bit is a zip of EXR frames. */
    fun requireCompatible(depth: OutputBitDepth, container: String) {
        when (writerFor(depth)) {
            Fp32Writer.NONE -> require(container != "zip") { "8/10-bit output cannot be a zip" }
            Fp32Writer.FFV1_MKV -> require(container == "mkv") { "16-bit output is an FFV1 MKV file only" }
            Fp32Writer.EXR_SEQUENCE -> require(container == "zip") { "32-bit output is a zip of float EXR frames only" }
        }
    }
}

/** Receives one FP32 RGBA frame (width*height*4 floats). Frames arrive in order. */
internal interface Fp32FrameSink : AutoCloseable {
    fun writeFrame(index: Int, rgba: FloatArray)
}

/** 32-bit: one uncompressed float EXR per frame, stored in a zip (frame_000000.exr, ...). */
internal class ExrZipSink(out: OutputStream, private val width: Int, private val height: Int) : Fp32FrameSink {
    private val zip = ZipOutputStream(out)
    override fun writeFrame(index: Int, rgba: FloatArray) {
        zip.putNextEntry(ZipEntry("frame_%06d.exr".format(index)))
        ExrFloatWriter.write(zip, width, height, rgba)
        zip.closeEntry()
    }
    override fun close() = zip.close()
}

/** One FP32 frame loop for every bit depth: RGBA8 frame → FP32 exposure → sink. Push model: the decoder calls [submit].
 * [render] fills the FP32 buffer (Vulkan or CPU reference). Frames are never clamped before the sink.
 */
internal class Fp32FrameLoop(
    private val width: Int,
    private val height: Int,
    private val stops: Float,
    private val render: (rgba8: ByteArray, stops: Float, out: FloatArray) -> Unit,
    private val sink: Fp32FrameSink,
) {
    private val out: FloatArray

    var framesWritten = 0
        private set

    init {
        require(width > 0 && height > 0 && width % 2 == 0 && height % 2 == 0) { "frame size must be positive and even" }
        require(stops.isFinite() && stops >= -8f && stops <= 8f) { "exposure must be finite within [-8, 8]" }
        out = FloatArray(Math.multiplyExact(Math.multiplyExact(width, height), 4))
    }

    fun submit(rgba8: ByteArray) {
        require(rgba8.size == Math.multiplyExact(Math.multiplyExact(width, height), 4)) {
            "frame has ${rgba8.size} bytes, expected ${width * height * 4}"
        }
        render(rgba8, stops, out)
        sink.writeFrame(framesWritten, out)
        framesWritten++
    }

    /** Returns the number of frames written. An export with no frames is an error, not an empty file. */
    fun finish(): Int {
        require(framesWritten > 0) { "no frames were decoded" }
        return framesWritten
    }
}
