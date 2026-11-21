package com.owncut.app

private fun refused(block: () -> Unit) {
    try { block() } catch (_: IllegalArgumentException) { return }
    error("invalid preview policy accepted")
}
fun main() {
    val pulse=PreviewPulse()
    check(pulse.request())
    repeat(10000) { check(!pulse.request()) } // movement cannot move the deadline
    check(pulse.take() && !pulse.take())
    check(pulse.request() && pulse.take())
    pulse.close();check(!pulse.request() && !pulse.take())
    val queue=PreviewWorkQueue()
    queue.request();val first=requireNotNull(queue.begin())
    repeat(10000) { queue.request();check(queue.begin()==null) }
    check(!queue.finish(first+1) && queue.active==first)
    check(queue.finish(first) && queue.pending)
    val latest=requireNotNull(queue.begin());check(latest==10001L)
    check(!queue.finish(first) && queue.finish(latest) && !queue.pending)
    queue.request();val canceled=requireNotNull(queue.begin());queue.cancel()
    check(!queue.finish(canceled) && !queue.pending)
    queue.request();val resumed=requireNotNull(queue.begin());check(resumed>canceled)
    queue.close();check(!queue.finish(resumed));queue.request();check(queue.begin()==null)
    val s=PreviewPlaybackState()
    check(!s.canTransport && !s.canSeek && !s.canCompare(true))
    check(s.buffering() && s.canTransport && !s.canSeek)
    check(s.ready() && s.canSeek && s.canCompare(true) && !s.canCompare(false))
    check(s.buffering() && !s.canCompare(true))
    check(s.fail() && !s.fail())
    check(!s.ready() && !s.buffering() && !s.loading() && !s.canTransport && !s.canSeek)
    check(s.close() && !s.close() && !s.ready() && !s.fail())
    check(s.phase==PreviewPlaybackState.Phase.CLOSED)
    val beforeReady=PreviewPlaybackState();beforeReady.close();check(!beforeReady.ready())
    repeat(500) { seed ->
        val state=PreviewPlaybackState()
        var terminal=false
        repeat(50) { tick ->
            when((seed*17+tick*31)%5) {
                0 -> state.loading();1 -> state.buffering();2 -> state.ready();3 -> state.fail();else -> state.close()
            }
            if(terminal) check(state.terminal)
            terminal=terminal || state.terminal
            if(terminal) check(!state.canSeek && !state.canCompare(true) && !state.canTransport)
        }
    }
    check(PreviewProgress.seekPosition(1000,1000)==999L)
    check(PreviewProgress.seekPosition(1,1000)==0L)
    check(PreviewProgress.seekPosition(Long.MAX_VALUE,1000)==Long.MAX_VALUE-1)
    check(PreviewProgress.clipRelative(Long.MIN_VALUE,100,200)==0L)
    check(PreviewProgress.clipRelative(Long.MAX_VALUE,100,200)==99L)
    check(PreviewProgress.clipRelative(150,100,200)==50L)
    refused { PreviewProgress.seekPosition(0,0) };refused { PreviewProgress.clipRelative(100,100,100) }
    for(duration in listOf(1L,2L,1000L,Long.MAX_VALUE)) for(anchor in listOf(0L,duration/2,duration-1)) {
        val before=PreviewProgress.freshFramePosition(anchor,duration,false)
        val after=PreviewProgress.freshFramePosition(anchor,duration,true)
        check(before in 0 until duration && after in 0 until duration)
        check(anchor-before in 0..1 && after-anchor in 0..1)
        repeat(100) { check(PreviewProgress.freshFramePosition(anchor,duration,it%2==0)==if(it%2==0) after else before) }
    }
    refused { PreviewProgress.freshFramePosition(0,0,true) }
    refused { PreviewProgress.freshFramePosition(100,100,true) }
    check(PreviewFrameSize.fit(3840,2160)==PreviewFrameSize.Size(1280,720))
    check(PreviewFrameSize.fit(1920,1080,90)==PreviewFrameSize.Size(405,720))
    check(PreviewFrameSize.fit(640,480)==PreviewFrameSize.Size(640,480))
    check(PreviewFrameSize.fitsUnscaled(640,480,90))
    check(!PreviewFrameSize.fitsUnscaled(1920,1080,0))
    for(w in listOf(1,128,640,3840,Int.MAX_VALUE)) for(h in listOf(1,128,480,2160,Int.MAX_VALUE))
        for(r in listOf(0,90,180,270)) {
            val size=PreviewFrameSize.fit(w,h,r)
            check(size.width in 1..1280 && size.height in 1..720)
            val orientedWidth=if(r==90 || r==270) h else w
            val orientedHeight=if(r==90 || r==270) w else h
            check(size.width<=orientedWidth && size.height<=orientedHeight)
        }
    refused { PreviewFrameSize.fit(0,100) };refused { PreviewFrameSize.fit(100,100,45) }
    println("Production preview policy: terminal failures/close, late events, control gates, exclusive seeks and bounded frame requests passed")
}
