package com.owncut.app

import java.nio.ByteBuffer

internal data class ColorChannels(val red: Double, val green: Double, val blue: Double)

/** Immutable settings only. C++ owns validation, transfer functions and all grading math.
 * RGB balance is channel stops, not calibrated Kelvin/tint. Clip/project persistence is owned by the native timeline codec.
 */
internal data class ColorGrade(
    val enabled: Boolean = true,
    val exposureStops: Double = 0.0,
    val contrast: Double = 1.0,
    val saturation: Double = 1.0,
    val pivot: Double = 0.18,
    val strength: Double = 1.0,
    val balanceStops: ColorChannels = ColorChannels(0.0,0.0,0.0),
    val lift: ColorChannels = ColorChannels(0.0,0.0,0.0),
    val gamma: ColorChannels = ColorChannels(1.0,1.0,1.0),
    val gain: ColorChannels = ColorChannels(1.0,1.0,1.0),
) {
    companion object {
        fun fromParametersV1(values: DoubleArray, enabled: Boolean): ColorGrade {
            require(values.size == 17)
            return ColorGrade(enabled,values[0],values[1],values[2],values[3],values[4],
                ColorChannels(values[5],values[6],values[7]),ColorChannels(values[8],values[9],values[10]),
                ColorChannels(values[11],values[12],values[13]),ColorChannels(values[14],values[15],values[16]))
        }
    }

    fun parametersV1(): DoubleArray = doubleArrayOf(exposureStops,contrast,saturation,pivot,strength,
        balanceStops.red,balanceStops.green,balanceStops.blue,lift.red,lift.green,lift.blue,
        gamma.red,gamma.green,gamma.blue,gain.red,gain.green,gain.blue)
}

/** Stateless native frame/shader bridge. Used by the still editor and isolated experimental
 * preview/candidate export; normal timeline playback/delivery remain unchanged.
 * Buffers must be nonoverlapping, direct, full-capacity slices at position zero.
 * Explicit sRGB straight-alpha RGBA8 only: do not pass YUV, HDR or premultiplied pixels.
 */
internal object ColorEngine {
    init { System.loadLibrary("owncut_android") }

    fun renderRgba8(source: ByteBuffer, output: ByteBuffer, width: Int, height: Int,
                    sourceStride: Int, outputStride: Int, grade: ColorGrade = ColorGrade()) {
        nativeRenderV1(source,output,width,height,sourceStride,outputStride,grade.parametersV1(),grade.enabled)
    }
    // Explicit intermediate texture contract, not source-media metadata inference.
    enum class TextureEncoding(val nativeCode: Int) { SRGB(0), LINEAR_709(1), MEDIA3_SDR_709(2) }
    data class ShaderSources(val vertex: String, val fragment: String)
    fun shaderSources(grade: ColorGrade, encoding: TextureEncoding): ShaderSources =
        ShaderSources(nativeShaderV1(grade.parametersV1(),grade.enabled,encoding.nativeCode,true),
            nativeShaderV1(grade.parametersV1(),grade.enabled,encoding.nativeCode,false))
    fun wheel(grade: ColorGrade, group: Int, x: Double, y: Double): ColorGrade =
        ColorGrade.fromParametersV1(nativeWheelV1(grade.parametersV1(),grade.enabled,group,x,y,false),grade.enabled)
    fun wheelPosition(grade: ColorGrade, group: Int): DoubleArray =
        nativeWheelV1(grade.parametersV1(),grade.enabled,group,0.0,0.0,true)
    fun hdrStaticPresent(bytes: ByteArray?): Boolean = nativeHdrStaticPresentV1(bytes)
    private external fun nativeWheelV1(parameters: DoubleArray,enabled: Boolean,group: Int,x: Double,y: Double,positionOnly: Boolean): DoubleArray
    private external fun nativeHdrStaticPresentV1(bytes: ByteArray?): Boolean
    enum class VideoPrimaries(val nativeCode: Int) { UNKNOWN(0), REC709(1), OTHER(2) }
    enum class VideoTransfer(val nativeCode: Int) { UNKNOWN(0), SDR_VIDEO(1), SRGB(2), PQ(3), HLG(4), OTHER(5) }
    enum class VideoRange(val nativeCode: Int) { UNKNOWN(0), FULL(1), LIMITED(2) }
    data class VideoColorMetadata(val primaries: VideoPrimaries, val transfer: VideoTransfer, val range: VideoRange, val hasHdrStaticMetadata: Boolean=false)
    fun validateVideoSource(source: VideoColorMetadata) = nativeValidateVideoSourceV1(
        source.primaries.nativeCode,source.transfer.nativeCode,source.range.nativeCode,source.hasHdrStaticMetadata)
    data class DecodedVideoFormat(val width: Int, val height: Int, val color: VideoColorMetadata) {
        fun parametersV1(): IntArray = intArrayOf(width,height,color.primaries.nativeCode,
            color.transfer.nativeCode,color.range.nativeCode,if(color.hasHdrStaticMetadata) 1 else 0)
    }
    fun validateDecodedFormat(expected: DecodedVideoFormat, actual: DecodedVideoFormat) =
        nativeValidateDecodedFormatV1(expected.parametersV1()+actual.parametersV1())
    /** Transport only; C++ validates inclusive crop bounds and computes visible geometry. */
    fun visibleVideoGeometry(width: Int,height: Int,cropMask: Int,crop: IntArray): IntArray {
        require(crop.size==4) { "Four crop coordinates required" }
        return nativeVisibleGeometryV1(intArrayOf(width,height,cropMask)+crop)
    }
    private external fun nativeVisibleGeometryV1(fields: IntArray): IntArray
    private external fun nativeValidateDecodedFormatV1(parameters: IntArray)
    private external fun nativeValidateVideoSourceV1(primaries: Int, transfer: Int, range: Int, hdrStatic: Boolean)
    private external fun nativeShaderV1(parameters: DoubleArray, enabled: Boolean, encoding: Int, vertex: Boolean): String
    data class FrameComparison(val pixels: Long, val maximumRed: Int, val maximumGreen: Int, val maximumBlue: Int,
        val meanAbsoluteRgbError: Double, val meanSignedRgbError: Double, val alphaMismatches: Long)
    fun compareRgba8(expected: ByteBuffer, actual: ByteBuffer, width: Int, height: Int,
        expectedStride: Int, actualStride: Int): FrameComparison {
        val fields=nativeCompareRgba8V1(expected,actual,width,height,expectedStride,actualStride)
        check(fields.size==7)
        return FrameComparison(fields[0].toLong(),fields[1].toInt(),fields[2].toInt(),fields[3].toInt(),
            fields[4],fields[5],fields[6].toLong())
    }
    private external fun nativeCompareRgba8V1(expected: ByteBuffer, actual: ByteBuffer, width: Int, height: Int,
        expectedStride: Int, actualStride: Int): DoubleArray
    private external fun nativeRenderV1(source: ByteBuffer, output: ByteBuffer, width: Int,
        height: Int, sourceStride: Int, outputStride: Int, parameters: DoubleArray, enabled: Boolean)
}
