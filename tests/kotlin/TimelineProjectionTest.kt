package com.owncut.app

import kotlin.random.Random

private fun fails(block: () -> Unit) {
    try { block() } catch (_: IllegalStateException) { return }
    error("expected rejected snapshot")
}

fun main() {
    val random = Random(641)
    val clips = List(8192) { i -> ClipSnapshot("clip-$i","v${i%3}","source",
        random.nextLong(0,200000),random.nextLong(1,10000),0,1000) }
    val index = TimelineClipIndex(clips)
    repeat(600) {
        val left = random.nextLong(-1000,210000)
        val right = left + random.nextLong(0,2000)
        val expected = clips.indices.filter { clips[it].startFrame <= right && clips[it].startFrame + clips[it].durationFrames >= left }
        check(expected.toIntArray().contentEquals(index.intersecting(left,right)))
    }
    val edges = listOf(ClipSnapshot("a","v","source",10,10,0,1000),ClipSnapshot("b","v","source",20,1,0,1000))
    check(TimelineClipIndex(edges).intersecting(20,20).contentEquals(intArrayOf(0,1)))
    check(TimelineClipIndex(emptyList()).intersecting(0,100).isEmpty())
    // A drag's original interval may be offscreen; force it in without reordering
    // ordinary painters or adding duplicates/invalid positions.
    check(TimelineClipIndex(edges).intersecting(100,100,0).contentEquals(intArrayOf(0)))
    check(TimelineClipIndex(edges).intersecting(20,20,0).contentEquals(intArrayOf(0,1)))
    check(TimelineClipIndex(edges).intersecting(100,100,-1).isEmpty())
    check(TimelineClipIndex(edges).intersecting(100,100,2).isEmpty())

    val cache = TimelineSnapshotCache()
    var calls = 0
    val fetch: (Int,Int,String) -> ClipSnapshotPage = { offset, limit, revision ->
        calls++
        val end = minOf(offset+limit,clips.size)
        ClipSnapshotPage(clips.subList(offset,end),revision,clips.size,end)
    }
    val all = cache.read("1",fetch)
    check(all == clips && calls == 32)
    check(cache.read("1") { _,_,_ -> error("cache miss") } === all)
    try { (all as MutableList).clear(); error("mutable cache") } catch (_: UnsupportedOperationException) { }
    // Every malformed or interrupted read must leave the previously published cache intact.
    val badPages = listOf(
        ClipSnapshotPage(emptyList(),"2",1,0), // no progress
        ClipSnapshotPage(edges,"2",2,1), // wrong cursor
        ClipSnapshotPage(edges,"wrong",2,2),
        ClipSnapshotPage(edges,"2",1,2),
        ClipSnapshotPage(emptyList(),"2",-1,0),
        ClipSnapshotPage(clips.take(257),"2",8192,257))
    for (bad in badPages) {
        fails { cache.read("2") { _,_,_ -> bad } }
        check(cache.read("1",fetch) === all)
    }
    fails { cache.read("2") { offset, limit, revision ->
        if (offset == 0) fetch(offset,limit,revision) else error("interrupted second page")
    } }
    fails { cache.read("2") { offset, limit, revision ->
        fetch(offset,limit,revision).let { if (offset == 0) it else it.copy(total=8191) }
    } }
    check(cache.read("1",fetch) === all)
    val newer = cache.read("2",fetch)
    check(newer == all && newer !== all)
    cache.clear()
    check(cache.read("2",fetch) !== newer)
    val empty = TimelineSnapshotCache().read("0") { _,_,rev -> ClipSnapshotPage(emptyList(),rev,0,0) }
    check(empty.isEmpty())
    println("Production Kotlin: 8192-clip interval queries, boundary/overlap order, immutable cache, stale/malformed/interrupted pages passed")
}
