package com.owncut.app

import android.content.Context
import android.opengl.GLES20
import androidx.media3.common.ColorInfo
import androidx.media3.common.VideoFrameProcessingException
import androidx.media3.common.util.GlProgram
import androidx.media3.common.util.GlUtil
import androidx.media3.common.util.Size
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.BaseGlShaderProgram
import androidx.media3.effect.DefaultVideoFrameProcessor
import androidx.media3.effect.GlEffect
import androidx.media3.effect.GlShaderProgram

/** Shared adapter for isolated experimental preview and candidate export.
 * NOT enabled in normal timeline playback or user delivery yet.
 * Always use the returned effect WITH its explicit processor factory. Media3 1.8.1
 * DEFAULT intermediate SDR is encoded SMPTE 170M, not the interface doc's linear RGB.
 * Input metadata must be explicit BT709 SDR; no unknown/HDR/other-primary fallback.
 */
@UnstableApi
internal object GradedFramePipeline {
    data class Prepared(val effect: GlEffect, val factory: DefaultVideoFrameProcessor.Factory)

    /** Synthetic adapter helper; ColorInfo alone does not prove original source tag provenance. */
    fun prepare(clip: ClipSnapshot, sourceColorInfo: ColorInfo?): Prepared {
        ColorEngine.validateVideoSource(VideoSourceProbe.color(sourceColorInfo))
        return prepareSources(ColorEngine.shaderSources(clip.grade,ColorEngine.TextureEncoding.MEDIA3_SDR_709))
    }

    fun prepare(clip: ColorRenderPlan.PreparedClip, onDraw: (() -> Unit)?=null): Prepared = prepareSources(clip.shaders,onDraw)

    data class PreparedPlan(val effects: List<GlEffect>, val factory: DefaultVideoFrameProcessor.Factory)
    fun prepare(plan: ColorRenderPlan, checkCancelled: () -> Unit): PreparedPlan {
        val effects=plan.clips.map { checkCancelled(); effect(it.shaders) }
        checkCancelled()
        return PreparedPlan(java.util.Collections.unmodifiableList(effects),processorFactory())
    }

    private fun processorFactory(replayable: Boolean=false)=DefaultVideoFrameProcessor.Factory.Builder()
        .setSdrWorkingColorSpace(DefaultVideoFrameProcessor.WORKING_COLOR_SPACE_DEFAULT)
        .setEnableReplayableCache(replayable).build()

    /** Public Media3 cache + one stable effect. Replacing the effect list would drain the cache. */
    class LivePrepared(sources: ColorEngine.ShaderSources, onDraw: () -> Unit) {
        private val latest=java.util.concurrent.atomic.AtomicReference(DrawRecipe(sources,onDraw))
        val prepared=Prepared(object: GlEffect {
            override fun toGlShaderProgram(context: Context,useHdr: Boolean): GlShaderProgram {
                if(useHdr) throw VideoFrameProcessingException("HDR grading is unsupported")
                return Program(latest.get()) { latest.get() }
            }
        },processorFactory(replayable=true))
        fun update(sources: ColorEngine.ShaderSources,onDraw: () -> Unit) {
            latest.set(DrawRecipe(sources,onDraw))
        }
    }
    private data class DrawRecipe(val sources: ColorEngine.ShaderSources,val onDraw: (() -> Unit)?)
    private fun prepareSources(sources: ColorEngine.ShaderSources, onDraw: (() -> Unit)?=null)=Prepared(effect(sources,onDraw),processorFactory())
    private fun effect(sources: ColorEngine.ShaderSources, onDraw: (() -> Unit)?=null): GlEffect = object : GlEffect {
        override fun toGlShaderProgram(context: Context, useHdr: Boolean): GlShaderProgram {
            if (useHdr) throw VideoFrameProcessingException("HDR grading is not supported by this SDR adapter")
            return Program(DrawRecipe(sources,onDraw))
        }
    }

    private class Program(initial: DrawRecipe,private val readLatest: (() -> DrawRecipe)?=null) : BaseGlShaderProgram(false,1) {
        private var recipe=initial
        private var program: GlProgram
        init {
            val range=IntArray(2); val precision=IntArray(1)
            GLES20.glGetShaderPrecisionFormat(GLES20.GL_FRAGMENT_SHADER,GLES20.GL_HIGH_FLOAT,range,0,precision,0)
            if (precision[0] < 23 || range[0] < 126 || range[1] < 127)
                throw VideoFrameProcessingException("Full float32 fragment highp is required for native grading")
            program=compile(initial.sources)
        }
        private fun compile(sources: ColorEngine.ShaderSources): GlProgram {
            try {
                GlUtil.checkGlError()
                val created=GlProgram(sources.vertex,sources.fragment)
                try {
                    created.setBufferAttribute("aPosition",floatArrayOf(-1f,-1f,0f,1f, 1f,-1f,0f,1f,
                        -1f,1f,0f,1f, 1f,1f,0f,1f),4)
                    created.setBufferAttribute("aTexCoord",floatArrayOf(0f,0f,1f,0f,0f,1f,1f,1f),2)
                    return created
                } catch(error: Exception) {
                    runCatching { created.delete() }.exceptionOrNull()?.let { error.addSuppressed(it) }
                    throw error
                }
            } catch(error: Exception) { throw VideoFrameProcessingException(error) }
        }
        override fun configure(inputWidth: Int, inputHeight: Int): Size {
            if (inputWidth <= 0 || inputHeight <= 0) throw VideoFrameProcessingException("Invalid grading frame dimensions")
            return Size(inputWidth,inputHeight)
        }
        override fun drawFrame(inputTexId: Int, presentationTimeUs: Long) {
            try {
                val next=readLatest?.invoke() ?: recipe
                if(next !== recipe) {
                    val replacement=compile(next.sources)
                    try { program.delete() }
                    catch(error: Exception) {
                        runCatching { replacement.delete() }.exceptionOrNull()?.let { error.addSuppressed(it) }
                        throw error
                    }
                    program=replacement;recipe=next
                }
                program.use()
                program.setSamplerTexIdUniform("uSource",inputTexId,0)
                program.bindAttributesAndUniforms()
                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP,0,4)
                GlUtil.checkGlError()
                recipe.onDraw?.invoke()
            } catch (error: Exception) { throw VideoFrameProcessingException(error,presentationTimeUs) }
        }
        override fun release() {
            var failure: Exception?=null
            try { program.delete() } catch (error: Exception) { failure=error }
            try { super.release() } catch (error: Exception) {
                if (failure == null) failure=error else failure.addSuppressed(error)
            }
            failure?.let { throw VideoFrameProcessingException(it) }
        }
    }
}
