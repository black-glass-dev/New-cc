package com.owncut.app

/** Local bounded diagnostics. Never infer missing source metadata from a processing error. */
internal object ColorPreviewFailure {
    fun describe(error: Throwable): String = collect(error,300)
    // Only the selectable Details surface uses the larger cap; normal controls stay bounded.
    fun details(error: Throwable): String = collect(error,1200)
    private fun collect(error: Throwable,perCause: Int): String {
        val seen=java.util.Collections.newSetFromMap(java.util.IdentityHashMap<Throwable,Boolean>())
        val parts=mutableListOf<String>()
        var current: Throwable?=error
        while(current!=null && parts.size<6 && seen.add(current)) {
            val item=current
            parts.add((item.javaClass.simpleName+": "+(item.message ?: "")).take(perCause))
            current=item.cause
        }
        return parts.joinToString(" → ")
    }
}
