package com.owncut.app

import android.media.metrics.LogSessionId
import android.view.Surface
import androidx.media3.common.Format
import androidx.media3.common.util.UnstableApi
import androidx.media3.transformer.Codec
import androidx.media3.transformer.ExportException

/** Candidate-only Media3 decoder boundary. Audio stays delegated. Every rendered video
 * release overload is guarded; discarded buffers do not require a verified format.
 * Input contract must match an admitted plan descriptor. Codec factory API has no URI,
 * so matching metadata is NOT proof of clip/source byte identity or callback order.
 */
@UnstableApi
internal class GuardedDecoderFactory(
    private val platform: Codec.DecoderFactory,
    plan: ColorRenderPlan,
) : Codec.DecoderFactory by platform {
    private val admitted=plan.clips.map {
        ColorEngine.DecodedVideoFormat(it.source.width,it.source.height,it.source.color)
    }.distinct()

    override fun createForVideoDecoding(format: Format, outputSurface: Surface,
        requestSdrToneMapping: Boolean, logSessionId: LogSessionId?): Codec {
        val input=descriptor(format)
        try {
            require(!requestSdrToneMapping) { "Tone mapping is not admitted by the SDR color candidate" }
            require(input in admitted) { "Decoder input differs from probed color plan" }
            ColorEngine.validateDecodedFormat(input,input)
        } catch (error: Exception) { throw unsupported(IllegalArgumentException(
            "Graded export decoder-input: actual=${input.width}x${input.height} admitted=" +
                admitted.joinToString { "${it.width}x${it.height}" } +
                " requested-sdr-tone-map=$requestSdrToneMapping",error)) }
        val codec=platform.createForVideoDecoding(format,outputSurface,false,logSessionId)
        try { return GuardedCodec(codec,input) }
        catch (error: Throwable) {
            runCatching { codec.release() }.exceptionOrNull()?.let { error.addSuppressed(it) }
            throw error
        }
    }

    internal class GuardedCodec(private val codec: Codec, private val expected: ColorEngine.DecodedVideoFormat) : Codec by codec {
        private val owner=Thread.currentThread()
        private val gate=DecoderColorGate(expected)
        private var released=false
        private fun ownerOnly() { check(Thread.currentThread() === owner) { "Decoder must stay on its owner thread" } }
        private fun verify() {
            ownerOnly()
            var actual: ColorEngine.DecodedVideoFormat?=null
            try { gate.beforeRender { codec.outputFormat?.let { descriptor(it).also { value -> actual=value } } } }
            catch (error: Exception) {
                gate.invalidate()
                throw unsupported(IllegalArgumentException(
                    "Graded export decoder-output: expected-visible=${expected.width}x${expected.height} " +
                        "actual-format=${actual?.width}x${actual?.height}; " +
                        "Media3 Codec.Format does not expose raw crop; no padding inference",error))
            }
        }
        override fun releaseOutputBuffer(render: Boolean) {
            ownerOnly();check(!released) { "Decoder is released" }
            if(render) verify()
            try { codec.releaseOutputBuffer(render) }
            catch (error: Exception) { gate.invalidate();throw error }
        }
        override fun releaseOutputBuffer(renderPresentationTimeUs: Long) {
            ownerOnly();check(!released) { "Decoder is released" }
            verify()
            try { codec.releaseOutputBuffer(renderPresentationTimeUs) }
            catch (error: Exception) { gate.invalidate();throw error }
        }
        override fun release() {
            ownerOnly()
            if(released) return
            released=true
            try { codec.release() } finally { gate.close() }
        }
    }
    companion object {
        internal fun descriptor(format: Format)=ColorEngine.DecodedVideoFormat(
            format.width,format.height,VideoSourceProbe.color(format.colorInfo))
        private fun unsupported(error: Exception)=ExportException.createForAssetLoader(
            error,ExportException.ERROR_CODE_DECODING_FORMAT_UNSUPPORTED)
    }
}
