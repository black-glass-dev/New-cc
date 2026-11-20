package com.owncut.app

import java.io.File

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

    /** 16-bit must be an MKV file; ordinary MP4/MOV stays 8/10-bit. 32-bit is a folder of EXR frames. */
    fun requireCompatible(depth: OutputBitDepth, containerOrDirectory: String) {
        when (writerFor(depth)) {
            Fp32Writer.NONE -> require(containerOrDirectory != "exr") { "8/10-bit output cannot be EXR" }
            Fp32Writer.FFV1_MKV -> require(containerOrDirectory == "mkv") {
                "16-bit output is an FFV1 MKV file only"
            }
            Fp32Writer.EXR_SEQUENCE -> require(containerOrDirectory == "exr") {
                "32-bit output is a float EXR sequence only"
            }
        }
    }
}

/** Receives one FP32 RGBA frame (width*height*4 floats). */
internal interface Fp32FrameSink : AutoCloseable {
    fun writeFrame(index: Int, rgba: FloatArray)
}

/** 32-bit: one uncompressed float EXR per frame in [directory], named frame_000000.exr. */
internal class ExrSequenceSink(private val directory: File, private val width: Int, private val height: Int) : Fp32FrameSink {
    init { directory.mkdirs() }
    override fun writeFrame(index: Int, rgba: FloatArray) {
        val file = File(directory, "frame_%06d.exr".format(index))
        file.outputStream().use { ExrFloatWriter.write(it, width, height, rgba) }
    }
    override fun close() {}
}

/** One FP32 frame loop for every bit depth: decode RGBA8 → exposure in FP32 → sink.
 * [source] returns width*height*4 bytes for a frame index. [render] fills the FP32 buffer (Vulkan or CPU reference).
 */
internal class Fp32FrameLoop(
    private val width: Int,
    private val height: Int,
    private val frameCount: Int,
    private val source: (Int) -> ByteArray,
    private val render: (rgba8: ByteArray, stops: Float, out: FloatArray) -> Unit,
) {
    init {
        require(width > 0 && height > 0 && width % 2 == 0 && height % 2 == 0) { "frame size must be positive and even" }
        require(frameCount > 0) { "no frames to export" }
    }

    /** Runs the loop. [cancelled] is checked before every frame; [progress] receives the frame count written so far. */
    fun run(stops: Float, sink: Fp32FrameSink, cancelled: () -> Boolean = { false }, progress: (Int) -> Unit = {}): Int {
        require(stops.isFinite() && stops >= -8f && stops <= 8f) { "exposure must be finite within [-8, 8]" }
        val pixelCount = Math.multiplyExact(width, height)
        val out = FloatArray(Math.multiplyExact(pixelCount, 4))
        var written = 0
        for (index in 0 until frameCount) {
            if (cancelled()) throw java.util.concurrent.CancellationException("Export cancelled")
            val frame = source(index)
            require(frame.size == pixelCount * 4) { "frame $index has ${frame.size} bytes, expected ${pixelCount * 4}" }
            render(frame, stops, out)
            sink.writeFrame(index, out)
            written++
            progress(written)
        }
        return written
    }
}
