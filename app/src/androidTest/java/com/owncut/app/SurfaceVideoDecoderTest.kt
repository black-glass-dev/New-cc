package com.owncut.app

import android.graphics.SurfaceTexture
import android.media.MediaExtractor
import android.os.Build
import android.view.Surface
import androidx.media3.common.C
import androidx.media3.common.util.MediaFormatUtil
import androidx.media3.common.util.UnstableApi
import androidx.media3.decoder.DecoderInputBuffer
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File
import java.util.concurrent.TimeUnit

/** Real codec test SOURCE. Must execute on Android29+ before claiming backend acceptance.
 * Output is dropped deliberately: this tests decoder/lease/EOS, NOT graded GPU pixels.
 */
@UnstableApi
class SurfaceVideoDecoderTest {
    @Test(timeout=120_000L) fun authoredVideoDrainsWithStableHeldFormatAndExplicitEos() {
        assumeTrue(Build.VERSION.SDK_INT>=29)
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val file=File(context.cacheDir,"raw-decoder-${System.nanoTime()}.mp4")
        try {
            GrayVideoFixture.create(file)
            val extractor=MediaExtractor()
            try {
                extractor.setDataSource(file.absolutePath)
                val track=(0 until extractor.trackCount).first {
                    extractor.getTrackFormat(it).getString("mime")?.startsWith("video/")==true
                }
                extractor.selectTrack(track)
                val format=MediaFormatUtil.createFormatFromMediaFormat(extractor.getTrackFormat(track))
                val texture=SurfaceTexture(false)
                val surface=Surface(texture)
                try {
                    val decoder=SurfaceVideoDecoder.create(format,surface)
                    try {
                        val input=DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DISABLED)
                        var inputEnded=false;var frames=0
                        val deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(45)
                        while(!decoder.isEnded()) {
                            check(System.nanoTime()<deadline) { "Decoder EOS timeout" }
                            if(!inputEnded && decoder.maybeDequeueInputBuffer(input)) {
                                val data=requireNotNull(input.data)
                                val size=extractor.readSampleData(data,0)
                                if(size<0) { input.addFlag(C.BUFFER_FLAG_END_OF_STREAM);inputEnded=true }
                                else {
                                    data.position(size);input.timeUs=extractor.sampleTime
                                    if(extractor.sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC !=0) input.addFlag(C.BUFFER_FLAG_KEY_FRAME)
                                    extractor.advance()
                                }
                                input.flip();decoder.queueInputBuffer(input)
                            }
                            val info=decoder.getOutputBufferInfo()
                            if(info!=null) {
                                val raw=requireNotNull(decoder.rawOutputFormatForCurrentBuffer())
                                val first=VideoSourceProbe.decoded(raw)
                                val current=requireNotNull(decoder.getOutputFormat())
                                assertEquals(first.width,current.width);assertEquals(first.height,current.height)
                                // Repeated queries must not advance to another buffer/epoch.
                                assertEquals(info.presentationTimeUs,decoder.getOutputBufferInfo()!!.presentationTimeUs)
                                ColorEngine.validateDecodedFormat(first,VideoSourceProbe.decoded(
                                    requireNotNull(decoder.rawOutputFormatForCurrentBuffer())))
                                decoder.releaseOutputBuffer(false);frames++
                            }
                        }
                        assertTrue(inputEnded);assertTrue(frames>0)
                    } finally { decoder.release();decoder.release() }
                } finally { surface.release();texture.release() }
            } finally { extractor.release() }
        } finally { file.delete() }
    }
}
