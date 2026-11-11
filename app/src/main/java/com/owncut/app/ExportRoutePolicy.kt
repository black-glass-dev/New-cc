package com.owncut.app

/** A candidate is reachable only after explicit UI consent, never automatic fallback. */
/** GRADED_SDR_MP4: normal export of graded clips, SDR Rec.709 sources only (checked in exporter). */
/** Raised by the export worker when a graded source needs an explicit user choice for missing color tags. */
internal class ColorTagsInterpretationRequired : IllegalStateException("Missing color tags need explicit interpretation")

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
