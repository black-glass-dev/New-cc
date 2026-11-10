package com.owncut.app

import java.util.Locale
import kotlin.math.roundToLong

/** UI projection only; native Timeline.resolve_playhead owns clip lookup/source seek mapping. */
internal object TimelineGeometry {
    const val FPS = 30L
    const val MAX_WIDTH_PX = 100_000

    fun frameForSource(clip: ClipSnapshot, sourceMs: Long): Long {
        val offset = sourceMs.coerceIn(clip.sourceInMs, clip.sourceOutMs) - clip.sourceInMs
        val frames = Math.addExact(Math.multiplyExact(offset / 1000L, FPS), (offset % 1000L * FPS + 500L) / 1000L)
        return Math.addExact(clip.startFrame, frames.coerceAtMost(clip.durationFrames))
    }

    fun timecode(frame: Long): String {
        val value = frame.coerceAtLeast(0L)
        val seconds = value / FPS
        return String.format(Locale.US, "%02d:%02d:%02d:%02d", seconds / 3600L,
            seconds / 60L % 60L, seconds % 60L, value % FPS)
    }

    fun pixelsPerFrame(end: Long, density: Float): Double =
        minOf(48.0 * density / FPS, (MAX_WIDTH_PX - 24.0 * density).coerceAtLeast(1.0) / end.coerceAtLeast(1L))

    fun pixelsPerFrame(end: Long, density: Float, zoomScale: Double): Double {
        val base = pixelsPerFrame(end, density)
        val maxFit = (MAX_WIDTH_PX - 24.0 * density).coerceAtLeast(1.0) / end.coerceAtLeast(1L)
        val safeZoom = if (zoomScale.isFinite() && zoomScale > 0.0) zoomScale else 1.0
        return (base * safeZoom).coerceIn(minOf(base, maxFit) * 0.25, maxFit.coerceAtLeast(base))
    }

    fun frameAt(x: Float, pixelsPerFrame: Double, end: Long): Long =
        (x.coerceAtLeast(0f) / pixelsPerFrame).roundToLong().coerceIn(0L, end.coerceAtLeast(0L))
}
