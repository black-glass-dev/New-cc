package com.owncut.app

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLExt
import android.opengl.GLES20
import java.io.File
import java.util.concurrent.TimeUnit

/** Original device-generated neutral RGB signal (.4,.4,.4), encoder explicitly configured
 * Rec709 limited SDR. Not relabelled imported media. Surface input avoids guessing vendor
 * YUV planes/strides. No codec/GL/device execution has been verified yet.
 */
internal object GrayVideoFixture {
    private const val EGL_RECORDABLE_ANDROID=0x3142
    fun create(file: File) {
        check(EGL14.eglGetCurrentContext()==EGL14.EGL_NO_CONTEXT) { "Fixture must own its test EGL context" }
        val codec=MediaCodec.createEncoderByType("video/avc")
        var success=false
        try {
            val format=MediaFormat.createVideoFormat("video/avc",128,128).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT,MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE,500_000);setInteger(MediaFormat.KEY_FRAME_RATE,30)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL,1)
                setInteger(MediaFormat.KEY_COLOR_STANDARD,MediaFormat.COLOR_STANDARD_BT709)
                setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
                setInteger(MediaFormat.KEY_COLOR_RANGE,MediaFormat.COLOR_RANGE_LIMITED)
            }
            codec.configure(format,null,null,MediaCodec.CONFIGURE_FLAG_ENCODE)
            val input=codec.createInputSurface()
            try {
                val display=EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
                check(EGL14.eglInitialize(display,IntArray(2),0,IntArray(2),0))
                try {
                    val configs=arrayOfNulls<EGLConfig>(1);val count=IntArray(1)
                    check(EGL14.eglChooseConfig(display,intArrayOf(EGL14.EGL_RED_SIZE,8,EGL14.EGL_GREEN_SIZE,8,
                        EGL14.EGL_BLUE_SIZE,8,EGL14.EGL_RENDERABLE_TYPE,EGL14.EGL_OPENGL_ES2_BIT,
                        EGL14.EGL_SURFACE_TYPE,EGL14.EGL_WINDOW_BIT,EGL_RECORDABLE_ANDROID,1,EGL14.EGL_NONE),0,configs,0,1,count,0) && count[0]>0)
                    val config=requireNotNull(configs[0])
                    val gl=EGL14.eglCreateContext(display,config,EGL14.EGL_NO_CONTEXT,
                        intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION,2,EGL14.EGL_NONE),0)
                    check(gl!=EGL14.EGL_NO_CONTEXT)
                    try {
                        val surface=EGL14.eglCreateWindowSurface(display,config,input,intArrayOf(EGL14.EGL_NONE),0)
                        check(surface!=EGL14.EGL_NO_SURFACE)
                        try {
                            check(EGL14.eglMakeCurrent(display,surface,surface,gl))
                            codec.start()
                            try {
                                val muxer=MediaMuxer(file.absolutePath,MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
                                var muxerStarted=false
                                try {
                                    val info=MediaCodec.BufferInfo();var track=-1;var eos=false
                                    val started=System.nanoTime()
                                    fun drain(final: Boolean) {
                                        while(true) {
                                            check(!Thread.currentThread().isInterrupted) { "Fixture interrupted" }
                                            check(System.nanoTime()-started<TimeUnit.SECONDS.toNanos(45)) { "Fixture encoder timeout" }
                                            when(val index=codec.dequeueOutputBuffer(info,if(final) 10_000L else 0L)) {
                                                MediaCodec.INFO_TRY_AGAIN_LATER -> if(!final) return
                                                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                                                    check(track<0)
                                                    val authored=codec.outputFormat.apply {
                                                        // Known authored signal semantics, never guessed imported tags.
                                                        setInteger(MediaFormat.KEY_COLOR_STANDARD,MediaFormat.COLOR_STANDARD_BT709)
                                                        setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
                                                        setInteger(MediaFormat.KEY_COLOR_RANGE,MediaFormat.COLOR_RANGE_LIMITED)
                                                    }
                                                    track=muxer.addTrack(authored);muxer.start();muxerStarted=true
                                                }
                                                else -> if(index>=0) {
                                                    try {
                                                        if(info.size>0 && (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG)==0) {
                                                            check(muxerStarted)
                                                            val bytes=requireNotNull(codec.getOutputBuffer(index))
                                                            bytes.position(info.offset);bytes.limit(info.offset+info.size)
                                                            muxer.writeSampleData(track,bytes,info)
                                                        }
                                                        eos=(info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM)!=0
                                                    } finally { codec.releaseOutputBuffer(index,false) }
                                                    if(eos) return
                                                }
                                            }
                                        }
                                    }
                                    GLES20.glViewport(0,0,128,128);GLES20.glDisable(GLES20.GL_DITHER)
                                    GLES20.glClearColor(.4f,.4f,.4f,1f)
                                    for(frame in 0 until 30) {
                                        drain(false)
                                        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                                        check(GLES20.glGetError()==GLES20.GL_NO_ERROR)
                                        check(EGLExt.eglPresentationTimeANDROID(display,surface,frame*1_000_000_000L/30))
                                        check(EGL14.eglSwapBuffers(display,surface))
                                    }
                                    codec.signalEndOfInputStream();drain(true)
                                    check(eos && muxerStarted)
                                    muxer.stop();muxerStarted=false
                                } finally {
                                    try { if(muxerStarted) muxer.stop() } finally { muxer.release() }
                                }
                            } finally { codec.stop() }
                        } finally {
                            EGL14.eglMakeCurrent(display,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_CONTEXT)
                            EGL14.eglDestroySurface(display,surface)
                        }
                    } finally { EGL14.eglDestroyContext(display,gl) }
                } finally { EGL14.eglTerminate(display) }
            } finally { input.release() }
            check(file.length()>0);success=true
        } finally { try { codec.release() } finally { if(!success) file.delete() } }
    }
}
