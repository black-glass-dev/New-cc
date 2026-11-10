package com.owncut.app

/** Explicit preview interpretation only. Never mutates source tags, saved grade or export plan. */
internal object PreviewColorPolicy {
    fun hasMissing(c: ColorEngine.VideoColorMetadata)=c.primaries==ColorEngine.VideoPrimaries.UNKNOWN ||
        c.transfer==ColorEngine.VideoTransfer.UNKNOWN || c.range==ColorEngine.VideoRange.UNKNOWN
    fun needsInterpretation(c: ColorEngine.VideoColorMetadata)=hasMissing(c) && canInterpret(c)
    fun canInterpret(c: ColorEngine.VideoColorMetadata)=!c.hasHdrStaticMetadata &&
        c.primaries!=ColorEngine.VideoPrimaries.OTHER &&
        c.transfer in listOf(ColorEngine.VideoTransfer.UNKNOWN,ColorEngine.VideoTransfer.SDR_VIDEO)
    fun resolve(c: ColorEngine.VideoColorMetadata, enabled: Boolean): ColorEngine.VideoColorMetadata {
        if(!enabled) return c
        require(canInterpret(c)) { "Explicit non-709/HDR color cannot be overridden: $c" }
        return c.copy(
            primaries=if(c.primaries==ColorEngine.VideoPrimaries.UNKNOWN) ColorEngine.VideoPrimaries.REC709 else c.primaries,
            transfer=if(c.transfer==ColorEngine.VideoTransfer.UNKNOWN) ColorEngine.VideoTransfer.SDR_VIDEO else c.transfer,
            range=if(c.range==ColorEngine.VideoRange.UNKNOWN) ColorEngine.VideoRange.LIMITED else c.range)
    }
    fun decoded(c: ColorEngine.VideoColorMetadata, expected: ColorEngine.VideoColorMetadata, enabled: Boolean): ColorEngine.VideoColorMetadata {
        if(!enabled) return c
        val resolved=resolve(c,true)
        return resolved.copy(
            primaries=if(c.primaries==ColorEngine.VideoPrimaries.UNKNOWN) expected.primaries else c.primaries,
            transfer=if(c.transfer==ColorEngine.VideoTransfer.UNKNOWN) expected.transfer else c.transfer,
            range=if(c.range==ColorEngine.VideoRange.UNKNOWN) expected.range else c.range)
    }
    /** Two actual parsers, not resolution heuristics. Known conflicts fail closed. */
    fun merge(a: ColorEngine.VideoColorMetadata,b: ColorEngine.VideoColorMetadata): ColorEngine.VideoColorMetadata {
        fun <T> tag(x: T,y: T,unknown: T): T {
            require(x==unknown || y==unknown || x==y) { "Conflicting platform/container color metadata: $a / $b" }
            return if(x==unknown) y else x
        }
        return ColorEngine.VideoColorMetadata(tag(a.primaries,b.primaries,ColorEngine.VideoPrimaries.UNKNOWN),
            tag(a.transfer,b.transfer,ColorEngine.VideoTransfer.UNKNOWN),tag(a.range,b.range,ColorEngine.VideoRange.UNKNOWN),
            a.hasHdrStaticMetadata || b.hasHdrStaticMetadata)
    }
}
