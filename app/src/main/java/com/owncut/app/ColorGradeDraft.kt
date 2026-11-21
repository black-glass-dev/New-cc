package com.owncut.app

/** Text transport for exactly English/Bengali entry; native engine owns value validity.
 * A draft is not a history mutation; native validation precedes saving or preview.
 */
internal object ColorGradeDraft {
    /** Immutable preview only; native shared GPU budget, no session/history mutation. */
    fun validatedPreview(values: List<String>, enabled: Boolean): ColorGrade {
        val grade=parse(values,enabled)
        ColorEngine.shaderSources(grade,ColorEngine.TextureEncoding.MEDIA3_SDR_709)
        return grade
    }

    fun parse(values: List<String>, enabled: Boolean): ColorGrade = ColorGrade.fromParametersV1(
        values.map { text -> text.trim().map { c -> when(c) {
            in '\u09e6'..'\u09ef' -> ('0'.code+c.code-'\u09e6'.code).toChar()
            '\u2212' -> '-'
            else -> c
        } }.joinToString("").toDouble() }.toDoubleArray(),enabled)
}
