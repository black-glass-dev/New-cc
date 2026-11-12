package com.owncut.app

import org.junit.Assert.*
import org.junit.Test
import kotlin.random.Random

/** Android sources: execution/compilation must be recorded separately from host tests. */
class TimelinePagingTest {
    @Test fun revisionPagesAndCacheFollowEveryMutation() {
        EngineSession().use { session ->
            val empty = session.clips()
            assertSame(empty,session.clips())
            repeat(270) { session.addMediaClip("content://page/$it",1000) }
            val first = session.clipPage(0)
            val last = session.clipPage(first.nextOffset,expectedRevision=first.revision)
            assertEquals(256,first.clips.size); assertEquals(14,last.clips.size)
            val all = session.clips()
            assertEquals(first.clips + last.clips,all)
            assertSame(all,session.clips())
            assertThrows(UnsupportedOperationException::class.java) { (all as MutableList<ClipSnapshot>).clear() }
            session.deleteClip(all.last().id)
            assertThrows(IllegalArgumentException::class.java) { session.clipPage(256,expectedRevision=first.revision) }
            assertEquals(269,session.clips().size)
            assertTrue(session.undo())
            val restored = session.clips()
            assertEquals(all,restored); assertNotSame(all,restored)
            assertTrue(session.redo()); assertEquals(269,session.clips().size)
        }
    }

    @Test fun intervalIndexMatchesBruteForceIncludingOverlapsAndEdges() {
        val random = Random(641)
        val clips = List(8192) { i ->
            ClipSnapshot("clip-$i","v${i%3}","source",random.nextLong(0,200000),
                random.nextLong(1,10000),0,1000)
        }
        val index = TimelineClipIndex(clips)
        repeat(300) {
            val left = random.nextLong(-1000,210000)
            val right = left + random.nextLong(0,2000)
            val expected = clips.indices.filter { clips[it].startFrame <= right && clips[it].startFrame + clips[it].durationFrames >= left }
            assertArrayEquals(expected.toIntArray(),index.intersecting(left,right))
        }
        assertTrue(TimelineClipIndex(emptyList()).intersecting(0,100).isEmpty())
        assertThrows(IllegalArgumentException::class.java) { index.intersecting(2,1) }
    }
}
