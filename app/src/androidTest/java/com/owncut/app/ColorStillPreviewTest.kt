package com.owncut.app

import android.graphics.Bitmap
import org.junit.Assert.*
import org.junit.Test

/** Android Bitmap transport assertions: sources added, not compiled/run here. */
class ColorStillPreviewTest {
    @Test fun straightArgbBitmapPixelsReachNativeGradeAndKeepAlpha() {
        val bitmap=Bitmap.createBitmap(intArrayOf(0xff808080.toInt(),0xffff0000.toInt()),2,1,Bitmap.Config.ARGB_8888)
        try {
            assertTrue(bitmap.colorSpace?.isSrgb==true)
            val input=IntArray(2);bitmap.getPixels(input,0,2,0,0,2,1)
            val original=input.copyOf()
            val output=ColorFrameBuffers.render(input,2,1,ColorGrade(exposureStops=1.0))
            assertArrayEquals(intArrayOf(0xffb0b0b0.toInt(),0xffff0000.toInt()),output)
            assertArrayEquals(original,input)
            assertArrayEquals(input,ColorFrameBuffers.render(input,2,1,ColorGrade(enabled=false,exposureStops=1.0)))
        } finally { bitmap.recycle() }
    }
    @Test fun draftNeverMutatesSavedNativeGradeUntilExplicitApply() {
        EngineSession().use { session ->
            val id=session.addMediaClip("content://draft-only",1000)
            val original=session.clips().single().grade
            val entries=original.parametersV1().map { it.toString() }.toMutableList().apply { set(0,"১.০") }
            val draft=ColorGradeDraft.parse(entries,true)
            ColorFrameBuffers.render(intArrayOf(0xff808080.toInt()),1,1,draft)
            assertEquals(original,session.clips().single().grade) // render and cancel-equivalent: no edit
            session.setClipGrades(listOf(id),draft)
            assertEquals(draft,session.clips().single().grade)
            assertTrue(session.undo());assertEquals(original,session.clips().single().grade)
            assertTrue(session.redo());assertEquals(draft,session.clips().single().grade)
        }
    }
}
