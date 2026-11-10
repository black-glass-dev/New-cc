package com.owncut.app

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.RectF
import android.opengl.GLES20
import android.opengl.GLUtils
import androidx.media3.common.C
import androidx.media3.common.VideoFrameProcessingException
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.BaseAudioProcessor
import androidx.media3.common.util.GlProgram
import androidx.media3.common.util.GlUtil
import androidx.media3.common.util.Size
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.BaseGlShaderProgram
import androidx.media3.effect.GlEffect
import androidx.media3.effect.GlShaderProgram
import java.nio.ByteBuffer
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

@UnstableApi
internal class TimelineClipGlEffect(
    private val fx: ClipRichFx,
    private val clipDurationUs: Long,
) : GlEffect {
    override fun toGlShaderProgram(context: Context, useHdr: Boolean): GlShaderProgram {
        if (useHdr) throw VideoFrameProcessingException("HDR clip FX is unsupported")
        return ClipProgram(fx, clipDurationUs.coerceAtLeast(1L))
    }

    private class ClipProgram(
        private val fx: ClipRichFx,
        private val durationUs: Long,
    ) : BaseGlShaderProgram(false, 1) {
        private var program: GlProgram
        private var aspect = 16f / 9f
        private val chromaRgb: FloatArray = parseHexRgb(fx.chromaKeyHex)
        private val hasChroma = fx.chromaKeyHex.isNotBlank() && fx.chromaTolerance > 0.01f

        init {
            try {
                GlUtil.checkGlError()
                val created = GlProgram(VERTEX_SHADER, FRAGMENT_SHADER)
                created.setBufferAttribute(
                    "aPosition",
                    floatArrayOf(-1f, -1f, 0f, 1f, 1f, -1f, 0f, 1f, -1f, 1f, 0f, 1f, 1f, 1f, 0f, 1f),
                    4,
                )
                created.setBufferAttribute(
                    "aTexCoord",
                    floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f),
                    2,
                )
                program = created
            } catch (e: Exception) {
                throw VideoFrameProcessingException(e)
            }
        }

        override fun configure(inputWidth: Int, inputHeight: Int): Size {
            if (inputWidth <= 0 || inputHeight <= 0) {
                throw VideoFrameProcessingException("Invalid clip dimensions")
            }
            aspect = inputWidth.toFloat() / inputHeight.toFloat()
            return Size(inputWidth, inputHeight)
        }

        override fun drawFrame(inputTexId: Int, presentationTimeUs: Long) {
            try {
                val u = (presentationTimeUs.toFloat() / durationUs.toFloat()).coerceIn(0f, 1f)
                val effScale = fx.effectiveScaleAt(u).coerceIn(0.1f, 8f)
                val rad = Math.toRadians(fx.rotationDeg.toDouble())
                val cosR = cos(rad).toFloat()
                val sinR = sin(rad).toFloat()

                val localMs = (presentationTimeUs / 1000L).coerceAtLeast(0L)
                val durMs = (durationUs / 1000L).coerceAtLeast(1L)
                var brightnessMul = fx.opacity.coerceIn(0f, 1f)
                if (fx.transitionIn == "fade_black" && fx.transitionMs > 0L && localMs < fx.transitionMs) {
                    brightnessMul *= (localMs.toFloat() / fx.transitionMs.toFloat()).coerceIn(0f, 1f)
                }
                if (fx.fadeInMs > 0L && localMs < fx.fadeInMs) {
                    brightnessMul *= (localMs.toFloat() / fx.fadeInMs.toFloat()).coerceIn(0f, 1f)
                }
                if (fx.fadeOutMs > 0L && (durMs - localMs) < fx.fadeOutMs) {
                    brightnessMul *= ((durMs - localMs).toFloat() / fx.fadeOutMs.toFloat()).coerceIn(0f, 1f)
                }

                program.use()
                program.setSamplerTexIdUniform("uSource", inputTexId, 0)
                program.setFloatUniform("uAspect", aspect)
                program.setFloatUniform("uInvScale", 1f / effScale)
                program.setFloatUniform("uPanX", fx.panX)
                program.setFloatUniform("uPanY", fx.panY)
                program.setFloatUniform("uCosR", cosR)
                program.setFloatUniform("uSinR", sinR)
                program.setFloatUniform("uAlphaMul", brightnessMul.coerceIn(0f, 1f))
                program.setFloatUniform("uChromaEnabled", if (hasChroma) 1f else 0f)
                program.setFloatUniform("uChromaTol", fx.chromaTolerance.coerceIn(0.02f, 0.8f))
                program.setFloatsUniformIfPresent("uChromaRgb", chromaRgb)
                program.bindAttributesAndUniforms()
                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
                GlUtil.checkGlError()
            } catch (e: Exception) {
                throw VideoFrameProcessingException(e, presentationTimeUs)
            }
        }

        override fun release() {
            var failure: Exception? = null
            try {
                program.delete()
            } catch (e: Exception) {
                failure = e
            }
            try {
                super.release()
            } catch (e: Exception) {
                if (failure == null) failure = e else failure.addSuppressed(e)
            }
            failure?.let { throw VideoFrameProcessingException(it) }
        }

        companion object {
            private fun parseHexRgb(hex: String): FloatArray {
                val c = runCatching { Color.parseColor(hex) }.getOrNull() ?: return floatArrayOf(0f, 1f, 0f)
                return floatArrayOf(Color.red(c) / 255f, Color.green(c) / 255f, Color.blue(c) / 255f)
            }

            private const val VERTEX_SHADER = """
                attribute vec4 aPosition;
                attribute vec2 aTexCoord;
                varying vec2 vTexCoord;
                void main() {
                    gl_Position = aPosition;
                    vTexCoord = aTexCoord;
                }
            """

            private const val FRAGMENT_SHADER = """
                precision highp float;
                uniform sampler2D uSource;
                uniform float uAspect;
                uniform float uInvScale;
                uniform float uPanX;
                uniform float uPanY;
                uniform float uCosR;
                uniform float uSinR;
                uniform float uAlphaMul;
                uniform float uChromaEnabled;
                uniform float uChromaTol;
                uniform vec3 uChromaRgb;
                varying vec2 vTexCoord;

                void main() {
                    vec2 p = vTexCoord - vec2(0.5 + uPanX, 0.5 - uPanY);
                    p.x *= uAspect;
                    vec2 rot = vec2(p.x * uCosR - p.y * uSinR, p.x * uSinR + p.y * uCosR);
                    rot *= uInvScale;
                    rot.x /= uAspect;
                    vec2 uv = rot + vec2(0.5, 0.5);

                    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
                        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                        return;
                    }
                    vec4 c = texture2D(uSource, uv);
                    float factor = uAlphaMul;
                    if (uChromaEnabled > 0.5) {
                        float d = distance(c.rgb, uChromaRgb);
                        float matte = smoothstep(uChromaTol * 0.7, uChromaTol * 1.25, d);
                        factor *= matte;
                    }
                    gl_FragColor = vec4(c.rgb * factor, c.a);
                }
            """
        }
    }
}

