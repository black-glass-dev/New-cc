package com.owncut.app

import android.content.Context
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.ViewConfiguration
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.TextView
import kotlin.math.abs
import kotlin.math.roundToInt

/** Relative numeric input; NOT a slider. Vertical gestures belong to the controls scroller. */
internal class GradeDragValue(context: Context, private val scale: ColorSliderScale) : TextView(context) {
    var onValue: ((Double) -> Unit)?=null
    var parameterName=""
    private var current=0.0
    private var startX=0f
    private var startY=0f
    private var startProgress=0
    private var captured=false
    private var dragging=false
    private var feedback=0f
    private var animator: android.animation.ValueAnimator?=null
    private val feedbackInk=android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
    private val quiet=Runnable { animateFeedback(0f) }
    private fun animateFeedback(target: Float) {
        animator?.cancel()
        if(!android.animation.ValueAnimator.areAnimatorsEnabled()) { feedback=target;invalidate();return }
        animator=android.animation.ValueAnimator.ofFloat(feedback,target).apply {
            duration=if(target>0) 75L else 150L
            addUpdateListener { feedback=it.animatedValue as Float;invalidate() };start()
        }
    }
    override fun onDraw(canvas: android.graphics.Canvas) {
        val d=resources.displayMetrics.density
        feedbackInk.color=(StudioUi.accent and 0x00ffffff) or (((12+feedback*28).toInt()) shl 24)
        canvas.drawRoundRect(d*3,d*6,width-d*3,height-d*6,d*8,d*8,feedbackInk)
        if(feedback>0) {
            feedbackInk.color=(StudioUi.accent and 0x00ffffff) or ((feedback*150).toInt() shl 24)
            canvas.drawRoundRect(d*12,height-d*7,width-d*12,height-d*5,d,d,feedbackInk)
        }
        super.onDraw(canvas)
    }
    override fun onDetachedFromWindow() {
        removeCallbacks(quiet);animator?.cancel();feedback=0f;dragging=false;captured=false;isPressed=false
        super.onDetachedFromWindow()
    }
    init {
        gravity=android.view.Gravity.CENTER;typeface=android.graphics.Typeface.MONOSPACE
        textSize=15f;setTextColor(StudioUi.accent);background=android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT)
        isFocusable=true;isClickable=true
        minimumHeight=StudioUi.dp(context,48)
    }
    fun bind(value: Double) {
        if(text.isNotEmpty() && value==current) return
        if(isAttachedToWindow && !isPressed) { removeCallbacks(quiet);animateFeedback(1f);postDelayed(quiet,90) }
        current=value;text=String.format(java.util.Locale.ROOT,"%.3f",value)
        contentDescription="$parameterName $text"
    }
    fun step(delta: Int) = emit((scale.progress(current)+delta).coerceIn(0,ColorSliderScale.STEPS))
    private fun emit(progress: Int) {
        val value=scale.value(progress)
        if(value!=current) { bind(value);onValue?.invoke(value) }
    }
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if(!isEnabled || (event.actionMasked!=MotionEvent.ACTION_DOWN && !captured)) return false
        when(event.actionMasked) {
            MotionEvent.ACTION_DOWN -> { startX=event.x;startY=event.y;startProgress=scale.progress(current);captured=true;dragging=false;isPressed=true;removeCallbacks(quiet);animateFeedback(1f);return true }
            MotionEvent.ACTION_MOVE -> {
                val dx=event.x-startX;val dy=event.y-startY
                val slop=ViewConfiguration.get(context).scaledTouchSlop
                if(!dragging && abs(dy)>slop && abs(dy)>abs(dx)) { captured=false;isPressed=false;animateFeedback(0f);return false }
                if(abs(dx)>slop) { dragging=true;parent?.requestDisallowInterceptTouchEvent(true) }
                if(dragging) emit((startProgress+(dx/resources.displayMetrics.density*5).roundToInt()).coerceIn(0,ColorSliderScale.STEPS))
                return true
            }
            MotionEvent.ACTION_UP -> { captured=false;isPressed=false;animateFeedback(0f);parent?.requestDisallowInterceptTouchEvent(false);if(!dragging) performClick();return true }
            MotionEvent.ACTION_CANCEL,MotionEvent.ACTION_POINTER_DOWN,MotionEvent.ACTION_POINTER_UP -> { captured=false;dragging=false;isPressed=false;animateFeedback(0f);parent?.requestDisallowInterceptTouchEvent(false);return true }
        }
        return super.onTouchEvent(event)
    }
    override fun performClick(): Boolean { super.performClick();return true }
    override fun onKeyDown(keyCode: Int,event: KeyEvent): Boolean = when(keyCode) {
        KeyEvent.KEYCODE_DPAD_LEFT -> { step(-1);true }
        KeyEvent.KEYCODE_DPAD_RIGHT -> { step(1);true }
        else -> super.onKeyDown(keyCode,event)
    }
    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.rangeInfo=AccessibilityNodeInfo.RangeInfo.obtain(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT,scale.low.toFloat(),scale.high.toFloat(),current.toFloat())
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD)
    }
    override fun performAccessibilityAction(action: Int,arguments: Bundle?): Boolean {
        if(!isEnabled) return false
        return when(action) {
            AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.id -> {
                if(arguments==null || !arguments.containsKey(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE)) false
                else {
                    val requested=arguments.getFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE).toDouble()
                    if(!requested.isFinite()) false else { emit(scale.progress(requested.coerceIn(scale.low,scale.high)));true }
                }
            }
            AccessibilityNodeInfo.ACTION_SCROLL_FORWARD -> { step(1);true }
            AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD -> { step(-1);true }
            else -> super.performAccessibilityAction(action,arguments)
        }
    }
}
