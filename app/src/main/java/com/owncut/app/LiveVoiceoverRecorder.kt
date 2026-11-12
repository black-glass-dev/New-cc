package com.owncut.app

import android.content.Context
import android.media.MediaRecorder
import android.os.Build
import android.os.SystemClock
import java.io.File

/** Platform microphone capture adapter. It only owns a private temporary M4A until stop(). */
internal class LiveVoiceoverRecorder(context: Context) {
    private val appContext = context.applicationContext
    private var recorder: MediaRecorder? = null
    private var startedAtMs = 0L
    private var output: File? = null

    @Synchronized
    fun start(): File {
        check(recorder == null) { "Voiceover is already recording" }
        val dir = File(appContext.filesDir, "voiceovers").apply { mkdirs() }
        val file = File(dir, "voice-${System.currentTimeMillis()}.m4a")
        val mediaRecorder = if (Build.VERSION.SDK_INT >= 31) MediaRecorder(appContext) else MediaRecorder()
        try {
            mediaRecorder.setAudioSource(MediaRecorder.AudioSource.MIC)
            mediaRecorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
            mediaRecorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
            mediaRecorder.setAudioSamplingRate(48_000)
            mediaRecorder.setAudioEncodingBitRate(128_000)
            mediaRecorder.setOutputFile(file.absolutePath)
            mediaRecorder.prepare()
            mediaRecorder.start()
            recorder = mediaRecorder
            output = file
            startedAtMs = SystemClock.elapsedRealtime()
            return file
        } catch (error: Throwable) {
            runCatching { mediaRecorder.reset() }
            runCatching { mediaRecorder.release() }
            file.delete()
            throw error
        }
    }

    @Synchronized
    fun stop(): Pair<File, Long>? {
        val active = recorder ?: return null
        val file = output
        val duration = (SystemClock.elapsedRealtime() - startedAtMs).coerceAtLeast(0L)
        recorder = null
        output = null
        try {
            active.stop()
            active.reset()
            active.release()
        } catch (error: Throwable) {
            runCatching { active.release() }
            file?.delete()
            throw error
        }
        val completed = file?.takeIf { it.isFile && it.length() > 0L } ?: return null
        return completed to duration
    }

    @Synchronized
    fun cancel() {
        val active = recorder ?: return
        val file = output
        recorder = null
        output = null
        runCatching { active.stop() }
        runCatching { active.reset() }
        runCatching { active.release() }
        file?.delete()
    }

    @Synchronized
    fun isRecording(): Boolean = recorder != null
}
