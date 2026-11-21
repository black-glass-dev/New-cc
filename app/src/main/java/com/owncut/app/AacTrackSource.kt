package com.owncut.app

import android.content.Context
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import java.nio.ByteBuffer
import java.util.concurrent.CancellationException

/** The first audio track of a source. [mime] is the codec MIME type; [asc] is the AAC AudioSpecificConfig (csd-0) when present. */
internal class AudioTrackInfo(val mime: String, val asc: ByteArray?, val sampleRate: Int, val channels: Int)

/** Copies compressed AAC access units of a clip's source range onto the output timeline. Nothing is decoded or re-encoded. */
internal object AacTrackSource {
    const val AAC_MIME = "audio/mp4a-latm"
    private const val DEFAULT_MAX_INPUT = 256 * 1024

    /** Probes the first audio track; null when the source has no audio. */
    fun probe(context: Context, uri: Uri): AudioTrackInfo? {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(context.applicationContext, uri, null)
            val index = firstAudioTrack(extractor) ?: return null
            val format = extractor.getTrackFormat(index)
            val mime = format.getString(MediaFormat.KEY_MIME).orEmpty()
            val asc = format.getByteBuffer("csd-0")?.let { buffer ->
                ByteArray(buffer.remaining()).also { buffer.duplicate().get(it) }
            }
            return AudioTrackInfo(
                mime = mime,
                asc = asc,
                sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE),
                channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT),
            )
        } finally {
            extractor.release()
        }
    }

    /** Copies the access units with presentation time in [startUs, endUs) to [loop]. Packet pts =
     * [offsetUs] + (sample time - startUs). Returns the number of packets copied. */
    fun copyRange(
        context: Context,
        uri: Uri,
        startUs: Long,
        endUs: Long,
        offsetUs: Long,
        loop: Fp32FrameLoop,
        cancelled: () -> Boolean,
    ): Int {
        require(startUs >= 0 && endUs > startUs && offsetUs >= 0) { "audio range must be non-empty" }
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(context.applicationContext, uri, null)
            val index = checkNotNull(firstAudioTrack(extractor)) { "source has no audio track" }
            extractor.selectTrack(index)
            val format = extractor.getTrackFormat(index)
            val maxInput = if (format.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)) {
                format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE).coerceAtLeast(DEFAULT_MAX_INPUT)
            } else {
                DEFAULT_MAX_INPUT
            }
            val packet = ByteBuffer.allocateDirect(maxInput)
            extractor.seekTo(startUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            var copied = 0
            while (true) {
                if (cancelled()) throw CancellationException("export cancelled")
                val time = extractor.sampleTime
                if (time < 0 || time >= endUs) break
                if (time >= startUs) {
                    packet.clear()
                    val size = extractor.readSampleData(packet, 0)
                    check(size > 0) { "audio sample could not be read" }
                    loop.writeAudio(packet, size, offsetUs + (time - startUs))
                    copied++
                }
                if (!extractor.advance()) break
            }
            return copied
        } finally {
            extractor.release()
        }
    }

    private fun firstAudioTrack(extractor: MediaExtractor): Int? {
        for (index in 0 until extractor.trackCount) {
            val mime = extractor.getTrackFormat(index).getString(MediaFormat.KEY_MIME).orEmpty()
            if (mime.startsWith("audio/")) return index
        }
        return null
    }
}
