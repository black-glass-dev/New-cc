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

    /** codec: "prores422" (MOV only) or "dnxhr_hq" (MOV or MXF). The input must be H.264. */
    fun transcode(input: File, output: File, container: String, codec: String) {
        require(container == "mov" || container == "mxf") { "unsupported container: $container" }
        require(codec == "prores422" || codec == "dnxhr_hq") { "unsupported codec: $codec" }
        require(codec != "prores422" || container == "mov") { "ProRes is written to MOV only" }
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
