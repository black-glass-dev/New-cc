package com.owncut.app

fun main() {
    val palette=WheelPalette.colors()
    check(palette.size==7 && palette.first()==palette.last())
    val expected=listOf(listOf(0),listOf(0,2),listOf(2),listOf(1,2),listOf(1),listOf(0,1))
    for(i in 0..5) {
        val rgb=listOf((palette[i] ushr 16) and 255,(palette[i] ushr 8) and 255,palette[i] and 255)
        check(rgb.indices.filter { rgb[it]==rgb.max() }==expected[i])
    }
    val drag=WheelDragSession(0.2,-0.3,100.0,100.0,100.0)
    check(drag.point(100.0,100.0).contentEquals(doubleArrayOf(0.2,-0.3)))
    check(kotlin.math.abs(drag.point(120.0,100.0)[0]-0.31)<1e-12)
    val edge=drag.point(10000.0,10000.0)
    check(kotlin.math.abs(kotlin.math.hypot(edge[0],edge[1])-1.0)<1e-12)
    check(runCatching { WheelDragSession(0.0,0.0,0.0,0.0,0.0) }.isFailure)
    val defaults=doubleArrayOf(0.0,1.0,1.0,0.18,1.0,0.0,0.0,0.0,0.0,0.0,0.0,1.0,1.0,1.0,1.0,1.0,1.0)
    defaults.forEachIndexed { index,saved ->
        val scale=ColorSliderScale.forParameter(index,saved)
        check(scale.progress(saved) in 0..ColorSliderScale.STEPS)
        check(kotlin.math.abs(scale.value(scale.progress(saved))-saved)<=(scale.high-scale.low)/ColorSliderScale.STEPS+1e-12)
        var previous=scale.low
        for(step in 0..ColorSliderScale.STEPS) {
            val value=scale.value(step)
            check(value>=previous && value in scale.low..scale.high)
            previous=value
        }
    }
    // Previously saved values outside practical UI defaults must NOT be silently clamped.
    for((index,saved) in listOf(0 to -8.0,0 to 8.0,1 to 4.0,8 to -1.0,11 to 4.0,14 to 4.0)) {
        val scale=ColorSliderScale.forParameter(index,saved)
        check(saved in scale.low..scale.high)
        check(kotlin.math.abs(scale.value(scale.progress(saved))-saved)<1e-9)
    }
    fun rejected(block: () -> Unit) { check(runCatching(block).isFailure) }
    rejected { ColorSliderScale.forParameter(17,0.0) }
    rejected { ColorSliderScale.forParameter(0,Double.NaN) }
    rejected { ColorSliderScale(0.0,0.0) }
    rejected { ColorSliderScale(0.0,1.0).value(-1) }
    val root=IllegalStateException("raw decoder color missing")
    val wrapped=RuntimeException("Video frame processing error",root)
    check(ColorPreviewFailure.describe(wrapped).contains(root.message!!))
    val long=RuntimeException("x".repeat(10000))
    check(ColorPreviewFailure.describe(long).length<=300)
    val report=RuntimeException("geometry="+"p".repeat(700)+" raw-crop-bottom=2159")
    check(!ColorPreviewFailure.describe(report).contains("raw-crop-bottom"))
    check(ColorPreviewFailure.details(report).contains("raw-crop-bottom=2159"))
    check(ColorPreviewFailure.details(long).length<=1200)
    val a=RuntimeException("A");val b=RuntimeException("B",a);a.initCause(b)
    check(ColorPreviewFailure.describe(a).length<100)
    for(stage in ExportFailureReport.Stage.entries) {
        val report=ExportFailureReport.details(wrapped,stage,3,2,720)
        check(report.contains("stage=${stage.name}") && report.contains("graded-clips=2"))
        check(report.contains("raw decoder color missing"))
    }
    check(ExportFailureReport.details(long,ExportFailureReport.Stage.RENDERING,1,0,720).length<1400)
    check(ExportFailureReport.details(a,ExportFailureReport.Stage.PREPARING,1,1,720).length<300)
    rejected { ExportFailureReport.details(root,ExportFailureReport.Stage.PREPARING,-1,0,720) }
    rejected { ExportFailureReport.details(root,ExportFailureReport.Stage.PREPARING,1,2,720) }
    rejected { ExportFailureReport.details(root,ExportFailureReport.Stage.PREPARING,1,0,0) }
    println("Production export reports: four phases, immutable context, nested/long/cyclic causes and invalid-context refusal passed")
    println("Production color controls: relative no-jump wheel transport, 34017 numeric range steps, saved-range preservation, bounded nested/cyclic diagnostics passed")
}
