package com.owncut.app

/** Offline, bounded native subject-motion/saliency tracking. Frames are 8-bit sRGB thumbnails. */
internal object NativeReframe {
    init { System.loadLibrary("owncut_android") }

    /** Returns [cx, cy, cropWidth, cropHeight] for each frame; no network/model access. */
    external fun nativeAnalyzeRgba(
        rgbaFrames: ByteArray,
        width: Int,
        height: Int,
        frameCount: Int,
        aspectNumerator: Int,
        aspectDenominator: Int,
        smoothing: Float,
    ): FloatArray

    /** Returns normalized pan x/y, correction angle in radians, and adaptive scale per frame. */
    external fun nativeAnalyzeStabilization(
        rgbaFrames: ByteArray,
        width: Int,
        height: Int,
        frameCount: Int,
        smoothness: Float,
        cameraLock: Boolean,
        maxZoom: Float,
    ): FloatArray
}
