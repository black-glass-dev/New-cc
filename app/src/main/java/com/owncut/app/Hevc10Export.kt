package com.owncut.app

import android.media.MediaCodecInfo
import android.media.MediaCodecList
import java.io.BufferedInputStream
import java.io.File

/**
 * 10-bit HEVC (Main 10) export support.
 *
 * Media3 silently drops a requested profile/level when the encoder does not advertise it, so this
 * object (a) tells the caller whether a device HEVC encoder offers Main 10 before encoding (when it
 * does not, the export uses the software kvazaar route), and (b) reads the hvcC box of the finished
 * file and refuses it unless general_profile_idc is 2.
 */
internal object Hevc10Export {
    const val HEVC_MIME = "video/hevc"
    /** Format-id marker. Not a real MIME type; the exporter maps it to [HEVC_MIME]. */
    const val HEVC10_MIME = "video/hevc10"
    const val MAIN10_PROFILE_IDC = 2

    fun isHevc10(codecMime: String) = codecMime == HEVC10_MIME

    /** The MIME type handed to Transformer. */
    fun transformerMime(codecMime: String) = if (isHevc10(codecMime)) HEVC_MIME else codecMime

    /** Main-tier level to request for Main 10 (5.1, else 4.1), or null when no device encoder offers it. */
    fun main10Level(): Int? {
        val advertised = mutableSetOf<Int>()
        for (info in MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos) {
            if (!info.isEncoder) continue
            if (HEVC_MIME !in info.supportedTypes) continue
            val caps = try { info.getCapabilitiesForType(HEVC_MIME) } catch (_: IllegalArgumentException) { continue }
            for (pl in caps.profileLevels) {
                if (pl.profile == MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10) advertised += pl.level
            }
        }
        return when {
            MediaCodecInfo.CodecProfileLevel.HEVCMainTierLevel51 in advertised -> MediaCodecInfo.CodecProfileLevel.HEVCMainTierLevel51
            MediaCodecInfo.CodecProfileLevel.HEVCMainTierLevel41 in advertised -> MediaCodecInfo.CodecProfileLevel.HEVCMainTierLevel41
            else -> null
        }
    }

    /** True when some device HEVC encoder exists (any profile). Used to choose the 8-bit route. */
    fun deviceHasHevcEncoder(): Boolean = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.any { info ->
        info.isEncoder && HEVC_MIME in info.supportedTypes
    }

    /** The Main 10 profile and level to request. Throws when the device has no Main 10 encoder. */
    fun requireEncoderLevel(): Pair<Int, Int> {
        val level = main10Level() ?: throw IllegalStateException(AppLanguage.text(R.string.hevc10_no_encoder))
        return MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10 to level
    }

    /**
     * Reads general_profile_idc from the first hvcC box in an MP4/MOV/MKV file by streaming bytes.
     * hvcC layout: configurationVersion (1 byte), then general_profile_space (2 bits),
     * general_tier_flag (1 bit), general_profile_idc (5 bits). Returns null when there is no hvcC.
     */
    fun profileIdc(file: File): Int? {
        val tag = byteArrayOf('h'.code.toByte(), 'v'.code.toByte(), 'c'.code.toByte(), 'C'.code.toByte())
        val window = ByteArray(6)
        var filled = 0
        BufferedInputStream(file.inputStream()).use { input ->
            while (true) {
                val next = input.read()
                if (next < 0) break
                if (filled < window.size) {
                    window[filled++] = next.toByte()
                } else {
                    System.arraycopy(window, 1, window, 0, window.size - 1)
                    window[window.size - 1] = next.toByte()
                }
                if (filled == window.size && window[0] == tag[0] && window[1] == tag[1] &&
                    window[2] == tag[2] && window[3] == tag[3]) {
                    return window[5].toInt() and 0x1F
                }
            }
        }
        return null
    }

    /** Throws unless the finished file declares Main 10 in its hvcC box. */
    fun requireMain10Output(file: File) {
        val idc = profileIdc(file)
        check(idc == MAIN10_PROFILE_IDC) {
            AppLanguage.text(R.string.hevc10_refused_output, idc?.toString() ?: "none")
        }
    }
}
