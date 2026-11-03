package com.owncut.app

import java.util.concurrent.CancellationException

private fun rejected(block: () -> Unit) {
    try { block() } catch (_: IllegalArgumentException) { return }
    error("expected invalid color plan rejection")
}
fun main() {
    val unknown=ColorEngine.VideoColorMetadata(ColorEngine.VideoPrimaries.UNKNOWN,ColorEngine.VideoTransfer.UNKNOWN,ColorEngine.VideoRange.UNKNOWN)
    val known=ColorEngine.VideoColorMetadata(ColorEngine.VideoPrimaries.REC709,ColorEngine.VideoTransfer.SDR_VIDEO,ColorEngine.VideoRange.LIMITED)
    check(PreviewColorPolicy.resolve(unknown,false)==unknown)
    check(PreviewColorPolicy.resolve(unknown,true)==known)
    check(PreviewColorPolicy.merge(unknown,known)==known)
    check(PreviewColorPolicy.merge(known,unknown)==known)
    for(c in listOf(unknown.copy(primaries=ColorEngine.VideoPrimaries.OTHER),unknown.copy(transfer=ColorEngine.VideoTransfer.PQ),
        unknown.copy(transfer=ColorEngine.VideoTransfer.HLG),unknown.copy(transfer=ColorEngine.VideoTransfer.SRGB),unknown.copy(hasHdrStaticMetadata=true))) {
        check(!PreviewColorPolicy.canInterpret(c))
        check(runCatching { PreviewColorPolicy.resolve(c,true) }.isFailure)
    }
    val full=known.copy(range=ColorEngine.VideoRange.FULL)
    check(PreviewColorPolicy.resolve(full,true)==full)
    check(PreviewColorPolicy.decoded(unknown,full,true)==full)
    check(PreviewColorPolicy.decoded(unknown,full,false)==unknown)
    for(p in ColorEngine.VideoPrimaries.entries) for(t in ColorEngine.VideoTransfer.entries)
        for(r in ColorEngine.VideoRange.entries) for(h in listOf(false,true)) {
            val c=ColorEngine.VideoColorMetadata(p,t,r,h)
            check(PreviewColorPolicy.resolve(c,false)==c)
            check(runCatching { PreviewColorPolicy.resolve(c,true) }.isSuccess==PreviewColorPolicy.canInterpret(c))
            if(PreviewColorPolicy.canInterpret(c)) ColorEngine.validateVideoSource(PreviewColorPolicy.resolve(c,true))
        }
    check(runCatching { PreviewColorPolicy.merge(full,known) }.isFailure)
    check(PreviewColorPolicy.merge(known,unknown.copy(hasHdrStaticMetadata=true)).hasHdrStaticMetadata)

    check(PreviewProgress.position(Long.MAX_VALUE,1000)==Long.MAX_VALUE)
    check(PreviewProgress.position(12345,500)==6172L)
    check(PreviewProgress.position(12345,0)==0L)
    check(PreviewProgress.progress(-1,100)==0 && PreviewProgress.progress(999,100)==1000)
    rejected { PreviewProgress.position(-1,500) }
    rejected { PreviewProgress.position(1000,1001) }
    for(p in 0..1000) {
        val exact=java.math.BigInteger.valueOf(Long.MAX_VALUE).multiply(java.math.BigInteger.valueOf(p.toLong()))
            .divide(java.math.BigInteger.valueOf(1000)).longValueExact()
        check(PreviewProgress.position(Long.MAX_VALUE,p)==exact)
    }
    println("Experimental preview seek transport: clip-relative progress and overflow-safe duration mapping passed")
    val supported=ColorEngine.VideoColorMetadata(ColorEngine.VideoPrimaries.REC709,
        ColorEngine.VideoTransfer.SDR_VIDEO,ColorEngine.VideoRange.LIMITED)
    for(p in ColorEngine.VideoPrimaries.entries) for(t in ColorEngine.VideoTransfer.entries)
        for(r in ColorEngine.VideoRange.entries) {
            val source=ColorEngine.VideoColorMetadata(p,t,r)
            if (p==supported.primaries && t==supported.transfer && r!=ColorEngine.VideoRange.UNKNOWN)
                ColorEngine.validateVideoSource(source)
            else rejected { ColorEngine.validateVideoSource(source) }
        }
    rejected { ColorEngine.validateVideoSource(supported.copy(hasHdrStaticMetadata=true)) }
    val expected=ColorEngine.DecodedVideoFormat(1920,1080,supported)
    val gate=DecoderColorGate(expected)
    check(gate.state==DecoderColorGate.State.AWAITING_FORMAT)
    gate.beforeRender(expected);gate.beforeRender(expected)
    check(gate.state==DecoderColorGate.State.VERIFIED)
    rejected { gate.beforeRender(expected.copy(width=1280)) }
    check(gate.state==DecoderColorGate.State.FAILED)
    try { gate.beforeRender(expected);error("failed gate resumed") } catch (_: IllegalStateException) { }
    gate.close();gate.close();check(gate.state==DecoderColorGate.State.CLOSED)
    try { gate.beforeRender(expected);error("closed gate rendered") } catch (_: IllegalStateException) { }
    val absent=DecoderColorGate(expected)
    rejected { absent.beforeRender(null) }
    check(absent.state==DecoderColorGate.State.FAILED)
    val query=DecoderColorGate(expected)
    try { query.beforeRender { error("output format query failed") };error("query failure ignored") }
    catch (_: IllegalStateException) { check(query.state==DecoderColorGate.State.FAILED) }
    var queriedAgain=false
    try { query.beforeRender { queriedAgain=true;expected };error("query failure resumed") }
    catch (_: IllegalStateException) { check(!queriedAgain) }
    val changed=DecoderColorGate(expected)
    changed.beforeRender(expected)
    rejected { changed.beforeRender(expected.copy(color=supported.copy(range=ColorEngine.VideoRange.FULL))) }
    val invalidated=DecoderColorGate(expected);invalidated.invalidate()
    try { invalidated.beforeRender(expected);error("query failure resumed") } catch (_: IllegalStateException) { }
    rejected { DecoderColorGate(expected.copy(width=0)) }
    println("Production Kotlin/native decoded gate: repeated validation, late format changes, unknown output and terminal lifecycle passed")
    val grade=ColorGrade(exposureStops=1.0)
    val first=ClipSnapshot("first","v1","content://shared",0,30,0,1000,grade,true)
    val second=first.copy(id="second",startFrame=30,sourceInMs=1000,sourceOutMs=2000,
        grade=grade.copy(enabled=false),gradeActive=false)
    var reads=0
    fun read(uri: String): VideoSourceMetadata {
        reads++;return VideoSourceMetadata(uri,1,1920,1080,90,supported)
    }
    val mutable=mutableListOf(first,second)
    val plan=ColorRenderPlan.prepare("42",mutable,{},::read)
    check(reads==1 && plan.revision=="42" && plan.clips.map {it.clip}==mutable)
    check(plan.clips[0].shaders.fragment.contains("toLight") && !plan.clips[1].shaders.fragment.contains("toLight"))
    check(plan.clips[0].source === plan.clips[1].source)
    mutable.clear();check(plan.clips.size==2 && plan.clips[0].clip.grade==grade)
    try { (plan.clips as MutableList).clear();error("mutable plan escaped") } catch (_: UnsupportedOperationException) { }
    rejected { ColorRenderPlan.prepare("",listOf(first),{},::read) }
    rejected { ColorRenderPlan.prepare("42",listOf(first,first),{},::read) }
    rejected { ColorRenderPlan.prepare("42",listOf(first),{}) { read(it).copy(mediaId="wrong") } }
    rejected { ColorRenderPlan.prepare("42",listOf(first),{}) { read(it).copy(width=0) } }
    rejected { ColorRenderPlan.prepare("42",listOf(first),{}) { read(it).copy(rotationDegrees=45) } }
    rejected { ColorRenderPlan.prepare("42",listOf(first),{}) { read(it).copy(color=supported.copy(transfer=ColorEngine.VideoTransfer.HLG)) } }
    rejected { ColorRenderPlan.prepare("42",listOf(first),{}) { read(it).copy(videoTrackIndex=-1) } }
    rejected { ColorRenderPlan.prepare("42",listOf(first),{}) { read(it).copy(height=0) } }
    for (cancelAt in 1..4) {
        var checks=0
        try { ColorRenderPlan.prepare("42",listOf(first),{
            checks++;if(checks==cancelAt) throw CancellationException()
        },::read);error("cancelled preparation published at stage $cancelAt") }
        catch (_: CancellationException) { check(checks==cancelAt) }
    }
    var cancelled=false
    try { ColorRenderPlan.prepare("42",listOf(first),{ if(cancelled) throw CancellationException() }) {
        cancelled=true;read(it)
    };error("cancelled result published") } catch (_: CancellationException) { }
    var attempted=false
    try { ColorRenderPlan.prepare("42",listOf(first),{throw CancellationException()}) {
        attempted=true;read(it)
    };error("cancelled probe started") } catch (_: CancellationException) { }
    check(!attempted)
    val bad=second.copy(grade=grade.copy(gamma=ColorChannels(0.0,1.0,1.0)))
    rejected { ColorRenderPlan.prepare("43",listOf(first,bad),{},::read) }
    val again=ColorRenderPlan.prepare("44",listOf(first),{},::read)
    check(again.revision=="44" && plan.revision=="42") // failure did not poison/publish cache
    check(reads>1) // no cross-request URI cache/freshness claim
    println("Production Kotlin/native source policy and shared plan: immutable per-clip recipes, request-local dedup, cancellation/failure atomicity passed")
}
