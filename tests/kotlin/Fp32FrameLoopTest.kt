import com.owncut.app.BitDepthRoute
import com.owncut.app.ExportBitDepthRoute
import com.owncut.app.ExportVideoFormat
import com.owncut.app.ExrZipSink
import com.owncut.app.Fp32FrameLoop
import com.owncut.app.AdtsAudioSpool
import com.owncut.app.AdtsHeader
import com.owncut.app.Fp32FrameSink
import com.owncut.app.Fp32Writer
import com.owncut.app.OutputBitDepth
import com.owncut.app.OutputBitDepthPolicy
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.FloatBuffer
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

    // Loop: frames pass in order, the render stage sees the RGBA8 bytes, values above 1.0 and negatives are not clamped.
    val w = 4
    val h = 2
    fun frame(value: Int): ByteBuffer = ByteBuffer.allocateDirect(w * h * 4).apply { repeat(w * h * 4) { put(value.toByte()) }; flip() }
    val written = mutableListOf<Pair<Int, Float>>()
    val rendered = mutableListOf<Int>()
    val sink = object : Fp32FrameSink {
        override fun writeFrame(index: Int, rgba: FloatBuffer) {
            check(rgba.remaining() == w * h * 4) { "frame has ${rgba.remaining()} floats" }
            written += index to rgba.get(0)
        }
        override fun writeAudio(packet: ByteBuffer, size: Int, ptsUs: Long) = throw IllegalStateException("no audio expected")
        override fun close() {}
    }
    val loop = Fp32FrameLoop(w, h,
        render = { rgba8, out ->
            rendered += rgba8.get(0).toInt()
            for (i in 0 until out.capacity()) out.put(i, 1.5f + rgba8.get(0).toInt())
        },
        sink = sink)
    for (index in 0 until 3) loop.submit(frame(index * 10))
    check(loop.finish() == 3) { "expected 3 frames" }
    check(written.map { it.first } == listOf(0, 1, 2)) { "frames out of order: $written" }
    check(rendered == listOf(0, 10, 20)) { "wrong source frames: $rendered" }
    check(written.map { it.second } == listOf(1.5f, 11.5f, 21.5f)) { "render stage not applied in FP32: $written" }

    var seenHigh = Float.NaN
    val high = Fp32FrameLoop(w, h, { _, out -> out.put(0, 2.75f); out.put(1, -0.5f); out.put(2, 0f); out.put(3, 1f) },
        object : Fp32FrameSink {
            override fun writeFrame(index: Int, rgba: FloatBuffer) { seenHigh = rgba.get(0) }
            override fun writeAudio(packet: ByteBuffer, size: Int, ptsUs: Long) {}
            override fun close() {}
        })
    high.submit(frame(0))
    check(seenHigh == 2.75f) { "FP32 value was clamped before the sink" }

    // Bad input is refused, not silently truncated; an export with no frames is an error.
    expectRejected("wrong frame size") { Fp32FrameLoop(w, h, { _, _ -> }, sink).submit(ByteBuffer.allocate(3)) }
    expectRejected("odd size") { Fp32FrameLoop(3, 2, { _, _ -> }, sink) }
    expectRejected("no frames") { Fp32FrameLoop(w, h, { _, _ -> }, sink).finish() }
    expectRejected("negative audio timestamp") { Fp32FrameLoop(w, h, { _, _ -> }, sink).writeAudio(ByteBuffer.allocate(4), 4, -1L) }

    // ADTS header: fixed layout for AAC LC 48 kHz stereo (AudioSpecificConfig 0x11 0x90), payload 100 bytes.
    val asc = byteArrayOf(0x11, 0x90.toByte())
    val header = AdtsHeader.build(asc, 100)
    check(header.size == 7) { "ADTS header must be 7 bytes" }
    check((header[0].toInt() and 0xFF) == 0xFF && (header[1].toInt() and 0xF6) == 0xF0) { "bad ADTS sync word" }
    check(((header[2].toInt() and 0xFF) ushr 6) == 1) { "AAC LC profile must be 1 in ADTS" }
    check((((header[2].toInt() and 0xFF) ushr 2) and 0xF) == 3) { "48 kHz is sampling index 3" }
    check(((((header[2].toInt() and 0x1) shl 2) or ((header[3].toInt() and 0xFF) ushr 6))) == 2) { "stereo channel field" }
    val frameLength = ((header[3].toInt() and 0x3) shl 11) or ((header[4].toInt() and 0xFF) shl 3) or ((header[5].toInt() and 0xFF) ushr 5)
    check(frameLength == 107) { "frame length must include the 7-byte header: $frameLength" }
    check(AdtsHeader.sampleRate(asc) == 48000 && AdtsHeader.channelCount(asc) == 2) { "ASC decode" }
    expectRejected("explicit sample rate (index 15) is not ADTS") { AdtsHeader.build(byteArrayOf(0x0F, 0x80.toByte()), 10) }
    expectRejected("empty payload") { AdtsHeader.build(asc, 0) }
    check(AdtsHeader.supports(asc) && !AdtsHeader.supports(byteArrayOf(0x0F, 0x80.toByte()))) { "ADTS support check" }
    check(!AdtsHeader.supports(byteArrayOf(0x29.toByte(), 0x10))) { "HE-AAC (object type 5) is not ADTS-carried here" }

    // 32-bit sink: a zip with one readable EXR per frame, then audio.aac when audio was spooled.
    val bytes = ByteArrayOutputStream()
    val spoolFile = File.createTempFile("owncut-adts-", ".aac")
    val spool = AdtsAudioSpool(asc, spoolFile)
    val zipSink = ExrZipSink(bytes, w, h, spool)
    val exrLoop = Fp32FrameLoop(w, h, { rgba8, out ->
        for (i in 0 until out.capacity()) out.put(i, (rgba8.get(i).toInt() and 0xFF).toFloat())
    }, zipSink)
    exrLoop.submit(frame(64))
    exrLoop.submit(frame(128))
    exrLoop.writeAudio(ByteBuffer.wrap(ByteArray(20) { 7 }), 20, 0L)
    check(exrLoop.finish() == 2)
    zipSink.close()
    spoolFile.delete()
    val names = mutableListOf<String>()
    var audioBytes = ByteArray(0)
    ZipInputStream(bytes.toByteArray().inputStream()).use { zip ->
        while (true) {
            val entry = zip.nextEntry ?: break
            names += entry.name
            if (entry.name == "audio.aac") { audioBytes = zip.readBytes(); continue }
            val magic = zip.readNBytes(4).map { it.toInt() and 0xff }
            check(magic == listOf(0x76, 0x2f, 0x31, 0x01)) { "bad EXR magic in ${entry.name}" }
        }
    }
    check(names == listOf("frame_000000.exr", "frame_000001.exr", "audio.aac")) { "unexpected zip entries: $names" }
    check(audioBytes.size == 7 + 20 && audioBytes[0] == 0xFF.toByte()) { "audio.aac must hold one ADTS frame of 27 bytes" }
    check(audioBytes.copyOfRange(7, 27).all { it == 7.toByte() }) { "AAC payload changed" }
    println("Fp32FrameLoop tests passed: ${names.size - 1} EXR frames and ADTS audio in zip")
}