@UnstableApi
internal class TextBurnGlEffect(
    private val doc: TimelineRichDocument,
) : GlEffect {
    override fun toGlShaderProgram(context: Context, useHdr: Boolean): GlShaderProgram {
        if (useHdr) throw VideoFrameProcessingException("HDR overlay burn-in unsupported")
        return TextBurnProgram(doc)
    }

    private class TextBurnProgram(
        private val doc: TimelineRichDocument,
    ) : BaseGlShaderProgram(false, 1) {
        private var program: GlProgram
        private var overlayTexId: Int = 0
        private var outW = 1280
        private var outH = 720
        private var overlayBitmap: Bitmap? = null
        private var overlayCanvas: Canvas? = null
        private var lastSignature: String = ""

        private val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG)
        private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
        private val outlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER
            style = Paint.Style.STROKE
            color = Color.BLACK
        }
        private val subBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(196, 10, 12, 16) }
        private val subTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textAlign = Paint.Align.CENTER
        }
        private val tempRect = RectF()

        init {
            try {
                GlUtil.checkGlError()
                val created = GlProgram(VERTEX_SHADER, FRAGMENT_SHADER)
                created.setBufferAttribute(
                    "aPosition",
                    floatArrayOf(-1f, -1f, 0f, 1f, 1f, -1f, 0f, 1f, -1f, 1f, 0f, 1f, 1f, 1f, 0f, 1f),
                    4,
                )
                created.setBufferAttribute(
                    "aTexCoord",
                    floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f),
                    2,
                )
                program = created
                val tex = IntArray(1)
                GLES20.glGenTextures(1, tex, 0)
                overlayTexId = tex[0]
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, overlayTexId)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
                GlUtil.checkGlError()
            } catch (e: Exception) {
                throw VideoFrameProcessingException(e)
            }
        }

        override fun configure(inputWidth: Int, inputHeight: Int): Size {
            if (inputWidth <= 0 || inputHeight <= 0) throw VideoFrameProcessingException("Invalid overlay frame dimensions")
            outW = inputWidth
            outH = inputHeight
            if (overlayBitmap == null || overlayBitmap?.width != outW || overlayBitmap?.height != outH) {
                overlayBitmap?.recycle()
                val bmp = Bitmap.createBitmap(outW, outH, Bitmap.Config.ARGB_8888)
                overlayBitmap = bmp
                overlayCanvas = Canvas(bmp)
                lastSignature = ""
                GLES20.glActiveTexture(GLES20.GL_TEXTURE1)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, overlayTexId)
                GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bmp, 0)
            }
            return Size(inputWidth, inputHeight)
        }

        override fun drawFrame(inputTexId: Int, presentationTimeUs: Long) {
            try {
                val timeMs = (presentationTimeUs / 1000L).coerceAtLeast(0L)
                val activeOv = doc.overlays.filter { it.isVisibleAt(timeMs) }
                val activeSub = doc.subtitles.lastOrNull { it.isVisibleAt(timeMs) }
                val hasContent = activeOv.isNotEmpty() || (activeSub != null && activeSub.text.isNotBlank())

                if (hasContent) {
                    val sig = buildString {
                        activeOv.forEach { append(it.id).append('@').append(it.x).append(',').append(it.y).append(';') }
                        if (activeSub != null) append(activeSub.id).append(';').append(activeSub.text)
                    }
                    if (sig != lastSignature) {
                        lastSignature = sig
                        renderOverlayBitmap(activeOv, activeSub)
                    }
                }

                program.use()
                program.setSamplerTexIdUniform("uSource", inputTexId, 0)
                program.setSamplerTexIdUniform("uOverlay", overlayTexId, 1)
                program.setFloatUniform("uHasOverlay", if (hasContent) 1f else 0f)
                program.bindAttributesAndUniforms()
                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
                GlUtil.checkGlError()
            } catch (e: Exception) {
                throw VideoFrameProcessingException(e, presentationTimeUs)
            }
        }

        private fun renderOverlayBitmap(activeOv: List<TextOverlayItem>, activeSub: SubtitleCueItem?) {
            val bmp = overlayBitmap ?: return
            val canvas = overlayCanvas ?: return
            canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)
            val unit = outH / 400f

            for (ov in activeOv) {
                val cx = ov.x * outW
                val cy = ov.y * outH
                textPaint.textSize = (22f * unit) * ov.scale.coerceIn(0.4f, 3f)
                textPaint.isFakeBoldText = ov.bold
                textPaint.color = runCatching { Color.parseColor(ov.fgColorHex) }.getOrDefault(Color.WHITE)
                outlinePaint.textSize = textPaint.textSize
                outlinePaint.isFakeBoldText = ov.bold
                outlinePaint.strokeWidth = 3f * unit

                val lines = ov.text.split('\n').ifEmpty { listOf("") }
                val lineH = textPaint.fontSpacing
                val maxW = lines.maxOf { textPaint.measureText(it) }
                val padX = 16f * unit
                val padY = 8f * unit
                val boxW = maxW + padX * 2f
                val boxH = lineH * lines.size + padY * 2f
                tempRect.set(cx - boxW / 2f, cy - boxH / 2f, cx + boxW / 2f, cy + boxH / 2f)

                canvas.save()
                canvas.rotate(ov.rotationDeg, cx, cy)
                if (ov.style != "outline") {
                    bgPaint.color = runCatching { Color.parseColor(ov.bgColorHex) }.getOrDefault(Color.argb(180, 0, 0, 0))
                    val r = if (ov.style == "pill") boxH / 2f else 8f * unit
                    canvas.drawRoundRect(tempRect, r, r, bgPaint)
                }
                var ty = tempRect.top + padY - textPaint.ascent()
                for (line in lines) {
                    if (ov.style == "outline") canvas.drawText(line, cx, ty, outlinePaint)
                    canvas.drawText(line, cx, ty, textPaint)
                    ty += lineH
                }
                canvas.restore()
            }

            if (activeSub != null && activeSub.text.isNotBlank()) {
                subTextPaint.textSize = 18f * unit
                val lines = activeSub.text.split('\n')
                val lineH = subTextPaint.fontSpacing
                val maxW = lines.maxOf { subTextPaint.measureText(it) }
                val boxW = min(outW * 0.88f, maxW + 26f * unit)
                val boxH = lineH * lines.size + 14f * unit
                val cx = outW / 2f
                val bottom = outH - 22f * unit
                tempRect.set(cx - boxW / 2f, bottom - boxH, cx + boxW / 2f, bottom)
                canvas.drawRoundRect(tempRect, 8f * unit, 8f * unit, subBgPaint)
                var ty = tempRect.top + 7f * unit - subTextPaint.ascent()
                for (line in lines) {
                    canvas.drawText(line, cx, ty, subTextPaint)
                    ty += lineH
                }
            }

            GLES20.glActiveTexture(GLES20.GL_TEXTURE1)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, overlayTexId)
            GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bmp, 0)
        }

        override fun release() {
            var failure: Exception? = null
            try {
                if (overlayTexId != 0) {
                    GLES20.glDeleteTextures(1, intArrayOf(overlayTexId), 0)
                    overlayTexId = 0
                }
                overlayBitmap?.recycle()
                overlayBitmap = null
                overlayCanvas = null
                program.delete()
            } catch (e: Exception) {
                failure = e
            }
            try {
                super.release()
            } catch (e: Exception) {
                if (failure == null) failure = e else failure.addSuppressed(e)
            }
            failure?.let { throw VideoFrameProcessingException(it) }
        }

        companion object {
            private const val VERTEX_SHADER = """
                attribute vec4 aPosition;
                attribute vec2 aTexCoord;
                varying vec2 vTexCoord;
                void main() {
                    gl_Position = aPosition;
                    vTexCoord = aTexCoord;
                }
            """

            private const val FRAGMENT_SHADER = """
                precision mediump float;
                uniform sampler2D uSource;
                uniform sampler2D uOverlay;
                uniform float uHasOverlay;
                varying vec2 vTexCoord;

                void main() {
                    vec4 base = texture2D(uSource, vTexCoord);
                    if (uHasOverlay < 0.5) {
                        gl_FragColor = base;
                        return;
                    }
                    vec2 ovUv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);
                    vec4 ov = texture2D(uOverlay, ovUv);
                    float a = ov.a;
                    vec3 mixed = base.rgb * (1.0 - a) + ov.rgb * a;
                    gl_FragColor = vec4(mixed, base.a);
                }
            """
        }
    }
}

