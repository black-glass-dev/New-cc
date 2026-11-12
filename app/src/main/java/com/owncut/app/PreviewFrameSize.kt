package com.owncut.app

/** Bitmap transport budget, NOT an engine/output resolution limit. Decoder internals can
 * still allocate full-size frames; only the requested/returned CPU proxy is bounded.
 */
internal object PreviewFrameSize {
    const val MAX_WIDTH=1280
    const val MAX_HEIGHT=720
    data class Size(val width: Int, val height: Int)
    fun fit(width: Int, height: Int, rotation: Int=0): Size {
        require(width>0 && height>0 && rotation in listOf(0,90,180,270))
        val w=if(rotation==90 || rotation==270) height else width
        val h=if(rotation==90 || rotation==270) width else height
        val factor=minOf(1.0,MAX_WIDTH.toDouble()/w,MAX_HEIGHT.toDouble()/h)
        return Size((w*factor).toInt().coerceIn(1,MAX_WIDTH),(h*factor).toInt().coerceIn(1,MAX_HEIGHT))
    }
    fun fitsUnscaled(width: Int,height: Int,rotation: Int): Boolean {
        val size=fit(width,height,rotation)
        val w=if(rotation==90 || rotation==270) height else width
        val h=if(rotation==90 || rotation==270) width else height
        return size.width==w && size.height==h
    }
}
