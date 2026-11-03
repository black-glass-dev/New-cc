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
    private var livePipeline: GradedFramePipeline.LivePrepared?=null
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
    private var surfaceTimeout: Runnable?=null
    private val pump=Runnable { applyLatest() }
    private var anchor=PreviewProgress.clipRelative(positionMs,clip.sourceInMs,clip.sourceOutMs)
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
        // Do not flush/replay before the first ordinary frame populated the public cache.
        if(player!=null && !surfaceReady) return
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
            val onDraw: () -> Unit = {
                if(acknowledged.compareAndSet(false,true)) main.post {
                    if(!closed && !stopped && epoch==generation && work.finish(token)) {
                        timeout?.let { main.removeCallbacks(it) };timeout=null
                        drawn=true;drawnBefore=selectedBefore
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
                requireNotNull(livePipeline).update(shaders,onDraw)
                if(!userPlaying) {
                    // Public sentinel keeps the stable effect list. The owned cache replays
                    // the last admitted input through the updated native shader recipe.
                    current.setVideoEffects(androidx.media3.common.VideoFrameProcessor.REDRAW)
                }
                return
            }
            val pipeline=GradedFramePipeline.LivePrepared(shaders,onDraw)
            livePipeline=pipeline
            val prepared=pipeline.prepared
            val p=ExoPlayer.Builder(activity.applicationContext,GradedPreviewRenderers(activity.applicationContext,prepared,admitted,interpretMissing)).build()
            player=p;surfaceReady=false;drawn=false
            p.setAudioAttributes(AudioAttributes.DEFAULT,true)
            p.addListener(object: Player.Listener {
                override fun onPlayerError(error: PlaybackException) { if(player===p) failed(error) }
                override fun onRenderedFirstFrame() {
                    if(!closed && player===p) {
                        surfaceReady=true
                        surfaceTimeout?.let { main.removeCallbacks(it) };surfaceTimeout=null
                        if(drawn) {
                            onFrame(drawnBefore,work.pending)
                        }
                        if(work.pending) main.postDelayed(pump,66)
                    }
                }
                override fun onVideoSizeChanged(size: VideoSize) {
                    if(player===p) texture.setVideoSize((size.width*size.pixelWidthHeightRatio).toInt().coerceAtLeast(1),size.height)
                }
            })
            p.setVideoTextureView(texture);p.setVideoEffects(listOf(prepared.effect));p.repeatMode=Player.REPEAT_MODE_ONE
            p.setMediaItem(MediaItem.Builder().setUri(clip.mediaId).setClippingConfiguration(
                MediaItem.ClippingConfiguration.Builder().setStartPositionMs(clip.sourceInMs).setEndPositionMs(clip.sourceOutMs).build()).build())
            val revealDeadline=Runnable {
                if(!closed && epoch==generation && !surfaceReady)
                    failed(IllegalStateException("Timed out waiting for the preview surface first frame; Refresh to retry"))
            }
            surfaceTimeout=revealDeadline;main.postDelayed(revealDeadline,10000)
            p.seekTo(anchor);p.playWhenReady=userPlaying;p.prepare()
        } catch(error: Exception) { failed(error) }
    }
    fun togglePlaying() {
        try { player?.let { p ->
            userPlaying=!userPlaying
            if(userPlaying) p.play() else {
                p.pause()
                if(work.active!=null && surfaceReady) p.setVideoEffects(androidx.media3.common.VideoFrameProcessor.REDRAW)
            }
            if(!userPlaying) anchor=p.currentPosition.coerceIn(0,clip.sourceOutMs-clip.sourceInMs-1)
        } } catch(error: Exception) { failed(error) }
    }
    private fun release() {
        generation++;work.cancel();main.removeCallbacks(pump)
        timeout?.let { main.removeCallbacks(it) };timeout=null
        surfaceTimeout?.let { main.removeCallbacks(it) };surfaceTimeout=null
        val old=player;player=null;livePipeline=null;userPlaying=false;surfaceReady=false;drawn=false
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