@UnstableApi
internal class ClipVolumeAudioProcessor(
    private val fx: ClipRichFx,
    private val clipDurationMs: Long,
    private val duckFactor: Float = 1.0f,
) : BaseAudioProcessor() {
    private var processedFrames: Long = 0L

    override fun onConfigure(inputAudioFormat: AudioProcessor.AudioFormat): AudioProcessor.AudioFormat {
        if (inputAudioFormat.encoding != C.ENCODING_PCM_16BIT) {
            throw AudioProcessor.UnhandledAudioFormatException(inputAudioFormat)
        }
        return inputAudioFormat
    }

    override fun queueInput(inputBuffer: ByteBuffer) {
        val remaining = inputBuffer.remaining()
        if (remaining == 0) return
        val out = replaceOutputBuffer(remaining)
        out.order(inputBuffer.order())
        val channels = max(1, inputAudioFormat.channelCount)
        val sampleRate = max(1, inputAudioFormat.sampleRate)
        while (inputBuffer.remaining() >= 2) {
            val localMs = (processedFrames * 1000L) / sampleRate
            val gain = fx.effectiveGainAt(localMs, clipDurationMs, duckFactor)
            for (ch in 0 until channels) {
                if (inputBuffer.remaining() < 2) break
                val raw = inputBuffer.short.toFloat()
                val scaled = (raw * gain).roundToInt().coerceIn(-32768, 32767).toShort()
                out.putShort(scaled)
            }
            processedFrames++
        }
        if (inputBuffer.hasRemaining()) {
            out.put(inputBuffer)
        }
        out.flip()
    }

    override fun onFlush() {
        processedFrames = 0L
    }

    override fun onReset() {
        processedFrames = 0L
    }
}
