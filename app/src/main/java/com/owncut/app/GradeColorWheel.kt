package com.owncut.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.graphics.SweepGradient
import android.view.MotionEvent
import android.view.View

/** Original color-disc artwork and pointer transport. Native C++ owns RGB mapping. */
internal class GradeColorWheel(context: Context) : View(context) {
    var onPoint: ((Double,Double) -> Unit)?=null
    var onBegin: (() -> Unit)?=null
    private val ink=Paint(Paint.ANTI_ALIAS_FLAG)
    private var px=0f
    private var py=0f
    private var captured=false
    private var drag: WheelDragSession?=null
    private var pressure=0f
    private var pulse: android.animation.ValueAnimator?=null
    private var hue: Shader?=null
    private var neutral: Shader?=null
    init { isFocusable=false;minimumHeight=StudioUi.dp(context,140);importantForAccessibility=IMPORTANT_FOR_ACCESSIBILITY_NO }
    fun bind(point: DoubleArray) {
        require(point.size==2)
        val x=point[0].toFloat();val y=point[1].toFloat()
        if(px==x && py==y) return
        px=x;py=y;invalidate()
    }
    private fun radius()=minOf(width,height)*0.43f
    override fun onSizeChanged(w: Int,h: Int,oldw: Int,oldh: Int) {
        super.onSizeChanged(w,h,oldw,oldh)
        val x=w/2f;val y=h/2f;val r=radius()
        if(r<=0) return
        hue=SweepGradient(x,y,WheelPalette.colors(),null)
        neutral=RadialGradient(x,y,r,intArrayOf(StudioUi.surface,StudioUi.surface and 0x00ffffff),null,Shader.TileMode.CLAMP)
    }
    override fun onDraw(canvas: Canvas) {
        val x=width/2f;val y=height/2f;val r=radius()
        if(r<=0) return
        val density=resources.displayMetrics.density
        ink.style=Paint.Style.FILL;ink.color=StudioUi.surface;canvas.drawCircle(x,y,r,ink)
        ink.shader=hue;ink.alpha=70
        canvas.drawCircle(x,y,r-density*4,ink)
        ink.alpha=255;ink.shader=neutral
        canvas.drawCircle(x,y,r-density*4,ink);ink.shader=null
        // A precise chromatic perimeter and quiet interior; no pasted reference artwork.
        ink.style=Paint.Style.STROKE;ink.strokeWidth=density*2.5f;ink.shader=hue
        canvas.drawCircle(x,y,r,ink);ink.shader=null;ink.style=Paint.Style.FILL
        ink.color=StudioUi.muted;ink.strokeWidth=resources.displayMetrics.density
        canvas.drawLine(x-5,y,x+5,y,ink);canvas.drawLine(x,y-5,x,y+5,ink)
        ink.style=Paint.Style.STROKE;ink.color=StudioUi.border;canvas.drawCircle(x,y,r-density*5,ink)
        if(kotlin.math.hypot(px,py)>0.02f) {
            ink.color=(StudioUi.text and 0x00ffffff) or 0x55000000
            canvas.drawLine(x,y,x+px*r,y+py*r,ink)
        }
        if(pressure>0) {
            ink.style=Paint.Style.FILL;ink.color=(StudioUi.accent and 0x00ffffff) or ((pressure*42).toInt() shl 24)
            canvas.drawCircle(x+px*r,y+py*r,resources.displayMetrics.density*(12+pressure*4),ink)
            ink.style=Paint.Style.STROKE
        }
        ink.style=Paint.Style.FILL;ink.color=StudioUi.background
        canvas.drawCircle(x+px*r,y+py*r,density*6,ink)
        ink.style=Paint.Style.STROKE;ink.strokeWidth=density*1.7f;ink.color=Color.WHITE
        canvas.drawCircle(x+px*r,y+py*r,resources.displayMetrics.density*6,ink)
        ink.style=Paint.Style.FILL
    }
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if(!isEnabled || radius()<=0) return false
        if(event.actionMasked==MotionEvent.ACTION_DOWN) {
            val dx=event.x-width/2f;val dy=event.y-height/2f
            if(dx*dx+dy*dy>radius()*radius()) return false
            captured=true;drag=WheelDragSession(px.toDouble(),py.toDouble(),event.x.toDouble(),event.y.toDouble(),radius().toDouble())
            parent?.requestDisallowInterceptTouchEvent(true);onBegin?.invoke();animatePressure(1f)
        }
        if(!captured) return false
        when(event.actionMasked) {
            MotionEvent.ACTION_POINTER_DOWN,MotionEvent.ACTION_POINTER_UP -> {
                captured=false;drag=null;animatePressure(0f);parent?.requestDisallowInterceptTouchEvent(false)
            }
            MotionEvent.ACTION_MOVE -> {
                val point=drag?.point(event.x.toDouble(),event.y.toDouble()) ?: return true
                onPoint?.invoke(point[0],point[1])
            }
            MotionEvent.ACTION_UP,MotionEvent.ACTION_CANCEL -> {
                captured=false;drag=null;animatePressure(0f);parent?.requestDisallowInterceptTouchEvent(false)
                if(event.actionMasked==MotionEvent.ACTION_UP) performClick()
            }
        }
        return true
    }
    private fun animatePressure(target: Float) {
        pulse?.cancel()
        if(!android.animation.ValueAnimator.areAnimatorsEnabled()) { pressure=target;invalidate();return }
        pulse=android.animation.ValueAnimator.ofFloat(pressure,target).apply {
            duration=if(target>0) 70L else 120L
            addUpdateListener { pressure=it.animatedValue as Float;invalidate() };start()
        }
    }
    override fun onDetachedFromWindow() { pulse?.cancel();pressure=0f;drag=null;captured=false;super.onDetachedFromWindow() }
    override fun performClick(): Boolean { super.performClick();return true }
}
