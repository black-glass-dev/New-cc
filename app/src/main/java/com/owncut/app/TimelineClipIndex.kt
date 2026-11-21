package com.owncut.app

/** UI interval projection, not an edit model. Indices refer to the original painter order.
 * Keeps offscreen clips out of the draw loop; the complete timeline is still retained.
 */
internal class TimelineClipIndex(clips: List<ClipSnapshot>) {
    private val order = clips.indices.sortedBy { clips[it].startFrame }.toIntArray()
    private val starts = LongArray(order.size) { clips[order[it]].startFrame }
    private val ends = LongArray(order.size) { Math.addExact(clips[order[it]].startFrame, clips[order[it]].durationFrames) }
    private val prefixEnd = LongArray(order.size)

    init {
        var end = Long.MIN_VALUE
        for (i in order.indices) { end = maxOf(end, ends[i]); prefixEnd[i] = end }
    }

    /** Inclusive conservative bounds match Canvas's one-pixel clips and edge clipping.
     * Prefix search supports overlapping intervals; pathological overlap may scan more
     * than the visible result. Does not promise strict O(log N + K) or memory virtualization.
     */
    fun intersecting(leftFrame: Long, rightFrame: Long, includeIndex: Int? = null): IntArray {
        require(rightFrame >= leftFrame)
        var lo = 0; var hi = order.size
        while (lo < hi) { val mid = (lo + hi) ushr 1; if (starts[mid] <= rightFrame) lo = mid + 1 else hi = mid }
        val end = lo
        lo = 0; hi = end
        while (lo < hi) { val mid = (lo + hi) ushr 1; if (prefixEnd[mid] < leftFrame) lo = mid + 1 else hi = mid }
        val visible = ArrayList<Int>()
        for (i in lo until end) if (ends[i] >= leftFrame) visible.add(order[i])
        if (includeIndex != null && includeIndex in order.indices && includeIndex !in visible)
            visible.add(includeIndex)
        return visible.sorted().toIntArray()
    }
}
