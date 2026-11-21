package com.owncut.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.min

/** Original rotary frame scrub control. One full revolution moves one second at 30 fps. */
internal class VirtualJogDialView(context: Context) : View(context) {
    private val density = resources.displayMetrics.density
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private var lastAngle = 0.0
    private var fractionalFrames = 0.0
    private var accumulatedFrames = 0L
    var onDelta: (Long, Boolean) -> Unit = { _, _ -> }

    init {
        isFocusable = true
        isClickable = true
        contentDescription = AppLanguage.text(R.string.timeline_jog_dial)
        minimumWidth = (120 * density).toInt()
        minimumHeight = (120 * density).toInt()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val size = (160 * density).toInt()
        setMeasuredDimension(resolveSize(size, widthMeasureSpec), resolveSize(size, heightMeasureSpec))
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = width / 2f
        val cy = height / 2f
        val radius = min(width, height) * 0.39f
        paint.style = Paint.Style.FILL
        paint.color = StudioUi.raised
        canvas.drawCircle(cx, cy, radius, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 2f * density
        paint.color = StudioUi.accent
        canvas.drawCircle(cx, cy, radius, paint)
        for (index in 0 until 24) {
            val angle = (index * 2.0 * PI / 24.0) - PI / 2.0
            val inner = radius - if (index % 3 == 0) 12f * density else 7f * density
            val outer = radius - 2f * density
            paint.strokeWidth = if (index % 3 == 0) 2f * density else density
            canvas.drawLine(cx + kotlin.math.cos(angle).toFloat() * inner,
                cy + kotlin.math.sin(angle).toFloat() * inner,
                cx + kotlin.math.cos(angle).toFloat() * outer,
                cy + kotlin.math.sin(angle).toFloat() * outer, paint)
        }
        paint.strokeWidth = 3f * density
        paint.color = StudioUi.text
        val angle = (accumulatedFrames.toDouble() / 30.0 * 2.0 * PI) - PI / 2.0
        canvas.drawLine(cx, cy, cx + kotlin.math.cos(angle).toFloat() * radius * 0.66f,
            cy + kotlin.math.sin(angle).toFloat() * radius * 0.66f, paint)
        paint.style = Paint.Style.FILL
        canvas.drawCircle(cx, cy, 5f * density, paint)
        paint.textAlign = Paint.Align.CENTER
        paint.textSize = 11f * density
        canvas.drawText("${accumulatedFrames}f", cx, cy + radius * 0.78f, paint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val cx = width / 2f
        val cy = height / 2f
        val angle = atan2((event.y - cy).toDouble(), (event.x - cx).toDouble())
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastAngle = angle
                fractionalFrames = 0.0
                accumulatedFrames = 0L
                parent?.requestDisallowInterceptTouchEvent(true)
                performClick()
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                var delta = angle - lastAngle
                if (delta > PI) delta -= 2.0 * PI
                if (delta < -PI) delta += 2.0 * PI
                lastAngle = angle
                fractionalFrames += delta * 30.0 / (2.0 * PI)
                val whole = fractionalFrames.toLong()
                if (whole != 0L) {
                    accumulatedFrames += whole
                    fractionalFrames -= whole
                    onDelta(accumulatedFrames, false)
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                onDelta(accumulatedFrames, true)
                parent?.requestDisallowInterceptTouchEvent(false)
                invalidate()
                return true
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }
}
