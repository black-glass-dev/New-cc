package com.owncut.app

import android.content.Context
import android.graphics.ImageFormat
import android.media.Image
import android.media.ImageReader
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.Handler
import android.os.HandlerThread
import java.nio.ByteBuffer
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.CancellationException
import java.util.concurrent.TimeUnit

/** Coded size and track facts of a source video, before rotation. */
internal data class SourceVideoInfo(
    val mime: String,
    val width: Int,
    val height: Int,
    val rotation: Int,
    val frameRate: Int,
    val hasAudio: Boolean,
) {
    val outputWidth: Int get() = if (rotation == 90 || rotation == 270) height else width
    val outputHeight: Int get() = if (rotation == 90 || rotation == 270) width else height
}

/** Decodes a clip's video frames to RGBA8 on the CPU, one frame at a time, in presentation order.
 * Hardware MediaCodec decodes into an ImageReader (YUV_420_888); [YuvFrameConverter] converts each frame.
 * No GL context is used, so the path does not depend on GPU drivers. Speed depends on the phone's decoder.
 */
internal object SourceFrameDecoder {
    private const val STALL_NS = 60_000_000_000L

    fun probe(context: Context, uri: Uri): SourceVideoInfo {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(context.applicationContext, uri, null)
            var video: MediaFormat? = null
            var audio = false
            for (index in 0 until extractor.trackCount) {
                val format = extractor.getTrackFormat(index)
                val mime = format.getString(MediaFormat.KEY_MIME).orEmpty()
                if (mime.startsWith("video/") && video == null) video = format
                if (mime.startsWith("audio/")) audio = true
            }
            val format = checkNotNull(video) { "no video track" }
            val rate = runCatching { format.getInteger(MediaFormat.KEY_FRAME_RATE) }.getOrDefault(0)
            val rotation = if (format.containsKey(MediaFormat.KEY_ROTATION)) format.getInteger(MediaFormat.KEY_ROTATION) else 0
            return SourceVideoInfo(
                mime = format.getString(MediaFormat.KEY_MIME)!!,
                width = format.getInteger(MediaFormat.KEY_WIDTH),
                height = format.getInteger(MediaFormat.KEY_HEIGHT),
                rotation = rotation,
                frameRate = rate,
                hasAudio = audio,
            )
        } finally {
            extractor.release()
        }
    }

    /** Decodes frames with presentation time in [startMs, endMs) and passes each one to [onFrame] as RGBA8
     * (outputWidth*outputHeight*4 bytes, rotated). Returns the number of frames delivered. */
    fun decodeRange(
        context: Context,
        uri: Uri,
        startMs: Long,
        endMs: Long,
        fullRange: Boolean,
        cancelled: () -> Boolean,
        onFrame: (ByteArray) -> Unit,
    ): Int {
        require(startMs >= 0 && endMs > startMs) { "clip range must be non-empty" }
        val startUs = startMs * 1000L
        val endUs = endMs * 1000L
        val extractor = MediaExtractor()
        val thread = HandlerThread("OwncutFrameDecode").apply { start() }
        val images = ArrayBlockingQueue<Image>(4)
        var reader: ImageReader? = null
        var decoder: MediaCodec? = null
        var written = 0
        try {
            extractor.setDataSource(context.applicationContext, uri, null)
            var track = -1
            for (index in 0 until extractor.trackCount) {
                if (extractor.getTrackFormat(index).getString(MediaFormat.KEY_MIME).orEmpty().startsWith("video/")) {
                    track = index
                    break
                }
            }
            check(track >= 0) { "no video track" }
            extractor.selectTrack(track)
            val format = extractor.getTrackFormat(track)
            val mime = format.getString(MediaFormat.KEY_MIME)!!
            val width = format.getInteger(MediaFormat.KEY_WIDTH)
            val height = format.getInteger(MediaFormat.KEY_HEIGHT)
            val rotation = if (format.containsKey(MediaFormat.KEY_ROTATION)) format.getInteger(MediaFormat.KEY_ROTATION) else 0
            // Rotation is applied by the YUV converter, so the decoder itself must not rotate.
            format.setInteger(MediaFormat.KEY_ROTATION, 0)
            val outWidth = if (rotation == 90 || rotation == 270) height else width
            val outHeight = if (rotation == 90 || rotation == 270) width else height
            val rgba = ByteBuffer.allocateDirect(outWidth * outHeight * 4)

            val imageReader = ImageReader.newInstance(width, height, ImageFormat.YUV_420_888, 4)
            reader = imageReader
            imageReader.setOnImageAvailableListener({ source ->
                val image = source.acquireNextImage()
                if (image != null && !images.offer(image)) image.close()
            }, Handler(thread.looper))

            val codec = MediaCodec.createDecoderByType(mime)
            decoder = codec
            codec.configure(format, imageReader.surface, null, 0)
            codec.start()
            extractor.seekTo(startUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)

            val info = MediaCodec.BufferInfo()
            var inputDone = false
            var outputDone = false
            var lastProgress = System.nanoTime()
            while (!outputDone) {
                if (cancelled()) throw CancellationException("Export cancelled")
                check(System.nanoTime() - lastProgress < STALL_NS) { "video decoder stalled" }
                if (!inputDone) {
                    val inputIndex = codec.dequeueInputBuffer(10_000)
                    if (inputIndex >= 0) {
                        val input = codec.getInputBuffer(inputIndex) ?: error("decoder has no input buffer")
                        val size = extractor.readSampleData(input, 0)
                        if (size < 0) {
                            codec.queueInputBuffer(inputIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inputDone = true
                        } else {
                            codec.queueInputBuffer(inputIndex, 0, size, extractor.sampleTime, extractor.sampleFlags)
                            extractor.advance()
                        }
                    }
                }
                val outputIndex = codec.dequeueOutputBuffer(info, 10_000)
                if (outputIndex >= 0) {
                    lastProgress = System.nanoTime()
                    val endOfStream = (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0
                    val pts = info.presentationTimeUs
                    val wanted = !endOfStream && pts >= startUs && pts < endUs
                    codec.releaseOutputBuffer(outputIndex, wanted)
                    if (wanted) {
                        val image = images.poll(10, TimeUnit.SECONDS) ?: throw IllegalStateException("decoded frame did not arrive")
                        try {
                            YuvFrameConverter.convert(image, rotation, fullRange, rgba)
                        } finally {
                            image.close()
                        }
                        rgba.clear()
                        val bytes = ByteArray(outWidth * outHeight * 4)
                        rgba.get(bytes)
                        onFrame(bytes)
                        written++
                    }
                    if (endOfStream || pts >= endUs) outputDone = true
                }
            }
        } finally {
            decoder?.let {
                runCatching { it.stop() }
                it.release()
            }
            generateSequence { images.poll() }.forEach { it.close() }
            reader?.close()
            extractor.release()
            thread.quitSafely()
        }
        return written
    }
}
