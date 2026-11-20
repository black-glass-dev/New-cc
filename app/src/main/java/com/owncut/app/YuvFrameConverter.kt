package com.owncut.app

import android.media.Image
import java.nio.ByteBuffer

/** YUV_420_888 frame → RGBA8, BT.709, rotated clockwise by [rotation] degrees (native: fp32_render_jni.cpp).
 * Output stays gamma-encoded Rec.709 so the FP32 exposure path reads it correctly.
 */
internal object YuvFrameConverter {
    init { System.loadLibrary("owncut_android") }

    /** [out] must be a direct buffer of outputWidth*outputHeight*4 bytes. */
    fun convert(image: Image, rotation: Int, fullRange: Boolean, out: ByteBuffer) {
        require(rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270) { "rotation must be a quarter turn" }
        require(image.format == android.graphics.ImageFormat.YUV_420_888) { "frame must be YUV_420_888" }
        require(image.width % 2 == 0 && image.height % 2 == 0) { "frame size must be even" }
        val planes = image.planes
        require(planes.size == 3) { "YUV_420_888 has three planes" }
        val y = planes[0]
        val u = planes[1]
        val v = planes[2]
        require(y.buffer.isDirect && u.buffer.isDirect && v.buffer.isDirect && out.isDirect) { "YUV planes must be direct" }
        val outWidth = if (rotation == 90 || rotation == 270) image.height else image.width
        val outHeight = if (rotation == 90 || rotation == 270) image.width else image.height
        require(out.capacity() >= outWidth * outHeight * 4) { "output buffer too small" }
        nativeConvert(
            y.buffer, y.rowStride, y.pixelStride,
            u.buffer, u.rowStride, u.pixelStride,
            v.buffer, v.rowStride, v.pixelStride,
            image.width, image.height, rotation, fullRange, out,
        )
    }

    @JvmStatic private external fun nativeConvert(
        yBuf: ByteBuffer, yRow: Int, yPix: Int,
        uBuf: ByteBuffer, uRow: Int, uPix: Int,
        vBuf: ByteBuffer, vRow: Int, vPix: Int,
        width: Int, height: Int, rotation: Int, fullRange: Boolean, out: ByteBuffer,
    )
}
