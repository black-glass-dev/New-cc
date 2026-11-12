package com.owncut.app

/** A candidate is reachable only after explicit UI consent, never automatic fallback. */
/** GRADED_SDR_MP4: normal export of graded clips, SDR Rec.709 sources only (checked in exporter). */
/** Raised by the export worker when a graded source needs an explicit user choice for missing color tags. */
internal class ColorTagsInterpretationRequired : IllegalStateException("Missing color tags need explicit interpretation")

/** Format ids are "container:codecMime". MOV and MKV are stream-copy remuxes of the MP4 export, and MXF is a
 * remux with AAC audio resampled to 48 kHz PCM (LGPL FFmpeg). MXF is H.264 only in this build.
 * ProRes, DNxHR and 10-bit HEVC (Main 10, see Hevc10Export) are offered; AV1 and WEBM are not offered yet. */
internal object ExportVideoFormat {
    const val MP4_H264 = "mp4:video/avc"
    const val MP4_HEVC = "mp4:video/hevc"
    const val MOV_H264 = "mov:video/avc"
    const val MOV_HEVC = "mov:video/hevc"
    const val MKV_H264 = "mkv:video/avc"
    const val MKV_HEVC = "mkv:video/hevc"
    const val MXF_H264 = "mxf:video/avc"
    const val PRORES_422_MOV = "mov:prores422"
    const val DNXHR_HQ_MOV = "mov:dnxhr_hq"
    const val DNXHR_HQ_MXF = "mxf:dnxhr_hq"
    const val MP4_HEVC10 = "mp4:video/hevc10"
    const val MOV_HEVC10 = "mov:video/hevc10"
    const val MKV_HEVC10 = "mkv:video/hevc10"
    val ALL = listOf(MP4_H264, MP4_HEVC, MOV_H264, MOV_HEVC, MKV_H264, MKV_HEVC, MXF_H264,
        PRORES_422_MOV, DNXHR_HQ_MOV, DNXHR_HQ_MXF, MP4_HEVC10, MOV_HEVC10, MKV_HEVC10)
    fun container(format: String) = format.substringBefore(':')
    fun codecMime(format: String) = format.substringAfter(':')
    /** ProRes and DNxHR are encoded by FFmpeg from the platform H.264 export (a second generation). */
    fun isTranscoded(format: String) = codecMime(format) == "prores422" || codecMime(format) == "dnxhr_hq"
    /** HEVC 8-bit or 10-bit formats: hardware Transformer first, software kvazaar when the device has no encoder. */
    fun isHevcFamily(format: String) = codecMime(format).let { it == "video/hevc" || it == Hevc10Export.HEVC10_MIME }
    /** The software codec id for the FFmpeg transcode of an H.264 export. */
    fun softwareHevcCodec(format: String) = if (Hevc10Export.isHevc10(codecMime(format))) "hevc10" else "hevc8"
    /** The codec the platform Transformer encodes. Transcoded formats start from H.264. */
    fun encoderMime(format: String) = if (isTranscoded(format)) "video/avc" else codecMime(format)
    fun publishMime(format: String) = when (container(format)) {
        "mov" -> "video/quicktime"
        "mkv" -> "video/x-matroska"
        "mxf" -> "application/mxf"
        else -> "video/mp4"
    }
}

internal enum class ExportRoute { SHIPPING, GRADED_SDR_MP4, EXPERIMENTAL_GRADED }
internal object ExportRoutePolicy {
    fun requireAllowed(route: ExportRoute, hasActiveGrades: Boolean, sdk: Int) {
        require(sdk>=26)
        if(route==ExportRoute.EXPERIMENTAL_GRADED || route==ExportRoute.GRADED_SDR_MP4) {
            require(sdk>=29) { "Experimental graded export requires Android10+" }
            require(hasActiveGrades) { "Experimental graded route requires an active grade" }
        }
    }
}
