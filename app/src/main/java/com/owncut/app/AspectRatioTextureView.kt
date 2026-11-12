package com.owncut.app

import android.content.Context
import android.view.TextureView
import kotlin.math.roundToInt

/** Keeps decoded preview pixels at their source aspect ratio inside the available workspace. */
internal class AspectRatioTextureView(context: Context) : TextureView(context) {
    private var videoWidth = 0
    private var videoHeight = 0
    private var outputAspectRatio: Float? = null

    fun setOutputAspectRatio(ratio: Float?) {
        val valid = ratio?.takeIf { it.isFinite() && it in 0.3f..3.2f }
        if (outputAspectRatio != valid) {
            outputAspectRatio = valid
            requestLayout()
        }
    }

    fun setVideoSize(width: Int, height: Int) {
        videoWidth = width
        videoHeight = height
        requestLayout()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val availableWidth = MeasureSpec.getSize(widthMeasureSpec)
        val availableHeight = MeasureSpec.getSize(heightMeasureSpec)
        if (videoWidth <= 0 || videoHeight <= 0 || availableWidth <= 0 || availableHeight <= 0) {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec)
            return
        }
        val ratio = outputAspectRatio ?: (videoWidth.toFloat() / videoHeight.toFloat())
        var width = availableWidth
        var height = (width / ratio).roundToInt()
        if (height > availableHeight) {
            height = availableHeight
            width = (height * ratio).roundToInt()
        }
        setMeasuredDimension(width, height)
    }
}
