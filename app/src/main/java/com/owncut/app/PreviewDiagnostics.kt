package com.owncut.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context

/** Local, explicit user-copy only; no automatic telemetry or media URI dump. */
internal object PreviewDiagnostics {
    fun report(error: Throwable): String = "Owncut ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) " +
        "revision=${BuildConfig.PREVIEW_REVISION} geometry-sequence-v2 SDK=${android.os.Build.VERSION.SDK_INT}\n" +
        ColorPreviewFailure.details(error)
    fun copy(context: Context, report: CharSequence) {
        context.getSystemService(ClipboardManager::class.java)?.setPrimaryClip(ClipData.newPlainText("Owncut preview",report))
    }
}
