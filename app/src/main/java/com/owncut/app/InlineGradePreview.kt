package com.owncut.app

import android.app.Activity
import android.app.Application
import android.os.Bundle
import androidx.media3.common.AudioAttributes
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.VideoSize
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer

/** Guarded video-graph alternative when the platform still retriever cannot produce a
 * frame (common with some high-bit-depth codecs). No source/HDR policy bypass or bitmap
 * reinterpretation. Main-thread owned; background releases codec/audio, resume is paused.
 */
@UnstableApi
internal class InlineGradePreview(
    private val activity: Activity, private val texture: AspectRatioTextureView,
    private val clip: ClipSnapshot, private val source: VideoSourceMetadata,
    positionMs: Long, private val onFrame: () -> Unit, private val onFailure: (Exception) -> Unit,
    private val interpretMissing: Boolean=false,
) : Application.ActivityLifecycleCallbacks {
    private var player: ExoPlayer?=null
    private var closed=false
    private var stopped=false
    private var grade=clip.grade
    private var before=false
    private var anchor=PreviewProgress.clipRelative(positionMs,clip.sourceInMs,clip.sourceOutMs)
    private var seekForward=false
    val isPlaying get()=player?.playWhenReady==true
    init { activity.application.registerActivityLifecycleCallbacks(this) }
    fun update(settings: ColorGrade, original: Boolean) {
        if(closed) return
        grade=settings;before=original
        if(stopped) return
        try {
            require(source.mediaId==clip.mediaId && source.videoTrackIndex>=0 && source.width>0 && source.height>0 && source.rotationDegrees in listOf(0,90,180,270)) { "Invalid or mismatched video fallback source" }
            val admitted=source.copy(color=PreviewColorPolicy.resolve(source.color,interpretMissing))
            try { ColorEngine.validateVideoSource(admitted.color) }
            catch(error: Exception) { throw IllegalArgumentException("Source color: ${source.color}. ${error.message}",error) }
            val selected=if(before) settings.copy(enabled=false) else settings
            val shaders=ColorEngine.shaderSources(selected,ColorEngine.TextureEncoding.MEDIA3_SDR_709)
            val prepared=GradedFramePipeline.prepare(ColorRenderPlan.PreparedClip(clip.copy(grade=selected),admitted,shaders))
            val current=player
            if(current!=null) {
                current.setVideoEffects(listOf(prepared.effect))
                // There is no safely public paused replay cache. Request a fresh decoded
                // frame around a fixed anchor (+/-1ms), never edit clip/timeline timing.
                if(!current.playWhenReady) {
                    seekForward=!seekForward
                    val duration=clip.sourceOutMs-clip.sourceInMs
                    current.seekTo(PreviewProgress.freshFramePosition(anchor,duration,seekForward))
                }
                return
            }
            val p=ExoPlayer.Builder(activity.applicationContext,GradedPreviewRenderers(activity.applicationContext,prepared,admitted,interpretMissing)).build()
            player=p
            p.setAudioAttributes(AudioAttributes.DEFAULT,true)
            p.addListener(object: Player.Listener {
                override fun onPlayerError(error: PlaybackException) { if(player===p) failed(error) }
                override fun onRenderedFirstFrame() { if(!closed && player===p) onFrame() }
                override fun onVideoSizeChanged(size: VideoSize) {
                    if(player===p) texture.setVideoSize((size.width*size.pixelWidthHeightRatio).toInt().coerceAtLeast(1),size.height)
                }
            })
            p.setVideoTextureView(texture);p.setVideoEffects(listOf(prepared.effect));p.repeatMode=Player.REPEAT_MODE_ONE
            p.setMediaItem(MediaItem.Builder().setUri(clip.mediaId).setClippingConfiguration(
                MediaItem.ClippingConfiguration.Builder().setStartPositionMs(clip.sourceInMs).setEndPositionMs(clip.sourceOutMs).build()).build())
            p.seekTo(anchor);p.playWhenReady=false;p.prepare()
        } catch(error: Exception) { failed(error) }
    }
    fun togglePlaying() {
        try { player?.let { p ->
            if(p.playWhenReady) { p.pause();anchor=p.currentPosition.coerceIn(0,clip.sourceOutMs-clip.sourceInMs-1) }
            else p.play()
        } } catch(error: Exception) { failed(error) }
    }
    private fun release() {
        val old=player;player=null
        try { old?.release() } catch(error: Exception) { android.util.Log.e("OwncutInlineGrade","Preview release failed",error) }
    }
    private fun failed(error: Exception) { release();if(!closed) onFailure(error) }
    fun close() { if(closed) return;closed=true;release();activity.application.unregisterActivityLifecycleCallbacks(this) }
    override fun onActivityStopped(value: Activity) { if(value===activity && !closed) {
        stopped=true
        player?.let { anchor=it.currentPosition.coerceIn(0,clip.sourceOutMs-clip.sourceInMs-1) }
        release()
    } }
    override fun onActivityStarted(value: Activity) { if(value===activity && stopped && !closed) { stopped=false;update(grade,before) } }
    override fun onActivityCreated(value: Activity,state: Bundle?) {}
    override fun onActivityResumed(value: Activity) {}
    override fun onActivityPaused(value: Activity) {}
    override fun onActivitySaveInstanceState(value: Activity,state: Bundle) {}
    override fun onActivityDestroyed(value: Activity) { if(value===activity) close() }
}
