package com.owncut.app

import java.util.Collections

internal data class ClipSnapshot(
    val id: String,
    val trackId: String,
    val mediaId: String,
    val startFrame: Long,
    val durationFrames: Long,
    val sourceInMs: Long,
    val sourceOutMs: Long,
    val grade: ColorGrade = ColorGrade(),
    val gradeActive: Boolean = false, // native pipeline identity predicate; not duplicated UI color math
)

internal data class ClipSnapshotPage(val clips: List<ClipSnapshot>, val revision: String, val total: Int, val nextOffset: Int)

/** Pure UI snapshot cache. Its EngineSession owner serializes access/lifetime.
 * Bounded transport and immutable publication, NOT full timeline-memory virtualization.
 */
internal class TimelineSnapshotCache {
    private var cachedRevision: String? = null
    private var cachedClips: List<ClipSnapshot> = emptyList()

    fun read(revision: String, fetch: (Int, Int, String) -> ClipSnapshotPage): List<ClipSnapshot> {
        if (revision == cachedRevision) return cachedClips
        val result = ArrayList<ClipSnapshot>()
        var offset = 0
        var total: Int? = null
        do {
            val page = fetch(offset, 256, revision)
            check(page.revision == revision && page.total >= 0 && (total == null || total == page.total))
            check(page.clips.size <= 256 && page.nextOffset == Math.addExact(offset,page.clips.size))
            check(page.nextOffset <= page.total && (page.clips.isNotEmpty() || page.nextOffset == page.total))
            result.addAll(page.clips)
            offset = page.nextOffset
            total = page.total
        } while (offset < requireNotNull(total))
        // Publish only a complete single-revision read. Failure keeps the previous cache.
        cachedClips = Collections.unmodifiableList(result)
        cachedRevision = revision
        return cachedClips
    }

    fun clear() { cachedRevision = null; cachedClips = emptyList() }
}
