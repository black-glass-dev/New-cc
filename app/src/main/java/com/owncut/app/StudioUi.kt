package com.owncut.app

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.util.AttributeSet
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

internal object StudioUi {
    private var palettes: org.json.JSONObject? = null
    @Volatile private var current = emptyMap<String,Int>()
    fun initialize(context: Context) {
        if (palettes == null) palettes = org.json.JSONObject(context.assets.open("studio_themes.json").bufferedReader().use { it.readText() })
        val palette = palettes!!.getJSONObject(theme(context))
        current = palette.keys().asSequence().associateWith { Color.parseColor("#" + palette.getString(it)) }
    }
    fun theme(context: Context): String {
        val value = context.getSharedPreferences("owncut_preferences", Context.MODE_PRIVATE).getString("theme", "graphite")
        return if(value in listOf("graphite","midnight","ember")) value!! else "graphite"
    }
    fun select(context: Context, id: String) {
        require(id in listOf("graphite","midnight","ember"))
        context.getSharedPreferences("owncut_preferences",Context.MODE_PRIVATE).edit().putString("theme",id).apply()
        initialize(context.applicationContext)
    }
    private fun color(role: String) = current.getValue(role)
    val background get() = color("background")
    val surface get() = color("surface")
    val raised get() = color("raised")
    val border get() = color("border")
    val text get() = color("text")
    val muted get() = color("muted")
    val accent get() = color("accent")
    val selection get() = color("selection")
    fun dp(context: Context, value: Int) = (value * context.resources.displayMetrics.density).toInt()
    fun shape(context: Context, color: Int = surface, radius: Int = 16) = GradientDrawable().apply {
        setColor(color); cornerRadius = dp(context, radius).toFloat(); setStroke(dp(context, 1), border)
    }
    fun card(context: Context) = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        background = shape(context)
        setPadding(dp(context, 16), dp(context, 14), dp(context, 16), dp(context, 14))
    }
    fun label(context: Context, value: String, size: Float = 14f, secondary: Boolean = false) = TextView(context).apply {
        text = value; textSize = size; setTextColor(if (secondary) muted else StudioUi.text)
        setLineSpacing(dp(context, 3).toFloat(), 1f)
        setPadding(0, dp(context, 4), 0, dp(context, 4))
    }
}

internal enum class StudioIcon { HOME, EDIT, MEDIA, COLOR, FLOW, EXPORT, SPLIT, TRIM_LEFT, TRIM_RIGHT, DELETE, RIPPLE,
    UNDO, REDO, SAVE, SAVE_AS, OPEN, RECOVER, IMPORT, LINK, PLAY, FOLLOW, CANCEL, PLUS, SETTINGS, RENAME, REFRESH, INFO,
    SPEED, AUDIO, TRANSFORM, TEXT, SUBTITLES, TRANSITION, PIP, BEAT, MAGNET, MARKER, DUPLICATE, FREEZE,
    ZOOM_IN, ZOOM_OUT, MUTE, LOCK, KEYFRAME,
    J_CUT, L_CUT, SLIP, ROLL, SLIDE, VOICEOVER, EQ, COMPRESSOR, LIMITER, GATE,
    REFRAME, STABILIZE, OPTICAL_FLOW, MULTICAM, COMPOUND, ADJUSTMENT, DUAL_TIMELINE,
    JOG_DIAL, SPEED_RAMP, SOURCE_VIEW, TRACK }

/** Original stroke-vector icons: no icon font, emoji, external assets or proprietary artwork.
 * Button's text is semantic state (TalkBack/tooltip); the visual face draws only the icon.
 */
