package com.owncut.app

/** A candidate is reachable only after explicit UI consent, never automatic fallback. */
/** GRADED_SDR_MP4: normal export of graded clips, SDR Rec.709 sources only (checked in exporter). */
/** Raised by the export worker when a graded source needs an explicit user choice for missing color tags. */
internal class ColorTagsInterpretationRequired : IllegalStateException("Missing color tags need explicit interpretation")

/** Format ids are "container:codecMime". MOV and MKV are stream-copy remuxes of the MP4 export, and MXF is a
 * remux with AAC audio resampled to 48 kHz PCM (LGPL FFmpeg). MXF is H.264 only in this build.
 * ProRes, DNxHR, AV1, WEBM and 10-bit are not offered yet. */
internal object ExportVideoFormat {
    const val MP4_H264 = "mp4:video/avc"
    const val MP4_HEVC = "mp4:video/hevc"
    const val MOV_H264 = "mov:video/avc"
    const val MOV_HEVC = "mov:video/hevc"
    const val MKV_H264 = "mkv:video/avc"
    const val MKV_HEVC = "mkv:video/hevc"
    const val MXF_H264 = "mxf:video/avc"
    val ALL = listOf(MP4_H264, MP4_HEVC, MOV_H264, MOV_HEVC, MKV_H264, MKV_HEVC, MXF_H264)
    fun container(format: String) = format.substringBefore(':')
    fun codecMime(format: String) = format.substringAfter(':')
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
