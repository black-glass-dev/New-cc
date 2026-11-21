import com.owncut.app.ColorFrameBuffers
import com.owncut.app.ColorGrade

// Host check of the Rec.709 exposure oracle used by GradedExportParityTest (step 2).
fun main() {
    val gray = IntArray(4) { 0xFF000000.toInt() or (128 shl 16) or (128 shl 8) or 128 }
    val zero = ColorFrameBuffers.renderRec709Exposure(gray, 2, 2, ColorGrade(exposureStops = 0.0))
    check(zero.all { (it and 255) == 128 }) { "0 stops must be identity" }
    val plus = ColorFrameBuffers.renderRec709Exposure(gray, 2, 2, ColorGrade(exposureStops = 1.0))
    check((plus[0] and 255) == 184) { "+1 stop on 128 must be 184, got ${plus[0] and 255}" }
    check((plus[0] ushr 24) == 255) { "alpha must pass through" }
    val minus = ColorFrameBuffers.renderRec709Exposure(gray, 2, 2, ColorGrade(exposureStops = -1.0))
    check((minus[0] and 255) == 87) { "-1 stop on 128 must be 87, got ${minus[0] and 255}" }
    var failed = false
    try { ColorFrameBuffers.renderRec709Exposure(gray, 2, 2, ColorGrade(contrast = 1.2)) } catch (e: IllegalArgumentException) { failed = true }
    check(failed) { "non-exposure grade must fail closed" }
    println("Rec.709 exposure oracle host checks passed")
}
