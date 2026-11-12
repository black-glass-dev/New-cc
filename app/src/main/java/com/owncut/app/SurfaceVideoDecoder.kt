package com.owncut.app

import android.media.MediaCodec
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import android.view.Surface
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.util.MediaFormatUtil
import androidx.media3.common.util.UnstableApi
import androidx.media3.decoder.DecoderInputBuffer
import androidx.media3.transformer.Codec
import androidx.media3.transformer.ExportException
import java.nio.ByteBuffer

/** Original synchronous surface-decoder backend for the graded CANDIDATE only.
 * No async latest-format polling, hidden APIs, source resizing, frame dropping or fallback.
 * All interaction stays on the creating thread. The caller owns the output Surface.
 */
@UnstableApi
internal class SurfaceVideoDecoder private constructor(
    private val codec: MediaCodec, private val configuration: Format,
) : Codec, RawVideoOutputFormatProvider {
    private val owner=Thread.currentThread()
    private val lease=DecoderBufferLease()
    private val info=MediaCodec.BufferInfo()
    private var inputIndex=-1
    private var inputData: ByteBuffer?=null
    private var inputHolder: DecoderInputBuffer?=null
    private var raw: MediaFormat?=null
    private var converted: Format?=null
    private var closed=false
    private val codecName=codec.name

    private fun ownerOnly() { check(Thread.currentThread()===owner) { "Surface decoder owner-thread violation" } }
    private fun active() { ownerOnly();check(!closed);lease.checkOpen() }
    private inline fun <T> operation(stage: String, block: () -> T): T {
        active()
        try { return block() } catch(error: Exception) {
            lease.fail()
            throw ExportException.createForAssetLoader(
                IllegalStateException("Owncut surface decoder stage=$stage codec=$codecName",error),
                ExportException.ERROR_CODE_DECODING_FAILED)
        }
    }
    override fun getConfigurationFormat(): Format = configuration
    override fun getName(): String = codecName
    override fun getInputSurface(): Surface = error("Surface video decoder has no input surface")
    override fun getInputFormat(): Format = operation("input-format") {
        MediaFormatUtil.createFormatFromMediaFormat(codec.inputFormat)
    }
    override fun maybeDequeueInputBuffer(inputBuffer: DecoderInputBuffer): Boolean = operation("input-dequeue") {
        if(lease.inputEnded) return@operation false
        if(inputIndex<0) {
            inputIndex=codec.dequeueInputBuffer(0)
            if(inputIndex<0) return@operation false
            inputData=requireNotNull(codec.getInputBuffer(inputIndex)) { "Missing codec input buffer" }
            inputHolder=inputBuffer
            inputBuffer.data=inputData
            inputBuffer.clear()
        } else require(inputBuffer===inputHolder) { "Another caller owns the pending input buffer" }
        true
    }
    override fun queueInputBuffer(inputBuffer: DecoderInputBuffer) = operation("input-queue") {
        check(inputIndex>=0 && !lease.inputEnded)
        require(inputBuffer===inputHolder) { "Input does not own the dequeued codec buffer" }
        require(!inputBuffer.isEncrypted) { "Encrypted source is not admitted" }
        val data=inputBuffer.data
        val eos=inputBuffer.isEndOfStream
        require(data===inputData || (eos && data==null)) { "Codec input storage was replaced" }
        if(eos) require(data==null || !data.hasRemaining()) { "Decoder EOS must not carry sample data" }
        val flags=(if(eos) MediaCodec.BUFFER_FLAG_END_OF_STREAM else 0) or
            (if(inputBuffer.isKeyFrame && !eos) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0)
        codec.queueInputBuffer(inputIndex,if(eos) 0 else requireNotNull(data).position(),
            if(eos) 0 else requireNotNull(data).remaining(),if(eos) 0L else inputBuffer.timeUs,flags)
        lease.inputQueued(eos);inputIndex=-1;inputData=null;inputHolder=null;inputBuffer.data=null
    }
    override fun signalEndOfInputStream(): Unit = error("Queue decoder input EOS, not surface-encoder EOS")

    /** Does not poll MediaCodec while a buffer is held; raw/Format share that exact epoch. */
    private fun pollOutput() {
        if(lease.heldIndex>=0 || lease.ended) return
        // Bound event processing per call so cancellation and other looper work can proceed.
        repeat(16) {
            when(val index=codec.dequeueOutputBuffer(info,0)) {
                MediaCodec.INFO_TRY_AGAIN_LATER -> return
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                    lease.formatChanged()
                    val next=codec.outputFormat
                    // Validate crop transport now; native color/expected-size gate runs on release.
                    val visible=VideoSourceProbe.visibleGeometry(next)
                    raw=next
                    converted=MediaFormatUtil.createFormatFromMediaFormat(next).buildUpon()
                        .setWidth(visible[0]).setHeight(visible[1]).build()
                }
                MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED -> Unit
                else -> {
                    check(index>=0) { "Unexpected codec output index $index" }
                    val eos=(info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM)!=0
                    val config=(info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG)!=0
                    if(config || (eos && info.size==0)) {
                        codec.releaseOutputBuffer(index,false)
                        if(eos) { lease.emptyEnd();return }
                    } else {
                        lease.acquire(index,eos)
                        return
                    }
                }
            }
        }
    }
    override fun getOutputFormat(): Format? = operation("output-format") { pollOutput();converted }
    override fun getOutputBufferInfo(): MediaCodec.BufferInfo? = operation("output-dequeue") {
        pollOutput()
        if(lease.heldIndex<0) null else MediaCodec.BufferInfo().apply {
            set(info.offset,info.size,info.presentationTimeUs,info.flags)
        }
    }
    override fun getOutputBuffer(): ByteBuffer? = error("Surface decoder output is not a byte buffer")
    override fun rawOutputFormatForCurrentBuffer(): MediaFormat? = operation("raw-evidence") {
        lease.requireHeld();raw
    }
    override fun releaseOutputBuffer(render: Boolean) = operation("output-release") {
        val index=lease.requireHeld()
        if(render) codec.releaseOutputBuffer(index,Math.multiplyExact(info.presentationTimeUs,1000L))
        else codec.releaseOutputBuffer(index,false)
        lease.releaseHeld()
    }
    override fun releaseOutputBuffer(renderPresentationTimeUs: Long) = operation("output-release-timed") {
        codec.releaseOutputBuffer(lease.requireHeld(),Math.multiplyExact(renderPresentationTimeUs,1000L))
        lease.releaseHeld()
    }
    override fun isEnded(): Boolean { ownerOnly();return lease.ended }
    override fun release() {
        ownerOnly();if(closed) return
        closed=true;lease.close();raw=null;converted=null;inputData=null
        inputHolder?.data=null;inputHolder=null
        codec.release() // releasing codec also releases all outstanding input/output buffers
    }
    companion object {
        fun create(format: Format, surface: Surface): SurfaceVideoDecoder {
            require(format.sampleMimeType?.startsWith("video/")==true)
            require(format.cryptoType==C.CRYPTO_TYPE_NONE) { "Protected video is not admitted" }
            require(format.rotationDegrees in listOf(0,90,180,270))
            if(Build.VERSION.SDK_INT<29) throw UnsupportedOperationException(
                "Lossless surface backpressure requires Android10+ candidate")
            ColorEngine.validateVideoSource(VideoSourceProbe.color(format.colorInfo))
            val media=MediaFormatUtil.createMediaFormatFromFormat(format)
            media.setInteger(MediaFormat.KEY_ALLOW_FRAME_DROP,0)
            val name=requireNotNull(MediaCodecList(MediaCodecList.REGULAR_CODECS).findDecoderForFormat(media)) {
                "No decoder for admitted candidate video"
            }
            val codec=MediaCodec.createByCodecName(name)
            try {
                codec.configure(media,surface,null,0)
                codec.start()
                return SurfaceVideoDecoder(codec,format)
            } catch(error: Throwable) {
                runCatching { codec.release() }.exceptionOrNull()?.let { error.addSuppressed(it) }
                throw error
            }
        }
    }
}
