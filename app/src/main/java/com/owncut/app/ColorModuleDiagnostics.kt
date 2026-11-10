package com.owncut.app

/** Diagnostic self-test for the standalone color_engine module (CPU reference path, no GPU). */
internal object ColorModuleDiagnostics {
    init { System.loadLibrary("owncut_android") }

    /** Returns ASCII lines for inspection. Throws IllegalStateException on native failure. */
    fun runSelfTest(width: Int = 64, height: Int = 64): String = nativeRunSelfTest(width, height)

    private external fun nativeRunSelfTest(width: Int, height: Int): String
}
