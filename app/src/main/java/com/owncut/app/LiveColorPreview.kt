package com.owncut.app

import android.app.Activity
import android.app.AlertDialog
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.*
import androidx.media3.common.AudioAttributes
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.VideoSize
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** Explicit experimental playback of ONE immutable saved/draft clip snapshot. No draft/session writes.
 * Main player stays paused. Close/background/surface teardown belongs to ExoPlayer.
 * Worker preflight never starts a stale player after dialog dismissal.
 */
@UnstableApi
internal object LiveColorPreview {
    fun show(activity: Activity, clip: ClipSnapshot, sourcePositionMs: Long, interpretMissing: Boolean=false): AlertDialog {
        require(clip.sourceInMs>=0 && clip.sourceOutMs>clip.sourceInMs)
        val handler=Handler(Looper.getMainLooper())
        val closed=AtomicBoolean(false)
        val worker=Executors.newSingleThreadExecutor()
        val root=LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL;setPadding(20,16,20,16) }
        val viewport=android.widget.FrameLayout(activity)
        val texture=AspectRatioTextureView(activity)
        viewport.addView(texture,android.widget.FrameLayout.LayoutParams(-1,-1,Gravity.CENTER))
        root.addView(viewport,LinearLayout.LayoutParams(-1,(240*activity.resources.displayMetrics.density).toInt()))
        val status=TextView(activity).apply {
            text=AppLanguage.text(R.string.color_live_loading);maxLines=3
            ellipsize=android.text.TextUtils.TruncateAt.END;textSize=12f;setTextColor(StudioUi.muted)
        }
        root.addView(status)
        var failureDetails=""
        var detailDialog: AlertDialog?=null
        val details=Button(activity).apply {
            text=AppLanguage.text(R.string.color_error_details);isAllCaps=false;visibility=android.view.View.GONE
            setOnClickListener {
                detailDialog?.dismiss()
                detailDialog=AlertDialog.Builder(activity).setTitle(AppLanguage.text(R.string.color_error_details))
                    .setMessage(failureDetails).setPositiveButton(AppLanguage.text(R.string.color_details_close),null).show()
                detailDialog?.findViewById<TextView>(android.R.id.message)?.setTextIsSelectable(true)
            }
        }
        root.addView(details)
        val play=Button(activity).apply { text=AppLanguage.text(R.string.action_pause);isEnabled=false }
        val before=CheckBox(activity).apply { text=AppLanguage.text(R.string.color_live_before);isEnabled=false }
        val position=SeekBar(activity).apply { isEnabled=false;max=1000 }
        root.addView(position);root.addView(play);root.addView(before)
        val dialog=AlertDialog.Builder(activity).setTitle(AppLanguage.text(if(interpretMissing) R.string.color_assumed_frame else R.string.color_live_title)).setView(root)
            .setNegativeButton(AppLanguage.text(R.string.color_cancel),null).create()
        var player: ExoPlayer?=null
        val playback=PreviewPlaybackState()
        var scrubbing=false
        var comparingOriginal=false
        val ticker=object: Runnable {
            override fun run() {
                if(closed.get() || playback.terminal) return
                player?.let { p ->
                    val duration=p.duration
                    if(duration>0 && !scrubbing) position.progress=PreviewProgress.progress(p.currentPosition,duration)
                    play.text=AppLanguage.text(if(p.playWhenReady) R.string.action_pause else R.string.message_b5cdf40ef5)
                }
                handler.postDelayed(this,200)
            }
        }
        fun releasePlayer() {
            val old=player;player=null
            try { old?.release() } catch(error: Exception) {
                android.util.Log.e("OwncutColorPreview","Experimental preview release failed",error)
            }
        }
        fun controls() {
            val p=player
            play.isEnabled=p!=null && playback.canTransport
            before.isEnabled=p!=null && playback.canCompare(p.playWhenReady) && !scrubbing
            position.isEnabled=p!=null && playback.canSeek
        }
        fun fail(error: Exception) {
            if(closed.get() || !playback.fail()) return
            handler.removeCallbacks(ticker);worker.shutdownNow();scrubbing=false
            controls();releasePlayer() // don't retain decoder/GL/audio focus until the user closes
            failureDetails=ColorPreviewFailure.describe(error)
            status.text=AppLanguage.text(R.string.color_live_stopped)
            details.visibility=android.view.View.VISIBLE
        }
        play.setOnClickListener {
            if(playback.canTransport) try { player?.let { it.playWhenReady=!it.playWhenReady } }
            catch(error: Exception) { fail(error) }
        }
        fun seekToProgress() {
            if(!playback.canSeek) return
            try {
                player?.let { p -> if(p.duration>0) p.seekTo(PreviewProgress.seekPosition(p.duration,position.progress)) }
                controls()
            } catch(error: Exception) { fail(error) }
        }
        position.setOnSeekBarChangeListener(object: SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?,progress: Int,fromUser: Boolean) {
                // Keyboard/TalkBack progress changes do not send touch start/stop callbacks.
                if(fromUser && !scrubbing) seekToProgress()
            }
            override fun onStartTrackingTouch(bar: SeekBar?) {
                if(!playback.canSeek) return
                scrubbing=true
                try { player?.pause();controls() } catch(error: Exception) { fail(error) }
            }
            override fun onStopTrackingTouch(bar: SeekBar?) {
                scrubbing=false
                if(!playback.canSeek) { controls();return }
                seekToProgress()
            }
        })
        dialog.setOnDismissListener {
            closed.set(true);playback.close();handler.removeCallbacks(ticker);worker.shutdownNow();scrubbing=false
            detailDialog?.dismiss();detailDialog=null
            releasePlayer()
        }
        dialog.show()
        worker.execute {
            try {
                val plan=ColorRenderPlan.prepare("clip-preview-${clip.id}",listOf(clip),{ check(!closed.get()) }) {
                    VideoSourceProbe.read(activity.applicationContext,it) { check(!closed.get()) }.let { metadata ->
                        metadata.copy(color=PreviewColorPolicy.resolve(metadata.color,interpretMissing))
                    }
                }
                val snapshot=plan.clips.single()
                val prepared=GradedFramePipeline.prepare(snapshot)
                handler.post {
                    if(!closed.get() && !playback.terminal) try {
                        val p=ExoPlayer.Builder(activity.applicationContext,
                            GradedPreviewRenderers(activity.applicationContext,prepared,snapshot.source,interpretMissing)).build()
                        player=p
                        p.setAudioAttributes(AudioAttributes.DEFAULT,true)
                        p.addListener(object: Player.Listener {
                            override fun onPlayerError(error: PlaybackException) { if(!closed.get()) fail(error) }
                            override fun onPlaybackStateChanged(state: Int) {
                                if(closed.get() || playback.terminal) return
                                when(state) {
                                    Player.STATE_READY -> playback.ready()
                                    Player.STATE_BUFFERING -> playback.buffering()
                                    else -> playback.loading()
                                }
                                controls()
                            }
                            override fun onPlayWhenReadyChanged(playWhenReady: Boolean, reason: Int) {
                                if(!closed.get() && !playback.terminal) controls()
                            }
                            override fun onRenderedFirstFrame() { if(!closed.get() && !playback.terminal) status.text=AppLanguage.text(R.string.color_live_notice) }
                            override fun onVideoSizeChanged(size: VideoSize) {
                                if(closed.get() || playback.terminal) return
                                texture.setVideoSize((size.width*size.pixelWidthHeightRatio).toInt().coerceAtLeast(1),size.height)
                            }
                        })
                        p.setVideoTextureView(texture)
                        p.setVideoEffects(listOf(prepared.effect)) // force renderer-owned graph with explicit factory
                        p.repeatMode=Player.REPEAT_MODE_ONE
                        p.setMediaItem(MediaItem.Builder().setUri(clip.mediaId).setClippingConfiguration(
                            MediaItem.ClippingConfiguration.Builder().setStartPositionMs(clip.sourceInMs)
                                .setEndPositionMs(clip.sourceOutMs).build()).build())
                        before.setOnCheckedChangeListener { _,original ->
                            if(playback.terminal || closed.get() || scrubbing || !playback.canCompare(p.playWhenReady)) {
                                if(original!=comparingOriginal) before.isChecked=comparingOriginal
                                return@setOnCheckedChangeListener
                            }
                            try {
                                p.setVideoEffects(if(original) emptyList() else listOf(prepared.effect))
                                comparingOriginal=original
                            }
                            catch(error: Exception) { fail(error) }
                        }
                        p.seekTo(PreviewProgress.clipRelative(sourcePositionMs,clip.sourceInMs,clip.sourceOutMs))
                        p.prepare()
                        if(closed.get() || playback.terminal) return@post
                        p.playWhenReady=true;handler.post(ticker)
                    } catch(error: Exception) { fail(error) }
                }
            } catch(error: Exception) { handler.post { if(!closed.get()) fail(error) } }
        }
        return dialog
    }
}
