package com.owncut.app

import android.content.Context
import java.io.File

/** Last-export marker kept in app storage. A native crash (for example inside FFmpeg or the encoder) kills
 * the process without any Java exception, so the report cannot be shown in the same run. The marker records
 * the export and its last stage. It is removed when the export reaches its end. If it is still there at the
 * next start, the previous export did not finish, and the app shows the recorded stage.
 */
internal object ExportBreadcrumb {
    private const val FILE_NAME = "owncut-export-breadcrumb.txt"

    private fun file(context: Context) = File(context.filesDir, FILE_NAME)

    fun begin(context: Context, summary: String) = write(context, summary)

    /** Overwrites the stage line. The summary stays on the first line. */
    fun stage(context: Context, stage: String) {
        val current = runCatching { file(context).readLines().firstOrNull().orEmpty() }.getOrDefault("")
        write(context, "$current\nstage=$stage")
    }

    /** Called on success, failure or cancellation: the export reached the end in this process. */
    fun finish(context: Context) {
        runCatching { file(context).delete() }
    }

    /** Returns the text of an export that did not finish, then removes the marker so it is shown once. */
    fun takeUnfinished(context: Context): String? {
        val f = file(context)
        if (!f.isFile) return null
        val text = runCatching { f.readText() }.getOrNull()
        runCatching { f.delete() }
        return text?.takeIf { it.isNotBlank() }
    }

    private fun write(context: Context, text: String) {
        runCatching { file(context).writeText(text.take(2000)) }
    }
}
