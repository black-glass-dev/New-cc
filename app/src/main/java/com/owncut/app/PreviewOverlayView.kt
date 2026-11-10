package com.owncut.app

import android.content.Context
import android.graphics.*
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.util.LruCache
import android.view.MotionEvent
import android.view.View
import java.io.File
import java.util.concurrent.Executors

internal class PreviewOverlayView(context: Context) : View(context) {
    private val density = resources.displayMetrics.density
    private val dp: (Float) -> Float = { it * density }

    private var currentTimelineMs: Long = 0L
    private var overlays: List<TextOverlayItem> = emptyList()
    private var subtitles: List<SubtitleCueItem> = emptyList()
    private var pipClip: ClipSnapshot? = null
    private var pipFx: ClipRichFx? = null
    private var pipSourceMs: Long = 0L
    private var primaryFx: ClipRichFx? = null
    private var primaryLocalMs: Long = 0L
    private var primaryDurMs: Long = 1L

    private var draggingOverlayId: String? = null
    private var dragOffsetX = 0f
    private var dragOffsetY = 0f

    var onOverlayMoved: ((String, Float, Float) -> Unit)? = null

    private val pipExecutor = Executors.newSingleThreadExecutor()
    private val pipFrameCache = LruCache<String, Bitmap>(24)
    private var pendingPipKey: String? = null

