package com.owncut.app

/** Main-thread control policy, not a codec/GL success claim. Failure/close are terminal:
 * late READY callbacks cannot re-enable controls or restart a dismissed preview.
 */
internal class PreviewPlaybackState {
    enum class Phase { PREFLIGHT, BUFFERING, READY, FAILED, CLOSED }
    var phase=Phase.PREFLIGHT
        private set
    val terminal: Boolean get()=phase==Phase.FAILED || phase==Phase.CLOSED
    val canTransport: Boolean get()=phase==Phase.BUFFERING || phase==Phase.READY
    val canSeek: Boolean get()=phase==Phase.READY
    fun canCompare(playWhenReady: Boolean): Boolean = canSeek && playWhenReady
    fun loading()=move(Phase.PREFLIGHT)
    fun buffering()=move(Phase.BUFFERING)
    fun ready()=move(Phase.READY)
    private fun move(next: Phase): Boolean {
        if(terminal) return false
        phase=next;return true
    }
    fun fail(): Boolean {
        if(terminal) return false
        phase=Phase.FAILED;return true
    }
    fun close(): Boolean {
        if(phase==Phase.CLOSED) return false
        phase=Phase.CLOSED;return true
    }
}