internal class StudioButton @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) : Button(context, attrs) {
    var icon = StudioIcon.INFO
        set(value) { field = value; invalidate() }
    private val ink = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND }
    init {
        val size = StudioUi.dp(context, 52)
        minimumWidth = size; minimumHeight = size
        minWidth = size; minHeight = size
        setPadding(0, 0, 0, 0)
        gravity = Gravity.CENTER
        stateListAnimator = null
        backgroundTintList = null
        background = RippleDrawable(ColorStateList.valueOf((StudioUi.accent and 0x00FFFFFF) or 0x33000000), android.graphics.drawable.ColorDrawable(Color.TRANSPARENT), null)
        isFocusable = true
    }
    override fun setText(text: CharSequence?, type: android.widget.TextView.BufferType?) {
        super.setText(text, type)
        contentDescription = text
        tooltipText = text
        invalidate()
    }
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val size = StudioUi.dp(context, 52)
        setMeasuredDimension(resolveSize(size, widthMeasureSpec), resolveSize(size, heightMeasureSpec))
    }
    override fun onDraw(canvas: Canvas) {
        // View.draw already rendered the RippleDrawable; do not draw the text label.
        val save = canvas.save()
        val scale = StudioUi.dp(context, 24) / 24f
        canvas.translate(width / 2f - 12f * scale, height / 2f - 12f * scale)
        canvas.scale(scale, scale)
        ink.strokeWidth = 1.8f
        ink.color = if (!isEnabled) StudioUi.muted else if (isSelected) StudioUi.accent else StudioUi.text
        fun line(x: Float, y: Float, xx: Float, yy: Float) = canvas.drawLine(x,y,xx,yy,ink)
        fun path(vararg values: Float) { val p = Path(); p.moveTo(values[0], values[1]); for(i in 2 until values.size step 2) p.lineTo(values[i],values[i+1]); canvas.drawPath(p,ink) }
        fun box(l: Float,t: Float,r: Float,b: Float) = canvas.drawRoundRect(RectF(l,t,r,b),2f,2f,ink)
        fun arrow(up: Boolean) { val y = if(up) 3f else 21f; val end = if(up) 8f else 16f
            line(12f,y,12f,17f); path(7f,end,12f,y,17f,end) }
        when(icon) {
            StudioIcon.COLOR -> { canvas.drawCircle(9f,9f,5f,ink);canvas.drawCircle(15f,9f,5f,ink);canvas.drawCircle(12f,15f,5f,ink) }
            StudioIcon.PLAY -> if (text.toString() == AppLanguage.text(R.string.action_pause)) {
                line(8f,5f,8f,19f); line(16f,5f,16f,19f)
            } else path(8f,4f,20f,12f,8f,20f,8f,4f)
            StudioIcon.SPLIT -> { canvas.drawCircle(6f,6f,3f,ink); canvas.drawCircle(6f,18f,3f,ink); line(8f,8f,20f,19f); line(8f,16f,20f,5f) }
            StudioIcon.TRIM_LEFT, StudioIcon.TRIM_RIGHT -> {
                val left = icon == StudioIcon.TRIM_LEFT
                val x = if(left) 5f else 19f; line(x,4f,x,20f); line(4f,12f,20f,12f)
                if(left) path(11f,7f,6f,12f,11f,17f) else path(13f,7f,18f,12f,13f,17f)
            }
            StudioIcon.DELETE -> { path(5f,7f,6f,21f,18f,21f,19f,7f); line(3f,6f,21f,6f); path(8f,6f,8f,3f,16f,3f,16f,6f); line(10f,10f,10f,17f); line(14f,10f,14f,17f) }
            StudioIcon.RIPPLE -> { line(3f,4f,3f,20f); line(21f,4f,21f,20f); path(5f,12f,10f,12f,7f,9f); path(19f,12f,14f,12f,17f,15f) }
            StudioIcon.UNDO, StudioIcon.REDO -> {
                if(icon == StudioIcon.REDO) { canvas.translate(24f,0f); canvas.scale(-1f,1f) }
                path(8f,4f,3f,9f,8f,14f); val p = Path(); p.moveTo(3f,9f); p.cubicTo(21f,2f,23f,21f,10f,20f); canvas.drawPath(p,ink)
            }
            StudioIcon.SAVE, StudioIcon.SAVE_AS -> { path(4f,3f,17f,3f,21f,7f,21f,21f,3f,21f,3f,3f,4f,3f); box(7f,3f,15f,9f); box(7f,14f,17f,21f)
                if(icon == StudioIcon.SAVE_AS) { line(17f,11f,23f,11f); line(20f,8f,20f,14f) } }
            StudioIcon.OPEN, StudioIcon.MEDIA -> { path(3f,8f,3f,4f,9f,4f,12f,7f,21f,7f,21f,19f,3f,19f,3f,8f); line(3f,10f,21f,10f) }
            StudioIcon.IMPORT -> { path(3f,15f,3f,21f,21f,21f,21f,15f); line(12f,3f,12f,16f); path(7f,11f,12f,16f,17f,11f) }
            StudioIcon.EXPORT -> { path(3f,15f,3f,21f,21f,21f,21f,15f); arrow(true) }
            StudioIcon.HOME -> { path(9f,5f,3f,12f,9f,19f); line(3f,12f,21f,12f) }
            StudioIcon.EDIT, StudioIcon.RENAME -> { path(4f,15f,15f,4f,20f,9f,9f,20f,3f,21f,4f,15f); line(12f,7f,17f,12f) }
            StudioIcon.FLOW -> { box(2f,2f,9f,9f); box(15f,15f,22f,22f); path(9f,5f,19f,5f,19f,15f) }
            StudioIcon.RECOVER, StudioIcon.REFRESH -> { canvas.drawArc(RectF(4f,4f,20f,20f),-60f,290f,false,ink); path(15f,2f,20f,5f,16f,9f)
                if(icon == StudioIcon.RECOVER) { line(12f,8f,12f,12f); line(12f,12f,15f,14f) } }
            StudioIcon.LINK -> { canvas.drawOval(RectF(2f,10f,14f,19f),ink); canvas.drawOval(RectF(10f,5f,22f,14f),ink) }
            StudioIcon.FOLLOW -> { canvas.drawCircle(12f,12f,6f,ink); canvas.drawCircle(12f,12f,2f,ink); line(12f,1f,12f,5f); line(12f,19f,12f,23f); line(1f,12f,5f,12f); line(19f,12f,23f,12f) }
            StudioIcon.CANCEL -> { box(5f,5f,19f,19f) }
            StudioIcon.PLUS -> { line(4f,12f,20f,12f); line(12f,4f,12f,20f) }
            StudioIcon.SETTINGS -> { canvas.drawCircle(12f,12f,7f,ink); canvas.drawCircle(12f,12f,2.5f,ink)
                for(i in 0..7) { canvas.save(); canvas.rotate(i*45f,12f,12f); line(12f,2f,12f,5f); canvas.restore() } }
            StudioIcon.INFO -> { canvas.drawCircle(12f,12f,9f,ink); line(12f,11f,12f,17f); canvas.drawCircle(12f,7f,.5f,ink) }
            StudioIcon.ZOOM_IN, StudioIcon.ZOOM_OUT -> {
                canvas.drawCircle(10f,10f,6f,ink); line(14.5f,14.5f,21f,21f)
                line(7f,10f,13f,10f)
                if (icon == StudioIcon.ZOOM_IN) line(10f,7f,10f,13f)
            }
            StudioIcon.MUTE -> {
                path(4f,9f,8f,9f,13f,5f,13f,19f,8f,15f,4f,15f,4f,9f)
                line(16f,8f,21f,16f); line(21f,8f,16f,16f)
            }
            StudioIcon.LOCK -> {
                canvas.drawRoundRect(RectF(4f,10f,20f,21f),2f,2f,ink)
                canvas.drawArc(RectF(7f,3f,17f,15f),180f,180f,false,ink)
            }
            StudioIcon.KEYFRAME -> path(12f,2f,22f,12f,12f,22f,2f,12f,12f,2f)
            StudioIcon.SPEED -> {
                canvas.drawArc(RectF(3f,5f,21f,23f), 180f, 180f, false, ink)
                line(12f, 14f, 17f, 8f); canvas.drawCircle(12f, 14f, 1.8f, ink)
            }
            StudioIcon.AUDIO -> {
                path(4f, 9f, 8f, 9f, 13f, 5f, 13f, 19f, 8f, 15f, 4f, 15f, 4f, 9f)
                canvas.drawArc(RectF(14f, 8f, 19f, 16f), -60f, 120f, false, ink)
                canvas.drawArc(RectF(15f, 5f, 22f, 19f), -60f, 120f, false, ink)
            }
            StudioIcon.TRANSFORM -> {
                path(4f, 8f, 4f, 4f, 8f, 4f); path(16f, 4f, 20f, 4f, 20f, 8f)
                path(4f, 16f, 4f, 20f, 8f, 20f); path(16f, 20f, 20f, 20f, 20f, 16f)
                box(8f, 8f, 16f, 16f)
            }
            StudioIcon.TEXT -> {
                line(5f, 5f, 19f, 5f); line(12f, 5f, 12f, 19f); line(8f, 19f, 16f, 19f)
            }
            StudioIcon.SUBTITLES -> {
                box(3f, 5f, 21f, 19f); line(6f, 11f, 14f, 11f); line(16f, 11f, 18f, 11f)
                line(6f, 15f, 10f, 15f); line(12f, 15f, 18f, 15f)
            }
            StudioIcon.TRANSITION -> {
                path(4f, 6f, 12f, 12f, 4f, 18f, 4f, 6f)
                path(20f, 6f, 12f, 12f, 20f, 18f, 20f, 6f)
            }
            StudioIcon.PIP -> {
                box(3f, 5f, 21f, 19f); box(12f, 11f, 19f, 17f)
            }
            StudioIcon.BEAT -> {
                line(10f, 17f, 10f, 5f); path(10f, 5f, 19f, 4f, 19f, 15f)
                canvas.drawCircle(7.5f, 17.5f, 2.5f, ink); canvas.drawCircle(16.5f, 15.5f, 2.5f, ink)
            }
            StudioIcon.MAGNET -> {
                path(6f, 4f, 6f, 13f); path(18f, 4f, 18f, 13f)
                canvas.drawArc(RectF(6f, 7f, 18f, 20f), 0f, 180f, false, ink)
                line(6f, 7f, 9f, 7f); line(15f, 7f, 18f, 7f)
            }
            StudioIcon.MARKER -> {
                path(7f, 3f, 17f, 3f, 17f, 20f, 12f, 15f, 7f, 20f, 7f, 3f)
            }
            StudioIcon.DUPLICATE -> {
                box(4f, 8f, 15f, 20f); path(9f, 8f, 9f, 4f, 20f, 4f, 20f, 15f, 15f, 15f)
            }
            StudioIcon.FREEZE -> {
                line(12f, 3f, 12f, 21f); line(4f, 7.5f, 20f, 16.5f); line(4f, 16.5f, 20f, 7.5f)
                canvas.drawCircle(12f, 12f, 2.5f, ink)
            }
            StudioIcon.J_CUT, StudioIcon.L_CUT -> {
                line(4f,6f,20f,6f); line(4f,18f,20f,18f)
                val offset = if (icon == StudioIcon.J_CUT) -4f else 4f
                line(7f,6f,7f,18f + offset); line(17f,6f,17f,18f + offset)
                canvas.drawCircle(7f,18f + offset,1.5f,ink); canvas.drawCircle(17f,18f + offset,1.5f,ink)
            }
            StudioIcon.SLIP -> {
                box(5f,5f,19f,19f); line(2f,12f,8f,12f); path(5f,9f,2f,12f,5f,15f)
                line(22f,12f,16f,12f); path(19f,9f,22f,12f,19f,15f)
            }
            StudioIcon.ROLL -> {
                line(12f,4f,12f,20f); line(3f,12f,9f,12f); path(6f,9f,3f,12f,6f,15f)
                line(21f,12f,15f,12f); path(18f,9f,21f,12f,18f,15f)
            }
            StudioIcon.SLIDE -> {
                box(8f,7f,16f,17f); line(2f,12f,7f,12f); path(5f,9f,2f,12f,5f,15f)
                line(22f,12f,17f,12f); path(19f,9f,22f,12f,19f,15f)
            }
            StudioIcon.VOICEOVER -> {
                canvas.drawRoundRect(RectF(9f,3f,15f,15f),3f,3f,ink)
                canvas.drawArc(RectF(6f,6f,18f,18f),0f,180f,false,ink); line(12f,18f,12f,22f); line(8f,22f,16f,22f)
            }
            StudioIcon.EQ -> {
                line(5f,4f,5f,20f); line(12f,4f,12f,20f); line(19f,4f,19f,20f)
                canvas.drawCircle(5f,9f,2f,ink); canvas.drawCircle(12f,15f,2f,ink); canvas.drawCircle(19f,7f,2f,ink)
            }
            StudioIcon.COMPRESSOR -> {
                val p=Path(); p.moveTo(3f,20f); p.lineTo(9f,12f); p.cubicTo(12f,8f,13f,7f,21f,5f); canvas.drawPath(p,ink)
                line(4f,4f,4f,20f); line(4f,20f,21f,20f)
            }
            StudioIcon.LIMITER -> {
                line(4f,5f,20f,5f); path(3f,15f,7f,15f,9f,8f,12f,15f,15f,9f,17f,15f,21f,15f)
                line(4f,20f,20f,20f)
            }
            StudioIcon.GATE -> {
                path(2f,12f,5f,12f,7f,7f,9f,17f,11f,12f,13f,12f)
                path(17f,12f,19f,7f,21f,17f,23f,12f)
            }
            StudioIcon.REFRAME -> {
                path(8f,3f,3f,3f,3f,8f); path(16f,3f,21f,3f,21f,8f)
                path(3f,16f,3f,21f,8f,21f); path(21f,16f,21f,21f,16f,21f)
                canvas.drawCircle(12f,12f,3f,ink)
            }
            StudioIcon.STABILIZE -> {
                path(12f,2f,20f,5f,19f,14f,12f,22f,5f,14f,4f,5f,12f,2f)
                line(8f,12f,11f,15f); line(11f,15f,16f,9f)
            }
            StudioIcon.OPTICAL_FLOW -> {
                canvas.drawCircle(5f,6f,2f,ink); canvas.drawCircle(19f,18f,2f,ink)
                path(7f,6f,12f,6f,12f,12f,19f,12f); path(16f,9f,19f,12f,16f,15f)
                path(17f,18f,12f,18f,12f,12f,5f,12f)
            }
            StudioIcon.MULTICAM -> {
                box(3f,3f,10f,10f); box(14f,3f,21f,10f); box(3f,14f,10f,21f); box(14f,14f,21f,21f)
            }
            StudioIcon.COMPOUND -> { box(3f,5f,16f,20f); box(8f,3f,21f,18f); line(11f,8f,18f,8f); line(11f,12f,18f,12f) }
            StudioIcon.ADJUSTMENT -> {
                line(4f,6f,20f,6f); line(4f,12f,20f,12f); line(4f,18f,20f,18f)
                canvas.drawCircle(9f,6f,2f,ink); canvas.drawCircle(15f,12f,2f,ink); canvas.drawCircle(7f,18f,2f,ink)
            }
            StudioIcon.DUAL_TIMELINE -> {
                box(3f,3f,21f,9f); box(3f,15f,21f,21f); line(8f,6f,13f,6f); line(13f,18f,18f,18f)
            }
            StudioIcon.JOG_DIAL -> {
                canvas.drawCircle(12f,12f,8f,ink); canvas.drawCircle(12f,12f,2f,ink)
                path(5f,6f,5f,3f,9f,3f); path(19f,18f,19f,21f,15f,21f)
            }
            StudioIcon.SPEED_RAMP -> {
                line(3f,20f,3f,4f); line(3f,20f,21f,20f)
                val p=Path(); p.moveTo(5f,18f); p.cubicTo(8f,18f,9f,15f,12f,12f); p.cubicTo(15f,9f,16f,6f,20f,5f); canvas.drawPath(p,ink)
            }
            StudioIcon.SOURCE_VIEW -> { box(2f,4f,22f,20f); path(9f,8f,16f,12f,9f,16f,9f,8f) }
            StudioIcon.TRACK -> {
                box(3f,4f,21f,9f); box(3f,11f,21f,16f); box(3f,18f,21f,22f)
            }
        }
        canvas.restoreToCount(save)
        if (isSelected || isFocused) {
            ink.color = StudioUi.accent; ink.strokeWidth = StudioUi.dp(context,1).toFloat()
            canvas.drawRoundRect(RectF(2f,2f,width-2f,height-2f),StudioUi.dp(context,12).toFloat(),StudioUi.dp(context,12).toFloat(),ink)
        }
    }
}

/** Routine progress stays contextual. Important failures/recovery also reach the quiet Edit view. */
internal class EditorStatusView(context: Context) : TextView(context) {
    var onImportant: ((String) -> Unit)? = null
    private var previousImportant: String? = null
    override fun setText(text: CharSequence?, type: BufferType?) {
        super.setText(text,type)
        val value = text?.toString().orEmpty()
        val important = listOf("failed", "could not", "cannot", "warning", "snapshot is available", "snapshot আছে", "ব্যর্থ", "হয়নি", "যায়নি", "সতর্কতা").any { value.lowercase().contains(it) }
        if(!important) previousImportant = null
        if(important && value != previousImportant) { previousImportant = value; onImportant?.invoke(value) }
    }
}
