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
    positionMs: Long, private val onFrame: (Boolean,Boolean) -> Unit, private val onFailure: (Exception) -> Unit,
    private val interpretMissing: Boolean=false,
) : Application.ActivityLifecycleCallbacks {
    private val main=android.os.Handler(android.os.Looper.getMainLooper())
    private val work=PreviewWorkQueue()
    private var player: ExoPlayer?=null
    private var closed=false
    private var stopped=false
    private var grade=clip.grade
    private var before=false
    private var userPlaying=false
    private var surfaceReady=false
    private var drawn=false
    private var drawnBefore=false
    private var generation=0L
    private var timeout: Runnable?=null
    private val pump=Runnable { applyLatest() }
    private var anchor=PreviewProgress.clipRelative(positionMs,clip.sourceInMs,clip.sourceOutMs)
    private var seekForward=false
    val isPlaying get()=userPlaying
    init { activity.application.registerActivityLifecycleCallbacks(this) }
    fun update(settings: ColorGrade, original: Boolean) {
        if(closed) return
        grade=settings;before=original
        work.request()
        if(!stopped) applyLatest()
    }
    private fun applyLatest() {
        if(closed || stopped) return
        val token=work.begin() ?: return
        val epoch=generation
        val initial=player==null
        try {
            require(source.mediaId==clip.mediaId && source.videoTrackIndex>=0 && source.width>0 && source.height>0 && source.rotationDegrees in listOf(0,90,180,270)) { "Invalid or mismatched video fallback source" }
            val admitted=source.copy(color=PreviewColorPolicy.resolve(source.color,interpretMissing))
            try { ColorEngine.validateVideoSource(admitted.color) }
            catch(error: Exception) { throw IllegalArgumentException("Source color: ${source.color}. ${error.message}",error) }
            val selectedBefore=before
            val selected=if(selectedBefore) grade.copy(enabled=false) else grade
            val shaders=ColorEngine.shaderSources(selected,ColorEngine.TextureEncoding.MEDIA3_SDR_709)
            // First successful shader draw acknowledges exactly this immutable recipe.
            // One active request + one latest snapshot prevents endless seek/recompile churn.
            val acknowledged=java.util.concurrent.atomic.AtomicBoolean(false)
            val prepared=GradedFramePipeline.prepare(ColorRenderPlan.PreparedClip(clip.copy(grade=selected),admitted,shaders)) {
                if(acknowledged.compareAndSet(false,true)) main.post {
                    if(!closed && !stopped && epoch==generation && work.finish(token)) {
                        timeout?.let { main.removeCallbacks(it) };timeout=null
                        drawn=true;drawnBefore=selectedBefore
                        if(!userPlaying) player?.pause()
                        if(surfaceReady) onFrame(drawnBefore,work.pending)
                        if(work.pending) main.postDelayed(pump,66)
                    }
                }
            }
            val deadline=Runnable {
                if(!closed && epoch==generation && work.active==token)
                    failed(IllegalStateException("Timed out waiting for a graded video frame; Refresh to retry"))
            }
            timeout=deadline;main.postDelayed(deadline,if(initial) 10000 else 5000)
            val current=player
            if(current!=null) {
                current.setVideoEffects(listOf(prepared.effect))
                if(!userPlaying) {
                    seekForward=!seekForward
                    current.volume=0f
                    current.seekTo(PreviewProgress.freshFramePosition(anchor,clip.sourceOutMs-clip.sourceInMs,seekForward))
                    // A tiny paused seek may be optimized away by the player/codec. Decode
                    // muted until this recipe actually draws, then pause. Never edit timing.
                    current.play()
                }
                return
            }
            val p=ExoPlayer.Builder(activity.applicationContext,GradedPreviewRenderers(activity.applicationContext,prepared,admitted,interpretMissing)).build()
            player=p;surfaceReady=false;drawn=false
            p.setAudioAttributes(AudioAttributes.DEFAULT,true)
            p.addListener(object: Player.Listener {
                override fun onPlayerError(error: PlaybackException) { if(player===p) failed(error) }
                override fun onRenderedFirstFrame() {
                    if(!closed && player===p) { surfaceReady=true;if(drawn) onFrame(drawnBefore,work.pending) }
                }
                override fun onVideoSizeChanged(size: VideoSize) {
                    if(player===p) texture.setVideoSize((size.width*size.pixelWidthHeightRatio).toInt().coerceAtLeast(1),size.height)
                }
            })
            p.setVideoTextureView(texture);p.setVideoEffects(listOf(prepared.effect));p.repeatMode=Player.REPEAT_MODE_ONE
            p.setMediaItem(MediaItem.Builder().setUri(clip.mediaId).setClippingConfiguration(
                MediaItem.ClippingConfiguration.Builder().setStartPositionMs(clip.sourceInMs).setEndPositionMs(clip.sourceOutMs).build()).build())
            p.seekTo(anchor);p.volume=if(userPlaying) 1f else 0f;p.playWhenReady=true;p.prepare()
        } catch(error: Exception) { failed(error) }
    }
    fun togglePlaying() {
        try { player?.let { p ->
            userPlaying=!userPlaying;p.volume=if(userPlaying) 1f else 0f
            if(userPlaying || work.active!=null) p.play() else p.pause()
            if(!userPlaying) anchor=p.currentPosition.coerceIn(0,clip.sourceOutMs-clip.sourceInMs-1)
        } } catch(error: Exception) { failed(error) }
    }
    private fun release() {
        generation++;work.cancel();main.removeCallbacks(pump)
        timeout?.let { main.removeCallbacks(it) };timeout=null
        val old=player;player=null;userPlaying=false;surfaceReady=false;drawn=false
        try { old?.release() } catch(error: Exception) { android.util.Log.e("OwncutInlineGrade","Preview release failed",error) }
    }
    private fun failed(error: Exception) { release();if(!closed) onFailure(error) }
    fun close() { if(closed) return;closed=true;release();work.close();activity.application.unregisterActivityLifecycleCallbacks(this) }
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
