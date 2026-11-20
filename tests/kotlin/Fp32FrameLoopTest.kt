import com.owncut.app.BitDepthRoute
import com.owncut.app.ExportBitDepthRoute
import com.owncut.app.ExrSequenceSink
import com.owncut.app.Fp32FrameLoop
import com.owncut.app.Fp32FrameSink
import com.owncut.app.Fp32Writer
import com.owncut.app.OutputBitDepth
import com.owncut.app.OutputBitDepthPolicy
import java.io.File
import java.util.concurrent.CancellationException

private fun expectRejected(message: String, block: () -> Unit) {
    val failed = try { block(); false } catch (_: IllegalArgumentException) { true } catch (_: IllegalStateException) { true }
    check(failed) { "expected rejection: $message" }
}

fun main(args: Array<String>) {
    // Bit-depth policy: 8/10 stay on the Transformer, 16 is FFV1 MKV, 32 is an EXR sequence.
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.EIGHT) == Fp32Writer.NONE)
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.TEN) == Fp32Writer.NONE)
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.SIXTEEN) == Fp32Writer.FFV1_MKV)
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.THIRTY_TWO) == Fp32Writer.EXR_SEQUENCE)
    check(OutputBitDepth.fromBits(16) == OutputBitDepth.SIXTEEN)
    expectRejected("12-bit is not offered") { OutputBitDepth.fromBits(12) }
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.SIXTEEN, "mkv")
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.THIRTY_TWO, "exr")
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.EIGHT, "mp4")
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.TEN, "mov")
    expectRejected("16-bit in MP4") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.SIXTEEN, "mp4") }
    expectRejected("16-bit in MOV") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.SIXTEEN, "mov") }
    expectRejected("32-bit in MKV") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.THIRTY_TWO, "mkv") }
    expectRejected("8-bit as EXR") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.EIGHT, "exr") }

    // Export route: 8/10 bit keeps the Transformer; 16/32 bit take the FP32 loop; mismatched containers are refused.
    check(ExportBitDepthRoute.of(OutputBitDepth.EIGHT, "mp4") == BitDepthRoute.TRANSFORMER)
    check(ExportBitDepthRoute.of(OutputBitDepth.TEN, "mkv") == BitDepthRoute.TRANSFORMER)
    check(ExportBitDepthRoute.of(OutputBitDepth.SIXTEEN, "mkv") == BitDepthRoute.FP32_LOOP)
    check(ExportBitDepthRoute.of(OutputBitDepth.THIRTY_TWO, "exr") == BitDepthRoute.FP32_LOOP)
    expectRejected("16-bit route to MP4") { ExportBitDepthRoute.of(OutputBitDepth.SIXTEEN, "mp4") }
    expectRejected("32-bit route to MKV") { ExportBitDepthRoute.of(OutputBitDepth.THIRTY_TWO, "mkv") }

    // Frame loop: every frame is read, rendered once, and written in order, with the exposure passed through.
    val w = 4
    val h = 2
    val frames = 3
    val written = mutableListOf<Pair<Int, Float>>()
    val rendered = mutableListOf<Int>()
    val sink = object : Fp32FrameSink {
        override fun writeFrame(index: Int, rgba: FloatArray) {
            check(rgba.size == w * h * 4)
            written += index to rgba[0]
        }
        override fun close() {}
    }
    val progress = mutableListOf<Int>()
    val loop = Fp32FrameLoop(w, h, frames,
        source = { index -> ByteArray(w * h * 4) { (index * 10).toByte() } },
        render = { rgba8, stops, out ->
            rendered += rgba8[0].toInt()
            for (i in out.indices) out[i] = stops + rgba8[0].toInt()
        })
    val count = loop.run(1.5f, sink, progress = { progress += it })
    check(count == frames) { "expected $frames frames, got $count" }
    check(written.map { it.first } == listOf(0, 1, 2)) { "frames out of order: $written" }
    check(rendered == listOf(0, 10, 20)) { "wrong source frames: $rendered" }
    check(written.map { it.second } == listOf(1.5f, 11.5f, 21.5f)) { "exposure not applied in FP32: $written" }
    check(progress == listOf(1, 2, 3))

    // Values above 1.0 and negatives must survive the FP32 path (no clamp before the sink).
    var seenHigh = Float.NaN
    Fp32FrameLoop(w, h, 1, { ByteArray(w * h * 4) }, { _, _, out -> out[0] = 2.75f; out[1] = -0.5f; out[2] = 0f; out[3] = 1f })
        .run(0f, object : Fp32FrameSink {
            override fun writeFrame(index: Int, rgba: FloatArray) { seenHigh = rgba[0] }
            override fun close() {}
        })
    check(seenHigh == 2.75f) { "FP32 value was clamped before the sink" }

    // Cancellation stops before the next frame is read.
    var reads = 0
    var cancelledRun = false
    try {
        Fp32FrameLoop(w, h, 5, { reads++; ByteArray(w * h * 4) }, { _, _, _ -> })
            .run(0f, sink, cancelled = { reads >= 2 })
    } catch (_: CancellationException) { cancelledRun = true }
    check(cancelledRun && reads == 2) { "cancel did not stop the loop: reads=$reads" }

    // Bad input is refused, not silently truncated.
    expectRejected("wrong frame size") {
        Fp32FrameLoop(w, h, 1, { ByteArray(3) }, { _, _, _ -> }).run(0f, sink)
    }
    expectRejected("exposure out of range") { Fp32FrameLoop(w, h, 1, { ByteArray(w * h * 4) }, { _, _, _ -> }).run(9f, sink) }
    expectRejected("odd size") { Fp32FrameLoop(3, 2, 1, { ByteArray(0) }, { _, _, _ -> }) }
    expectRejected("zero frames") { Fp32FrameLoop(w, h, 0, { ByteArray(0) }, { _, _, _ -> }) }

    // 32-bit sink: one readable EXR per frame.
    val dir = File(args[0]).apply { deleteRecursively(); mkdirs() }
    ExrSequenceSink(dir, w, h).use { exr ->
        Fp32FrameLoop(w, h, 2, { ByteArray(w * h * 4) { 64 } }, { rgba8, stops, out ->
            for (i in out.indices) out[i] = rgba8[i].toInt() / 255f * (1f + stops)
        }).run(0f, exr)
    }
    val files = dir.listFiles()!!.map { it.name }.sorted()
    check(files == listOf("frame_000000.exr", "frame_000001.exr")) { "unexpected EXR files: $files" }
    check(files.all { File(dir, it).readBytes().take(4) == listOf(0x76, 0x2f, 0x31, 0x01).map { b -> b.toByte() } }) { "bad EXR magic" }
    println("Fp32FrameLoop tests passed: ${files.size} EXR frames")
}
