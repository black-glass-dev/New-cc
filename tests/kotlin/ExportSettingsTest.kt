// Host test for ExportSettings (pure Kotlin, no Android). Run through tests/run_host_kotlin.py.
package com.owncut.app

fun main() {
    val defaults = ExportSettings.DEFAULT
    check(defaults.resolution == ExportResolution.ORIGINAL && defaults.quality == ExportQuality.ULTRA &&
        defaults.customMbps == null && defaults.profile == ExportCodecProfile.AUTO)

    // Original keeps the source size exactly, including odd sizes (no scaling effect).
    check(defaults.outputSize(1920, 1080) == (1920 to 1080))
    check(defaults.outputSize(1281, 721) == (1281 to 721))

    // Fixed heights keep aspect ratio and even sides.
    check(ExportSettings(resolution = ExportResolution.P720).outputSize(1920, 1080) == (1280 to 720))
    check(ExportSettings(resolution = ExportResolution.P1080).outputSize(1920, 1080) == (1920 to 1080))
    check(ExportSettings(resolution = ExportResolution.QHD_2K).outputSize(1920, 1080) == (2560 to 1440))
    check(ExportSettings(resolution = ExportResolution.UHD_4K).outputSize(1920, 1080) == (3840 to 2160))
    // Portrait: requested size applies to the long side.
    check(ExportSettings(resolution = ExportResolution.P1080).outputSize(1080, 1920) == (1080 to 1920))
    check(ExportSettings(resolution = ExportResolution.P720).outputSize(1080, 1920) == (720 to 1280))
    // Odd result is rounded up to even.
    check(ExportSettings(resolution = ExportResolution.P720).outputSize(1000, 999).second == 720)
    check(ExportSettings(resolution = ExportResolution.P720).outputSize(1001, 1000).first % 2 == 0)

    // Automatic bitrate scales with pixels, frame rate and quality preset.
    val ultra1080 = defaults.bitrateFor(1920, 1080, 30.0)
    check(ultra1080 == (1920.0 * 1080 * 30 * 0.25).toInt())
    check(ExportSettings(quality = ExportQuality.LOW).bitrateFor(1920, 1080, 30.0) < ultra1080)
    check(ExportSettings(quality = ExportQuality.HIGH).bitrateFor(1920, 1080, 30.0) < ultra1080)
    // Automatic bitrate is clamped to 1..80 Mbps.
    check(ExportSettings(quality = ExportQuality.LOW).bitrateFor(64, 64, 1.0) == 1_000_000)
    check(defaults.bitrateFor(3840, 2160, 60.0) == 80_000_000)

    // Custom bitrate always wins over the preset.
    check(ExportSettings(customMbps = 12).bitrateFor(3840, 2160, 60.0) == 12_000_000)

    // Parsing: blank is automatic; only whole numbers in 1..100 are accepted.
    check(ExportSettings.parseCustomMbps("  ") == null)
    check(ExportSettings.parseCustomMbps("25") == 25)
    check(ExportSettings.parseCustomMbps("1") == 1 && ExportSettings.parseCustomMbps("100") == 100)
    for (bad in listOf("0", "101", "-3", "2.5", "abc")) {
        val rejected = runCatching { ExportSettings.parseCustomMbps(bad) }.isFailure
        check(rejected) { "should reject $bad" }
    }
    check(runCatching { ExportSettings(customMbps = 0) }.isFailure)
    check(runCatching { ExportSettings(customMbps = 101) }.isFailure)
    println("ExportSettingsTest passed")
}
