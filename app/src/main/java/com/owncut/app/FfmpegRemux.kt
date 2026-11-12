package com.owncut.app

import java.io.File

/** LGPL FFmpeg in owncut_android. remux stream-copies (MOV, MKV, MXF); transcode encodes ProRes or DNxHR. */
internal object FfmpegRemux {
    init { System.loadLibrary("owncut_android") }

    /** container: "mov", "mkv" or "mxf". Throws IllegalStateException with the native reason on failure. */
    fun remux(input: File, output: File, container: String) {
        require(container == "mov" || container == "mkv" || container == "mxf") { "unsupported container: $container" }
        val error = nativeRemux(input.absolutePath, output.absolutePath, container)
        if (error != null) {
            output.delete()
            throw IllegalStateException("Container remux failed: $error")
        }
    }

    /**
     * codec: "prores422" (MOV only), "dnxhr_hq" (MOV or MXF), or "hevc8" / "hevc10" (MP4, MOV or MKV;
     * software kvazaar). The input must be H.264.
     */
    fun transcode(input: File, output: File, container: String, codec: String) {
        require(container == "mov" || container == "mxf" || container == "mp4" || container == "mkv") {
            "unsupported container: $container"
        }
        require(codec == "prores422" || codec == "dnxhr_hq" || codec == "hevc8" || codec == "hevc10") {
            "unsupported codec: $codec"
        }
        require(codec != "prores422" || container == "mov") { "ProRes is written to MOV only" }
        require(codec != "dnxhr_hq" || container == "mov" || container == "mxf") { "DNxHR is written to MOV or MXF only" }
        require((codec != "hevc8" && codec != "hevc10") || container != "mxf") { "HEVC is not written to MXF" }
        val error = nativeTranscode(input.absolutePath, output.absolutePath, container, codec)
        if (error != null) {
            output.delete()
            throw IllegalStateException("Transcode failed: $error")
        }
    }

    /** Returns null on success, otherwise a short diagnostic. */
    private external fun nativeRemux(input: String, output: String, container: String): String?

    /** Returns null on success, otherwise a short diagnostic. */
    private external fun nativeTranscode(input: String, output: String, container: String, codec: String): String?
}
