package com.owncut.app

import java.util.Collections

/** Ephemeral worker snapshot, NOT proof that a URI's bytes remain unchanged after probing.
 * Decoder format must be revalidated when the live integration is installed.
 */
internal data class VideoSourceMetadata(
    val mediaId: String, val videoTrackIndex: Int, val width: Int, val height: Int,
    val rotationDegrees: Int, val color: ColorEngine.VideoColorMetadata,
    val geometryDescription: String="",
)

/** Pure immutable transport for BOTH candidate preview and export. No timeline math.
 * One preparation captures native grade values and compiles native shader recipes;
 * no per-frame shader generation or source probing. Decoder gates may perform per-render
 * native metadata validation. No global URI cache or mutable partially published plan.
 */
internal class ColorRenderPlan private constructor(val revision: String, val clips: List<PreparedClip>) {
    data class PreparedClip(val clip: ClipSnapshot, val source: VideoSourceMetadata,
                            val shaders: ColorEngine.ShaderSources)
    companion object {
        fun prepare(revision: String, clips: List<ClipSnapshot>,
                    checkCancelled: () -> Unit, readSource: (String) -> VideoSourceMetadata): ColorRenderPlan {
            require(revision.isNotBlank() && clips.isNotEmpty()) { "A revision and clip snapshot are required" }
            val snapshot=clips.toList()
            require(snapshot.map { it.id }.toSet().size == snapshot.size) { "Duplicate clip IDs" }
            val sources=mutableMapOf<String,VideoSourceMetadata>()
            val prepared=snapshot.map { clip ->
                checkCancelled()
                require(clip.id.isNotBlank() && clip.mediaId.isNotBlank()) { "Invalid clip/source identity" }
                val source=sources.getOrPut(clip.mediaId) {
                    val result=readSource(clip.mediaId)
                    checkCancelled()
                    require(result.mediaId == clip.mediaId && result.videoTrackIndex >= 0 &&
                        result.width > 0 && result.height > 0 && result.rotationDegrees in listOf(0,90,180,270)) {
                        "Invalid or mismatched video source metadata"
                    }
                    ColorEngine.validateVideoSource(result.color)
                    result
                }
                val shaders=ColorEngine.shaderSources(clip.grade,ColorEngine.TextureEncoding.MEDIA3_SDR_709)
                checkCancelled()
                PreparedClip(clip,source,shaders)
            }
            checkCancelled()
            return ColorRenderPlan(revision,Collections.unmodifiableList(prepared))
        }
    }
}
