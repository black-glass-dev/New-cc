package com.owncut.app

import android.content.Context
import android.media.MediaFormat
import android.os.Handler
import androidx.media3.common.C
import androidx.media3.common.ColorInfo
import androidx.media3.common.Format
import androidx.media3.common.PlaybackException
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.SingleInputVideoGraph
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.Renderer
import androidx.media3.exoplayer.mediacodec.MediaCodecAdapter
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector
import androidx.media3.exoplayer.video.MediaCodecVideoRenderer
import androidx.media3.exoplayer.video.PlaybackVideoGraphWrapper
import androidx.media3.exoplayer.video.VideoFrameReleaseControl
import androidx.media3.exoplayer.video.VideoSink
import androidx.media3.exoplayer.video.VideoRendererEventListener
import java.nio.ByteBuffer

/** Isolated opt-in player; does not replace the working timeline MediaPlayer lifecycle.
 * Renderer OWNS its video sink: base class creates/releases the graph through our factory.
 * Explicit same DEFAULT SDR texture pipeline as candidate export, never interface defaults.
 */
@UnstableApi
internal class GradedPreviewRenderers(context: Context, private val prepared: GradedFramePipeline.Prepared,
    private val source: VideoSourceMetadata, private val interpretMissing: Boolean=false) : DefaultRenderersFactory(context) {
    override fun buildVideoRenderers(context: Context, extensionRendererMode: Int,
        mediaCodecSelector: MediaCodecSelector, enableDecoderFallback: Boolean, eventHandler: Handler,
        eventListener: VideoRendererEventListener, allowedVideoJoiningTimeMs: Long, out: ArrayList<Renderer>) {
        val builder=MediaCodecVideoRenderer.Builder(context).setMediaCodecSelector(mediaCodecSelector)
            .setEnableDecoderFallback(enableDecoderFallback).setEventHandler(eventHandler).setEventListener(eventListener)
            .setAllowedJoiningTimeMs(allowedVideoJoiningTimeMs)
        fun admitted(format: ColorEngine.DecodedVideoFormat)=format.copy(color=PreviewColorPolicy.decoded(format.color,source.color,interpretMissing))
        val expected=ColorEngine.DecodedVideoFormat(source.width,source.height,source.color)
        out.add(object : MediaCodecVideoRenderer(builder) {
            private val gate=DecoderColorGate(expected)
            private fun diagnostic(stage: String,error: Exception,format: Format?,raw: MediaFormat?): PlaybackException {
                gate.invalidate()
                val message="Grade preview stage=$stage expected-visible=${source.width}x${source.height} " +
                    "source-rotation=${source.rotationDegrees} codec=${getCodecInfo()?.name ?: "not-created"}; " +
                    "input=${format?.width}x${format?.height} input-rotation=${format?.rotationDegrees} pixel-ratio=${format?.pixelWidthHeightRatio}; " +
                    "decoded-raw={${VideoSourceProbe.geometryDescription(raw)}}; source-raw={${source.geometryDescription}}"
                return createRendererException(IllegalArgumentException(message,error),format,
                    PlaybackException.ERROR_CODE_DECODING_FORMAT_UNSUPPORTED)
            }
            private fun verifyRaw(raw: MediaFormat?) {
                // Normalize inside the terminal gate, so malformed crop cannot leave it VERIFIED.
                gate.beforeRender { raw?.let { admitted(VideoSourceProbe.decoded(it)) } }
            }
            override fun createPlaybackVideoGraphWrapper(context: Context,
                videoFrameReleaseControl: VideoFrameReleaseControl): PlaybackVideoGraphWrapper =
                PlaybackVideoGraphWrapper.Builder(context,videoFrameReleaseControl)
                    .setVideoGraphFactory(SingleInputVideoGraph.Factory(prepared.factory))
                    .setEnablePlaylistMode(true).setClock(getClock()).build()

            override fun changeVideoSinkInputStream(videoSink: VideoSink, inputType: Int,
                format: Format, firstFrameReleaseInstruction: Int) {
                if(!interpretMissing) {
                    super.changeVideoSinkInputStream(videoSink,inputType,format,firstFrameReleaseInstruction)
                    return
                }
                try {
                    val color=PreviewColorPolicy.decoded(VideoSourceProbe.color(format.colorInfo),source.color,true)
                    require(color==source.color) { "Video graph input conflicts with preview interpretation: $color" }
                    // Explicitly carry the user's interpretation into the graph, not just the
                    // admission gate. Preserve bit-depth fields; this is NOT measured source info.
                    val builder=format.colorInfo?.buildUpon() ?: ColorInfo.Builder()
                    val interpreted=builder.setColorSpace(C.COLOR_SPACE_BT709).setColorTransfer(C.COLOR_TRANSFER_SDR)
                        .setColorRange(if(color.range==ColorEngine.VideoRange.FULL) C.COLOR_RANGE_FULL else C.COLOR_RANGE_LIMITED).build()
                    super.changeVideoSinkInputStream(videoSink,inputType,
                        format.buildUpon().setColorInfo(interpreted).build(),firstFrameReleaseInstruction)
                } catch(error: Exception) {
                    throw diagnostic("graph-input",error,format,null)
                }
            }

            override fun onOutputFormatChanged(format: Format, mediaFormat: MediaFormat?) {
                try {
                    // Track format is pre-rotation visible geometry, not codec allocation size.
                    ColorEngine.validateDecodedFormat(expected,admitted(GuardedDecoderFactory.descriptor(format)))
                } catch(error: Exception) { throw diagnostic("input-track",error,format,mediaFormat) }
                try {
                    if(mediaFormat!=null) verifyRaw(mediaFormat)
                } catch(error: Exception) { throw diagnostic("decoder-format",error,format,mediaFormat) }
                try { super.onOutputFormatChanged(format,mediaFormat) }
                catch(error: Exception) { throw diagnostic("video-sink-setup",error,format,mediaFormat) }
            }
            override fun processOutputBuffer(positionUs: Long, elapsedRealtimeUs: Long, codec: MediaCodecAdapter?,
                buffer: ByteBuffer?, bufferIndex: Int, bufferFlags: Int, sampleCount: Int,
                bufferPresentationTimeUs: Long, isDecodeOnlyBuffer: Boolean, isLastBuffer: Boolean, format: Format): Boolean {
                if(!isDecodeOnlyBuffer || isLastBuffer) {
                    var raw: MediaFormat?=null
                    try { raw=codec?.outputFormat;verifyRaw(raw) }
                    catch(error: Exception) { throw diagnostic("decoded-buffer",error,format,raw) }
                }
                return super.processOutputBuffer(positionUs,elapsedRealtimeUs,codec,buffer,bufferIndex,bufferFlags,
                    sampleCount,bufferPresentationTimeUs,isDecodeOnlyBuffer,isLastBuffer,format)
            }
            override fun onRelease() {
                try { super.onRelease() } finally { gate.close() }
            }
        })
    }
}
