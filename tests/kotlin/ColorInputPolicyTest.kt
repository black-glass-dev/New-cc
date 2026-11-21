package com.owncut.app

fun main() {
    val known=ColorEngine.VideoColorMetadata(ColorEngine.VideoPrimaries.REC709,
        ColorEngine.VideoTransfer.SDR_VIDEO,ColorEngine.VideoRange.LIMITED)
    check(!PreviewColorPolicy.hasMissing(known))
    check(!PreviewColorPolicy.needsInterpretation(known))
    for(c in listOf(known.copy(primaries=ColorEngine.VideoPrimaries.UNKNOWN),
        known.copy(transfer=ColorEngine.VideoTransfer.UNKNOWN),known.copy(range=ColorEngine.VideoRange.UNKNOWN))) {
        check(PreviewColorPolicy.hasMissing(c) && PreviewColorPolicy.needsInterpretation(c))
        check(PreviewColorPolicy.resolve(c,true)==known)
    }
    for(c in listOf(known.copy(transfer=ColorEngine.VideoTransfer.PQ,range=ColorEngine.VideoRange.UNKNOWN),
        known.copy(transfer=ColorEngine.VideoTransfer.HLG,range=ColorEngine.VideoRange.UNKNOWN),
        known.copy(primaries=ColorEngine.VideoPrimaries.OTHER,range=ColorEngine.VideoRange.UNKNOWN),
        known.copy(hasHdrStaticMetadata=true,range=ColorEngine.VideoRange.UNKNOWN))) {
        check(PreviewColorPolicy.hasMissing(c) && !PreviewColorPolicy.needsInterpretation(c))
        check(runCatching { PreviewColorPolicy.resolve(c,true) }.isFailure)
    }
    // UI eligibility must not change existing decoder interpretation for known tags.
    check(PreviewColorPolicy.decoded(known,known,true)==known)
    println("Production Input UI policy: known tags informational, missing SDR opt-in, HDR/other conflicts blocked PASS")
}
