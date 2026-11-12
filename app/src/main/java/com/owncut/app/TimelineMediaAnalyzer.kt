package com.owncut.app

import android.content.Context
import android.graphics.Bitmap
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.LruCache
import com.owncut.timeline.AudioAnalysisResult
import java.io.File
import java.nio.ByteOrder
import java.util.concurrent.Executors

internal object TimelineMediaAnalyzer {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    private val pendingKeys = mutableSetOf<String>()

    private val thumbCache = LruCache<String, List<Bitmap>>(48)

    private val audioCache = LruCache<String, AudioAnalysisResult>(64)

    private fun parseMediaUri(mediaId: String): Uri =
        if (mediaId.contains("://")) Uri.parse(mediaId) else Uri.fromFile(File(mediaId))

    fun clipKey(clip: ClipSnapshot): String =
        "${clip.mediaId}#${clip.sourceInMs}#${clip.sourceOutMs}"

    fun cachedThumbnails(clip: ClipSnapshot): List<Bitmap>? =
        synchronized(thumbCache) { thumbCache.get(clipKey(clip)) }

    fun cachedAudio(clip: ClipSnapshot): AudioAnalysisResult? =
        synchronized(audioCache) { audioCache.get(clipKey(clip)) }

    fun requestClipThumbnails(
        context: Context,
        clip: ClipSnapshot,
        frameCount: Int = 5,
        onReady: () -> Unit,
    ) {
        if (clip.trackId.startsWith("a", ignoreCase = true)) return
        val key = "thumb:${clipKey(clip)}"
        if (cachedThumbnails(clip) != null) return
        synchronized(pendingKeys) {
            if (!pendingKeys.add(key)) return
        }
        val appCtx = context.applicationContext
        worker.execute {
            try {
                val frames = extractThumbnails(appCtx, clip, frameCount.coerceIn(2, 8))
                if (frames.isNotEmpty()) {
                    synchronized(thumbCache) { thumbCache.put(clipKey(clip), frames) }
                    mainHandler.post { onReady() }
                }
            } catch (_: Throwable) {
            } finally {
                synchronized(pendingKeys) { pendingKeys.remove(key) }
            }
        }
    }

    fun requestAudioAnalysis(
        context: Context,
        clip: ClipSnapshot,
        session: EngineSession?,
        onReady: (AudioAnalysisResult) -> Unit,
    ) {
        val existing = cachedAudio(clip)
        if (existing != null) {
            onReady(existing)
            return
        }
        val key = "audio:${clipKey(clip)}"
        synchronized(pendingKeys) {
            if (!pendingKeys.add(key)) return
        }
        val appCtx = context.applicationContext
        worker.execute {
            try {
                val res = extractAndAnalyzeAudio(appCtx, clip, session)
                if (res != null && res.peaks.isNotEmpty()) {
                    synchronized(audioCache) { audioCache.put(clipKey(clip), res) }
                    mainHandler.post { onReady(res) }
                }
            } catch (_: Throwable) {
            } finally {
                synchronized(pendingKeys) { pendingKeys.remove(key) }
            }
        }
    }

