package com.owncut.app

/** Output size. ORIGINAL keeps the timeline size: no scaling effect is added. */
internal enum class ExportResolution(val targetHeight: Int?) {
    ORIGINAL(null),
    P720(720),
    P1080(1080),
    QHD_2K(1440),
    UHD_4K(2160),
}

/** Quality preset. Automatic bitrate = pixels x frame rate x bits per pixel of the preset. */
internal enum class ExportQuality(val bitsPerPixel: Double) {
    LOW(0.05),
    MEDIUM(0.10),
    HIGH(0.16),
    ULTRA(0.25),
}

/** AUTO leaves the encoder defaults. MAIN requests H.264 Main, and HEVC 10-bit keeps Main 10 (see TimelineRenderExporter). */
internal enum class ExportCodecProfile { AUTO, MAIN }

/** User export choices for Transformer-encoded formats. Pure Kotlin so it can be tested without Android.
 * Defaults keep the original size at the highest preset (ULTRA), so default export quality is not reduced.
 * Stream-copy remuxes (MOV/MKV of an already encoded MP4) inherit the MP4 encode. FP32 formats ignore these settings.
 */
internal data class ExportSettings(
    val resolution: ExportResolution = ExportResolution.ORIGINAL,
    val quality: ExportQuality = ExportQuality.ULTRA,
    val customMbps: Int? = null,
    val profile: ExportCodecProfile = ExportCodecProfile.AUTO,
) {
    init {
        require(customMbps == null || customMbps in MIN_CUSTOM_MBPS..MAX_CUSTOM_MBPS) {
            "Custom bitrate must be $MIN_CUSTOM_MBPS to $MAX_CUSTOM_MBPS Mbps"
        }
    }

    /** Output width and height for a source of the given size. The aspect ratio is kept, both sides even. */
    fun outputSize(sourceWidth: Int, sourceHeight: Int): Pair<Int, Int> {
        require(sourceWidth > 0 && sourceHeight > 0) { "Source size must be positive" }
        val target = resolution.targetHeight ?: return sourceWidth to sourceHeight
        if (sourceWidth >= sourceHeight) {
            val width = Math.round(sourceWidth.toDouble() * target / sourceHeight).toInt()
            return even(width) to target
        }
        // Portrait: the requested size applies to the long side so the 720/1080/2K/4K labels stay meaningful.
        val height = Math.round(sourceHeight.toDouble() * target / sourceWidth).toInt()
        return target to even(height)
    }

    /** Bits per second for an encode at the output size. A custom bitrate always wins. */
    fun bitrateFor(width: Int, height: Int, frameRate: Double): Int {
        customMbps?.let { return it * 1_000_000 }
        require(width > 0 && height > 0 && frameRate > 0.0) { "Bitrate needs a positive size and frame rate" }
        val bits = width.toDouble() * height * frameRate * quality.bitsPerPixel
        return bits.coerceIn(MIN_AUTO_BPS.toDouble(), MAX_AUTO_BPS.toDouble()).toInt()
    }

    companion object {
        const val MIN_CUSTOM_MBPS = 1
        const val MAX_CUSTOM_MBPS = 100
        const val MIN_AUTO_BPS = 1_000_000L
        const val MAX_AUTO_BPS = 80_000_000L
        val DEFAULT = ExportSettings()

        /** Blank text means automatic. Anything else must be a whole number of Mbps in range. */
        fun parseCustomMbps(text: String): Int? {
            val trimmed = text.trim()
            if (trimmed.isEmpty()) return null
            val value = trimmed.toIntOrNull() ?: throw IllegalArgumentException("Bitrate must be a whole number of Mbps")
            require(value in MIN_CUSTOM_MBPS..MAX_CUSTOM_MBPS) { "Bitrate must be $MIN_CUSTOM_MBPS to $MAX_CUSTOM_MBPS Mbps" }
            return value
        }

        private fun even(value: Int): Int = if (value % 2 == 0) value else value + 1
    }
}
