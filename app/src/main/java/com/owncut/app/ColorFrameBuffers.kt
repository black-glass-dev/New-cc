package com.owncut.app

import java.nio.ByteBuffer

/** Android Bitmap.getPixels straight ARGB sRGB ints -> native explicit RGBA8 -> ARGB.
 * Pixel transport only, no transfer/grade math. Not raw codec/YUV/HDR/premultiplied data.
 */
internal object ColorFrameBuffers {
    fun render(source: IntArray, width: Int, height: Int, grade: ColorGrade): IntArray {
        require(width>0 && height>0 && Math.multiplyExact(width,height)==source.size)
        val bytes=Math.multiplyExact(source.size,4)
        val input=pack(source,bytes)
        val output=ByteBuffer.allocateDirect(bytes)
        ColorEngine.renderRgba8(input,output,width,height,Math.multiplyExact(width,4),Math.multiplyExact(width,4),grade)
        return IntArray(source.size) { i ->
            val o=i*4
            ((output.get(o+3).toInt() and 255) shl 24) or ((output.get(o).toInt() and 255) shl 16) or
                ((output.get(o+1).toInt() and 255) shl 8) or (output.get(o+2).toInt() and 255)
        }
    }
    /** Rec.709 exposure oracle (step 2). Mirrors engine/color/src/color_fp32.cpp in double precision:
     * decode Rec.709 -> linear, multiply by 2^stops, encode Rec.709, round to 8 bit. Alpha passes through.
     * Fails closed for any grade field other than exposure, so it never silently models a different grade.
     */
    fun renderRec709Exposure(source: IntArray, width: Int, height: Int, grade: ColorGrade): IntArray {
        require(width>0 && height>0 && Math.multiplyExact(width,height)==source.size)
        require(grade.contrast==1.0 && grade.saturation==1.0 && grade.strength==1.0 &&
            grade.balanceStops==ColorChannels(0.0,0.0,0.0) && grade.lift==ColorChannels(0.0,0.0,0.0) &&
            grade.gamma==ColorChannels(1.0,1.0,1.0) && grade.gain==ColorChannels(1.0,1.0,1.0)) {
            "Rec.709 oracle covers exposure only; non-neutral fields need their own oracle"
        }
        val scale=Math.pow(2.0,grade.exposureStops)
        return IntArray(source.size) { i ->
            val p=source[i]
            fun channel(shift: Int): Int {
                val encoded=((p shr shift) and 255)/255.0
                val linear=rec709ToLinear(encoded)*scale
                val out=rec709FromLinear(linear).coerceIn(0.0,1.0)
                return Math.floor(out*255.0+0.5).toInt()
            }
            (p and 0xFF000000.toInt()) or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
        }
    }
    private fun rec709ToLinear(e: Double): Double {
        val a=Math.abs(e)
        val l=if(a<0.081) a/4.5 else Math.pow((a+0.099)/1.099,1.0/0.45)
        return Math.copySign(l,e)
    }
    private fun rec709FromLinear(l: Double): Double {
        val a=Math.abs(l)
        val e=if(a<0.018) a*4.5 else 1.099*Math.pow(a,0.45)-0.099
        return Math.copySign(e,l)
    }
    fun compare(expected: IntArray, actual: IntArray, width: Int, height: Int): ColorEngine.FrameComparison {
        require(width>0 && height>0 && Math.multiplyExact(width,height)==expected.size && expected.size==actual.size)
        val bytes=Math.multiplyExact(expected.size,4)
        return ColorEngine.compareRgba8(pack(expected,bytes),pack(actual,bytes),width,height,
            Math.multiplyExact(width,4),Math.multiplyExact(width,4))
    }
    private fun pack(source: IntArray, bytes: Int)=ByteBuffer.allocateDirect(bytes).apply {
        source.forEach { p -> put((p shr 16).toByte());put((p shr 8).toByte());put(p.toByte());put((p ushr 24).toByte()) }
        position(0)
    }
}