    private val ovBgPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val ovTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        textSize = dp(18f)
    }
    private val ovOutlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        textSize = dp(18f)
        style = Paint.Style.STROKE
        strokeWidth = dp(3f)
        color = Color.BLACK
    }
    private val subBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(190, 10, 12, 16)
    }
    private val subTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = dp(14f)
        textAlign = Paint.Align.CENTER
        setShadowLayer(dp(2f), 0f, dp(1f), Color.BLACK)
    }
    private val pipBitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val pipBorderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.5f)
        color = Color.parseColor("#54A8E0")
    }
    private val fadeOverlayPaint = Paint()
    private val tempRect = RectF()
    private val overlayHitBoxes = LinkedHashMap<String, RectF>()

    fun updateState(
        timelineMs: Long,
        richDoc: TimelineRichDocument,
        v1Clip: ClipSnapshot?,
        v1SourceMs: Long,
        v2Clip: ClipSnapshot?,
        v2SourceMs: Long,
    ) {
        currentTimelineMs = timelineMs
        overlays = richDoc.overlays.filter { it.isVisibleAt(timelineMs) }
        subtitles = richDoc.subtitles.filter { it.isVisibleAt(timelineMs) }
        primaryFx = v1Clip?.let { richDoc.fxFor(it.id) }
        if (v1Clip != null) {
            val startMs = (v1Clip.startFrame * 1000L) / 30L
            primaryLocalMs = (timelineMs - startMs).coerceAtLeast(0L)
            primaryDurMs = ((v1Clip.durationFrames * 1000L) / 30L).coerceAtLeast(1L)
        }
        pipClip = v2Clip
        pipFx = v2Clip?.let { richDoc.fxFor(it.id) }
        pipSourceMs = v2SourceMs

        if (v2Clip != null) {
            val bucketMs = (v2SourceMs / 150L) * 150L
            val key = "${v2Clip.mediaId}#$bucketMs"
            if (pipFrameCache.get(key) == null && pendingPipKey != key) {
                pendingPipKey = key
                val appCtx = context.applicationContext
                pipExecutor.execute {
                    val bmp = extractPipFrame(appCtx, v2Clip.mediaId, bucketMs)
                    if (bmp != null) {
                        pipFrameCache.put(key, bmp)
                        postInvalidate()
                    }
                    if (pendingPipKey == key) pendingPipKey = null
                }
            }
        }
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        if (width <= 0 || height <= 0) return

        // Primary transition / fade preview dimming
        primaryFx?.let { fx ->
            var alpha = 0f
            if (fx.transitionIn == "fade_black" && fx.transitionMs > 0L && primaryLocalMs < fx.transitionMs) {
                alpha = (1f - primaryLocalMs.toFloat() / fx.transitionMs.toFloat()).coerceIn(0f, 1f)
            }
            if (fx.fadeInMs > 0L && primaryLocalMs < fx.fadeInMs) {
                alpha = maxOf(alpha, 1f - primaryLocalMs.toFloat() / fx.fadeInMs.toFloat())
            }
            if (fx.fadeOutMs > 0L && (primaryDurMs - primaryLocalMs) < fx.fadeOutMs) {
                alpha = maxOf(alpha, 1f - (primaryDurMs - primaryLocalMs).toFloat() / fx.fadeOutMs.toFloat())
            }
            if (alpha > 0.01f) {
                fadeOverlayPaint.color = Color.argb((alpha * 255).toInt().coerceIn(0, 255), 0, 0, 0)
                canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), fadeOverlayPaint)
            }
        }

        // Track V2 / Picture-in-Picture composite preview
        val v2 = pipClip
        val v2Fx = pipFx
        if (v2 != null && v2Fx != null && v2Fx.opacity > 0.01f) {
            val bucketMs = (pipSourceMs / 150L) * 150L
            val key = "${v2.mediaId}#$bucketMs"
            val bmp = pipFrameCache.get(key) ?: TimelineMediaAnalyzer.cachedThumbnails(v2)?.firstOrNull()
            if (bmp != null && !bmp.isRecycled) {
                val baseW = width * 0.36f * v2Fx.scale.coerceIn(0.2f, 2.5f)
                val baseH = baseW * 9f / 16f
                val cx = width * (0.76f + v2Fx.panX * 0.4f)
                val cy = height * (0.26f + v2Fx.panY * 0.4f)
                tempRect.set(cx - baseW / 2f, cy - baseH / 2f, cx + baseW / 2f, cy + baseH / 2f)
                canvas.save()
                canvas.rotate(v2Fx.rotationDeg, cx, cy)
                pipBitmapPaint.alpha = (v2Fx.opacity * 255f).toInt().coerceIn(0, 255)
                canvas.drawBitmap(bmp, null, tempRect, pipBitmapPaint)
                canvas.drawRect(tempRect, pipBorderPaint)
                canvas.restore()
            }
        }

        // Text / Title Overlays
        overlayHitBoxes.clear()
        for (ov in overlays) {
            val cx = ov.x * width
            val cy = ov.y * height
            ovTextPaint.textSize = dp(18f) * ov.scale.coerceIn(0.4f, 3f)
            ovTextPaint.isFakeBoldText = ov.bold
            ovTextPaint.color = runCatching { Color.parseColor(ov.fgColorHex) }.getOrDefault(Color.WHITE)
            ovOutlinePaint.textSize = ovTextPaint.textSize
            ovOutlinePaint.isFakeBoldText = ov.bold

            val lines = ov.text.split('\n').ifEmpty { listOf("") }
            val lineHeight = ovTextPaint.fontSpacing
            val maxW = lines.maxOf { ovTextPaint.measureText(it) }
            val boxW = maxW + dp(20f)
            val boxH = lineHeight * lines.size + dp(10f)
            val box = RectF(cx - boxW / 2f, cy - boxH / 2f, cx + boxW / 2f, cy + boxH / 2f)
            overlayHitBoxes[ov.id] = RectF(box)

            canvas.save()
            canvas.rotate(ov.rotationDeg, cx, cy)
            if (ov.style != "outline") {
                ovBgPaint.color = runCatching { Color.parseColor(ov.bgColorHex) }.getOrDefault(Color.argb(175, 0, 0, 0))
                val r = if (ov.style == "pill") boxH / 2f else dp(8f)
                canvas.drawRoundRect(box, r, r, ovBgPaint)
            }
            var ty = box.top + dp(5f) - ovTextPaint.ascent()
            for (line in lines) {
                if (ov.style == "outline") canvas.drawText(line, cx, ty, ovOutlinePaint)
                canvas.drawText(line, cx, ty, ovTextPaint)
                ty += lineHeight
            }
            canvas.restore()
        }

        // Subtitles at lower third
        val activeSub = subtitles.lastOrNull()
        if (activeSub != null && activeSub.text.isNotBlank()) {
            val lines = activeSub.text.split('\n')
            val lineH = subTextPaint.fontSpacing
            val maxW = lines.maxOf { subTextPaint.measureText(it) }
            val boxW = minOf(width * 0.9f, maxW + dp(22f))
            val boxH = lineH * lines.size + dp(10f)
            val cx = width / 2f
            val bottom = height - dp(12f)
            tempRect.set(cx - boxW / 2f, bottom - boxH, cx + boxW / 2f, bottom)
            canvas.drawRoundRect(tempRect, dp(6f), dp(6f), subBgPaint)
            var ty = tempRect.top + dp(5f) - subTextPaint.ascent()
            for (line in lines) {
                canvas.drawText(line, cx, ty, subTextPaint)
                ty += lineH
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val hit = overlayHitBoxes.entries.lastOrNull { it.value.contains(event.x, event.y) }
                if (hit != null) {
                    draggingOverlayId = hit.key
                    dragOffsetX = hit.value.centerX() - event.x
                    dragOffsetY = hit.value.centerY() - event.y
                    parent?.requestDisallowInterceptTouchEvent(true)
                    return true
                }
                return false
            }
            MotionEvent.ACTION_MOVE -> {
                val id = draggingOverlayId ?: return false
                val nx = ((event.x + dragOffsetX) / width.coerceAtLeast(1)).coerceIn(0.08f, 0.92f)
                val ny = ((event.y + dragOffsetY) / height.coerceAtLeast(1)).coerceIn(0.08f, 0.92f)
                onOverlayMoved?.invoke(id, nx, ny)
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (draggingOverlayId != null) {
                    draggingOverlayId = null
                    parent?.requestDisallowInterceptTouchEvent(false)
                    return true
                }
                return false
            }
        }
        return false
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        pipFrameCache.evictAll()
    }

    private fun extractPipFrame(context: Context, mediaId: String, timeMs: Long): Bitmap? {
        val retriever = MediaMetadataRetriever()
        return try {
            val uri = if (mediaId.contains("://")) Uri.parse(mediaId) else Uri.fromFile(File(mediaId))
            retriever.setDataSource(context, uri)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                retriever.getScaledFrameAtTime(timeMs * 1000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC, 320, 180)
            } else {
                retriever.getFrameAtTime(timeMs * 1000L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
            }
        } catch (_: Throwable) {
            null
        } finally {
            runCatching { retriever.release() }
        }
    }
}
