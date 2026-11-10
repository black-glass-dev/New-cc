package com.owncut.app

import android.content.Context
import android.view.TextureView
import kotlin.math.roundToInt

/** Keeps decoded preview pixels at their source aspect ratio inside the available workspace. */
internal class AspectRatioTextureView(context: Context) : TextureView(context) {
    private var videoWidth = 0
    private var videoHeight = 0

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
        val ratio = videoWidth.toFloat() / videoHeight.toFloat()
        var width = availableWidth
        var height = (width / ratio).roundToInt()
        if (height > availableHeight) {
            height = availableHeight
            width = (height * ratio).roundToInt()
        }
        setMeasuredDimension(width, height)
    }
}
