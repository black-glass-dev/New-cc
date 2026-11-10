package com.owncut.app

/** Immutable bounded operation context. No URI, project name or destination is added.
 * The exception chain itself can contain platform messages: copy is explicitly user-owned.
 */
internal object ExportFailureReport {
    enum class Stage { PREPARING, RENDERING, PUBLISHING_GALLERY, COPYING_DOCUMENT }
    fun details(error: Throwable, stage: Stage, clips: Int, gradedClips: Int, outputHeight: Int): String {
        require(clips>=0 && gradedClips in 0..clips && outputHeight>0)
        return "Export stage=${stage.name} clips=$clips graded-clips=$gradedClips output-height=$outputHeight\n" +
            ColorPreviewFailure.details(error)
    }
}
