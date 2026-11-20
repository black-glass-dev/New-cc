package com.owncut.app

import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
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

/** Receives FP32 output. Frames and audio arrive in timeline order.
 * [writeFrame]: one RGBA frame, width*height*4 floats, read from position 0 to the limit (a direct buffer owned by the loop).
 * [writeAudio]: one compressed AAC access unit (first [size] bytes from position 0) at [ptsUs] on the output timeline.
 */
internal interface Fp32FrameSink : AutoCloseable {
    fun writeFrame(index: Int, rgba: FloatBuffer)
    fun writeAudio(packet: ByteBuffer, size: Int, ptsUs: Long)
}

/** Keeps AAC access units as an ADTS stream on disk (one file, appended in order). Pure Kotlin, host-tested. */
internal class AdtsAudioSpool(private val asc: ByteArray, val file: File) : AutoCloseable {
    private val out = FileOutputStream(file)
    var packets = 0
        private set

    fun write(packet: ByteBuffer, size: Int) {
        require(size > 0) { "empty AAC packet" }
        val payload = ByteArray(size)
        val view = packet.duplicate()
        view.clear() // Buffer.clear() returns Buffer on Android API levels, so the call is not chained
        view.get(payload)
        out.write(AdtsHeader.build(asc, size))
        out.write(payload)
        packets++
    }

    override fun close() = out.close()
}

/** 32-bit: one uncompressed float EXR per frame in a zip (frame_000000.exr, ...). Audio, when present, is added last as audio.aac (ADTS). */
internal class ExrZipSink(
    out: OutputStream,
    private val width: Int,
    private val height: Int,
    private val audio: AdtsAudioSpool?,
) : Fp32FrameSink {
    private val zip = ZipOutputStream(out)
    private val values = FloatArray(width * height * 4)
    private var frames = 0
    private var closed = false

    override fun writeFrame(index: Int, rgba: FloatBuffer) {
        val view = rgba.duplicate()
        view.clear() // see AdtsAudioSpool.write: not chained, for Android API compatibility
        view.get(values)
        zip.putNextEntry(ZipEntry("frame_%06d.exr".format(index)))
        ExrFloatWriter.write(zip, width, height, values)
        zip.closeEntry()
        frames++
    }

    override fun writeAudio(packet: ByteBuffer, size: Int, ptsUs: Long) {
        checkNotNull(audio) { "no audio spool for this export" }
        audio.write(packet, size)
    }

    /** Number of frame entries written so far. */
    val frameEntries: Int get() = frames

    override fun close() {
        if (closed) return
        closed = true
        try {
            audio?.let { spool ->
                spool.close()
                if (spool.packets > 0) {
                    zip.putNextEntry(ZipEntry("audio.aac"))
                    spool.file.inputStream().use { it.copyTo(zip) }
                    zip.closeEntry()
                }
            }
        } finally {
            zip.close()
        }
    }
}

/** One FP32 frame loop for every bit depth: RGBA8 frame → FP32 grade → sink. Push model: the decoder calls [submit].
 * [render] fills the FP32 buffer from the RGBA8 frame (native grade, CPU reference). Frames are never clamped before the sink.
 */
internal class Fp32FrameLoop(
    private val width: Int,
    private val height: Int,
    private val render: (rgba8: ByteBuffer, out: FloatBuffer) -> Unit,
    private val sink: Fp32FrameSink,
) {
    private val frameBytes: Int = Math.multiplyExact(Math.multiplyExact(width, height), 4)
    private val out: FloatBuffer

    var framesWritten = 0
        private set

    init {
        require(width > 0 && height > 0 && width % 2 == 0 && height % 2 == 0) { "frame size must be positive and even" }
        out = ByteBuffer.allocateDirect(Math.multiplyExact(frameBytes, 4)).order(ByteOrder.nativeOrder()).asFloatBuffer()
    }

    /** [rgba8] holds width*height*4 bytes from its position to its limit. */
    fun submit(rgba8: ByteBuffer) {
        require(rgba8.remaining() == frameBytes) { "frame has ${rgba8.remaining()} bytes, expected $frameBytes" }
        out.clear()
        render(rgba8, out)
        out.clear()
        sink.writeFrame(framesWritten, out)
        framesWritten++
    }

    fun writeAudio(packet: ByteBuffer, size: Int, ptsUs: Long) {
        require(ptsUs >= 0L) { "audio timestamp must not be negative" }
        sink.writeAudio(packet, size, ptsUs)
    }

    /** Returns the number of frames written. An export with no frames is an error, not an empty file. */
    fun finish(): Int {
        require(framesWritten > 0) { "no frames were decoded" }
        return framesWritten
    }
}
