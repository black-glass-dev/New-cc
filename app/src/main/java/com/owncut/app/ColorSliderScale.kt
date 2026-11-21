package com.owncut.app

/** UI transport ranges only, not native parameter validity or grading math.
 * Expand around existing settings so opening an editor never silently clamps a saved grade.
 */
internal data class ColorSliderScale(val low: Double, val high: Double) {
    init { require(low.isFinite() && high.isFinite() && high>low) }
    fun value(progress: Int): Double {
        require(progress in 0..STEPS)
        return low+(high-low)*progress/STEPS
    }
    fun progress(value: Double): Int {
        require(value.isFinite())
        return (((value-low)/(high-low))*STEPS).toInt().coerceIn(0,STEPS)
    }
    companion object {
        const val STEPS=2000
        fun forParameter(index: Int, saved: Double): ColorSliderScale {
            require(index in 0..16 && saved.isFinite())
            val bounds=when(index) {
                0 -> -4.0 to 4.0
                1,2 -> 0.0 to 2.0
                3 -> 0.001 to 1.0
                4 -> 0.0 to 1.0
                in 5..7 -> -2.0 to 2.0
                in 8..10 -> -0.25 to 0.25
                in 11..13 -> 0.1 to 3.0
                else -> 0.0 to 3.0
            }
            return ColorSliderScale(minOf(bounds.first,saved),maxOf(bounds.second,saved))
        }
    }
}
