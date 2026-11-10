package com.owncut.app

/** Relative pointer geometry only; no RGB/grade policy or momentum after finger release. */
internal class WheelDragSession(private val px: Double,private val py: Double,
    private val downX: Double,private val downY: Double,private val radius: Double) {
    init { require(listOf(px,py,downX,downY,radius).all { it.isFinite() } && radius>0) }
    fun point(x: Double,y: Double): DoubleArray {
        require(x.isFinite() && y.isFinite())
        var nx=px+(x-downX)/radius*0.55
        var ny=py+(y-downY)/radius*0.55
        val length=kotlin.math.hypot(nx,ny)
        if(length>1) { nx/=length;ny/=length }
        return doubleArrayOf(nx,ny)
    }
}
