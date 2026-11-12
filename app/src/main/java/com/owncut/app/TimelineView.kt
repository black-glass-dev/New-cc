package com.owncut.app

import android.content.Context
import android.graphics.*
import android.net.Uri
import android.os.Bundle
import android.view.*
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityNodeProvider
import com.owncut.timeline.SnapDecision
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToLong

/** Multi-track timeline view with filmstrip thumbnails, audio waveforms, beat grid, trim handles,
 * magnetic snap guides, pinch-zoom, and 2/3-finger undo/redo gestures in one shared scroll space. */
internal class TimelineView(context: Context) : View(context) {
    private val density = resources.displayMetrics.density
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val thumbPaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val wavePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF5EEAD4.toInt()
        strokeWidth = 1.4f * density
        strokeCap = Paint.Cap.ROUND
    }
    private val beatPaint = Paint().apply {
        color = 0x5538BDF8
        strokeWidth = density
    }
    private val snapGuidePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF38BDF8.toInt()
        strokeWidth = 1.8f * density
    }
    private val badgeBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xB80D121C.toInt() }
    private val tempRect = RectF()
    private val tempPath = Path()
    private val marker = Path()

    private val red = 0xFFFF5964.toInt()
    private val rulerHeight = 34f * density
    private val laneHeight = 72f * density

    private var clipNames = emptyMap<String, String>()
    private var tracks = emptyList<String>()
    private var explicitTracks = emptyList<String>()
    private var laneIndices = emptyMap<String, Int>()
    private var clipIndicesById = emptyMap<String, Int>()
    private var clipIndex = TimelineClipIndex(emptyList())

    internal var clips = emptyList<ClipSnapshot>(); private set
    internal var richDoc = TimelineRichDocument(); private set
    internal var endFrame = 0L; private set
    internal var playheadFrame = 0L; private set
    internal var pixelsPerFrame = 1.0; private set

    private var zoomMultiplier = 1.0
    private var selectedId: String? = null
    private var selectedTrack: String? = null
    private var draggingHead = false
    private var movingClip: ClipSnapshot? = null
    private var movingTargetTrack: String? = null
    private var trimClip: ClipSnapshot? = null
    private var trimLeftEdge = true
    private var trimDeltaFrames = 0L
    private data class RichBlockDrag(
        val id: String, val subtitle: Boolean, val startMs: Long, val endMs: Long,
        val resizeEnd: Boolean, val downX: Float, var deltaFrames: Long = 0L,
    )
    private var richBlockDrag: RichBlockDrag? = null
    private var downX = 0f
    private var moveDelta = 0f
    private var snapGuideFrame = -1L
    private var lastHapticSnapFrame = -1L
    private var maxPointerCount = 1
    private var pointerDownTimeMs = 0L
    private var pinchAccumulatedDelta = 0f
    private var focusedVirtualId = View.NO_ID
    private var hoveredVirtualId = View.NO_ID

    var onSeek: (String, Long) -> Unit = { _, _ -> }
    var onScrub: (Long, Boolean) -> Unit = { _, _ -> }
    var onScrubCancel: () -> Unit = {}
    var onMove: (ClipSnapshot, Long) -> Unit = { _, _ -> }
    var onMoveToTrack: ((ClipSnapshot, String, Long) -> Unit)? = null
    var onTrimClip: ((ClipSnapshot, Boolean, Long) -> Unit)? = null
    var onEvaluateSnap: ((ClipSnapshot, Long, Double) -> SnapDecision?)? = null
    var onPinchZoom: ((Double, Double) -> Double)? = null
    var onTwoFingerUndo: (() -> Unit)? = null
    var onThreeFingerRedo: (() -> Unit)? = null
    var onMarkerClick: ((TimelineMarkerItem) -> Unit)? = null
    var onKeyframeClick: ((ClipSnapshot, Long) -> Unit)? = null
    var onEditRichBlock: ((String, Boolean, Long, Long) -> Unit)? = null

    private val scaleDetector = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
            pinchAccumulatedDelta = 0f
            return true
        }

        override fun onScale(detector: ScaleGestureDetector): Boolean {
            pinchAccumulatedDelta += abs(detector.scaleFactor - 1f)
            val native = onPinchZoom?.invoke(detector.scaleFactor.toDouble(), detector.focusX.toDouble())
            zoomMultiplier = (native ?: (zoomMultiplier * detector.scaleFactor.toDouble())).coerceIn(0.25, 8.0)
            pixelsPerFrame = TimelineGeometry.pixelsPerFrame(endFrame, density) * zoomMultiplier
            requestLayout()
            invalidate()
            return true
        }
    })

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        isClickable = true
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
        contentDescription = AppLanguage.text(R.string.message_9909c1b640)
    }

    fun setTimeline(values: List<ClipSnapshot>, selected: String?) {
        val nextTracks = buildList {
            explicitTracks.forEach { if (it !in this) add(it) }
            values.forEach { if (it.trackId !in this) add(it.trackId) }
        }.sortedWith(compareBy<String>({ if (it.startsWith("a", true)) 1 else 0 }, { it }))

        if (clips != values || tracks != nextTracks) {
            focusedVirtualId = View.NO_ID
            hoveredVirtualId = View.NO_ID
            clipIndex = TimelineClipIndex(values)
            clipIndicesById = values.withIndex().associate { it.value.id to it.index }
            clipNames = values.associate {
                it.id to (Uri.parse(it.mediaId).lastPathSegment?.substringAfterLast('/')?.takeLast(24) ?: it.id)
            }
            tracks = nextTracks
            laneIndices = tracks.withIndex().associate { it.value to it.index }
            sendAccessibilityEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED)
        }
        clips = values
        selectedId = selected
        selectedTrack = values.firstOrNull { it.id == selected }?.trackId
        endFrame = values.maxOfOrNull { Math.addExact(it.startFrame, it.durationFrames) } ?: 0L
        pixelsPerFrame = TimelineGeometry.pixelsPerFrame(endFrame, density) * zoomMultiplier
        playheadFrame = playheadFrame.coerceIn(0L, endFrame)
        requestLayout()
        invalidate()
    }

    fun updateRich(doc: TimelineRichDocument, trackOrder: List<String>? = null, session: EngineSession? = null) {
        richDoc = doc
        if (trackOrder != null) {
            explicitTracks = trackOrder
            val nextTracks = buildList {
                explicitTracks.forEach { if (it !in this) add(it) }
                clips.forEach { if (it.trackId !in this) add(it.trackId) }
            }.sortedWith(compareBy<String>({ if (it.startsWith("a", true)) 1 else 0 }, { it }))
            if (nextTracks != tracks) {
                tracks = nextTracks
                laneIndices = tracks.withIndex().associate { it.value to it.index }
                requestLayout()
            }
        }
        for (c in clips.take(24)) {
            if (!c.trackId.startsWith("a", true) && TimelineMediaAnalyzer.cachedThumbnails(c) == null) {
                TimelineMediaAnalyzer.requestClipThumbnails(context, c, 5) { postInvalidate() }
            }
            val fx = richDoc.fxFor(c.id)
            if (fx.audioPeaks.isEmpty() && TimelineMediaAnalyzer.cachedAudio(c) == null) {
                TimelineMediaAnalyzer.requestAudioAnalysis(context, c, session) { res ->
                    session?.setClipWaveformPeaks(c.id, res.peaks)
                    if (session != null) richDoc = session.richState()
                    postInvalidate()
                }
            }
        }
        invalidate()
    }

    fun setPlayhead(frame: Long) {
        playheadFrame = frame.coerceIn(0L, endFrame)
        invalidate()
    }

    /** Accessible button zoom shares the native zoom state used by pinch gestures. */
    fun zoomBy(factor: Double, focusXPx: Double = width / 2.0) {
        val native = onPinchZoom?.invoke(factor, focusXPx)
        zoomMultiplier = (native ?: (zoomMultiplier * factor)).coerceIn(0.25, 8.0)
        pixelsPerFrame = TimelineGeometry.pixelsPerFrame(endFrame, density) * zoomMultiplier
        requestLayout()
        invalidate()
    }

    fun xForFrame(frame: Long): Float = (frame * pixelsPerFrame).toFloat()
    private fun frameAt(x: Float) = TimelineGeometry.frameAt(x, pixelsPerFrame, endFrame)
    private fun trackAt(y: Float): String? = if (y < rulerHeight) selectedTrack ?: tracks.firstOrNull()
        else tracks.getOrNull(((y - rulerHeight) / laneHeight).toInt().coerceIn(0, max(0, tracks.lastIndex)))

    private fun clipRect(clip: ClipSnapshot): RectF {
        val targetTrack = if (movingClip?.id == clip.id && movingTargetTrack != null) movingTargetTrack!! else clip.trackId
        val lane = laneIndices[targetTrack] ?: laneIndices[clip.trackId] ?: return RectF()
        val top = rulerHeight + lane * laneHeight + 17f * density
        var startF = clip.startFrame
        var durF = clip.durationFrames
        if (trimClip?.id == clip.id) {
            if (trimLeftEdge) {
                val applied = trimDeltaFrames.coerceIn(-startF, durF - 1L)
                startF += applied
                durF -= applied
            } else {
                durF = (durF + trimDeltaFrames).coerceAtLeast(1L)
            }
        }
        val left = xForFrame(startF)
        val right = xForFrame(startF + durF).coerceAtLeast(left + 1f)
        return RectF(left, top, right, top + 47f * density)
    }

    private fun isInTrimHandle(clip: ClipSnapshot, x: Float, y: Float, leftEdge: Boolean): Boolean {
        val r = clipRect(clip)
        if (r.isEmpty || y < r.top || y > r.bottom || r.width() < 34f * density) return false
        val handleW = 14f * density
        return if (leftEdge) x >= r.left - 3f * density && x <= r.left + handleW
        else x >= r.right - handleW && x <= r.right + 3f * density
    }

    private fun findRichBlockAt(x: Float, y: Float): RichBlockDrag? {
        val laneTop = rulerHeight + tracks.size.coerceAtLeast(1) * laneHeight
        val resizeSlop = 12f * density
        for (item in richDoc.overlays) {
            val x0 = xForFrame(item.startMs * 30L / 1000L)
            val x1 = max(x0 + 10f * density, xForFrame((item.startMs + item.durationMs) * 30L / 1000L))
            if (y in (laneTop + 2f * density)..(laneTop + 31f * density) && x in x0..x1)
                return RichBlockDrag(item.id, false, item.startMs, item.startMs + item.durationMs, x >= x1 - resizeSlop, x)
        }
        val subTop = laneTop + 34f * density
        for (item in richDoc.subtitles) {
            val x0 = xForFrame(item.startMs * 30L / 1000L)
            val x1 = max(x0 + 10f * density, xForFrame(item.endMs * 30L / 1000L))
            if (y in (subTop + 2f * density)..(subTop + 31f * density) && x in x0..x1)
                return RichBlockDrag(item.id, true, item.startMs, item.endMs, x >= x1 - resizeSlop, x)
        }
        return null
    }

    private fun findKeyframeAt(x: Float, y: Float): Pair<ClipSnapshot, Long>? {
        val radius = 10f * density
        var best: Pair<ClipSnapshot, Long>? = null
        var bestDistance = Float.MAX_VALUE
        for (clip in clips) {
            val rect = clipRect(clip)
            val fx = richDoc.fxFor(clip.id)
            if (fx.transformKeyframes.isEmpty() || y !in (rect.top - radius)..(rect.top + 18f * density)) continue
            fx.transformKeyframes.forEach { key ->
                val frame = clip.startFrame + key.timeMs.coerceAtLeast(0L) * 30L / 1000L
                val px = xForFrame(frame) + if (movingClip?.id == clip.id) moveDelta else 0f
                val distance = abs(px - x)
                if (distance <= radius && distance < bestDistance) {
                    best = clip to frame
                    bestDistance = distance
                }
            }
        }
        return best
    }

    private fun findMarkerAt(x: Float): TimelineMarkerItem? {
        val hitPx = 12f * density
        return richDoc.markers.minByOrNull { mk ->
            abs(xForFrame((mk.positionMs * 30L) / 1000L) - x)
        }?.takeIf { mk ->
            abs(xForFrame((mk.positionMs * 30L) / 1000L) - x) <= hitPx
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val wantedWidth = ceil(xForFrame(endFrame) + 24f * density).toInt().coerceAtLeast((240 * density).toInt())
        setMeasuredDimension(
            resolveSize(wantedWidth.coerceAtLeast(suggestedMinimumWidth), widthMeasureSpec),
            resolveSize((rulerHeight + tracks.size.coerceAtLeast(1) * laneHeight + 68f * density).toInt(), heightMeasureSpec),
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(StudioUi.surface)
        val visible = canvas.clipBounds
        paint.style = Paint.Style.FILL
        paint.color = StudioUi.background
        canvas.drawRect(visible.left.toFloat(), 0f, visible.right.toFloat(), rulerHeight, paint)
        for (lane in tracks.indices) {
            paint.color = if (lane % 2 == 0) StudioUi.surface else StudioUi.background
            canvas.drawRect(visible.left.toFloat(), rulerHeight + lane * laneHeight, visible.right.toFloat(), rulerHeight + (lane + 1) * laneHeight, paint)
            paint.color = StudioUi.border
            paint.strokeWidth = density
            canvas.drawLine(visible.left.toFloat(), rulerHeight + (lane + 1) * laneHeight, visible.right.toFloat(), rulerHeight + (lane + 1) * laneHeight, paint)
        }
        val richLaneTop = rulerHeight + tracks.size.coerceAtLeast(1) * laneHeight
        paint.color = StudioUi.background
        canvas.drawRect(visible.left.toFloat(), richLaneTop, visible.right.toFloat(), richLaneTop + 34f * density, paint)
        paint.color = StudioUi.surface
        canvas.drawRect(visible.left.toFloat(), richLaneTop + 34f * density, visible.right.toFloat(), richLaneTop + 68f * density, paint)
        paint.color = StudioUi.border
        canvas.drawLine(visible.left.toFloat(), richLaneTop + 34f * density, visible.right.toFloat(), richLaneTop + 34f * density, paint)
        canvas.drawLine(visible.left.toFloat(), richLaneTop + 68f * density, visible.right.toFloat(), richLaneTop + 68f * density, paint)
        if (clips.isEmpty()) {
            paint.color = StudioUi.muted
            paint.textSize = 12f * density
            canvas.drawText(AppLanguage.text(R.string.timeline_empty), visible.left + 16 * density, rulerHeight + 36 * density, paint)
        }

        // Beat grid lines
        if (richDoc.beatBpm > 1.0 && pixelsPerFrame > 0.0) {
            val framesPerBeat = (60.0 / richDoc.beatBpm) * 30.0
            val offsetFrames = (richDoc.beatOffsetMs * 30.0) / 1000.0
            val visStart = frameAt(visible.left.toFloat()).toDouble()
            val visEnd = frameAt(visible.right.toFloat()).toDouble()
            if (framesPerBeat >= 2.0) {
                var b = kotlin.math.floor((visStart - offsetFrames) / framesPerBeat).toLong().coerceAtLeast(0L)
                var guard = 0
                while (guard++ < 300) {
                    val f = offsetFrames + b * framesPerBeat
                    if (f > visEnd + framesPerBeat) break
                    val bx = (f * pixelsPerFrame).toFloat()
                    if (bx >= visible.left && bx <= visible.right) {
                        canvas.drawLine(bx, rulerHeight, bx, height.toFloat(), beatPaint)
                    }
                    b++
                }
            }
        }

        // Ruler ticks
        paint.color = StudioUi.muted
        paint.textSize = 10f * density
        val minTickFrames = (72.0 * density / pixelsPerFrame).coerceAtLeast(30.0)
        var tickFrames = 30L
        while (tickFrames < minTickFrames && tickFrames <= Long.MAX_VALUE / 2L) tickFrames *= 2L
        var tick = (frameAt(visible.left.toFloat()) / tickFrames) * tickFrames
        while (tick <= endFrame && xForFrame(tick) <= visible.right + 100f * density) {
            val x = xForFrame(tick)
            paint.color = StudioUi.muted
            canvas.drawLine(x, rulerHeight - 8 * density, x, rulerHeight, paint)
            canvas.drawText(TimelineGeometry.timecode(tick).substringBeforeLast(':'), x + 4 * density, 14 * density, paint)
            paint.color = StudioUi.border
            paint.strokeWidth = density
            canvas.drawLine(x, rulerHeight, x, height.toFloat(), paint)
            for (minor in 1..3) {
                val minorX = x + (tickFrames * pixelsPerFrame * minor / 4).toFloat()
                canvas.drawLine(minorX, rulerHeight - 4 * density, minorX, rulerHeight, paint)
            }
            if (endFrame - tick < tickFrames) break
            tick += tickFrames
        }

        // Dedicated visual text/subtitle lanes keep rich timing editable concepts visible.
        paint.textSize = 9f * density
        val textLaneTop = rulerHeight + tracks.size.coerceAtLeast(1) * laneHeight
        paint.color = 0xFFFACC15.toInt()
        canvas.drawText(AppLanguage.text(R.string.timeline_text_lane), visible.left + 4f * density, textLaneTop + 12f * density, paint)
        richDoc.overlays.forEach { overlay ->
            val drag = richBlockDrag?.takeIf { !it.subtitle && it.id == overlay.id }
            val deltaMs = drag?.let { it.deltaFrames * 1000L / 30L } ?: 0L
            val startMs = when { drag == null -> overlay.startMs; drag.resizeEnd -> overlay.startMs; else -> (overlay.startMs + deltaMs).coerceAtLeast(0L) }
            val durationMs = if (drag?.resizeEnd == true) (overlay.durationMs + deltaMs).coerceAtLeast(100L) else overlay.durationMs
            val x0 = xForFrame(startMs * 30L / 1000L)
            val x1 = max(x0 + 10f * density, xForFrame((startMs + durationMs) * 30L / 1000L))
            if (x1 >= visible.left && x0 <= visible.right) {
                paint.color = 0xCC8A6E00.toInt()
                val block = RectF(x0, textLaneTop + 3f * density, x1, textLaneTop + 29f * density)
                canvas.drawRoundRect(block, 4f * density, 4f * density, paint)
                paint.color = 0xFFFFF3B0.toInt(); paint.strokeWidth = 1.5f * density
                canvas.drawLine(block.right - 4f * density, block.top + 4f * density, block.right - 4f * density, block.bottom - 4f * density, paint)
                paint.textSize = 9f * density
                canvas.save(); canvas.clipRect(block)
                canvas.drawText(overlay.text.ifBlank { "Text overlay" }, block.left + 4f * density, block.centerY() + 3f * density, paint)
                canvas.restore()
            }
        }
        val subtitleLaneTop = textLaneTop + 34f * density
        paint.color = 0xFF7DD3FC.toInt(); paint.textSize = 9f * density
        canvas.drawText(AppLanguage.text(R.string.timeline_subtitle_lane), visible.left + 4f * density, subtitleLaneTop + 12f * density, paint)
        richDoc.subtitles.forEach { cue ->
            val drag = richBlockDrag?.takeIf { it.subtitle && it.id == cue.id }
            val deltaMs = drag?.let { it.deltaFrames * 1000L / 30L } ?: 0L
            val startMs = when { drag == null -> cue.startMs; drag.resizeEnd -> cue.startMs; else -> (cue.startMs + deltaMs).coerceAtLeast(0L) }
            val endMs = if (drag?.resizeEnd == true) (cue.endMs + deltaMs).coerceAtLeast(startMs + 100L) else startMs + (cue.endMs - cue.startMs)
            val x0 = xForFrame(startMs * 30L / 1000L)
            val x1 = max(x0 + 10f * density, xForFrame(endMs * 30L / 1000L))
            if (x1 >= visible.left && x0 <= visible.right) {
                paint.color = 0xCC075985.toInt()
                val block = RectF(x0, subtitleLaneTop + 3f * density, x1, subtitleLaneTop + 29f * density)
                canvas.drawRoundRect(block, 4f * density, 4f * density, paint)
                paint.color = 0xFFE0F2FE.toInt(); paint.strokeWidth = 1.5f * density
                canvas.drawLine(block.right - 4f * density, block.top + 4f * density, block.right - 4f * density, block.bottom - 4f * density, paint)
                paint.textSize = 9f * density
                canvas.save(); canvas.clipRect(block)
                canvas.drawText(cue.text, block.left + 4f * density, block.centerY() + 3f * density, paint)
                canvas.restore()
            }
        }

        // Markers on ruler
        for (mk in richDoc.markers) {
            val mx = xForFrame((mk.positionMs * 30L) / 1000L)
            if (mx >= visible.left - 12 * density && mx <= visible.right + 12 * density) {
                paint.color = runCatching { Color.parseColor(mk.colorHex) }.getOrDefault(0xFF4F9CF9.toInt())
                val d = 4.5f * density
                tempPath.reset()
                tempPath.moveTo(mx, rulerHeight - 13f * density)
                tempPath.lineTo(mx + d, rulerHeight - 8f * density)
                tempPath.lineTo(mx, rulerHeight - 3f * density)
                tempPath.lineTo(mx - d, rulerHeight - 8f * density)
                tempPath.close()
                canvas.drawPath(tempPath, paint)
            }
        }

        for ((lane, track) in tracks.withIndex()) {
            paint.color = StudioUi.muted
            paint.textSize = 10f * density
            canvas.drawText(AppLanguage.text(R.string.message_88e5aebc23, track), visible.left + 4f * density, rulerHeight + lane * laneHeight + 12 * density, paint)
        }

        // Draw clips
        val padding = ceil(1.0 / pixelsPerFrame).toLong().coerceAtLeast(1L)
        val left = (frameAt(visible.left.toFloat()) - padding).coerceAtLeast(0L)
        val rightFrame = frameAt(visible.right.toFloat())
        val right = if (Long.MAX_VALUE - rightFrame < padding) Long.MAX_VALUE else rightFrame + padding
        val candidates = clipIndex.intersecting(left, right, movingClip?.let { clipIndicesById[it.id] })
        for (index in candidates) {
            val clip = clips[index]
            val rect = clipRect(clip)
            if (movingClip?.id == clip.id) rect.offset(moveDelta, 0f)
            if (rect.right < visible.left || rect.left > visible.right) continue
            val fx = richDoc.fxFor(clip.id)
            val isAudioTrack = clip.trackId.startsWith("a", ignoreCase = true)

            paint.style = Paint.Style.FILL
            paint.color = when {
                clip.id == selectedId -> StudioUi.selection
                isAudioTrack -> 0xFF1A463B.toInt()
                else -> StudioUi.raised
            }
            canvas.drawRoundRect(rect, 7 * density, 7 * density, paint)

            canvas.save()
            tempPath.reset()
            tempPath.addRoundRect(rect, 7 * density, 7 * density, Path.Direction.CW)
            canvas.clipPath(tempPath)

            // Video thumbnail filmstrip
            val thumbs = if (!isAudioTrack) TimelineMediaAnalyzer.cachedThumbnails(clip) else null
            if (!thumbs.isNullOrEmpty() && rect.width() > 18f * density) {
                val tileW = (rect.height() * 16f / 9f).coerceAtLeast(24f * density)
                var cx = rect.left
                var tIdx = 0
                while (cx < rect.right) {
                    val bmp = thumbs[tIdx % thumbs.size]
                    if (!bmp.isRecycled) {
                        tempRect.set(cx, rect.top, cx + tileW, rect.bottom)
                        canvas.drawBitmap(bmp, null, tempRect, thumbPaint)
                    }
                    cx += tileW
                    tIdx++
                }
                paint.color = 0x7A0B0F14
                canvas.drawRect(rect, paint)
            }

            // Audio waveform bars
            val peaks = fx.audioPeaks.ifEmpty { TimelineMediaAnalyzer.cachedAudio(clip)?.peaks.orEmpty() }
            if (peaks.isNotEmpty() && rect.width() > 14f * density) {
                val innerL = rect.left + 3f * density
                val innerR = rect.right - 3f * density
                val spanW = (innerR - innerL).coerceAtLeast(1f)
                val bars = min(peaks.size, max(4, (spanW / (3.2f * density)).toInt()))
                val midY = if (isAudioTrack) rect.centerY() else rect.bottom - 9f * density
                val maxAmp = if (isAudioTrack) rect.height() * 0.38f else 7f * density
                wavePaint.alpha = if (fx.muted) 70 else 210
                for (b in 0 until bars) {
                    val pIdx = (b * peaks.size) / bars
                    val amp = (peaks[pIdx] * maxAmp * fx.volume.coerceIn(0.2f, 1.5f)).coerceIn(1.5f * density, maxAmp)
                    val bx = innerL + ((b + 0.5f) * spanW / bars)
                    canvas.drawLine(bx, midY - amp, bx, midY + amp, wavePaint)
                }
            }

            // Transition wedge on left corner
            if (fx.transitionIn != "none" && fx.transitionMs > 0L && rect.width() > 16f * density) {
                val w = min(13f * density, rect.width() * 0.28f)
                paint.color = 0xB8F4B43E.toInt()
                tempPath.reset()
                tempPath.moveTo(rect.left, rect.top)
                tempPath.lineTo(rect.left + w, rect.top)
                tempPath.lineTo(rect.left, rect.top + w)
                tempPath.close()
                canvas.drawPath(tempPath, paint)
            }

            // Top bar & trim handles
            paint.color = if (clip.id == selectedId) StudioUi.accent else StudioUi.muted
            canvas.drawRect(rect.left, rect.top, rect.right, rect.top + 3 * density, paint)
            if (clip.id == selectedId && rect.width() > 30 * density) {
                canvas.drawRoundRect(RectF(rect.left + 3 * density, rect.top + 14 * density, rect.left + 6 * density, rect.bottom - 8 * density), density, density, paint)
                canvas.drawRoundRect(RectF(rect.right - 6 * density, rect.top + 14 * density, rect.right - 3 * density, rect.bottom - 8 * density), density, density, paint)
            }

            // Clip label + badges
            val baseName = clipNames[clip.id] ?: clip.id
            val badges = buildString {
                if (abs(fx.speed - 1.0) > 0.02) append(" · ${String.format(java.util.Locale.US, "%.1fx", fx.speed)}")
                if (fx.reversed) append(" · REV")
                if (fx.freezeMs >= 0L) append(" · FRZ")
                if (fx.muted) append(" · MUTE")
                if (clip.gradeActive) append(" · COL")
            }
            paint.color = StudioUi.text
            paint.textSize = 11f * density
            canvas.drawText(baseName + badges, rect.left + 10 * density, rect.top + 19 * density, paint)
            paint.textSize = 9f * density
            canvas.drawText(TimelineGeometry.timecode(clip.durationFrames), rect.left + 10 * density, rect.top + 36 * density, paint)
            canvas.restore()

            if (clip.id == selectedId) {
                paint.color = StudioUi.accent
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 1.6f * density
                canvas.drawRoundRect(rect, 7 * density, 7 * density, paint)
                paint.style = Paint.Style.FILL
            }
            if (!fx.transformKeyframes.isEmpty()) {
                paint.color = 0xFFFFD166.toInt()
                val radius = 3.5f * density
                fx.transformKeyframes.forEach { key ->
                    val keyFrame = clip.startFrame + (key.timeMs.coerceAtLeast(0L) * 30L / 1000L)
                    val x = xForFrame(keyFrame) + if (movingClip?.id == clip.id) moveDelta else 0f
                    if (x in (visible.left - radius)..(visible.right + radius)) {
                        val y = rect.top + 9f * density
                        marker.reset()
                        marker.moveTo(x, y - radius); marker.lineTo(x + radius, y)
                        marker.lineTo(x, y + radius); marker.lineTo(x - radius, y); marker.close()
                        canvas.drawPath(marker, paint)
                    }
                }
            }
            if (fx.speedKeyframes.size >= 2) {
                val keys = fx.speedKeyframes.sortedBy { it.timeMs }
                tempPath.reset()
                keys.forEachIndexed { keyIndex, key ->
                    val frame = clip.startFrame + key.timeMs.coerceAtLeast(0L) * 30L / 1000L
                    val x = xForFrame(frame) + if (movingClip?.id == clip.id) moveDelta else 0f
                    val ratio = ((key.speed - 0.25f) / 3.75f).coerceIn(0f, 1f)
                    val y = rect.bottom - 5f * density - ratio * (rect.height() - 10f * density)
                    if (keyIndex == 0) tempPath.moveTo(x, y) else tempPath.lineTo(x, y)
                    marker.reset(); marker.moveTo(x, y - 3f * density); marker.lineTo(x + 3f * density, y)
                    marker.lineTo(x, y + 3f * density); marker.lineTo(x - 3f * density, y); marker.close()
                    paint.color = 0xFFB794F4.toInt(); canvas.drawPath(marker, paint)
                }
                paint.color = 0xFFB794F4.toInt(); paint.style = Paint.Style.STROKE; paint.strokeWidth = 2f * density
                canvas.drawPath(tempPath, paint); paint.style = Paint.Style.FILL
            }
        }

        // Magnetic snap guide line
        if (snapGuideFrame >= 0L) {
            val sx = xForFrame(snapGuideFrame)
            canvas.drawLine(sx, rulerHeight, sx, height.toFloat(), snapGuidePaint)
        }

        // Playhead vertical red line down to height (tested at height - 2 in TimelinePlayheadTest)
        val headX = xForFrame(playheadFrame)
        paint.color = red
        paint.strokeWidth = 2f * density
        canvas.drawLine(headX, 18 * density, headX, height.toFloat(), paint)
        marker.reset()
        marker.moveTo(headX - 6 * density, 17 * density)
        marker.lineTo(headX + 6 * density, 17 * density)
        marker.lineTo(headX, 25 * density)
        marker.close()
        canvas.drawPath(marker, paint)
    }

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent) = true
        override fun onSingleTapUp(e: MotionEvent): Boolean {
            if (maxPointerCount > 1) return false
            performClick()
            findKeyframeAt(e.x, e.y)?.let { (clip, frame) ->
                selectedId = clip.id
                onKeyframeClick?.invoke(clip, frame)
                onSeek(clip.trackId, frame.coerceIn(0L, endFrame))
                invalidate()
                return true
            }
            if (e.y < rulerHeight) {
                val tappedMarker = findMarkerAt(e.x)
                if (tappedMarker != null) {
                    onMarkerClick?.invoke(tappedMarker)
                    val mFrame = ((tappedMarker.positionMs * 30L) / 1000L).coerceIn(0L, endFrame)
                    (selectedTrack ?: tracks.firstOrNull())?.let { onSeek(it, mFrame) }
                    return true
                }
            }
            trackAt(e.y)?.let { onSeek(it, frameAt(e.x)) }
            return true
        }

        override fun onLongPress(e: MotionEvent) {
            if (maxPointerCount > 1 || e.y < rulerHeight || trimClip != null) return
            richBlockDrag = findRichBlockAt(e.x, e.y)
            if (richBlockDrag != null) {
                parent?.requestDisallowInterceptTouchEvent(true)
                performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
                return
            }
            movingClip = clips.lastOrNull { clipRect(it).contains(e.x, e.y) }
            if (movingClip != null) {
                movingTargetTrack = movingClip?.trackId
                snapGuideFrame = -1L
                lastHapticSnapFrame = -1L
                parent?.requestDisallowInterceptTouchEvent(true)
                performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            }
        }
    })

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isEnabled || (clips.isEmpty() && richDoc.overlays.isEmpty() && richDoc.subtitles.isEmpty())) return false
        maxPointerCount = max(maxPointerCount, event.pointerCount)
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            maxPointerCount = 1
            pointerDownTimeMs = System.currentTimeMillis()
            pinchAccumulatedDelta = 0f
        }

        scaleDetector.onTouchEvent(event)
        if (scaleDetector.isInProgress) {
            if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
                maybeFireMultiFingerHistory()
                maxPointerCount = 1
            }
            return true
        }

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                requestFocus()
                downX = event.x
                val selClip = clips.firstOrNull { it.id == selectedId }
                if (selClip != null && isInTrimHandle(selClip, event.x, event.y, true)) {
                    trimClip = selClip
                    trimLeftEdge = true
                    trimDeltaFrames = 0L
                    parent?.requestDisallowInterceptTouchEvent(true)
                    return true
                }
                if (selClip != null && isInTrimHandle(selClip, event.x, event.y, false)) {
                    trimClip = selClip
                    trimLeftEdge = false
                    trimDeltaFrames = 0L
                    parent?.requestDisallowInterceptTouchEvent(true)
                    return true
                }
                draggingHead = event.y < rulerHeight && abs(event.x - xForFrame(playheadFrame)) < 22f * density
                if (draggingHead) {
                    parent?.requestDisallowInterceptTouchEvent(true)
                    onScrub(frameAt(event.x), false)
                    return true
                }
            }
            MotionEvent.ACTION_MOVE -> {
                richBlockDrag?.let { drag ->
                    drag.deltaFrames = ((event.x - drag.downX) / pixelsPerFrame).roundToLong()
                    invalidate()
                    return true
                }
                if (draggingHead) {
                    setPlayhead(frameAt(event.x))
                    onScrub(playheadFrame, false)
                    return true
                }
                trimClip?.let {
                    trimDeltaFrames = ((event.x - downX) / pixelsPerFrame).roundToLong()
                    invalidate()
                    return true
                }
                movingClip?.let { clip ->
                    val rawDelta = event.x - downX
                    val rawProposed = (clip.startFrame + (rawDelta / pixelsPerFrame).roundToLong()).coerceAtLeast(0L)
                    val decision = if (richDoc.snapEnabled) onEvaluateSnap?.invoke(clip, rawProposed, pixelsPerFrame) else null
                    val finalFrame = decision?.finalFrame ?: rawProposed
                    moveDelta = ((finalFrame - clip.startFrame) * pixelsPerFrame).toFloat()
                    snapGuideFrame = if (decision?.snapped == true) decision.snapGuideFrame else -1L
                    if (decision?.hapticPulse == true && decision.snapGuideFrame != lastHapticSnapFrame) {
                        lastHapticSnapFrame = decision.snapGuideFrame
                        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
                    } else if (decision?.snapped != true) {
                        lastHapticSnapFrame = -1L
                    }
                    trackAt(event.y)?.let { candidateTrack ->
                        val sameKind = candidateTrack.startsWith("a", true) == clip.trackId.startsWith("a", true)
                        if (sameKind) movingTargetTrack = candidateTrack
                    }
                    invalidate()
                    return true
                }
            }
            MotionEvent.ACTION_UP -> {
                richBlockDrag?.let { drag ->
                    val deltaMs = drag.deltaFrames * 1000L / 30L
                    val start = if (drag.resizeEnd) drag.startMs else (drag.startMs + deltaMs).coerceAtLeast(0L)
                    val end = if (drag.resizeEnd) (drag.endMs + deltaMs).coerceAtLeast(start + 100L)
                        else start + (drag.endMs - drag.startMs).coerceAtLeast(100L)
                    if (deltaMs != 0L) onEditRichBlock?.invoke(drag.id, drag.subtitle, start, end)
                    richBlockDrag = null
                    parent?.requestDisallowInterceptTouchEvent(false)
                    invalidate()
                    performClick()
                    return true
                }
                if (maybeFireMultiFingerHistory()) {
                    draggingHead = false; movingClip = null; trimClip = null; snapGuideFrame = -1L
                    maxPointerCount = 1; invalidate(); return true
                }
                if (draggingHead) {
                    draggingHead = false
                    performClick()
                    onScrub(frameAt(event.x), true)
                    return true
                }
                trimClip?.let { clip ->
                    val left = trimLeftEdge
                    val delta = trimDeltaFrames
                    trimClip = null; trimDeltaFrames = 0L; invalidate()
                    if (delta != 0L) onTrimClip?.invoke(clip, left, delta)
                    return true
                }
                movingClip?.let { clip ->
                    gestures.onTouchEvent(event)
                    val proposed = (clip.startFrame + (moveDelta / pixelsPerFrame).roundToLong()).coerceAtLeast(0L)
                    val targetTrack = movingTargetTrack ?: clip.trackId
                    movingClip = null; movingTargetTrack = null; moveDelta = 0f; snapGuideFrame = -1L; lastHapticSnapFrame = -1L
                    invalidate()
                    if (targetTrack != clip.trackId && onMoveToTrack != null) {
                        onMoveToTrack?.invoke(clip, targetTrack, proposed)
                    } else {
                        onMove(clip, proposed)
                    }
                    return true
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                gestures.onTouchEvent(event)
                draggingHead = false; movingClip = null; movingTargetTrack = null
                trimClip = null; trimDeltaFrames = 0L; richBlockDrag = null
                moveDelta = 0f; snapGuideFrame = -1L; lastHapticSnapFrame = -1L
                onScrubCancel(); invalidate(); return true
            }
        }
        gestures.onTouchEvent(event)
        return true
    }

    private fun maybeFireMultiFingerHistory(): Boolean {
        val elapsed = System.currentTimeMillis() - pointerDownTimeMs
        if (maxPointerCount >= 2 && elapsed < 380L && pinchAccumulatedDelta < 0.04f && !draggingHead && movingClip == null && trimClip == null) {
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            if (maxPointerCount >= 3) onThreeFingerRedo?.invoke() else onTwoFingerUndo?.invoke()
            return true
        }
        return false
    }

    override fun performClick(): Boolean { super.performClick(); return true }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val frame = when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT -> (playheadFrame - 1L).coerceAtLeast(0L)
            KeyEvent.KEYCODE_DPAD_RIGHT -> (playheadFrame + 1L).coerceAtMost(endFrame)
            KeyEvent.KEYCODE_MOVE_HOME -> 0L
            KeyEvent.KEYCODE_MOVE_END -> endFrame
            else -> return super.onKeyDown(keyCode, event)
        }
        (selectedTrack ?: tracks.firstOrNull())?.let { onSeek(it, frame) }
        return true
    }

    override fun dispatchHoverEvent(event: MotionEvent): Boolean {
        val manager = context.getSystemService(Context.ACCESSIBILITY_SERVICE) as android.view.accessibility.AccessibilityManager
        if (!manager.isEnabled || !manager.isTouchExplorationEnabled) return super.dispatchHoverEvent(event)
        val next = if (event.actionMasked == MotionEvent.ACTION_HOVER_EXIT) View.NO_ID else
            clips.indexOfFirst { clipRect(it).contains(event.x, event.y) }.let { if (it < 0) View.NO_ID else it + 1 }
        if (next != hoveredVirtualId) {
            for ((id, type) in listOf(
                hoveredVirtualId to AccessibilityEvent.TYPE_VIEW_HOVER_EXIT,
                next to AccessibilityEvent.TYPE_VIEW_HOVER_ENTER,
            )) {
                if (id == View.NO_ID) continue
                val changed = AccessibilityEvent.obtain(type)
                changed.setSource(this, id); changed.packageName = context.packageName
                parent?.requestSendAccessibilityEvent(this, changed)
            }
            hoveredVirtualId = next
        }
        return next != View.NO_ID || event.actionMasked == MotionEvent.ACTION_HOVER_EXIT
    }

    private val nodes = object : AccessibilityNodeProvider() {
        override fun createAccessibilityNodeInfo(id: Int): AccessibilityNodeInfo? {
            if (id == View.NO_ID) return AccessibilityNodeInfo.obtain(this@TimelineView).also { info ->
                onInitializeAccessibilityNodeInfo(info)
                clips.indices.forEach { info.addChild(this@TimelineView, it + 1) }
                info.contentDescription = AppLanguage.text(R.string.message_97a400a822, TimelineGeometry.timecode(playheadFrame))
            }
            val clip = clips.getOrNull(id - 1) ?: return null
            val bounds = Rect(); clipRect(clip).roundOut(bounds)
            return AccessibilityNodeInfo.obtain().also { info ->
                info.setSource(this@TimelineView, id); info.setParent(this@TimelineView)
                info.packageName = context.packageName; info.className = "android.widget.Button"
                info.contentDescription = "${clip.trackId} · ${clip.id} · ${TimelineGeometry.timecode(clip.startFrame)}"
                info.isEnabled = isEnabled; info.isClickable = true; info.isFocusable = true
                info.isAccessibilityFocused = focusedVirtualId == id
                info.setBoundsInParent(bounds)
                val location = IntArray(2); getLocationOnScreen(location); bounds.offset(location[0], location[1]); info.setBoundsInScreen(bounds)
                val visible = Rect()
                val localBounds = Rect(); clipRect(clip).roundOut(localBounds)
                info.isVisibleToUser = isShown && getLocalVisibleRect(visible) && Rect.intersects(visible, localBounds)
                info.isSelected = clip.id == selectedId
                info.addAction(AccessibilityNodeInfo.ACTION_CLICK)
                info.addAction(if (focusedVirtualId == id) AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS else AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS)
            }
        }
        override fun performAction(id: Int, action: Int, arguments: Bundle?): Boolean {
            val clip = clips.getOrNull(id - 1) ?: return false
            if (!isEnabled) return false
            if (action == AccessibilityNodeInfo.ACTION_CLICK) { onSeek(clip.trackId, clip.startFrame); return true }
            if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS || action == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
                focusedVirtualId = if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) id else View.NO_ID
                val event = AccessibilityEvent.obtain(if (focusedVirtualId == id) AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED else AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED)
                event.setSource(this@TimelineView, id); event.packageName = context.packageName
                parent?.requestSendAccessibilityEvent(this@TimelineView, event)
                return true
            }
            return false
        }
    }
    override fun getAccessibilityNodeProvider(): AccessibilityNodeProvider = nodes
}
