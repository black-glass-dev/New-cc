package com.owncut.app

import android.content.Context
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.Looper
import androidx.media3.common.C
import androidx.media3.common.ColorInfo
import androidx.media3.common.util.UnstableApi

/** Platform metadata transport only. Missing tags remain UNKNOWN; never resolution defaults.
 * Worker-only, request-scoped extraction; ownership ends even on cancellation/failure.
 */
@UnstableApi
internal object VideoSourceProbe {
    fun read(context: Context, mediaId: String, checkCancelled: () -> Unit): VideoSourceMetadata {
        check(Looper.myLooper() != Looper.getMainLooper()) { "Video metadata probing must run off the UI thread" }
        checkCancelled()
        val uri=Uri.parse(mediaId)
        require(uri.scheme in listOf("content","file")) { "Only local video sources are supported" }
        val extractor=MediaExtractor()
        try {
            extractor.setDataSource(context.applicationContext,uri,null)
            checkCancelled()
            var found: VideoSourceMetadata?=null
            for (index in 0 until extractor.trackCount) {
                checkCancelled()
                val format=extractor.getTrackFormat(index)
                if (format.getString(MediaFormat.KEY_MIME)?.startsWith("video/") != true) continue
                require(found == null) { "Multiple video tracks require an explicit track selector" }
                fun integer(key: String): Int? = if(format.containsKey(key)) format.getInteger(key) else null
                val size=visibleGeometry(format)
                found=VideoSourceMetadata(mediaId,index,size[0],size[1],integer(MediaFormat.KEY_ROTATION) ?: 0,
                    color(integer(MediaFormat.KEY_COLOR_STANDARD),integer(MediaFormat.KEY_COLOR_TRANSFER),
                        integer(MediaFormat.KEY_COLOR_RANGE)).copy(hasHdrStaticMetadata=
                            hdrStatic(format)),geometryDescription(format))
            }
            checkCancelled()
            return enrich(context,requireNotNull(found) { "No video track in source" },checkCancelled)
        } finally { extractor.release() }
    }

    private fun enrich(context: Context, source: VideoSourceMetadata, cancelled: () -> Unit): VideoSourceMetadata {
        val c=source.color
        if(c.primaries!=ColorEngine.VideoPrimaries.UNKNOWN && c.transfer!=ColorEngine.VideoTransfer.UNKNOWN && c.range!=ColorEngine.VideoRange.UNKNOWN) return source
        // Media3 can expose container/codec tags omitted by Android MediaExtractor.
        // A single matching video format only; no selecting a different track by accident.
        val retriever=androidx.media3.exoplayer.MetadataRetriever.Builder(context,
            androidx.media3.common.MediaItem.fromUri(source.mediaId)).build()
        val future=retriever.retrieveTrackGroups()
        val formats: List<androidx.media3.common.Format> = try {
            cancelled()
            val tracks=future.get(8,java.util.concurrent.TimeUnit.SECONDS)
            cancelled()
            buildList<androidx.media3.common.Format> {
                for(g in 0 until tracks.length) for(t in 0 until tracks[g].length) {
                    val f=tracks[g].getFormat(t)
                    if(f.sampleMimeType?.startsWith("video/")==true) add(f)
                }
            }
        } catch(error: Exception) {
            if(error is InterruptedException) Thread.currentThread().interrupt()
            cancelled()
            android.util.Log.w("OwncutSourceProbe","Secondary color probe unavailable",error)
            return source // UNKNOWN remains UNKNOWN; no guessed tags
        } finally { future.cancel(true);retriever.close() }
        if(formats.size!=1) return source
        val f=formats.single()
        if(f.width!=source.width || f.height!=source.height) return source
        return source.copy(color=PreviewColorPolicy.merge(source.color,color(f.colorInfo)))
    }

    internal fun decoded(format: MediaFormat): ColorEngine.DecodedVideoFormat {
        fun integer(key: String): Int? = if(format.containsKey(key)) format.getInteger(key) else null
        val size=visibleGeometry(format)
        return ColorEngine.DecodedVideoFormat(size[0],size[1],
            color(integer(MediaFormat.KEY_COLOR_STANDARD),integer(MediaFormat.KEY_COLOR_TRANSFER),integer(MediaFormat.KEY_COLOR_RANGE))
                .copy(hasHdrStaticMetadata=hdrStatic(format)))
    }

