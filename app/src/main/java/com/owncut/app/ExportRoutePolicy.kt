package com.owncut.app

/** A candidate is reachable only after explicit UI consent, never automatic fallback. */
internal enum class ExportRoute { SHIPPING, EXPERIMENTAL_GRADED }
internal object ExportRoutePolicy {
    fun requireAllowed(route: ExportRoute, hasActiveGrades: Boolean, sdk: Int) {
        require(sdk>=26)
        if(route==ExportRoute.EXPERIMENTAL_GRADED) {
            require(sdk>=29) { "Experimental graded export requires Android10+" }
            require(hasActiveGrades) { "Experimental graded route requires an active grade" }
        }
    }
}
