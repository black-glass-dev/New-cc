package com.owncut.app

/** Codec-owner-thread gate. No one-time "verified" cache: every surface release must
 * validate the current output format. Once invalid, only drop/close/new decoder is safe.
 * This is metadata verification, not pixel, crop/rotation or source-byte verification.
 */
internal class DecoderColorGate(private val expected: ColorEngine.DecodedVideoFormat) {
    enum class State { AWAITING_FORMAT, VERIFIED, FAILED, CLOSED }
    var state=State.AWAITING_FORMAT
        private set
    init { ColorEngine.validateDecodedFormat(expected,expected) }
    fun beforeRender(actual: ColorEngine.DecodedVideoFormat?) = beforeRender { actual }
    fun beforeRender(readActual: () -> ColorEngine.DecodedVideoFormat?) {
        check(state != State.CLOSED && state != State.FAILED) { "Decoder color gate is terminal" }
        try {
            val actual=requireNotNull(readActual()) { "No decoded output format before surface render" }
            ColorEngine.validateDecodedFormat(expected,actual)
            state=State.VERIFIED
        } catch (error: Exception) {
            state=State.FAILED
            throw error
        }
    }
    fun invalidate() { if(state != State.CLOSED) state=State.FAILED }
    fun close() { state=State.CLOSED }
}
