package com.owncut.app

import java.io.Closeable

/** Stateful PCM effects hosted by the portable C++ audio engine. One handle per export item. */
internal class NativeAudioFx(
    bandGainsDb: FloatArray,
    compressorEnabled: Boolean,
    compressorThresholdDb: Float,
    compressorRatio: Float,
    limiterEnabled: Boolean,
    limiterCeilingDb: Float,
    gateEnabled: Boolean,
    gateThresholdDb: Float,
    sampleRate: Int,
    channels: Int,
) : Closeable {
    private var handle = nativeCreate(
        bandGainsDb, compressorEnabled, compressorThresholdDb, compressorRatio,
        limiterEnabled, limiterCeilingDb, gateEnabled, gateThresholdDb, sampleRate, channels,
    )

    fun process(samples: ShortArray) {
        val current = handle
        check(current != 0L) { "Audio effect handle is closed" }
        nativeProcess(current, samples)
    }

    fun reset() {
        val current = handle
        if (current != 0L) nativeReset(current)
    }

    override fun close() {
        val current = handle
        handle = 0L
        if (current != 0L) nativeDestroy(current)
    }

    private external fun nativeCreate(
        bandGainsDb: FloatArray,
        compressorEnabled: Boolean,
        compressorThresholdDb: Float,
        compressorRatio: Float,
        limiterEnabled: Boolean,
        limiterCeilingDb: Float,
        gateEnabled: Boolean,
        gateThresholdDb: Float,
        sampleRate: Int,
        channels: Int,
    ): Long
    private external fun nativeProcess(handle: Long, samples: ShortArray)
    private external fun nativeReset(handle: Long)
    private external fun nativeDestroy(handle: Long)

    companion object { init { System.loadLibrary("owncut_android") } }
}
