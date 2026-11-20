import com.owncut.app.BitDepthRoute
import com.owncut.app.ExportBitDepthRoute
import com.owncut.app.ExportVideoFormat
import com.owncut.app.ExrZipSink
import com.owncut.app.Fp32FrameLoop
import com.owncut.app.Fp32FrameSink
import com.owncut.app.Fp32Writer
import com.owncut.app.OutputBitDepth
import com.owncut.app.OutputBitDepthPolicy
import java.io.ByteArrayOutputStream
import java.util.zip.ZipInputStream

private fun expectRejected(message: String, block: () -> Unit) {
    val failed = try { block(); false } catch (_: IllegalArgumentException) { true } catch (_: IllegalStateException) { true }
    check(failed) { "expected rejection: $message" }
}

fun main() {
    // Bit-depth policy: 8/10 stay on the Transformer, 16 is FFV1 MKV, 32 is a zip of EXR frames.
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.EIGHT) == Fp32Writer.NONE)
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.TEN) == Fp32Writer.NONE)
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.SIXTEEN) == Fp32Writer.FFV1_MKV)
    check(OutputBitDepthPolicy.writerFor(OutputBitDepth.THIRTY_TWO) == Fp32Writer.EXR_SEQUENCE)
    check(OutputBitDepth.fromBits(16) == OutputBitDepth.SIXTEEN)
    expectRejected("12-bit is not offered") { OutputBitDepth.fromBits(12) }
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.SIXTEEN, "mkv")
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.THIRTY_TWO, "zip")
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.EIGHT, "mp4")
    OutputBitDepthPolicy.requireCompatible(OutputBitDepth.TEN, "mov")
    expectRejected("16-bit in MP4") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.SIXTEEN, "mp4") }
    expectRejected("16-bit in MOV") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.SIXTEEN, "mov") }
    expectRejected("32-bit in MKV") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.THIRTY_TWO, "mkv") }
    expectRejected("8-bit as zip") { OutputBitDepthPolicy.requireCompatible(OutputBitDepth.EIGHT, "zip") }

    // Format ids offered in the export list.
    check(ExportVideoFormat.container(ExportVideoFormat.FFV1_16_MKV) == "mkv")
    check(ExportVideoFormat.container(ExportVideoFormat.EXR_32_ZIP) == "zip")
    check(ExportVideoFormat.isFp32(ExportVideoFormat.FFV1_16_MKV) && ExportVideoFormat.isFp32(ExportVideoFormat.EXR_32_ZIP))
    check(!ExportVideoFormat.isFp32(ExportVideoFormat.MP4_H264) && !ExportVideoFormat.isFp32(ExportVideoFormat.MKV_HEVC10))
    check(ExportVideoFormat.bitDepth(ExportVideoFormat.MP4_H264) == OutputBitDepth.EIGHT)
    check(ExportVideoFormat.bitDepth(ExportVideoFormat.MKV_HEVC10) == OutputBitDepth.TEN)
    check(ExportVideoFormat.bitDepth(ExportVideoFormat.FFV1_16_MKV) == OutputBitDepth.SIXTEEN)
    check(ExportVideoFormat.bitDepth(ExportVideoFormat.EXR_32_ZIP) == OutputBitDepth.THIRTY_TWO)
    check(ExportVideoFormat.publishMime(ExportVideoFormat.EXR_32_ZIP) == "application/zip")

    // Export route: 8/10 bit keeps the Transformer; 16/32 bit take the FP32 loop; mismatched containers are refused.
    check(ExportBitDepthRoute.of(OutputBitDepth.EIGHT, "mp4") == BitDepthRoute.TRANSFORMER)
    check(ExportBitDepthRoute.of(OutputBitDepth.TEN, "mkv") == BitDepthRoute.TRANSFORMER)
    check(ExportBitDepthRoute.of(OutputBitDepth.SIXTEEN, "mkv") == BitDepthRoute.FP32_LOOP)
    check(ExportBitDepthRoute.of(OutputBitDepth.THIRTY_TWO, "zip") == BitDepthRoute.FP32_LOOP)
    expectRejected("16-bit route to MP4") { ExportBitDepthRoute.of(OutputBitDepth.SIXTEEN, "mp4") }
    expectRejected("32-bit route to MKV") { ExportBitDepthRoute.of(OutputBitDepth.THIRTY_TWO, "mkv") }

    // Loop: frames pass in order, exposure is applied in FP32, values above 1.0 and negatives are not clamped.
    val w = 4
    val h = 2
    val written = mutableListOf<Pair<Int, Float>>()
    val rendered = mutableListOf<Int>()
    val sink = object : Fp32FrameSink {
        override fun writeFrame(index: Int, rgba: FloatArray) {
            check(rgba.size == w * h * 4)
            written += index to rgba[0]
        }
        override fun close() {}
    }
    val loop = Fp32FrameLoop(w, h, 1.5f,
        render = { rgba8, stops, out ->
            rendered += rgba8[0].toInt()
            for (i in out.indices) out[i] = stops + rgba8[0].toInt()
        },
        sink = sink)
    for (index in 0 until 3) loop.submit(ByteArray(w * h * 4) { (index * 10).toByte() })
    check(loop.finish() == 3) { "expected 3 frames" }
    check(written.map { it.first } == listOf(0, 1, 2)) { "frames out of order: $written" }
    check(rendered == listOf(0, 10, 20)) { "wrong source frames: $rendered" }
    check(written.map { it.second } == listOf(1.5f, 11.5f, 21.5f)) { "exposure not applied in FP32: $written" }

    var seenHigh = Float.NaN
    val high = Fp32FrameLoop(w, h, 0f, { _, _, out -> out[0] = 2.75f; out[1] = -0.5f; out[2] = 0f; out[3] = 1f },
        object : Fp32FrameSink {
            override fun writeFrame(index: Int, rgba: FloatArray) { seenHigh = rgba[0] }
            override fun close() {}
        })
    high.submit(ByteArray(w * h * 4))
    check(seenHigh == 2.75f) { "FP32 value was clamped before the sink" }

    // Bad input is refused, not silently truncated; an export with no frames is an error.
    expectRejected("wrong frame size") { Fp32FrameLoop(w, h, 0f, { _, _, _ -> }, sink).submit(ByteArray(3)) }
    expectRejected("exposure out of range") { Fp32FrameLoop(w, h, 9f, { _, _, _ -> }, sink) }
    expectRejected("odd size") { Fp32FrameLoop(3, 2, 0f, { _, _, _ -> }, sink) }
    expectRejected("no frames") { Fp32FrameLoop(w, h, 0f, { _, _, _ -> }, sink).finish() }

    // 32-bit sink: a zip with one readable EXR per frame.
    val bytes = ByteArrayOutputStream()
    val zipSink = ExrZipSink(bytes, w, h)
    val exrLoop = Fp32FrameLoop(w, h, 0f, { rgba8, stops, out ->
        for (i in out.indices) out[i] = rgba8[i].toInt() / 255f * (1f + stops)
    }, zipSink)
    exrLoop.submit(ByteArray(w * h * 4) { 64 })
    exrLoop.submit(ByteArray(w * h * 4) { 128.toByte() })
    check(exrLoop.finish() == 2)
    zipSink.close()
    val names = mutableListOf<String>()
    ZipInputStream(bytes.toByteArray().inputStream()).use { zip ->
        while (true) {
            val entry = zip.nextEntry ?: break
            names += entry.name
            val magic = zip.readNBytes(4).map { it.toInt() and 0xff }
            check(magic == listOf(0x76, 0x2f, 0x31, 0x01)) { "bad EXR magic in ${entry.name}" }
        }
    }
    check(names == listOf("frame_000000.exr", "frame_000001.exr")) { "unexpected zip entries: $names" }
    println("Fp32FrameLoop tests passed: ${names.size} EXR frames in zip")
}