    // Stable MediaFormat key strings work on minSdk26; KEY_CROP_* public constants are newer.
    private val cropKeys=listOf("crop-left","crop-top","crop-right","crop-bottom")
    internal fun visibleGeometry(format: MediaFormat): IntArray {
        var mask=0
        val crop=IntArray(4)
        cropKeys.forEachIndexed { index,key -> if(format.containsKey(key)) {
            mask=mask or (1 shl index);crop[index]=format.getInteger(key)
        } }
        return ColorEngine.visibleVideoGeometry(
            if(format.containsKey(MediaFormat.KEY_WIDTH)) format.getInteger(MediaFormat.KEY_WIDTH) else 0,
            if(format.containsKey(MediaFormat.KEY_HEIGHT)) format.getInteger(MediaFormat.KEY_HEIGHT) else 0,mask,crop)
    }
    /** Bounded, URI-free diagnostics; never dumps CSD/HDR buffers or provider paths. */
    internal fun geometryDescription(format: MediaFormat?): String {
        if(format==null) return "no raw MediaFormat"
        val keys=listOf("width","height")+cropKeys+listOf("rotation-degrees","sar-width","sar-height","stride","slice-height",
            "color-standard","color-transfer","color-range")
        return keys.joinToString(" ") { key ->
            val value=if(!format.containsKey(key)) "absent" else runCatching { format.getInteger(key).toString() }.getOrDefault("invalid")
            "$key=$value"
        }
    }

    private fun hdrStatic(format: MediaFormat): Boolean {
        if(!format.containsKey(MediaFormat.KEY_HDR_STATIC_INFO)) return false
        val data=format.getByteBuffer(MediaFormat.KEY_HDR_STATIC_INFO) ?: return false
        if(data.limit()==0) return false
        if(data.limit()!=25) return true // preserve unknown/malformed presence, don't infer SDR
        val bytes=ByteArray(25)
        data.duplicate().apply { position(0);get(bytes) }
        return ColorEngine.hdrStaticPresent(bytes)
    }

    // Android MediaFormat and Media3 constants are mapped independently, not assumed equal.
    // Exact unset sentinels stay UNKNOWN, never Rec709. Other unsupported codes stay OTHER.
    internal fun color(standard: Int?, transfer: Int?, range: Int?)=ColorEngine.VideoColorMetadata(
        when(standard) { null,0,-1 -> ColorEngine.VideoPrimaries.UNKNOWN
            MediaFormat.COLOR_STANDARD_BT709 -> ColorEngine.VideoPrimaries.REC709
            else -> ColorEngine.VideoPrimaries.OTHER },
        when(transfer) { null,0,-1 -> ColorEngine.VideoTransfer.UNKNOWN
            MediaFormat.COLOR_TRANSFER_SDR_VIDEO -> ColorEngine.VideoTransfer.SDR_VIDEO
            MediaFormat.COLOR_TRANSFER_ST2084 -> ColorEngine.VideoTransfer.PQ
            MediaFormat.COLOR_TRANSFER_HLG -> ColorEngine.VideoTransfer.HLG
            else -> ColorEngine.VideoTransfer.OTHER },
        when(range) { null -> ColorEngine.VideoRange.UNKNOWN
            MediaFormat.COLOR_RANGE_FULL -> ColorEngine.VideoRange.FULL
            MediaFormat.COLOR_RANGE_LIMITED -> ColorEngine.VideoRange.LIMITED
            else -> ColorEngine.VideoRange.UNKNOWN })

    internal fun color(info: ColorInfo?)=ColorEngine.VideoColorMetadata(
        when(info?.colorSpace) { null,androidx.media3.common.Format.NO_VALUE -> ColorEngine.VideoPrimaries.UNKNOWN
            C.COLOR_SPACE_BT709 -> ColorEngine.VideoPrimaries.REC709
            else -> ColorEngine.VideoPrimaries.OTHER },
        when(info?.colorTransfer) { null,androidx.media3.common.Format.NO_VALUE -> ColorEngine.VideoTransfer.UNKNOWN
            C.COLOR_TRANSFER_SDR -> ColorEngine.VideoTransfer.SDR_VIDEO
            C.COLOR_TRANSFER_SRGB -> ColorEngine.VideoTransfer.SRGB
            C.COLOR_TRANSFER_ST2084 -> ColorEngine.VideoTransfer.PQ
            C.COLOR_TRANSFER_HLG -> ColorEngine.VideoTransfer.HLG
            else -> ColorEngine.VideoTransfer.OTHER },
        when(info?.colorRange) { C.COLOR_RANGE_FULL -> ColorEngine.VideoRange.FULL
            C.COLOR_RANGE_LIMITED -> ColorEngine.VideoRange.LIMITED
            else -> ColorEngine.VideoRange.UNKNOWN },ColorEngine.hdrStaticPresent(info?.hdrStaticInfo))
}
