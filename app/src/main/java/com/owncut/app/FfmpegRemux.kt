package com.owncut.app

import java.io.File

/** Stream-copy remux only: no decode and no re-encode. The LGPL FFmpeg build is linked into owncut_android. */
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

    /** Returns null on success, otherwise a short diagnostic. */
    private external fun nativeRemux(input: String, output: String, container: String): String?
}
