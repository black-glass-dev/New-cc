package com.owncut.app

import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

/** ART/ABI contract sources. Added, not a claim of device compilation/execution. */
class ColorEngineContractTest {
    @Test fun actualPixelsPreserveSourceAndAlpha() {
        val input=ByteBuffer.allocateDirect(4)
        listOf(128,128,128,37).forEachIndexed { i,x -> input.put(i,x.toByte()) }
        val output=ByteBuffer.allocateDirect(4)
        ColorEngine.renderRgba8(input,output,1,1,4,4,ColorGrade(exposureStops=1.0))
        assertEquals(176,output.get(0).toInt() and 255)
        assertEquals(176,output.get(1).toInt() and 255)
        assertEquals(37,output.get(3).toInt() and 255)
        assertEquals(128,input.get(0).toInt() and 255)
        ColorEngine.renderRgba8(input,output,1,1,4,4,ColorGrade(enabled=false,exposureStops=8.0))
        assertEquals(128,output.get(0).toInt() and 255)
    }
    @Test fun invalidBuffersAndSettingsFailWithoutWrites() {
        val input=ByteBuffer.allocateDirect(4); val output=ByteBuffer.allocateDirect(4)
        output.put(0,77.toByte())
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.renderRgba8(input,input,1,1,4,4) }
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.renderRgba8(input,output.asReadOnlyBuffer(),1,1,4,4) }
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.renderRgba8(ByteBuffer.allocate(4),output,1,1,4,4) }
        assertThrows(IllegalArgumentException::class.java) { ColorEngine.renderRgba8(input,output,1,1,4,4,ColorGrade(exposureStops=Double.NaN)) }
        assertEquals(77,output.get(0).toInt())
    }
}
