package com.owncut.app

import android.media.MediaFormat
import androidx.media3.common.C
import androidx.media3.common.ColorInfo
import androidx.media3.common.Format
import androidx.media3.common.MimeTypes
import androidx.media3.common.util.UnstableApi
import androidx.media3.transformer.Codec
import androidx.media3.transformer.ExportException
import org.junit.Assert.*
import org.junit.Test
import java.lang.reflect.Proxy
import java.util.concurrent.atomic.AtomicReference

/** Synthetic Codec delegation only; added, Android compilation/execution NOT verified.
 * No actual MediaCodec, rendered surface, source identity or encoded-pixel proof.
 */
@UnstableApi
class GuardedDecoderTest {
    private val color=ColorEngine.VideoColorMetadata(ColorEngine.VideoPrimaries.REC709,
        ColorEngine.VideoTransfer.SDR_VIDEO,ColorEngine.VideoRange.LIMITED)
    private val expected=ColorEngine.DecodedVideoFormat(1920,1080,color)
    private fun format()=Format.Builder().setSampleMimeType(MimeTypes.VIDEO_H264).setWidth(1920).setHeight(1080)
        .setColorInfo(ColorInfo.Builder().setColorSpace(C.COLOR_SPACE_BT709).setColorTransfer(C.COLOR_TRANSFER_SDR)
            .setColorRange(C.COLOR_RANGE_LIMITED).build()).build()
    private class Fake(private val rawCapable: Boolean=false) {
        var raw: MediaFormat?=null
        var rawReads=0
        var formatReads=0
        var output: Format?=null
        var queryFails=false
        var releaseFails=false
        var closes=0
        val renders=mutableListOf<Any>()
        val codec=Proxy.newProxyInstance(Codec::class.java.classLoader,if(rawCapable) arrayOf(Codec::class.java,RawVideoOutputFormatProvider::class.java)
            else arrayOf(Codec::class.java)) { proxy,method,args ->
            when(method.name) {
                "getOutputFormat" -> { formatReads++;check(!queryFails) { "synthetic output query failure" };output }
                "rawOutputFormatForCurrentBuffer" -> { rawReads++;check(!queryFails) { "synthetic raw query failure" };raw }
                "releaseOutputBuffer" -> { check(!releaseFails) { "synthetic release failure" };renders.add(args!![0]);null }
                "release" -> { closes++;null }
                "toString" -> "Synthetic decoder"
                "hashCode" -> System.identityHashCode(proxy)
                "equals" -> proxy === args?.get(0)
                else -> error("Unexpected synthetic Codec call: ${method.name}")
            }
        } as Codec
    }
    private fun padded(): MediaFormat = MediaFormat.createVideoFormat("video/raw",1920,1088).apply {
        setInteger("crop-left",0);setInteger("crop-top",0)
        setInteger("crop-right",1919);setInteger("crop-bottom",1079)
        setInteger(MediaFormat.KEY_COLOR_STANDARD,MediaFormat.COLOR_STANDARD_BT709)
        setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
        setInteger(MediaFormat.KEY_COLOR_RANGE,MediaFormat.COLOR_RANGE_LIMITED)
    }
    @Test fun rawEvidenceNormalizesCropForEachReleaseWithoutReadingLossyFormat() {
        val fake=Fake(true).apply { raw=padded();output=format().buildUpon().setHeight(1088).build() }
        val codec=GuardedDecoderFactory.GuardedCodec(fake.codec,expected)
        try {
            codec.releaseOutputBuffer(false)
            assertEquals(0,fake.rawReads)
            codec.releaseOutputBuffer(true);codec.releaseOutputBuffer(321L)
            assertEquals(2,fake.rawReads);assertEquals(0,fake.formatReads)
            fake.raw!!.setInteger("crop-bottom",1080)
            assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(true) }
            fake.raw=padded()
            assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(true) }
            assertEquals(listOf(false,true,321L),fake.renders)
        } finally { codec.release() }
    }
    @Test fun declaredRawCapabilityNeverFallsBackWhenEvidenceIsMissingMalformedOrThrows() {
        for(kind in listOf("missing","partial","query","hdr","color")) {
            val fake=Fake(true).apply {
                output=format() // valid lossy Format must not rescue invalid raw evidence
                raw=when(kind) {
                    "missing" -> null
                    "partial" -> MediaFormat.createVideoFormat("video/raw",1920,1088).apply { setInteger("crop-right",1919) }
                    else -> padded()
                }
                queryFails=kind=="query"
                if(kind=="hdr") raw!!.setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_ST2084)
                if(kind=="color") raw=MediaFormat.createVideoFormat("video/raw",1920,1080)
            }
            val codec=GuardedDecoderFactory.GuardedCodec(fake.codec,expected)
            try {
                assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(true) }
                fake.queryFails=false;fake.raw=padded()
                assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(100L) }
                assertEquals(0,fake.formatReads);assertTrue(fake.renders.isEmpty())
            } finally { codec.release() }
        }
    }
    @Test fun bothRenderedOverloadsValidateEveryTimeAndFailureIsTerminal() {
        val fake=Fake()
        val codec=GuardedDecoderFactory.GuardedCodec(fake.codec,expected)
        codec.releaseOutputBuffer(false) // dropped before output format is known
        fake.output=format()
        codec.releaseOutputBuffer(true)
        codec.releaseOutputBuffer(123456L)
        assertEquals(listOf(false,true,123456L),fake.renders)
        fake.output=format().buildUpon().setWidth(1280).build()
        assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(true) }
        fake.output=format()
        assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(999L) }
        assertEquals(3,fake.renders.size)
        codec.releaseOutputBuffer(false) // cleanup drop still allowed
        codec.release();codec.release();assertEquals(1,fake.closes)
        assertThrows(IllegalStateException::class.java) { codec.releaseOutputBuffer(true) }
    }
    @Test fun absentFormatAndFailedQueryNeverRenderOrResume() {
        for (queryFails in listOf(false,true)) {
            val fake=Fake().apply { this.queryFails=queryFails }
            val codec=GuardedDecoderFactory.GuardedCodec(fake.codec,expected)
            try {
                assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(true) }
                fake.queryFails=false;fake.output=format()
                assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(true) }
                assertTrue(fake.renders.isEmpty())
            } finally { codec.release() }
        }
    }
    @Test fun delegateFailureAndWrongThreadCannotBypassGate() {
        val fake=Fake().apply { output=format();releaseFails=true }
        val codec=GuardedDecoderFactory.GuardedCodec(fake.codec,expected)
        try {
            assertThrows(IllegalStateException::class.java) { codec.releaseOutputBuffer(true) }
            fake.releaseFails=false
            assertThrows(ExportException::class.java) { codec.releaseOutputBuffer(true) }
            val failure=AtomicReference<Throwable?>()
            Thread { try { codec.releaseOutputBuffer(false) } catch(error: Throwable) { failure.set(error) } }
                .apply { start();join() }
            assertTrue(failure.get() is IllegalStateException)
            assertTrue(fake.renders.isEmpty())
        } finally { codec.release() }
    }
}
