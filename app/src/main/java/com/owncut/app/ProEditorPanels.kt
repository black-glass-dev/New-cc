package com.owncut.app

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import java.util.Locale
import java.util.UUID
import java.util.concurrent.Executors
import kotlin.math.roundToInt
import kotlin.math.roundToLong

internal object ProEditorPanels {
    private val worker = Executors.newSingleThreadExecutor()

    private fun container(activity: Activity): LinearLayout = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL
        val pad = StudioUi.dp(activity, 18)
        setPadding(pad, StudioUi.dp(activity, 12), pad, StudioUi.dp(activity, 8))
    }

    private fun wrapScroll(activity: Activity, content: LinearLayout): ScrollView = ScrollView(activity).apply {
        addView(content)
    }

    private fun addSeek(
        parent: LinearLayout,
        activity: Activity,
        initialLabel: String,
        max: Int,
        progress: Int,
        labelUpdater: (Int) -> String,
        onChanged: (Int) -> Unit,
    ): TextView {
        val label = StudioUi.label(activity, initialLabel, 13f)
        val bar = SeekBar(activity).apply {
            this.max = max
            this.progress = progress.coerceIn(0, max)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, value: Int, fromUser: Boolean) {
                    label.text = labelUpdater(value)
                    onChanged(value)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }
        parent.addView(label)
        parent.addView(bar)
        return label
    }

    fun showSpeedPanel(
        activity: Activity,
        engine: EngineSession,
        clip: ClipSnapshot,
        playheadMs: Long,
        onRefresh: () -> Unit,
    ) {
        val current = engine.clipFx(clip.id)
        var speed = current.speed
        var reversed = current.reversed
        var freezeMs = current.freezeMs
        val root = container(activity)

        addSeek(
            parent = root,
            activity = activity,
            initialLabel = AppLanguage.text(R.string.pro_speed_value, String.format(Locale.US, "%.2fx", speed)),
            max = 375, // 0.25x .. 4.00x (value + 25) / 100.0
            progress = ((speed * 100.0).roundToInt() - 25).coerceIn(0, 375),
            labelUpdater = { v ->
                val s = (v + 25) / 100.0
                AppLanguage.text(R.string.pro_speed_value, String.format(Locale.US, "%.2fx", s))
            },
        ) { v ->
            speed = (v + 25) / 100.0
        }

        val reverseCheck = CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_reverse_clip)
            isChecked = reversed
            setTextColor(StudioUi.text)
            setOnCheckedChangeListener { _, checked -> reversed = checked }
        }
        root.addView(reverseCheck)

        val clipStartMs = (clip.startFrame * 1000L) / 30L
        val localSourceMs = (clip.sourceInMs + (playheadMs - clipStartMs)).coerceIn(clip.sourceInMs, clip.sourceOutMs)
        val freezeCheck = CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_freeze_frame)
            isChecked = freezeMs >= 0L
            setTextColor(StudioUi.text)
            setOnCheckedChangeListener { _, checked ->
                freezeMs = if (checked) localSourceMs else -1L
            }
        }
        root.addView(freezeCheck)

        AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_speed_title))
            .setView(wrapScroll(activity, root))
            .setPositiveButton(AppLanguage.text(R.string.pro_apply)) { _, _ ->
                engine.setClipFx(clip.id) {
                    it.copy(speed = speed, reversed = reversed, freezeMs = freezeMs)
                }
                Toast.makeText(
                    activity,
                    AppLanguage.text(R.string.pro_speed_applied, String.format(Locale.US, "%.2fx", speed)),
                    Toast.LENGTH_SHORT,
                ).show()
                onRefresh()
            }
            .setNeutralButton(AppLanguage.text(R.string.pro_reset)) { _, _ ->
                engine.setClipFx(clip.id) { it.copy(speed = 1.0, reversed = false, freezeMs = -1L) }
                onRefresh()
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    fun showAudioPanel(
        activity: Activity,
        engine: EngineSession,
        clip: ClipSnapshot,
        onRefresh: () -> Unit,
    ) {
        val current = engine.clipFx(clip.id)
        var volume = current.volume
        var fadeInMs = current.fadeInMs
        var fadeOutMs = current.fadeOutMs
        var muted = current.muted
        var duckingDb = current.duckingDb
        val eqBandsDb = current.eqBandsDb.take(6).toMutableList().apply { while (size < 6) add(0f) }
        var compressorEnabled = current.compressorEnabled
        var compressorThresholdDb = current.compressorThresholdDb
        var compressorRatio = current.compressorRatio
        var limiterEnabled = current.limiterEnabled
        var limiterCeilingDb = current.limiterCeilingDb
        var gateEnabled = current.gateEnabled
        var gateThresholdDb = current.gateThresholdDb
        val root = container(activity)
        root.addView(StudioUi.label(activity, AppLanguage.text(R.string.pro_audio_dsp_export_note), 12f, secondary = true))

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_volume_label, (volume * 100f).roundToInt()),
            200, (volume * 100f).roundToInt(),
            { v -> AppLanguage.text(R.string.pro_volume_label, v) },
        ) { v -> volume = v / 100f }

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_fade_in_label, fadeInMs.toInt()),
            2000, fadeInMs.toInt(),
            { v -> AppLanguage.text(R.string.pro_fade_in_label, v) },
        ) { v -> fadeInMs = v.toLong() }

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_fade_out_label, fadeOutMs.toInt()),
            2000, fadeOutMs.toInt(),
            { v -> AppLanguage.text(R.string.pro_fade_out_label, v) },
        ) { v -> fadeOutMs = v.toLong() }

        root.addView(CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_mute_clip)
            isChecked = muted
            setTextColor(StudioUi.text)
            setOnCheckedChangeListener { _, checked -> muted = checked }
        })

        root.addView(CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_ducking_toggle)
            isChecked = duckingDb < -0.5f
            setTextColor(StudioUi.text)
            setOnCheckedChangeListener { _, checked -> duckingDb = if (checked) -10f else 0f }
        })

        val bandFrequencies = intArrayOf(60, 150, 400, 1000, 4000, 10000)
        bandFrequencies.forEachIndexed { index, frequency ->
            addSeek(
                root, activity,
                AppLanguage.text(R.string.pro_eq_band_label, frequency, eqBandsDb[index].roundToInt()),
                360, (eqBandsDb[index].roundToInt() + 180).coerceIn(0, 360),
                { value -> AppLanguage.text(R.string.pro_eq_band_label, frequency, value - 180) },
            ) { value -> eqBandsDb[index] = (value - 180).toFloat() }
        }
        root.addView(CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_compressor_toggle); isChecked = compressorEnabled
            setTextColor(StudioUi.text); setOnCheckedChangeListener { _, checked -> compressorEnabled = checked }
        })
        addSeek(root, activity, AppLanguage.text(R.string.pro_compressor_threshold, compressorThresholdDb.roundToInt()),
            60, (compressorThresholdDb.roundToInt() + 60).coerceIn(0, 60),
            { value -> AppLanguage.text(R.string.pro_compressor_threshold, value - 60) }) { compressorThresholdDb = (it - 60).toFloat() }
        addSeek(root, activity, AppLanguage.text(R.string.pro_compressor_ratio, compressorRatio.roundToInt()),
            19, (compressorRatio.roundToInt() - 1).coerceIn(0, 19),
            { value -> AppLanguage.text(R.string.pro_compressor_ratio, value + 1) }) { compressorRatio = (it + 1).toFloat() }
        root.addView(CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_limiter_toggle); isChecked = limiterEnabled
            setTextColor(StudioUi.text); setOnCheckedChangeListener { _, checked -> limiterEnabled = checked }
        })
        addSeek(root, activity, AppLanguage.text(R.string.pro_limiter_ceiling, limiterCeilingDb.roundToInt()),
            12, (limiterCeilingDb.roundToInt() + 12).coerceIn(0, 12),
            { value -> AppLanguage.text(R.string.pro_limiter_ceiling, value - 12) }) { limiterCeilingDb = (it - 12).toFloat() }
        root.addView(CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_gate_toggle); isChecked = gateEnabled
            setTextColor(StudioUi.text); setOnCheckedChangeListener { _, checked -> gateEnabled = checked }
        })
        addSeek(root, activity, AppLanguage.text(R.string.pro_gate_threshold, gateThresholdDb.roundToInt()),
            74, (gateThresholdDb.roundToInt() + 80).coerceIn(0, 74),
            { value -> AppLanguage.text(R.string.pro_gate_threshold, value - 80) }) { gateThresholdDb = (it - 80).toFloat() }

        val statsView = StudioUi.label(activity, "", 12f, secondary = true)
        TimelineMediaAnalyzer.cachedAudio(clip)?.let { cached ->
            statsView.text = AppLanguage.text(R.string.pro_audio_stats, cached.integratedLufs, cached.detectedBpm)
        }
        root.addView(statsView)

        AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_audio_title))
            .setView(wrapScroll(activity, root))
            .setPositiveButton(AppLanguage.text(R.string.pro_apply)) { _, _ ->
                engine.setClipFx(clip.id) {
                    it.copy(
                        volume = volume, fadeInMs = fadeInMs, fadeOutMs = fadeOutMs, muted = muted, duckingDb = duckingDb,
                        eqBandsDb = eqBandsDb.toList(), compressorEnabled = compressorEnabled,
                        compressorThresholdDb = compressorThresholdDb, compressorRatio = compressorRatio,
                        limiterEnabled = limiterEnabled, limiterCeilingDb = limiterCeilingDb,
                        gateEnabled = gateEnabled, gateThresholdDb = gateThresholdDb,
                    )
                }
                onRefresh()
            }
            .setNeutralButton(AppLanguage.text(R.string.pro_analyze_waveform)) { _, _ ->
                TimelineMediaAnalyzer.requestAudioAnalysis(activity, clip, engine) { result ->
                    engine.setClipWaveformPeaks(clip.id, result.peaks)
                    if (result.detectedBpm > 20.0 && engine.richState().beatBpm < 1.0) {
                        engine.setBeatGrid(result.detectedBpm, 0L)
                    }
                    Toast.makeText(
                        activity,
                        AppLanguage.text(R.string.pro_audio_stats, result.integratedLufs, result.detectedBpm),
                        Toast.LENGTH_SHORT,
                    ).show()
                    onRefresh()
                }
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    fun showTransformPanel(
        activity: Activity,
        engine: EngineSession,
        clip: ClipSnapshot,
        onRefresh: () -> Unit,
    ) {
        val current = engine.clipFx(clip.id)
        var scale = current.scale
        var panX = current.panX
        var panY = current.panY
        var rotDeg = current.rotationDeg
        var opacity = current.opacity
        var kenBurns = kotlin.math.abs(current.dynamicZoomEnd - current.dynamicZoomStart) > 0.02f
        var chromaEnabled = current.chromaKeyHex.isNotBlank()
        var chromaTol = if (current.chromaTolerance > 0.01f) current.chromaTolerance else 0.28f

        val root = container(activity)
        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_scale_label, (scale * 100f).roundToInt()),
            270, ((scale * 100f).roundToInt() - 30).coerceIn(0, 270),
            { v -> AppLanguage.text(R.string.pro_scale_label, v + 30) },
        ) { v -> scale = (v + 30) / 100f }

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_pan_x_label, (panX * 100f).roundToInt()),
            100, ((panX * 100f).roundToInt() + 50).coerceIn(0, 100),
            { v -> AppLanguage.text(R.string.pro_pan_x_label, v - 50) },
        ) { v -> panX = (v - 50) / 100f }

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_pan_y_label, (panY * 100f).roundToInt()),
            100, ((panY * 100f).roundToInt() + 50).coerceIn(0, 100),
            { v -> AppLanguage.text(R.string.pro_pan_y_label, v - 50) },
        ) { v -> panY = (v - 50) / 100f }

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_rotation_label, rotDeg.roundToInt()),
            360, (rotDeg.roundToInt() + 180).coerceIn(0, 360),
            { v -> AppLanguage.text(R.string.pro_rotation_label, v - 180) },
        ) { v -> rotDeg = (v - 180).toFloat() }

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_opacity_label, (opacity * 100f).roundToInt()),
            100, (opacity * 100f).roundToInt(),
            { v -> AppLanguage.text(R.string.pro_opacity_label, v) },
        ) { v -> opacity = v / 100f }

        root.addView(CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_ken_burns_label)
            isChecked = kenBurns
            setTextColor(StudioUi.text)
            setOnCheckedChangeListener { _, checked -> kenBurns = checked }
        })

        root.addView(CheckBox(activity).apply {
            text = AppLanguage.text(R.string.pro_chroma_key_label)
            isChecked = chromaEnabled
            setTextColor(StudioUi.text)
            setOnCheckedChangeListener { _, checked -> chromaEnabled = checked }
        })

        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_chroma_tol_label, (chromaTol * 100f).roundToInt()),
            70, (chromaTol * 100f).roundToInt().coerceIn(0, 70),
            { v -> AppLanguage.text(R.string.pro_chroma_tol_label, v) },
        ) { v -> chromaTol = (v / 100f).coerceIn(0.02f, 0.7f) }

        AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_transform_title))
            .setView(wrapScroll(activity, root))
            .setPositiveButton(AppLanguage.text(R.string.pro_apply)) { _, _ ->
                engine.setClipFx(clip.id) {
                    it.copy(
                        scale = scale,
                        panX = panX,
                        panY = panY,
                        rotationDeg = rotDeg,
                        opacity = opacity,
                        dynamicZoomStart = 1.0f,
                        dynamicZoomEnd = if (kenBurns) 1.25f else 1.0f,
                        chromaKeyHex = if (chromaEnabled) "#00FF00" else "",
                        chromaTolerance = if (chromaEnabled) chromaTol else 0.0f,
                    )
                }
                onRefresh()
            }
            .setNeutralButton(AppLanguage.text(R.string.pro_reset)) { _, _ ->
                engine.setClipFx(clip.id) {
                    it.copy(
                        scale = 1f, panX = 0f, panY = 0f, rotationDeg = 0f, opacity = 1f,
                        dynamicZoomStart = 1f, dynamicZoomEnd = 1f, chromaKeyHex = "", chromaTolerance = 0f,
                    )
                }
                onRefresh()
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    fun showTextOverlayPanel(
        activity: Activity,
        engine: EngineSession,
        playheadMs: Long,
        onRefresh: () -> Unit,
    ) {
        val root = container(activity)
        val input = EditText(activity).apply {
            hint = AppLanguage.text(R.string.pro_text_hint)
            setTextColor(StudioUi.text)
            setHintTextColor(StudioUi.muted)
        }
        root.addView(input)

        val styles = listOf("title", "pill", "outline")
        var selectedStyleIdx = 0
        val styleLabel = StudioUi.label(activity, AppLanguage.text(R.string.pro_text_style_label, styles[0]), 13f)
        styleLabel.setOnClickListener {
            selectedStyleIdx = (selectedStyleIdx + 1) % styles.size
            styleLabel.text = AppLanguage.text(R.string.pro_text_style_label, styles[selectedStyleIdx])
        }
        root.addView(styleLabel)

        var durMs = 3000L
        addSeek(
            root, activity,
            "${(durMs / 1000L)}s",
            95, 25,
            { v -> "${((v + 5) / 10.0)}s" },
        ) { v -> durMs = (v + 5) * 100L }

        AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_text_title))
            .setView(wrapScroll(activity, root))
            .setPositiveButton(AppLanguage.text(R.string.pro_text_add)) { _, _ ->
                val text = input.text.toString().trim()
                if (text.isNotEmpty()) {
                    engine.upsertOverlay(
                        TextOverlayItem(
                            id = "ov-${UUID.randomUUID().toString().take(8)}",
                            text = text,
                            startMs = playheadMs,
                            durationMs = durMs,
                            style = styles[selectedStyleIdx],
                        )
                    )
                    Toast.makeText(activity, AppLanguage.text(R.string.pro_text_added), Toast.LENGTH_SHORT).show()
                    onRefresh()
                }
            }
            .setNeutralButton(AppLanguage.text(R.string.pro_text_remove_all)) { _, _ ->
                engine.mutateRichState(recordUndo = true) { it.copy(overlays = emptyList()) }
                onRefresh()
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    fun showSubtitlesPanel(
        activity: Activity,
        engine: EngineSession,
        playheadMs: Long,
        onRefresh: () -> Unit,
    ) {
        val current = engine.richState().subtitles
        val root = container(activity)
        val input = EditText(activity).apply {
            hint = AppLanguage.text(R.string.pro_text_hint)
            setTextColor(StudioUi.text)
            setHintTextColor(StudioUi.muted)
            minLines = 3
        }
        root.addView(input)

        val preview = StudioUi.label(
            activity,
            current.joinToString("\n") { "${TimelineGeometry.timecode((it.startMs * 30L) / 1000L)} · ${it.text}" }
                .ifBlank { "00:00:00" },
            12f,
            secondary = true,
        )
        root.addView(preview)

        AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_subtitles_title))
            .setView(wrapScroll(activity, root))
            .setPositiveButton(AppLanguage.text(R.string.pro_subtitle_add_at_playhead)) { _, _ ->
                val body = input.text.toString().trim()
                if (body.isNotEmpty()) {
                    if (body.contains("-->")) {
                        val imported = engine.importSrtSubtitles(body)
                        Toast.makeText(activity, AppLanguage.text(R.string.pro_subtitle_imported, imported), Toast.LENGTH_SHORT).show()
                    } else {
                        engine.upsertSubtitle(
                            SubtitleCueItem(
                                id = "sub-${UUID.randomUUID().toString().take(8)}",
                                startMs = playheadMs,
                                endMs = playheadMs + 2000L,
                                text = body,
                            )
                        )
                    }
                    onRefresh()
                }
            }
            .setNeutralButton(AppLanguage.text(R.string.pro_subtitle_export_srt)) { _, _ ->
                val srt = engine.exportSrtSubtitles()
                val cb = activity.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cb.setPrimaryClip(ClipData.newPlainText("Subtitles SRT", srt))
                Toast.makeText(
                    activity,
                    AppLanguage.text(R.string.pro_subtitle_exported, engine.richState().subtitles.size),
                    Toast.LENGTH_SHORT,
                ).show()
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_subtitle_clear)) { _, _ ->
                engine.mutateRichState(recordUndo = true) { it.copy(subtitles = emptyList()) }
                onRefresh()
            }
            .show()
    }

    fun showTransitionPanel(
        activity: Activity,
        engine: EngineSession,
        clip: ClipSnapshot,
        onRefresh: () -> Unit,
    ) {
        val current = engine.clipFx(clip.id)
        val kinds = arrayOf("none", "dissolve", "fade_black")
        val labels = arrayOf(
            AppLanguage.text(R.string.pro_transition_none),
            AppLanguage.text(R.string.pro_transition_dissolve),
            AppLanguage.text(R.string.pro_transition_fade_black),
        )
        var selectedIdx = kinds.indexOf(current.transitionIn).coerceAtLeast(0)
        var durMs = if (current.transitionMs > 0L) current.transitionMs else 500L

        val root = container(activity)
        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_transition_duration, durMs.toInt()),
            1500, durMs.toInt().coerceIn(100, 1500),
            { v -> AppLanguage.text(R.string.pro_transition_duration, v.coerceAtLeast(100)) },
        ) { v -> durMs = v.coerceAtLeast(100).toLong() }

        AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_transition_title))
            .setSingleChoiceItems(labels, selectedIdx) { _, which -> selectedIdx = which }
            .setView(root)
            .setPositiveButton(AppLanguage.text(R.string.pro_apply)) { _, _ ->
                val kind = kinds[selectedIdx]
                engine.setClipFx(clip.id) {
                    it.copy(
                        transitionIn = kind,
                        transitionMs = if (kind == "none") 0L else durMs,
                    )
                }
                onRefresh()
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    fun showTracksPanel(
        activity: Activity,
        engine: EngineSession,
        onPickMediaForTrack: (String) -> Unit,
        onRefresh: () -> Unit,
    ) {
        val tracks = engine.tracks()
        val trackActions = tracks.flatMap { t ->
            val state = buildString {
                append(" (").append(t.clipCount).append(')')
                if (t.muted) append(" · MUTE")
                if (t.locked) append(" · LOCK")
            }
            listOf(
                "${t.id.uppercase(Locale.US)}$state: ${AppLanguage.text(R.string.pro_track_mute_toggle)}",
                "${t.id.uppercase(Locale.US)}$state: ${AppLanguage.text(R.string.pro_track_lock_toggle)}",
            )
        }

        AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_tracks_title))
            .setItems(
                arrayOf(
                    AppLanguage.text(R.string.pro_add_to_v2),
                    AppLanguage.text(R.string.pro_add_to_a1),
                    AppLanguage.text(R.string.pro_add_to_a2),
                    *trackActions.toTypedArray(),
                ),
            ) { _, which ->
                when (which) {
                    0 -> { engine.ensureTrack("v2", false); onPickMediaForTrack("v2") }
                    1 -> { engine.ensureTrack("a1", true); onPickMediaForTrack("a1") }
                    2 -> { engine.ensureTrack("a2", true); onPickMediaForTrack("a2") }
                    else -> {
                        val idx = which - 3
                        val track = tracks.getOrNull(idx / 2)
                        if (track != null) {
                            if (idx % 2 == 0) engine.setTrackState(track.id, !track.muted, track.locked)
                            else engine.setTrackState(track.id, track.muted, !track.locked)
                            onRefresh()
                        }
                    }
                }
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    fun showBeatAndSceneCutPanel(
        activity: Activity,
        engine: EngineSession,
        selectedClip: ClipSnapshot?,
        onRefresh: () -> Unit,
    ) {
        var bpm = engine.richState().beatBpm
        val root = container(activity)
        addSeek(
            root, activity,
            AppLanguage.text(R.string.pro_beat_bpm_label, bpm.roundToInt()),
            200, bpm.roundToInt().coerceIn(0, 200),
            { v -> AppLanguage.text(R.string.pro_beat_bpm_label, v) },
        ) { v -> bpm = v.toDouble() }

        val dialog = AlertDialog.Builder(activity)
            .setTitle(AppLanguage.text(R.string.pro_beat_cut_title))
            .setView(wrapScroll(activity, root))
            .setPositiveButton(AppLanguage.text(R.string.pro_apply)) { _, _ ->
                engine.setBeatGrid(bpm, 0L)
                onRefresh()
            }
            .setNeutralButton(AppLanguage.text(R.string.pro_detect_bpm)) { _, _ ->
                if (selectedClip == null) {
                    Toast.makeText(activity, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
                } else {
                    TimelineMediaAnalyzer.requestAudioAnalysis(activity, selectedClip, engine) { res ->
                        if (activity.isFinishing || activity.isDestroyed) return@requestAudioAnalysis
                        if (res.detectedBpm > 20.0) engine.setBeatGrid(res.detectedBpm, 0L)
                        engine.setClipWaveformPeaks(selectedClip.id, res.peaks)
                        Toast.makeText(
                            activity,
                            AppLanguage.text(R.string.pro_audio_stats, res.integratedLufs, res.detectedBpm),
                            Toast.LENGTH_SHORT,
                        ).show()
                        onRefresh()
                    }
                }
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_auto_scene_cut)) { _, _ ->
                if (selectedClip == null) {
                    Toast.makeText(activity, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
                } else {
                    val appCtx = activity.applicationContext
                    worker.execute {
                        val cutsMs = TimelineMediaAnalyzer.detectSceneCutsMs(appCtx, selectedClip, engine)
                        activity.runOnUiThread {
                            if (activity.isFinishing || activity.isDestroyed) return@runOnUiThread
                            var splitCount = 0
                            var currentClipId = selectedClip.id
                            var lastCutFrame = selectedClip.startFrame
                            for (cutSourceMs in cutsMs.sorted()) {
                                val cutFrame = selectedClip.startFrame +
                                    (((cutSourceMs - selectedClip.sourceInMs) * 30.0) / 1000.0).roundToLong()
                                if (cutFrame - lastCutFrame < 8L) continue
                                val rightId = "cut-${UUID.randomUUID().toString().take(8)}"
                                runCatching {
                                    engine.splitClip(currentClipId, cutFrame, rightId)
                                    splitCount++
                                    currentClipId = rightId
                                    lastCutFrame = cutFrame
                                }
                            }
                            Toast.makeText(
                                activity,
                                AppLanguage.text(R.string.pro_scene_cuts_found, splitCount),
                                Toast.LENGTH_SHORT,
                            ).show()
                            onRefresh()
                        }
                    }
                }
            }
            .create()
        dialog.show()
    }
}
