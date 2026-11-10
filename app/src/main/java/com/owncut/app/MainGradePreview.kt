package com.owncut.app

import android.app.Activity
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
import androidx.media3.common.util.UnstableApi
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** Saved-grade viewer adapter. No timeline edit logic; owns only its overlay/decoder.
 * Strict source evidence, no preview-only interpretation, no ungraded success fallback.
 */
@UnstableApi
internal class MainGradePreview(
    private val activity: Activity, private val parent: FrameLayout, val clip: ClipSnapshot,
    position: Long, playing: Boolean, private val onPosition: (Long,Boolean) -> Unit,
    private val onEnded: () -> Unit, private val onFailure: (Exception) -> Unit,
) {
    private val closed=AtomicBoolean(false)
    private val main=Handler(Looper.getMainLooper())
    private val worker=Executors.newSingleThreadExecutor()
    private val layer=FrameLayout(activity).apply { setBackgroundColor(Color.BLACK) }
    private val texture=AspectRatioTextureView(activity).apply { isOpaque=false }
    private val message=TextView(activity).apply {
        setTextColor(Color.WHITE);gravity=Gravity.CENTER;setPadding(16,16,16,16)
        text=AppLanguage.text(R.string.color_frame_loading);setTextIsSelectable(true)
    }
    private var controller: InlineGradePreview?=null
    private var desiredPlaying=playing
    private var anchor=position
    val isPlaying get()=desiredPlaying
    private val tick=object: Runnable {
        override fun run() {
            if(closed.get()) return
            val video=controller ?: return
            anchor=video.sourcePositionMs.coerceIn(clip.sourceInMs,clip.sourceOutMs)
            onPosition(anchor,desiredPlaying)
            if(desiredPlaying) main.postDelayed(this,50)
        }
    }
    init {
        layer.addView(texture,FrameLayout.LayoutParams(-1,-1,Gravity.CENTER))
        layer.addView(message,FrameLayout.LayoutParams(-1,-1))
        parent.addView(layer,FrameLayout.LayoutParams(-1,-1))
        worker.execute {
            try {
                val source=VideoSourceProbe.read(activity.applicationContext,clip.mediaId) {
                    check(!closed.get() && !Thread.currentThread().isInterrupted) { "Saved-grade probe cancelled" }
                }
                main.post {
                    if(!closed.get()) {
                        val video=InlineGradePreview(activity,texture,clip,source,anchor,{ _,_ ->
                            if(!closed.get()) { message.visibility=View.GONE;main.removeCallbacks(tick);main.post(tick) }
                        },{ fail(it) },loop=false,onEnded={
                            if(!closed.get()) { desiredPlaying=false;main.removeCallbacks(tick)
                                anchor=clip.sourceOutMs;onPosition(anchor,false);onEnded() }
                        })
                        controller=video
                        video.setPlaying(desiredPlaying);video.update(clip.grade,false)
                    }
                }
            } catch(error: Exception) { main.post { if(!closed.get()) fail(error) } }
            finally { worker.shutdown() }
        }
    }
    private fun fail(error: Exception) {
        if(closed.get()) return
        desiredPlaying=false;main.removeCallbacks(tick)
        controller?.close();controller=null
        message.text=AppLanguage.text(R.string.color_preview_unavailable,PreviewDiagnostics.report(error))
        message.visibility=View.VISIBLE;onFailure(error)
    }
    fun pause() {
        desiredPlaying=false;controller?.let { video ->
            anchor=video.sourcePositionMs.coerceIn(clip.sourceInMs,clip.sourceOutMs)
            video.setPlaying(false);onPosition(anchor,false)
        }
        main.removeCallbacks(tick)
    }
    fun toggle() {
        desiredPlaying=!desiredPlaying;controller?.setPlaying(desiredPlaying)
        main.removeCallbacks(tick);if(desiredPlaying) main.post(tick)
    }
    fun seek(position: Long, resume: Boolean) {
        anchor=position.coerceIn(clip.sourceInMs,(clip.sourceOutMs-1).coerceAtLeast(clip.sourceInMs))
        desiredPlaying=resume;controller?.seekSource(anchor);controller?.setPlaying(resume)
        onPosition(anchor,resume);main.removeCallbacks(tick);if(resume) main.post(tick)
    }
    fun close() {
        if(!closed.compareAndSet(false,true)) return
        main.removeCallbacks(tick);worker.shutdownNow();controller?.close();controller=null
        parent.removeView(layer)
    }
}
