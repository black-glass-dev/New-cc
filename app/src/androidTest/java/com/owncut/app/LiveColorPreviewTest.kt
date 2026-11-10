package com.owncut.app

import android.media.MediaFormat
import androidx.media3.common.util.UnstableApi
import org.junit.Assert.*
import org.junit.Test

/** Added source, UNCOMPILED/UNRUN on Android. This is raw format transport, not live
 * decoder/graph playback proof. Actual clip-loop/seek/audio/surface checks remain open.
 */
@UnstableApi
class LiveColorPreviewTest {
    @Test fun paddedHevcUsesInclusiveVisibleCropWithoutChangingSourceColorOrRotation() {
        val format=MediaFormat.createVideoFormat("video/raw",2880,2176)
        format.setInteger(MediaFormat.KEY_COLOR_STANDARD,MediaFormat.COLOR_STANDARD_BT709)
        format.setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
        format.setInteger(MediaFormat.KEY_COLOR_RANGE,MediaFormat.COLOR_RANGE_LIMITED)
        format.setInteger("crop-left",0);format.setInteger("crop-top",0)
        format.setInteger("crop-right",2879);format.setInteger("crop-bottom",2159)
        format.setInteger(MediaFormat.KEY_ROTATION,90)
        format.setInteger("stride",3072);format.setInteger("slice-height",2304)
        val visible=VideoSourceProbe.decoded(format)
        assertEquals(2880,visible.width);assertEquals(2160,visible.height)
        val gate=DecoderColorGate(visible)
        gate.beforeRender { VideoSourceProbe.decoded(format) }
        assertTrue(VideoSourceProbe.geometryDescription(format).contains("crop-bottom=2159"))
        format.setInteger("crop-bottom",2160)
        assertThrows(IllegalArgumentException::class.java) { gate.beforeRender { VideoSourceProbe.decoded(format) } }
        assertEquals(DecoderColorGate.State.FAILED,gate.state)
        val malformed=MediaFormat.createVideoFormat("video/raw",2880,2176)
        malformed.setInteger("crop-right",2879)
        assertThrows(IllegalArgumentException::class.java) { VideoSourceProbe.decoded(malformed) }
    }

    @Test fun media3CodecFormatCannotBeUsedAsRawCropEvidence() {
        val raw=MediaFormat.createVideoFormat("video/raw",2880,2176)
        raw.setInteger(MediaFormat.KEY_COLOR_STANDARD,MediaFormat.COLOR_STANDARD_BT709)
        raw.setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
        raw.setInteger(MediaFormat.KEY_COLOR_RANGE,MediaFormat.COLOR_RANGE_LIMITED)
        raw.setInteger("crop-left",0);raw.setInteger("crop-top",0)
        raw.setInteger("crop-right",2879);raw.setInteger("crop-bottom",2159)
        val visible=VideoSourceProbe.decoded(raw)
        val converted=androidx.media3.common.util.MediaFormatUtil.createFormatFromMediaFormat(raw)
        // Pinned Media3 1.8.1 conversion retains allocation height, not explicit crop.
        assertEquals(2176,converted.height)
        assertEquals(2160,visible.height)
        assertThrows(IllegalArgumentException::class.java) {
            ColorEngine.validateDecodedFormat(visible,GuardedDecoderFactory.descriptor(converted))
        }
    }

    @Test fun rendererRawOutputNeverDefaultsMissingColorTags() {
        val format=MediaFormat.createVideoFormat("video/raw",1920,1080)
        val unknown=VideoSourceProbe.decoded(format)
        assertEquals(ColorEngine.VideoPrimaries.UNKNOWN,unknown.color.primaries)
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.validateDecodedFormat(unknown,unknown) }
        format.setInteger(MediaFormat.KEY_COLOR_STANDARD,MediaFormat.COLOR_STANDARD_BT709)
        format.setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_SDR_VIDEO)
        format.setInteger(MediaFormat.KEY_COLOR_RANGE,MediaFormat.COLOR_RANGE_LIMITED)
        val known=VideoSourceProbe.decoded(format)
        ColorEngine.validateDecodedFormat(known,known)
        // Some codecs attach an unset CTA payload to SDR output. Nonzero or malformed
        // metadata must still fail. Buffer cursor is not the metadata's presence flag.
        val placeholder=java.nio.ByteBuffer.wrap(ByteArray(25)).apply { position(25) }
        format.setByteBuffer(MediaFormat.KEY_HDR_STATIC_INFO,placeholder)
        assertFalse(VideoSourceProbe.decoded(format).color.hasHdrStaticMetadata)
        assertEquals(25,placeholder.position())
        ColorEngine.validateDecodedFormat(known,VideoSourceProbe.decoded(format))
        format.setByteBuffer(MediaFormat.KEY_HDR_STATIC_INFO,java.nio.ByteBuffer.wrap(ByteArray(24)))
        assertTrue(VideoSourceProbe.decoded(format).color.hasHdrStaticMetadata)
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.validateDecodedFormat(known,VideoSourceProbe.decoded(format)) }
        format.setByteBuffer(MediaFormat.KEY_HDR_STATIC_INFO,java.nio.ByteBuffer.wrap(ByteArray(25).apply { this[1]=1 }))
        assertTrue(VideoSourceProbe.decoded(format).color.hasHdrStaticMetadata)
        format.setByteBuffer(MediaFormat.KEY_HDR_STATIC_INFO,java.nio.ByteBuffer.wrap(ByteArray(25)))
        val gate=DecoderColorGate(known)
        gate.beforeRender(known)
        format.setInteger(MediaFormat.KEY_COLOR_TRANSFER,MediaFormat.COLOR_TRANSFER_HLG)
        assertThrows(IllegalArgumentException::class.java) { gate.beforeRender(VideoSourceProbe.decoded(format)) }
        assertEquals(DecoderColorGate.State.FAILED,gate.state)
        assertThrows(IllegalStateException::class.java) { gate.beforeRender(known) }
    }
}