    fun detectSceneCutsMs(
        context: Context,
        clip: ClipSnapshot,
        session: EngineSession,
        threshold: Double = 0.35,
    ): List<Long> {
        val durMs = (clip.sourceOutMs - clip.sourceInMs).coerceAtLeast(1L)
        val steps = (durMs / 250L).toInt().coerceIn(4, 32)
        val w = 80
        val h = 45
        val rgba = ByteArray(steps * w * h * 4)
        val retriever = MediaMetadataRetriever()
        var extracted = 0
        try {
            retriever.setDataSource(context.applicationContext, parseMediaUri(clip.mediaId))
            for (i in 0 until steps) {
                val tUs = (clip.sourceInMs + (durMs * i) / steps) * 1000L
                val rawBmp = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                    retriever.getScaledFrameAtTime(tUs, MediaMetadataRetriever.OPTION_CLOSEST, w, h)
                } else {
                    retriever.getFrameAtTime(tUs, MediaMetadataRetriever.OPTION_CLOSEST)
                } ?: continue
                val scaled = if (rawBmp.width != w || rawBmp.height != h) {
                    Bitmap.createScaledBitmap(rawBmp, w, h, false).also { if (it !== rawBmp) rawBmp.recycle() }
                } else rawBmp
                val argb = IntArray(w * h)
                scaled.getPixels(argb, 0, w, 0, 0, w, h)
                scaled.recycle()
                val baseOff = extracted * w * h * 4
                for (p in argb.indices) {
                    val c = argb[p]
                    val o = baseOff + p * 4
                    rgba[o + 0] = ((c ushr 16) and 0xFF).toByte()
                    rgba[o + 1] = ((c ushr 8) and 0xFF).toByte()
                    rgba[o + 2] = (c and 0xFF).toByte()
                    rgba[o + 3] = ((c ushr 24) and 0xFF).toByte()
                }
                extracted++
            }
        } finally {
            runCatching { retriever.release() }
        }
        if (extracted < 2) return emptyList()
        val trimmed = if (extracted == steps) rgba else rgba.copyOf(extracted * w * h * 4)
        val cutIndices = session.detectVideoSceneCuts(trimmed, extracted, w, h, threshold)
        return cutIndices
            .filter { it in 1 until extracted }
            .map { idx -> clip.sourceInMs + (durMs * idx) / extracted }
            .distinct()
    }

    private fun extractThumbnails(context: Context, clip: ClipSnapshot, count: Int): List<Bitmap> {
        val durMs = (clip.sourceOutMs - clip.sourceInMs).coerceAtLeast(1L)
        val out = mutableListOf<Bitmap>()
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(context, parseMediaUri(clip.mediaId))
            for (i in 0 until count) {
                val timeUs = (clip.sourceInMs + (durMs * i) / count.coerceAtLeast(1)) * 1000L
                val bmp = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                    retriever.getScaledFrameAtTime(timeUs, MediaMetadataRetriever.OPTION_CLOSEST_SYNC, 96, 54)
                } else {
                    retriever.getFrameAtTime(timeUs, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)?.let { full ->
                        Bitmap.createScaledBitmap(full, 96, 54, true).also { if (it !== full) full.recycle() }
                    }
                }
                if (bmp != null) out += bmp
            }
        } finally {
            runCatching { retriever.release() }
        }
        return out
    }

    private fun extractAndAnalyzeAudio(
        context: Context,
        clip: ClipSnapshot,
        session: EngineSession?,
    ): AudioAnalysisResult? {
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        try {
            extractor.setDataSource(context, parseMediaUri(clip.mediaId), null)
            var audioTrackIndex = -1
            var format: MediaFormat? = null
            for (i in 0 until extractor.trackCount) {
                val f = extractor.getTrackFormat(i)
                val mime = f.getString(MediaFormat.KEY_MIME).orEmpty()
                if (mime.startsWith("audio/")) {
                    audioTrackIndex = i
                    format = f
                    break
                }
            }
            if (audioTrackIndex < 0 || format == null) return null
            val mime = format.getString(MediaFormat.KEY_MIME) ?: return null
            var sampleRate = if (format.containsKey(MediaFormat.KEY_SAMPLE_RATE)) format.getInteger(MediaFormat.KEY_SAMPLE_RATE) else 48000
            var channels = if (format.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) format.getInteger(MediaFormat.KEY_CHANNEL_COUNT) else 2
            channels = channels.coerceIn(1, 2)

            extractor.selectTrack(audioTrackIndex)
            extractor.seekTo(clip.sourceInMs * 1000L, MediaExtractor.SEEK_TO_CLOSEST_SYNC)

            codec = MediaCodec.createDecoderByType(mime)
            codec.configure(format, null, null, 0)
            codec.start()

            val maxSamples = 192_000
            val pcmList = ShortArray(maxSamples)
            var pcmCount = 0
            val info = MediaCodec.BufferInfo()
            var sawEos = false
            val endUs = clip.sourceOutMs * 1000L
            val startUs = clip.sourceInMs * 1000L
            var iter = 0

            while (iter < 1200 && pcmCount < maxSamples) {
                iter++
                if (!sawEos) {
                    val inIdx = codec.dequeueInputBuffer(4000L)
                    if (inIdx >= 0) {
                        val inBuf = codec.getInputBuffer(inIdx)
                        val sampleSize = if (inBuf != null) extractor.readSampleData(inBuf, 0) else -1
                        val ptsUs = extractor.sampleTime
                        if (sampleSize < 0 || ptsUs > endUs + 50_000L) {
                            codec.queueInputBuffer(inIdx, 0, 0, 0L, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            sawEos = true
                        } else {
                            codec.queueInputBuffer(inIdx, 0, sampleSize, ptsUs.coerceAtLeast(0L), 0)
                            extractor.advance()
                        }
                    }
                }
                val outIdx = codec.dequeueOutputBuffer(info, 4000L)
                if (outIdx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    val ofmt = codec.outputFormat
                    if (ofmt.containsKey(MediaFormat.KEY_SAMPLE_RATE)) sampleRate = ofmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                    if (ofmt.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) channels = ofmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT).coerceIn(1, 2)
                } else if (outIdx >= 0) {
                    if (info.offset >= 0 && info.size > 0 && info.presentationTimeUs >= startUs) {
                        val outBuf = codec.getOutputBuffer(outIdx)
                        if (outBuf != null && info.offset + info.size <= outBuf.capacity()) {
                            outBuf.position(info.offset)
                            outBuf.limit(info.offset + info.size)
                            val sb = outBuf.order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
                            val avail = sb.remaining().coerceAtMost(maxSamples - pcmCount)
                            if (avail > 0) {
                                sb.get(pcmList, pcmCount, avail)
                                pcmCount += avail
                            }
                        }
                    }
                    codec.releaseOutputBuffer(outIdx, false)
                    if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) break
                }
            }

            if (pcmCount <= channels) return null
            val trimmedPcm = pcmList.copyOf(pcmCount)
            return session?.analyzeAudioPcm16(trimmedPcm, channels, sampleRate, 48) ?: run {
                val bins = 48
                val peaks = List(bins) { b ->
                    val s0 = (b * trimmedPcm.size) / bins
                    val s1 = ((b + 1) * trimmedPcm.size) / bins
                    var maxVal = 0f
                    for (i in s0 until s1) {
                        val v = kotlin.math.abs(trimmedPcm[i].toInt()) / 32768f
                        if (v > maxVal) maxVal = v
                    }
                    maxVal.coerceIn(0f, 1f)
                }
                AudioAnalysisResult(0.0, -24.0, peaks)
            }
        } finally {
            runCatching { codec?.stop() }
            runCatching { codec?.release() }
            runCatching { extractor.release() }
        }
    }
}
