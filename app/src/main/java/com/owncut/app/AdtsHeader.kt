package com.owncut.app

/** 7-byte ADTS header (no CRC) for one AAC access unit, built from the track's AudioSpecificConfig.
 * Pure Kotlin so the header layout is host-tested. Supports the AAC object types ADTS can carry (1..4).
 */
internal object AdtsHeader {
    private val SAMPLE_RATES = intArrayOf(96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350)

    /** Returns the header for a payload of [payloadBytes]. Throws IllegalArgumentException for unsupported config. */
    fun build(asc: ByteArray, payloadBytes: Int): ByteArray {
        require(asc.size >= 2) { "AudioSpecificConfig is too short" }
        require(payloadBytes > 0) { "empty AAC payload" }
        val objectType = (asc[0].toInt() and 0xFF) ushr 3
        require(objectType in 1..4) { "AAC object type $objectType cannot be carried in ADTS" }
        val samplingIndex = ((asc[0].toInt() and 0x07) shl 1) or ((asc[1].toInt() and 0xFF) ushr 7)
        require(samplingIndex < 13) { "sampling frequency index $samplingIndex cannot be carried in ADTS" }
        val channels = ((asc[1].toInt() and 0xFF) ushr 3) and 0x0F
        require(channels in 1..7) { "channel configuration $channels cannot be carried in ADTS" }
        val frameLength = payloadBytes + 7
        require(frameLength <= 0x1FFF) { "AAC access unit too large for ADTS" }
        val profile = objectType - 1
        return byteArrayOf(
            0xFF.toByte(),
            0xF1.toByte(), // sync word, MPEG-4, layer 0, no CRC
            ((profile shl 6) or (samplingIndex shl 2) or (channels shr 2)).toByte(),
            (((channels and 0x3) shl 6) or (frameLength shr 11)).toByte(),
            ((frameLength shr 3) and 0xFF).toByte(),
            (((frameLength and 0x7) shl 5) or 0x1F).toByte(),
            0xFC.toByte(), // buffer fullness 0x7FF (VBR), one raw block
        )
    }

    /** True when [asc] can be carried in ADTS (AAC object types 1..4, sampling index below 13, 1..7 channels). */
    fun supports(asc: ByteArray): Boolean = try {
        build(asc, 1)
        true
    } catch (_: IllegalArgumentException) {
        false
    }

    /** Sample rate in Hz from an AudioSpecificConfig, or null when it uses an explicit rate (index 15). */
    fun sampleRate(asc: ByteArray): Int? {
        require(asc.size >= 2) { "AudioSpecificConfig is too short" }
        val index = ((asc[0].toInt() and 0x07) shl 1) or ((asc[1].toInt() and 0xFF) ushr 7)
        return SAMPLE_RATES.getOrNull(index)
    }

    /** Channel count from an AudioSpecificConfig (channel configuration field). */
    fun channelCount(asc: ByteArray): Int {
        require(asc.size >= 2) { "AudioSpecificConfig is too short" }
        return ((asc[1].toInt() and 0xFF) ushr 3) and 0x0F
    }
}
