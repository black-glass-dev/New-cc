package com.owncut.app

/** Codec-owner-thread binding to Media3's timestamp-associated output-format events.
 * Never polls an asynchronous codec's latest format for an older dequeued buffer.
 * Every frame still passes native validation; failure/close remain terminal.
 */
internal class DecoderOutputSequence(expected: ColorEngine.DecodedVideoFormat) {
    private val gate=DecoderColorGate(expected)
    private var actual: ColorEngine.DecodedVideoFormat?=null
    fun formatChanged(read: () -> ColorEngine.DecodedVideoFormat?) {
        actual=null
        var next: ColorEngine.DecodedVideoFormat?=null
        gate.beforeRender { read().also { next=it } }
        actual=next
    }
    fun beforeRender() { gate.beforeRender(actual) }
    fun clear() { actual=null }
    fun invalidate() { actual=null;gate.invalidate() }
    fun close() { actual=null;gate.close() }
}
