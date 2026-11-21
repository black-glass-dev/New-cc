package com.owncut.app

import java.nio.ByteBuffer

private fun rejected(block: () -> Unit) {
    try { block() } catch (_: IllegalArgumentException) { return }
    error("expected rejected color call")
}
private fun bytes(buffer: ByteBuffer): List<Int> = (0 until buffer.capacity()).map { buffer.get(it).toInt() and 255 }
fun main() {
    check(ColorEngine.visibleVideoGeometry(2880,2176,15,intArrayOf(0,0,2879,2159)).contentEquals(intArrayOf(2880,2160)))
    check(ColorEngine.visibleVideoGeometry(2880,2176,0,IntArray(4)).contentEquals(intArrayOf(2880,2176)))
    check(ColorEngine.visibleVideoGeometry(Int.MAX_VALUE,Int.MAX_VALUE,15,intArrayOf(0,0,Int.MAX_VALUE-1,Int.MAX_VALUE-1)).contentEquals(intArrayOf(Int.MAX_VALUE,Int.MAX_VALUE)))
    for(mask in -1..16) if(mask!=0 && mask!=15) rejected { ColorEngine.visibleVideoGeometry(2880,2176,mask,intArrayOf(0,0,2879,2159)) }
    rejected { ColorEngine.visibleVideoGeometry(0,2176,0,IntArray(4)) }
    rejected { ColorEngine.visibleVideoGeometry(2880,2176,15,intArrayOf(0,0,2880,2159)) }
    rejected { ColorEngine.visibleVideoGeometry(2880,2176,15,intArrayOf(-1,0,2879,2159)) }
    rejected { ColorEngine.visibleVideoGeometry(2880,2176,15,IntArray(3)) }
    val nativeGeometry=ColorEngine::class.java.getDeclaredMethod("nativeVisibleGeometryV1",IntArray::class.java).apply { isAccessible=true }
    check(runCatching { nativeGeometry.invoke(ColorEngine,intArrayOf(2880,2176)) }.exceptionOrNull()?.cause is IllegalArgumentException)
    println("Native inclusive crop geometry through real JNI: padding, partial/invalid bounds, overflow and malformed schema passed")
    val values=ColorGrade().parametersV1().map { it.toString() }.toMutableList()
    values[0]="১.৫";values[5]="−০.২৫"
    val parsed=ColorGradeDraft.parse(values,true)
    check(parsed.exposureStops==1.5 && parsed.balanceStops.red== -0.25)
    check(ColorGradeDraft.parse(values,false)==parsed.copy(enabled=false))
    rejected { ColorGradeDraft.parse(values.drop(1),true) }
    rejected { ColorGradeDraft.parse(values.toMutableList().apply { set(0,"not a number") },true) }
    val preview=ColorGradeDraft.validatedPreview(values,true)
    check(preview==parsed)
    values[0]="২.০"
    check(preview.exposureStops==1.5) // immutable captured settings
    rejected { ColorGradeDraft.validatedPreview(values.toMutableList().apply { set(0,"NaN") },true) }
    rejected { ColorGradeDraft.validatedPreview(values.toMutableList().apply { set(0,"999999") },true) }
    check(!ColorGradeDraft.validatedPreview(values,false).enabled)
    println("Draft preview: bilingual immutable snapshot, native GPU budget and invalid-value rejection passed")
    check(!ColorEngine.hdrStaticPresent(null))
    check(!ColorEngine.hdrStaticPresent(ByteArray(0)))
    check(!ColorEngine.hdrStaticPresent(ByteArray(25)))
    check(ColorEngine.hdrStaticPresent(ByteArray(24)))
    for(index in 0..24) check(ColorEngine.hdrStaticPresent(ByteArray(25).apply { this[index]=1 }))
    for(group in 0..3) {
        check(ColorEngine.wheel(ColorGrade(),group,0.0,0.0)==ColorGrade())
        val adjusted=ColorEngine.wheel(ColorGrade(),group,0.1,0.0)
        check(kotlin.math.abs(ColorEngine.wheelPosition(adjusted,group)[0]-0.1)<1e-12)
    }
    rejected { ColorEngine.wheel(ColorGrade(),4,0.0,0.0) }
    rejected { ColorEngine.wheel(ColorGrade(),0,Double.NaN,0.0) }
    println("Native wheels and conservative HDR-placeholder classification through JNI passed")
    val argb=intArrayOf(0x25808080,0x00ff0000)
    val unchanged=argb.copyOf()
    check(ColorFrameBuffers.render(argb,2,1,ColorGrade(exposureStops=1.0)).contentEquals(intArrayOf(0x25b0b0b0,0x00ff0000)))
    check(ColorFrameBuffers.render(argb,2,1,ColorGrade(enabled=false)).contentEquals(argb))
    check(argb.contentEquals(unchanged))
    rejected { ColorFrameBuffers.render(argb,3,1,ColorGrade()) }
    rejected { ColorFrameBuffers.render(argb,0,1,ColorGrade()) }
    println("Actual ARGB sRGB still-frame transport/native pixels: channels, alpha, bypass, source and shape passed")
    val stats=ColorFrameBuffers.compare(intArrayOf(0x280a141e,0x82646e78.toInt()),
        intArrayOf(0x290f121e,0x825a6e7d.toInt()),2,1)
    check(stats.pixels==2L && stats.maximumRed==10 && stats.maximumGreen==2 && stats.maximumBlue==5)
    check(kotlin.math.abs(stats.meanAbsoluteRgbError-22.0/6)<1e-12 && stats.alphaMismatches==1L)
    check(kotlin.math.abs(stats.meanSignedRgbError+2.0/6)<1e-12)
    rejected { ColorFrameBuffers.compare(argb,intArrayOf(0),2,1) }
    println("Native comparison diagnostics: exact per-channel max/mean/bias/alpha metrics passed")
    val source=ByteBuffer.allocateDirect(8)
    listOf(128,128,128,37,255,0,0,0).forEachIndexed { i,value -> source.put(i,value.toByte()) }
    val out=ByteBuffer.allocateDirect(8)
    val original=bytes(source)
    ColorEngine.renderRgba8(source,out,2,1,8,8,ColorGrade(exposureStops=1.0))
    check(bytes(out)==listOf(176,176,176,37,255,0,0,0))
    ColorEngine.renderRgba8(source.asReadOnlyBuffer(),out,2,1,8,8,ColorGrade(balanceStops=ColorChannels(1.0,0.0,0.0)))
    check(bytes(out).take(4)==listOf(176,128,128,37))
    ColorEngine.renderRgba8(source,out,2,1,8,8,ColorGrade(enabled=false,exposureStops=8.0))
    check(bytes(out)==original)
    val distinct=ColorGrade(exposureStops=0.75,contrast=1.5,saturation=0.8,pivot=0.22,strength=0.6,
        balanceStops=ColorChannels(0.1,0.2,0.3),lift=ColorChannels(0.01,0.02,0.03),
        gamma=ColorChannels(0.8,1.1,1.3),gain=ColorChannels(0.8,1.2,1.4))
    check(distinct.parametersV1().contentEquals(doubleArrayOf(0.75,1.5,0.8,0.22,0.6,
        0.1,0.2,0.3,0.01,0.02,0.03,0.8,1.1,1.3,0.8,1.2,1.4)))
    check(ColorGrade.fromParametersV1(distinct.parametersV1(),distinct.enabled)==distinct)
    ColorEngine.renderRgba8(source,out,2,1,8,8,distinct)
    check(out.get(3).toInt()==37 && out.get(7).toInt()==0 && bytes(source)==original)
    val before=bytes(out)
    rejected { ColorEngine.renderRgba8(source,out,2,1,8,8,ColorGrade(gamma=ColorChannels(0.0,1.0,1.0))) }
    rejected { ColorEngine.renderRgba8(source,out,2,1,8,8,ColorGrade(exposureStops=Double.NaN)) }
    rejected { ColorEngine.renderRgba8(source,source,2,1,8,8) }
    rejected { ColorEngine.renderRgba8(source,out.asReadOnlyBuffer(),2,1,8,8) }
    rejected { ColorEngine.renderRgba8(ByteBuffer.allocate(8),out,2,1,8,8) }
    rejected { ColorEngine.renderRgba8(source,out,2,2,8,8) }
    rejected { ColorEngine.renderRgba8(source,out,2,1,7,8) }
    rejected { ColorEngine.renderRgba8(source,out,0,1,8,8) }
    source.position(1); rejected { ColorEngine.renderRgba8(source,out,2,1,8,8) }; source.position(0)
    out.limit(4); rejected { ColorEngine.renderRgba8(source,out,2,1,8,8) }; out.limit(8)
    check(bytes(out)==before && bytes(source)==original)
    for (encoding in ColorEngine.TextureEncoding.entries) {
        val recipe=ColorEngine.shaderSources(distinct,encoding)
        check(recipe.vertex.startsWith("#version 100") && recipe.fragment.contains("sampleColor.a"))
    }
    check(ColorEngine.shaderSources(ColorGrade(),ColorEngine.TextureEncoding.MEDIA3_SDR_709).fragment.contains("texture2D"))
    check(ColorEngine.shaderSources(distinct,ColorEngine.TextureEncoding.MEDIA3_SDR_709).fragment.contains("0.0812"))
    val excessive=ColorGrade(exposureStops=8.0,balanceStops=ColorChannels(2.0,2.0,2.0),
        lift=ColorChannels(-1.0,-1.0,-1.0),gamma=ColorChannels(0.1,0.1,0.1),gain=ColorChannels(4.0,4.0,4.0))
    rejected { ColorEngine.shaderSources(excessive,ColorEngine.TextureEncoding.SRGB) }
    check(!ColorEngine.shaderSources(excessive.copy(enabled=false),ColorEngine.TextureEncoding.SRGB).fragment.contains("toLight"))
    println("Production Kotlin native GPU recipes: immutable parameters, 3 encodings and safety rejection passed")
    println("Production Kotlin -> JNI -> C++: real grading pixels, bypass, RGB parameters, alpha/source preservation and invalid-buffer safety passed")
}
