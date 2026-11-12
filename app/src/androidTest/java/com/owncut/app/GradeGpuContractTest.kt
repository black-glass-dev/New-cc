package com.owncut.app

import android.content.Context
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.GLES20
import androidx.media3.common.C
import androidx.media3.common.ColorInfo
import androidx.media3.common.VideoFrameProcessingException
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.BaseGlShaderProgram
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer

/** Added, not yet compiled/run on Android. Synthetic texture: NOT decoder/codec parity. */
@UnstableApi
class GradeGpuContractTest {
    private fun snapshot(grade: ColorGrade): ClipSnapshot = EngineSession().use { session ->
        val id=session.addMediaClip("content://synthetic-not-decoded",1000)
        session.setClipGrades(listOf(id),grade)
        session.clips().single()
    }
    private fun info(transfer: Int=C.COLOR_TRANSFER_SDR, space: Int=C.COLOR_SPACE_BT709)=
        ColorInfo.Builder().setColorSpace(space).setColorRange(C.COLOR_RANGE_LIMITED).setColorTransfer(transfer).build()

    @Test fun unknownNon709AndHdrRefuseInsteadOfGuessing() {
        val clip=snapshot(ColorGrade(exposureStops=1.0))
        for (metadata in listOf(null,ColorInfo.Builder().build(),info(C.COLOR_TRANSFER_ST2084),
            info(C.COLOR_TRANSFER_HLG),info(space=C.COLOR_SPACE_BT2020))) {
            assertThrows(IllegalArgumentException::class.java) { GradedFramePipeline.prepare(clip,metadata) }
        }
        val prepared=GradedFramePipeline.prepare(clip,info())
        val context=ApplicationProvider.getApplicationContext<Context>()
        assertThrows(VideoFrameProcessingException::class.java) { prepared.effect.toGlShaderProgram(context,true) }
    }

    @Test fun explicitSdrTextureNeutralExposureAndBypassKeepAlpha() {
        val display=EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        val versions=IntArray(2)
        assertTrue(EGL14.eglInitialize(display,versions,0,versions,1))
        try {
            val configs=arrayOfNulls<EGLConfig>(1);val count=IntArray(1)
            val attributes=intArrayOf(EGL14.EGL_SURFACE_TYPE,EGL14.EGL_PBUFFER_BIT,
                EGL14.EGL_RENDERABLE_TYPE,EGL14.EGL_OPENGL_ES2_BIT,EGL14.EGL_RED_SIZE,8,
                EGL14.EGL_GREEN_SIZE,8,EGL14.EGL_BLUE_SIZE,8,EGL14.EGL_ALPHA_SIZE,8,EGL14.EGL_NONE)
            assertTrue(EGL14.eglChooseConfig(display,attributes,0,configs,0,1,count,0));assertTrue(count[0]>0)
            val eglContext=EGL14.eglCreateContext(display,configs[0],EGL14.EGL_NO_CONTEXT,
                intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION,2,EGL14.EGL_NONE),0)
            assertNotEquals(EGL14.EGL_NO_CONTEXT,eglContext)
            try {
                val surface=EGL14.eglCreatePbufferSurface(display,configs[0],
                    intArrayOf(EGL14.EGL_WIDTH,1,EGL14.EGL_HEIGHT,1,EGL14.EGL_NONE),0)
                assertNotEquals(EGL14.EGL_NO_SURFACE,surface)
                try {
                    assertTrue(EGL14.eglMakeCurrent(display,surface,surface,eglContext))
                    val ids=IntArray(1);GLES20.glGenTextures(1,ids,0)
                    try {
                        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D,ids[0])
                        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,GLES20.GL_TEXTURE_MIN_FILTER,GLES20.GL_NEAREST)
                        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,GLES20.GL_TEXTURE_MAG_FILTER,GLES20.GL_NEAREST)
                        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,GLES20.GL_TEXTURE_WRAP_S,GLES20.GL_CLAMP_TO_EDGE)
                        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,GLES20.GL_TEXTURE_WRAP_T,GLES20.GL_CLAMP_TO_EDGE)
                        val pixels=ByteBuffer.allocateDirect(4)
                        pixels.put(byteArrayOf(128.toByte(),128.toByte(),128.toByte(),37)).position(0)
                        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D,0,GLES20.GL_RGBA,1,1,0,GLES20.GL_RGBA,GLES20.GL_UNSIGNED_BYTE,pixels)
                        GLES20.glViewport(0,0,1,1);GLES20.glDisable(GLES20.GL_DITHER)
                        val context=ApplicationProvider.getApplicationContext<Context>()
                        for ((grade,expected) in listOf(ColorGrade() to 128,
                            ColorGrade(exposureStops=1.0) to 184,
                            ColorGrade(exposureStops=1.0,enabled=false) to 128)) {
                            val prepared=GradedFramePipeline.prepare(snapshot(grade),info())
                            val program=prepared.effect.toGlShaderProgram(context,false) as BaseGlShaderProgram
                            try {
                                val size=program.configure(1,1);assertEquals(1,size.width);assertEquals(1,size.height)
                                program.drawFrame(ids[0],123456L)
                                val output=ByteBuffer.allocateDirect(4)
                                GLES20.glReadPixels(0,0,1,1,GLES20.GL_RGBA,GLES20.GL_UNSIGNED_BYTE,output)
                                assertEquals(GLES20.GL_NO_ERROR,GLES20.glGetError())
                                for (channel in 0..2) assertEquals(expected.toDouble(),(output.get(channel).toInt() and 255).toDouble(),
                                    if (expected==128) 0.0 else 2.0)
                                assertEquals(37,output.get(3).toInt() and 255)
                            } finally { program.release() }
                        }
                    } finally { GLES20.glDeleteTextures(1,ids,0) }
                } finally {
                    EGL14.eglMakeCurrent(display,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_CONTEXT)
                    EGL14.eglDestroySurface(display,surface)
                }
            } finally { EGL14.eglDestroyContext(display,eglContext) }
        } finally { EGL14.eglTerminate(display) }
    }
}
