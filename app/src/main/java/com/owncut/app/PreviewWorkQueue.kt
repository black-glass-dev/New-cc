package com.owncut.app

/** Main-owner latest-only work. A new request never postpones already scheduled work.
 * GPU requests are acknowledged by their own draw token, not by UI parameter changes.
 */
internal class PreviewWorkQueue {
    private var requested=0L
    private var completed=0L
    var active: Long?=null
        private set
    private var closed=false
    val pending get()=!closed && requested>completed
    fun request() { if(!closed) requested++ }
    fun begin(): Long? {
        if(!pending || active!=null) return null
        return requested.also { active=it }
    }
    fun finish(token: Long): Boolean {
        if(closed || active!=token) return false
        completed=token;active=null;return true
    }
    fun cancel() { active=null;completed=requested }
    fun close() { closed=true;cancel() }
}

/** Leading/coalesced pulse: repeated moves cannot starve a queued preview callback. */
internal class PreviewPulse {
    private var scheduled=false
    private var closed=false
    fun request(): Boolean {
        if(closed || scheduled) return false
        scheduled=true;return true
    }
    fun take(): Boolean {
        if(closed || !scheduled) return false
        scheduled=false;return true
    }
    fun close() { closed=true;scheduled=false }
}
