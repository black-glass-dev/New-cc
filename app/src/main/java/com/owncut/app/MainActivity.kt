package com.owncut.app

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.SurfaceTexture
import android.media.MediaMetadataRetriever
import android.media.MediaPlayer
import android.net.Uri
import android.os.Bundle
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.provider.DocumentsContract
import android.view.Gravity
import android.view.MotionEvent
import android.view.Surface
import android.view.TextureView
import android.view.WindowInsets
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import com.owncut.timeline.SnapDecision
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean

@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
class MainActivity : LocalizedActivity(), TextureView.SurfaceTextureListener {
    private var engine: EngineSession? = null
    private var voiceoverRecorder: LiveVoiceoverRecorder? = null
    private var voiceoverButton: Button? = null
    private val voiceoverPermissionRequest = 9031
    @Volatile private var managedStore: ManagedProjectStore? = null
    private var libraryId: String? = null
    private var projectName = AppLanguage.text(R.string.workspace_name)
    private var workspacePage = "Edit"
    private var projectTitle: TextView? = null
    private var projectToolsScroll: HorizontalScrollView? = null
    private var detailsPanel: ScrollView? = null
    private var liveColorDialog: AlertDialog? = null
    private var colorDialog: AlertDialog? = null
    private var mediaSourcesDialog: AlertDialog? = null
    private var checkpointRepairDialog: AlertDialog? = null
    private val workspaceTabs = mutableMapOf<String, Button>()
    private val workspacePanels = mutableMapOf<String, MutableList<android.view.View>>()
    private lateinit var preview: AspectRatioTextureView
    private var overlayView: PreviewOverlayView? = null
    private var pendingImportTrackId: String? = null
    private var gradeViewerParent: FrameLayout?=null
    private var mainGradePreview: MainGradePreview?=null
    // Clip the user explicitly allowed to interpret missing color tags for main-viewer preview only.
    private var mainAssumedClipId: String?=null
    private var previewSurface: Surface? = null
    private lateinit var statusText: TextView
    private lateinit var engineStatusText: TextView
    private lateinit var mediaText: TextView
    private lateinit var timelineContainer: LinearLayout
    private var timelineTrackHeaders: LinearLayout? = null
    private var timelineView: TimelineView? = null
    private var timelineViewport: HorizontalScrollView? = null
    private lateinit var timelinePositionText: TextView
    private lateinit var followPlayheadButton: Button
    private var timelineClips = emptyList<ClipSnapshot>()
    private var timelineFrame = 0L
    private var timelineGapPosition = false
    private var timelineScrubbing = false
    private var followPlayhead = true
    private var restoreTimelineScrollX: Int? = null
    @Volatile private var previewSeeking = false
    @Volatile private var seekSerial = 0
    private lateinit var selectedClipText: TextView
    private var selectedClipId: String? = null
    private var hasUnsavedChanges = false
    private var editGeneration = 0L
    private var projectIoBusy = false
    private var selectedMediaLoading = false
    private var handingOffEngine = false
    @Volatile private var resumeAfterSelection = false
    private lateinit var projectPath: String
    private val mainHandler = Handler(Looper.getMainLooper())
    private val mediaThread = HandlerThread("OwncutMediaPlayer").apply { start() }
    private val mediaHandler = Handler(mediaThread.looper)
    private val metadataExecutor = Executors.newSingleThreadScheduledExecutor()
    private val exportExecutor = Executors.newSingleThreadExecutor()
    private val reframeExecutor = Executors.newSingleThreadExecutor()
    @Volatile private var reframeGeneration = 0
    @Volatile private var exportCancellation: AtomicBoolean? = null
    private lateinit var cancelExportButton: Button
    private var pendingAutosave: ScheduledFuture<*>? = null
    private var metadataRequestSequence = 0
    @Volatile private var playerGeneration = 0
    @Volatile private var lastPreviewSpeed = Float.NaN
    private val playbackTicker = object : Runnable {
        override fun run() {
            if(mainGradePreview!=null) return
            val generation = playerGeneration
            mediaHandler.post {
                val current = player
                val tick = try {
                    if (current != null && !previewSeeking && current.isPlaying) {
                        val position = current.currentPosition
                        val end = previewEndMs.coerceAtMost(current.duration.toLong()).coerceAtLeast(0L).toInt()
                        if (position >= end) {
                            current.pause()
                            current.seekTo(end.toLong(), MediaPlayer.SEEK_CLOSEST)
                            end to true
                        } else position to false
                    } else null
                } catch (_: IllegalStateException) { null }
                if (tick != null) mainHandler.post {
                    if (generation == playerGeneration && player === current && mainGradePreview==null) {
                        if (timelineScrubbing || isSeeking || previewSeeking) return@post
                        val (position, reachedClipEnd) = tick
                        if (!isSeeking && !timelineScrubbing && !previewSeeking) {
                            seekBar.progress = position
                            previewPositionMs = position.toLong()
                            updateTimelinePlayhead(follow = true)
                        }
                        if (reachedClipEnd) {
                            advanceOrStopTimeline(generation, current)
                        } else {
                            if (firstVideoFrameRendered) {
                                statusText.text = AppLanguage.text(R.string.message_2045e197ce, formatTime(position.toLong()), formatTime(durationMs))
                            }
                            mainHandler.postDelayed(this, 50L)
                        }
                    }
                }
            }
        }
    }
    private lateinit var seekBar: SeekBar
    private lateinit var playButton: Button
    private lateinit var relinkButton: Button
    private var selectedUri: Uri? = null
    @Volatile private var player: MediaPlayer? = null
    @Volatile private var playerPrepared = false
    @Volatile private var firstVideoFrameRendered = false
    private var firstFrameDiagnostic: Runnable? = null
    private var surfaceReady = false
    private var durationMs = 0L
    private var previewStartMs = 0L
    private var previewEndMs = 0L
    @Volatile private var previewPositionMs = 0L
    private var isSeeking = false

    private enum class ProjectIoOperation { SAVE, LOAD, RECOVER, EXPORT }

    private data class RetainedEditorState(
        val engine: EngineSession,
        val projectPath: String,
        val managedStore: ManagedProjectStore?,
        val selectedUri: String?,
        val selectedClipId: String?,
        val hasUnsavedChanges: Boolean,
        val editGeneration: Long,
        val previewPositionMs: Long,
        val timelineFrame: Long,
        val timelineGapPosition: Boolean,
        val followPlayhead: Boolean,
        val timelineScrollX: Int,
        val workspacePage: String,
    )

    private var projectIoOperation: ProjectIoOperation? = null

    @Suppress("DEPRECATION")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        try {
            initializeEditor(savedInstanceState)
        } catch (error: Exception) {
            showStartupFailure(error)
        } catch (error: LinkageError) {
            showStartupFailure(error)
        }
    }

    @Suppress("DEPRECATION")
    private fun initializeEditor(savedInstanceState: Bundle?) {
        libraryId = intent.getStringExtra(EXTRA_LIBRARY_ID)
        val retained = lastNonConfigurationInstance as? RetainedEditorState
        if (retained != null) {
            engine = retained.engine
            managedStore = retained.managedStore
            workspacePage = retained.workspacePage
            projectPath = retained.projectPath
            selectedUri = retained.selectedUri?.let { Uri.parse(it) }
            selectedClipId = retained.selectedClipId
            hasUnsavedChanges = retained.hasUnsavedChanges
            editGeneration = retained.editGeneration
            previewPositionMs = retained.previewPositionMs
            timelineFrame = retained.timelineFrame
            timelineGapPosition = retained.timelineGapPosition
            followPlayhead = retained.followPlayhead
            restoreTimelineScrollX = retained.timelineScrollX
            engine?.clips()?.firstOrNull { it.id == selectedClipId }?.let {
                previewStartMs = it.sourceInMs
                previewEndMs = it.sourceOutMs
            }
        } else {
            engine = EngineSession()
            projectPath = libraryId?.let { ProjectLibrary(filesDir).projectFile(it).absolutePath }
                ?: File(filesDir, "active.owncut").absolutePath
        }
        buildWorkspace()
        preview.surfaceTextureListener = this
        if (timelineGapPosition) preview.alpha = 0f
        if (retained != null) {
            selectedUri?.let { readMetadataAndRegister(it, existingClipId = selectedClipId) }
            if (hasUnsavedChanges) scheduleAutosave()
        } else if (libraryId != null) {
            loadLibraryProject()
        } else if (savedInstanceState != null) {
            statusText.text = AppLanguage.text(R.string.message_522d3a1484)
        }
        if (retained != null && libraryId != null) loadLibraryTitle()
        updateEngineStatus()
        refreshTimeline()
    }

    private fun loadLibraryTitle() {
        val id = libraryId ?: return
        metadataExecutor.execute {
            try {
                val name = ProjectLibrary(filesDir).read(id).name
                mainHandler.post { if (!isDestroyed) { projectName = name; projectTitle?.text = "${name} · ${pageLabel(workspacePage)}" } }
            } catch (_: Exception) { /* Native retained session stays usable even if catalog metadata is damaged. */ }
        }
    }

    private fun loadLibraryProject() {
        val id = libraryId ?: return
        val session = engine ?: return
        projectIoBusy = true; projectIoOperation = ProjectIoOperation.LOAD
        statusText.text = AppLanguage.text(R.string.message_d36d2b9e2d)
        metadataExecutor.execute {
            try {
                val library = ProjectLibrary(filesDir)
                val record = library.read(id)
                val store = ManagedProjectStore.acquire(File(projectPath))
                managedStore = store
                store.load(session)
                library.touch(id)
                mainHandler.post {
                    if (!isDestroyed) {
                        projectName = record.name; projectTitle?.text = "${projectName} · ${pageLabel(workspacePage)}"
                        projectIoBusy = false; projectIoOperation = null
                        updateEngineStatus(); showFirstClip(session)
                        statusText.text = if (record.recovery)
                            AppLanguage.text(R.string.message_687e9c4466)
                        else AppLanguage.text(R.string.message_3cbf4f8e3e)
                    }
                }
            } catch (error: Exception) {
                mainHandler.post {
                    if (!isDestroyed) {
                        // Do not allow edits/saves of a blank replacement after a failed load.
                        statusText.text = AppLanguage.text(R.string.message_7496096196, error.message)
                        AlertDialog.Builder(this).setTitle(AppLanguage.text(R.string.message_e73f93c4f2))
                            .setMessage(AppLanguage.text(R.string.message_ca488e2680, error.message))
                            .setCancelable(false).setPositiveButton(AppLanguage.text(R.string.message_53e890d5f0)) { _, _ -> finish() }.show()
                    }
                }
            }
        }
    }

    private fun returnToLibrary() {
        if (projectIoBusy || blockWhileMediaLoads()) return
        val id = libraryId
        if (id == null) { finish(); return }
        val session = engine ?: return
        pendingAutosave?.cancel(false)
        releasePlayer()
        projectIoBusy = true; projectIoOperation = ProjectIoOperation.SAVE
        statusText.text = AppLanguage.text(R.string.message_ddc4d8e4bd)
        val changed = hasUnsavedChanges
        metadataExecutor.execute {
            try {
                if (changed) { if (managedStore != null) managedStore!!.checkpoint(session) else session.saveProject(projectPath); session.clearAutosave(projectPath) }
                ProjectLibrary(filesDir).touch(id)
                managedStore?.close() // release before Home can reopen this project
                mainHandler.post {
                    if (!isDestroyed) {
                        hasUnsavedChanges = false; projectIoBusy = false; projectIoOperation = null
                        finish()
                    }
                }
            } catch (error: Exception) {
                mainHandler.post {
                    if (!isDestroyed) {
                        projectIoBusy = false; projectIoOperation = null
                        statusText.text = AppLanguage.text(R.string.message_b7dd6d731a, error.message)
                    }
                }
            }
        }
    }

    @Deprecated("Platform back adapter")
    override fun onBackPressed() { returnToLibrary() }

    private fun showStartupFailure(error: Throwable) {
        android.util.Log.e("OwncutStartup", "Owncut could not create its initial workspace", error)
        try { engine?.close() } catch (_: Throwable) { }
        engine = null
        if (::preview.isInitialized) preview.surfaceTextureListener = null
        val surfaceToRelease = previewSurface
        previewSurface = null
        if (surfaceToRelease != null) mediaHandler.post { surfaceToRelease.release() }
        metadataExecutor.shutdownNow()
        mediaThread.quitSafely()
        val details = error.stackTraceToString().lineSequence().take(10).joinToString("\n")
        val message = TextView(this).apply {
            text = AppLanguage.text(R.string.message_b266d68863, error.javaClass.simpleName, error.message ?: AppLanguage.text(R.string.message_bcef4ffd8a), details)
            textSize = 15f
            setTextColor(StudioUi.text)
            setTextIsSelectable(true)
            setPadding(dp(20), dp(24), dp(20), dp(24))
        }
        setContentView(ScrollView(this).apply {
            setBackgroundColor(StudioUi.background)
            addView(message)
        })
    }

    @Suppress("DEPRECATION")
    private fun buildWorkspace() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(16), dp(12), dp(16), dp(12))
            setBackgroundColor(StudioUi.background)
        }
        val title = TextView(this).apply {
            text = getString(R.string.app_name)
            textSize = 16f
            setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL))
            setTextColor(StudioUi.text)
            gravity = Gravity.CENTER_VERTICAL
        }
        projectTitle = title
        title.text = "Owncut · ${projectName}"
        val header = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        header.addView(actionButton(AppLanguage.text(R.string.message_e8a5c38200), StudioIcon.HOME) { returnToLibrary() }, LinearLayout.LayoutParams(dp(52), dp(52)))
        header.addView(title, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply { leftMargin = dp(12) })
        root.addView(header, matchWrap())
        val navigation = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for (page in listOf("Edit", "Media", "Color", "Flow", "Deliver")) {
            val tab = actionButton(pageLabel(page), when(page) { "Color" -> StudioIcon.COLOR; "Edit" -> StudioIcon.EDIT; "Media" -> StudioIcon.MEDIA; "Flow" -> StudioIcon.FLOW; else -> StudioIcon.EXPORT }) { showWorkspacePage(page) }
            workspaceTabs[page] = tab
            navigation.addView(tab, LinearLayout.LayoutParams(0, dp(52), 1f).apply { setMargins(dp(3),dp(6),dp(3),dp(6)) })
        }
        root.addView(navigation, matchWrap())

        preview = AspectRatioTextureView(this).apply {
            // TextureView rejects background drawables/colors; keep its empty surface as the placeholder.
            contentDescription = AppLanguage.text(R.string.message_86474f5303)
        }
        val previewParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(240)).apply {
            topMargin = dp(12)
            bottomMargin = dp(12)
        }
        root.addView(preview, previewParams)

        mediaText = TextView(this).apply {
            text = AppLanguage.text(R.string.message_ab6daf2ef3)
            textSize = 14f
            setTextColor(StudioUi.muted)
        }
        root.addView(mediaText, matchWrap())

        selectedClipText = TextView(this).apply {
            text = AppLanguage.text(R.string.message_a89bb287f6)
            textSize = 13f
            setTextColor(StudioUi.text)
            setPadding(0, dp(10), 0, dp(4))
        }
        root.addView(selectedClipText, matchWrap())
        val transportRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        timelinePositionText = TextView(this).apply {
            textSize = 12f; setTextColor(0xFFFF8991.toInt()); gravity = Gravity.CENTER_VERTICAL
            contentDescription = AppLanguage.text(R.string.message_67854d2c0b)
        }
        transportRow.addView(timelinePositionText, LinearLayout.LayoutParams(0, dp(48), 1f))
        followPlayheadButton = actionButton(AppLanguage.text(R.string.message_2086b19f67), StudioIcon.FOLLOW) {
            followPlayhead = !followPlayhead; updateFollowButton()
            if (followPlayhead) revealPlayhead()
        }
        transportRow.addView(followPlayheadButton, wrapButtonParams())
        root.addView(transportRow, matchWrap())
        updateFollowButton()
        val timelineScroll = ScrollView(this).apply {
            isFillViewport = true
            isVerticalScrollBarEnabled = true
        }
        timelineContainer = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = StudioUi.shape(this@MainActivity)
        }
        timelineScroll.addView(timelineContainer, ScrollView.LayoutParams(-1, -2))
        root.addView(timelineScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(164)))

        seekBar = SeekBar(this).apply {
            isEnabled = false
            max = 1
            contentDescription = AppLanguage.text(R.string.message_0e20aaa694)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar, progress: Int, fromUser: Boolean) {
                    if (fromUser && !previewSeeking) {
                        previewPositionMs = progress.toLong().coerceIn(previewStartMs, previewEndMs.coerceAtLeast(previewStartMs))
                        updateTimelinePlayhead()
                        statusText.text = AppLanguage.text(R.string.message_dcc08d209e, formatTime(progress.toLong()), formatTime(durationMs))
                    }
                }
                override fun onStartTrackingTouch(bar: SeekBar) { isSeeking = true }
                override fun onStopTrackingTouch(bar: SeekBar) {
                    isSeeking = false
                    if (playerPrepared && !previewSeeking) seekPreviewTo(bar.progress.toLong(), resumeIfPlaying = true)
                }
            })
        }
        root.addView(seekBar, matchWrap())

        val editHelpText = TextView(this).apply {
            text = AppLanguage.text(R.string.message_85149b860c)
            textSize = 12f
            setTextColor(StudioUi.muted)
            setPadding(0, dp(2), 0, dp(6))
        }
        root.addView(editHelpText, matchWrap())
        workspacePanels.getOrPut("Tools") { mutableListOf() }.add(editHelpText)

        // Primary Edit actions are fixed in two rows; no horizontal swipe hides essential tools.
        val editScroll = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val editControls = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        editControls.addView(actionButton(AppLanguage.text(R.string.message_ef1f98dae3), StudioIcon.SPLIT) { splitAtPlayhead() }, wrapButtonParams())
        editControls.addView(actionButton(AppLanguage.text(R.string.message_2f1b3a4ea9), StudioIcon.TRIM_LEFT) { trimAtPlayhead(leftEdge = true) }, wrapButtonParams())
        editControls.addView(actionButton(AppLanguage.text(R.string.message_0a1e880c02), StudioIcon.TRIM_RIGHT) { trimAtPlayhead(leftEdge = false) }, wrapButtonParams())
        editControls.addView(actionButton(AppLanguage.text(R.string.message_1c025b7a2c), StudioIcon.DELETE) { deleteSelected(ripple = false) }, wrapButtonParams())
        editControls.addView(actionButton(AppLanguage.text(R.string.message_895cdbe3da), StudioIcon.RIPPLE) { deleteSelected(ripple = true) }, wrapButtonParams())
        root.addView(editScroll, matchWrap())
        workspacePanels.getOrPut("Edit") { mutableListOf() }.add(editScroll)

        val projectControls = HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false }
        val projectButtons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        projectButtons.addView(actionButton(AppLanguage.text(R.string.message_39fc721248), StudioIcon.UNDO) { runHistory(undo = true) }, wrapButtonParams())
        projectButtons.addView(actionButton(AppLanguage.text(R.string.message_471b94d402), StudioIcon.REDO) { runHistory(undo = false) }, wrapButtonParams())
        projectButtons.addView(actionButton(AppLanguage.text(R.string.message_efc007a393), StudioIcon.SAVE) { saveProject() }, wrapButtonParams())
        projectButtons.addView(actionButton(AppLanguage.text(R.string.message_1e9ea62c75), StudioIcon.OPEN) { confirmLoadProject() }, wrapButtonParams())
        projectButtons.addView(actionButton(AppLanguage.text(R.string.message_15d58dd451), StudioIcon.SAVE_AS) { launchProjectSavePicker() }, wrapButtonParams())
        if (libraryId == null) projectButtons.addView(actionButton(AppLanguage.text(R.string.message_15341f178d), StudioIcon.OPEN) { launchProjectOpenPicker() }, wrapButtonParams())
        projectButtons.addView(actionButton(AppLanguage.text(R.string.message_a7f8686836), StudioIcon.RECOVER) { confirmRecoverAutosave() }, wrapButtonParams())
        if (libraryId != null) projectButtons.addView(actionButton(AppLanguage.text(R.string.checkpoint_repair), StudioIcon.INFO) { confirmRepairCheckpoint() }, wrapButtonParams())
        val deliverPanel = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        deliverPanel.addView(TextView(this).apply {
            text = AppLanguage.text(R.string.message_fd76f9371a)
            setTextColor(StudioUi.text); textSize = 14f
        })
        deliverPanel.addView(actionButton(AppLanguage.text(R.string.message_323a11800b), StudioIcon.EXPORT) { launchExportPicker() }, matchWrap())
        cancelExportButton = StudioButton(this).apply {
            icon = StudioIcon.CANCEL
            text = AppLanguage.text(R.string.message_90f2b63925)
            isEnabled = false
            setOnClickListener {
                exportCancellation?.set(true)
                text = AppLanguage.text(R.string.message_c5cffea976)
            }
        }
        deliverPanel.addView(cancelExportButton, matchWrap())
        workspacePanels.getOrPut("Deliver") { mutableListOf() }.add(deliverPanel)
        projectControls.addView(projectButtons)
        root.addView(projectControls, matchWrap())
        projectToolsScroll = projectControls
        workspacePanels.getOrPut("Tools") { mutableListOf() }.add(projectControls)

        val controls = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val importButton = StudioButton(this).apply {
            icon = StudioIcon.IMPORT
            text = AppLanguage.text(R.string.message_f04897b60d)
            setOnClickListener { launchVideoPicker() }
        }
        relinkButton = StudioButton(this).apply {
            icon = StudioIcon.LINK
            text = AppLanguage.text(R.string.message_43d6f66acd)
            isEnabled = false
            contentDescription = AppLanguage.text(R.string.message_72c1407ecc)
            setOnClickListener { launchRelinkPicker() }
        }
        playButton = StudioButton(this).apply {
            icon = StudioIcon.PLAY
            text = AppLanguage.text(R.string.message_b5cdf40ef5)
            isEnabled = false
            contentDescription = AppLanguage.text(R.string.message_bf7889b7b2)
            setOnClickListener { togglePlayback() }
        }
        controls.addView(importButton, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        controls.addView(relinkButton, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        controls.addView(actionButton(AppLanguage.text(R.string.media_sources), StudioIcon.MEDIA) { showMediaSources() },
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(playButton, matchWrap())
        root.addView(controls, matchWrap())
        deliverPanel.background = StudioUi.shape(this)
        deliverPanel.setPadding(dp(16),dp(16),dp(16),dp(16))
        root.addView(deliverPanel, matchWrap())
        workspacePanels.getOrPut("Media") { mutableListOf() }.add(controls)
        val colorPanel=LinearLayout(this).apply { orientation=LinearLayout.VERTICAL }
        // Context actions need visible labels; StudioButton intentionally draws icons only.
        fun colorAction(label: String, action: () -> Unit) = Button(this).apply {
            text=label;isAllCaps=false;setTextColor(StudioUi.accent)
            backgroundTintList=null;background=StudioUi.shape(this@MainActivity)
            minHeight=dp(52);setPadding(dp(12),dp(8),dp(12),dp(8))
            setOnClickListener {
                if(projectIoBusy) Toast.makeText(this@MainActivity,AppLanguage.text(R.string.message_ccdca32722),Toast.LENGTH_SHORT).show()
                else action()
            }
        }
        colorPanel.addView(colorAction(AppLanguage.text(R.string.color_editor_title)) { showClipColorEditor() },matchWrap())
        colorPanel.addView(colorAction(AppLanguage.text(R.string.color_live_title)) {
            if(!blockWhileMediaLoads() && liveColorDialog?.isShowing != true) {
                try {
                    val clip=currentSelectedClip() ?: error(AppLanguage.text(R.string.message_f4e00f9522))
                    pauseForTimelineScrub()
                    liveColorDialog=LiveColorPreview.show(this,clip,previewPositionMs)
                } catch(error: Exception) { showError(error) }
            }
        },matchWrap())
        colorPanel.addView(colorAction(AppLanguage.text(R.string.color_module_title)) { showColorModuleSelfTest() },matchWrap())
        colorPanel.addView(TextView(this).apply { text=AppLanguage.text(R.string.color_still_notice);setTextColor(StudioUi.muted) })
        root.addView(colorPanel,matchWrap())
        workspacePanels.getOrPut("Color") { mutableListOf() }.add(colorPanel)
        val flow = TextView(this).apply {
            text = AppLanguage.text(R.string.message_4f099c46c1)
            textSize = 15f; setTextColor(StudioUi.text); setPadding(0, dp(10), 0, dp(10))
        }
        root.addView(flow, matchWrap())
        workspacePanels.getOrPut("Flow") { mutableListOf() }.add(flow)
        showWorkspacePage(workspacePage)

        statusText = EditorStatusView(this).apply {
            onImportant = { message -> if(workspacePage == "Edit" && !isDestroyed) Toast.makeText(this@MainActivity,message,Toast.LENGTH_LONG).show() }
            textSize = 12f
            setTextColor(StudioUi.muted)
            setPadding(0, dp(12), 0, 0)
        }
        root.addView(statusText, matchWrap())
        engineStatusText = TextView(this).apply {
            visibility = android.view.View.GONE // native diagnostics are not a permanent customer-facing toolbar
            textSize = 12f
            setTextColor(StudioUi.muted)
            setPadding(0, dp(6), 0, 0)
        }
        root.addView(engineStatusText, matchWrap())
        root.setOnApplyWindowInsetsListener { view, insets ->
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars())
                val cutout = insets.displayCutout
                val left = maxOf(bars.left, cutout?.safeInsetLeft ?: 0)
                val top = maxOf(bars.top, cutout?.safeInsetTop ?: 0)
                val right = maxOf(bars.right, cutout?.safeInsetRight ?: 0)
                val bottom = maxOf(bars.bottom, cutout?.safeInsetBottom ?: 0)
                view.setPadding(dp(16) + left, dp(12) + top, dp(16) + right, dp(12) + bottom)
            } else {
                view.setPadding(
                    dp(16) + insets.systemWindowInsetLeft,
                    dp(12) + insets.systemWindowInsetTop,
                    dp(16) + insets.systemWindowInsetRight,
                    dp(12) + insets.systemWindowInsetBottom,
                )
            }
            insets
        }
        // Assemble the real editor: fixed chrome, fitted viewer, transport, one timeline,
        // one focused tool row. Secondary information is contextual, never permanent logs.
        root.removeAllViews()
        header.addView(actionButton(AppLanguage.text(R.string.tools), StudioIcon.SETTINGS) {
            showWorkspacePage(if(workspacePage == "Tools") "Edit" else "Tools")
        }, LinearLayout.LayoutParams(dp(52),dp(52)))
        root.addView(header, matchWrap())
        val compactFallback = resources.configuration.screenHeightDp < 560 || resources.configuration.fontScale > 1.35f
        val viewer = FrameLayout(this).apply {
            setBackgroundColor(android.graphics.Color.BLACK)
            addView(preview, FrameLayout.LayoutParams(-1,-1,Gravity.CENTER))
        }
        val ov = PreviewOverlayView(this).apply {
            onOverlayMoved = { ovId, nx, ny ->
                engine?.mutateRichState(recordUndo = true) { doc ->
                    val next = doc.overlays.map { if (it.id == ovId) it.copy(x = nx, y = ny) else it }
                    doc.copy(overlays = next)
                }
                updatePreviewOverlay()
                scheduleAutosave()
            }
        }
        overlayView = ov
        viewer.addView(ov, FrameLayout.LayoutParams(-1,-1))
        gradeViewerParent=viewer
        root.addView(viewer, if(compactFallback) LinearLayout.LayoutParams(-1,dp(180)) else LinearLayout.LayoutParams(-1,0,1f))
        // Move Play to the transport; eliminate full-width Play and duplicate Import bars.
        transportRow.addView(playButton, 0, LinearLayout.LayoutParams(dp(52),dp(52)))
        transportRow.addView(actionButton(AppLanguage.text(R.string.message_f04897b60d), StudioIcon.IMPORT) { launchVideoPicker() }, LinearLayout.LayoutParams(dp(52),dp(52)))
        transportRow.addView(actionButton(AppLanguage.text(R.string.timeline_jog_dial), StudioIcon.JOG_DIAL) {
            showJogDialDialog()
        }, LinearLayout.LayoutParams(dp(52), dp(52)))
        timelinePositionText.typeface = android.graphics.Typeface.MONOSPACE
        timelinePositionText.setTextColor(StudioUi.text)
        root.addView(transportRow,matchWrap())
        val timelineZoomRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END or Gravity.CENTER_VERTICAL
        }
        timelineZoomRow.addView(actionButton("V+", StudioIcon.PLUS) {
            val s = engine ?: return@actionButton
            val existing = s.tracks().map { it.id }.toSet()
            val id = (1..999).asSequence().map { "v$it" }.first { it !in existing }
            s.ensureTrack(id, false); refreshTimeline(); scheduleAutosave()
        }.apply { contentDescription = AppLanguage.text(R.string.timeline_add_video_track); tooltipText = contentDescription }, wrapButtonParams())
        timelineZoomRow.addView(actionButton("A+", StudioIcon.PLUS) {
            val s = engine ?: return@actionButton
            val existing = s.tracks().map { it.id }.toSet()
            val id = (1..999).asSequence().map { "a$it" }.first { it !in existing }
            s.ensureTrack(id, true); refreshTimeline(); scheduleAutosave()
        }.apply { contentDescription = AppLanguage.text(R.string.timeline_add_audio_track); tooltipText = contentDescription }, wrapButtonParams())
        timelineZoomRow.addView(actionButton("−", StudioIcon.ZOOM_OUT) {
            timelineView?.zoomBy(1.0 / 1.25)
        }.apply { contentDescription = AppLanguage.text(R.string.timeline_zoom_out); tooltipText = contentDescription }, wrapButtonParams())
        timelineZoomRow.addView(actionButton("+", StudioIcon.ZOOM_IN) {
            timelineView?.zoomBy(1.25)
        }.apply { contentDescription = AppLanguage.text(R.string.timeline_zoom_in); tooltipText = contentDescription }, wrapButtonParams())
        root.addView(timelineZoomRow, matchWrap())
        root.addView(timelineScroll,LinearLayout.LayoutParams(-1,dp(224)))
        val secondaryTools = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        // Trim and ripple remain accessible in Tools; Split/Delete + history are primary.
        val advanced = listOf(editControls.getChildAt(1),editControls.getChildAt(2),editControls.getChildAt(4))
        for(view in advanced) { editControls.removeView(view); secondaryTools.addView(view,wrapButtonParams()) }
        secondaryTools.addView(actionButton(AppLanguage.text(R.string.action_duplicate), StudioIcon.DUPLICATE) { duplicateSelectedClip() }, wrapButtonParams())
        secondaryTools.addView(actionButton(AppLanguage.text(R.string.action_freeze), StudioIcon.FREEZE) { freezeAtPlayhead() }, wrapButtonParams())
        secondaryTools.addView(actionButton(AppLanguage.text(R.string.action_detach_audio), StudioIcon.AUDIO) {
            val s = engine ?: return@actionButton
            val c = currentSelectedClip() ?: run {
                Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
                return@actionButton
            }
            try {
                val detachedId = s.detachAudio(c.id)
                val trackId = s.clips().firstOrNull { it.id == detachedId }?.trackId ?: "a1"
                refreshTimeline(); scheduleAutosave()
                Toast.makeText(this, AppLanguage.text(R.string.timeline_detached_audio, trackId.uppercase()), Toast.LENGTH_LONG).show()
            } catch (error: Exception) { showError(error) }
        }, wrapButtonParams())
        val secondaryScroll = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val secondaryFirstRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val secondarySecondRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        while (secondaryTools.childCount > 0) {
            val child = secondaryTools.getChildAt(0)
            secondaryTools.removeViewAt(0)
            (if (secondaryFirstRow.childCount < 3) secondaryFirstRow else secondarySecondRow)
                .addView(child, wrapButtonParams())
        }
        secondaryScroll.addView(secondaryFirstRow, matchWrap())
        secondaryScroll.addView(secondarySecondRow, matchWrap())
        val undo = projectButtons.getChildAt(0); val redo = projectButtons.getChildAt(1)
        projectButtons.removeView(undo); projectButtons.removeView(redo)
        editControls.addView(undo,0,wrapButtonParams()); editControls.addView(redo,wrapButtonParams())
        var snapBtn: Button? = null
        snapBtn = actionButton(AppLanguage.text(R.string.action_snap), StudioIcon.MAGNET) {
            val s = engine ?: return@actionButton
            val next = !s.isSnapEnabled()
            s.setSnapEnabled(next)
            snapBtn?.isSelected = next
            refreshTimeline()
            Toast.makeText(this, AppLanguage.text(R.string.pro_snap_toggled, if (next) "ON" else "OFF"), Toast.LENGTH_SHORT).show()
        }
        snapBtn.isSelected = engine?.isSnapEnabled() ?: true
        editControls.addView(snapBtn, wrapButtonParams())
        editControls.addView(actionButton(AppLanguage.text(R.string.action_marker), StudioIcon.MARKER) {
            val s = engine ?: return@actionButton
            val posMs = frameToMs(timelineFrame)
            s.upsertMarker(
                TimelineMarkerItem(
                    id = "mk-${System.nanoTime()}",
                    positionMs = posMs,
                    label = formatTime(posMs),
                )
            )
            refreshTimeline()
            scheduleAutosave()
            Toast.makeText(this, AppLanguage.text(R.string.pro_marker_added, formatTime(posMs)), Toast.LENGTH_SHORT).show()
        }, wrapButtonParams())
        while (editControls.childCount > 0) {
            val row = editScroll.getChildAt(editScroll.childCount - 1) as? LinearLayout
                ?: LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }.also { editScroll.addView(it, matchWrap()) }
            if (row.childCount >= 3) {
                val nextRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                editScroll.addView(nextRow, matchWrap())
            } else {
                val child = editControls.getChildAt(0)
                editControls.removeViewAt(0)
                row.addView(child, wrapButtonParams())
            }
        }
        root.addView(editScroll,matchWrap())
        val proToolsRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_speed), StudioIcon.SPEED) {
            val s = engine ?: return@actionButton
            val c = currentSelectedClip() ?: run { Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show(); return@actionButton }
            ProEditorPanels.showSpeedPanel(this, s, c, frameToMs(timelineFrame)) {
                applyClipPlaybackParams(); refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_audio), StudioIcon.AUDIO) {
            val s = engine ?: return@actionButton
            val c = currentSelectedClip() ?: run { Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show(); return@actionButton }
            ProEditorPanels.showAudioPanel(this, s, c) {
                applyClipPlaybackParams(); refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        voiceoverButton = actionButton(AppLanguage.text(R.string.action_voiceover), StudioIcon.AUDIO) {
            toggleVoiceoverRecording()
        }.also { it.contentDescription = AppLanguage.text(R.string.action_voiceover) }
        proToolsRow.addView(voiceoverButton, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_transform), StudioIcon.TRANSFORM) {
            val s = engine ?: return@actionButton
            val c = currentSelectedClip() ?: run { Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show(); return@actionButton }
            ProEditorPanels.showTransformPanel(this, s, c) {
                updatePreviewOverlay(); refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.reframe_auto_action), StudioIcon.REFRAME) {
            showOfflineReframeOptions()
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.stabilize_action), StudioIcon.STABILIZE) {
            showOfflineStabilizationOptions()
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.timeline_keyframe_toggle), StudioIcon.KEYFRAME) {
            val s = engine ?: return@actionButton
            val clip = currentSelectedClip() ?: run {
                Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
                return@actionButton
            }
            if (clip.trackId.startsWith("a", true)) return@actionButton
            val localMs = frameToMs((timelineFrame - clip.startFrame).coerceIn(0L, clip.durationFrames))
            val currentFx = s.clipFx(clip.id)
            s.mutateRichState(recordUndo = true) { doc ->
                val fx = doc.clipFx[clip.id] ?: currentFx
                val existing = fx.transformKeyframes.firstOrNull { kotlin.math.abs(it.timeMs - localMs) <= 1L }
                val unchanged = existing != null && kotlin.math.abs(existing.scale - fx.scale) < 1e-3f &&
                    kotlin.math.abs(existing.panX - fx.panX) < 1e-3f && kotlin.math.abs(existing.panY - fx.panY) < 1e-3f &&
                    kotlin.math.abs(existing.rotationDeg - fx.rotationDeg) < 1e-2f
                val withoutCurrent = fx.transformKeyframes.filterNot { kotlin.math.abs(it.timeMs - localMs) <= 1L }
                val next = if (unchanged) withoutCurrent else
                    (withoutCurrent + TransformKeyframe(localMs, fx.scale, fx.panX, fx.panY, fx.rotationDeg)).sortedBy { it.timeMs }
                val map = LinkedHashMap(doc.clipFx)
                map[clip.id] = fx.copy(transformKeyframes = next)
                doc.copy(clipFx = map)
            }
            refreshTimeline(); scheduleAutosave()
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.timeline_speed_keyframe_toggle), StudioIcon.SPEED) {
            val s = engine ?: return@actionButton
            val clip = currentSelectedClip() ?: run {
                Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
                return@actionButton
            }
            val localMs = frameToMs((timelineFrame - clip.startFrame).coerceIn(0L, clip.durationFrames))
            val currentFx = s.clipFx(clip.id)
            s.mutateRichState(recordUndo = true) { doc ->
                val fx = doc.clipFx[clip.id] ?: currentFx
                val existing = fx.speedKeyframes.firstOrNull { kotlin.math.abs(it.timeMs - localMs) <= 1L }
                val unchanged = existing != null && kotlin.math.abs(existing.speed - fx.speed.toFloat()) < 1e-3f
                val without = fx.speedKeyframes.filterNot { kotlin.math.abs(it.timeMs - localMs) <= 1L }
                val next = if (unchanged) without else (without + SpeedKeyframe(localMs, fx.speed.toFloat().coerceIn(0.25f, 4f))).sortedBy { it.timeMs }
                val map = LinkedHashMap(doc.clipFx); map[clip.id] = fx.copy(speedKeyframes = next)
                doc.copy(clipFx = map)
            }
            applyClipPlaybackParams(); refreshTimeline(); scheduleAutosave()
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_slip), StudioIcon.TRIM_LEFT) {
            showSlipRollDialog(isRoll = false)
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_roll), StudioIcon.TRIM_RIGHT) {
            showSlipRollDialog(isRoll = true)
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_slide), StudioIcon.TRANSFORM) {
            showSlideDialog()
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_jl_cut), StudioIcon.TRANSITION) {
            val clip = currentSelectedClip() ?: run {
                Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
                return@actionButton
            }
            if (clip.trackId.startsWith("a", true)) {
                Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
                return@actionButton
            }
            val labels = arrayOf("J" to 500, "L" to 500, "J" to 1000, "L" to 1000)
            AlertDialog.Builder(this)
                .setTitle(AppLanguage.text(R.string.action_jl_cut))
                .setItems(labels.map { AppLanguage.text(R.string.jl_cut_option, it.first, it.second) }.toTypedArray()) { _, which ->
                    runCatching { applyJLCut(clip, labels[which].first == "J", labels[which].second.toLong()) }
                        .onFailure { error -> showError(error as? Exception ?: RuntimeException(error)) }
                }
                .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
                .show()
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_text), StudioIcon.TEXT) {
            val s = engine ?: return@actionButton
            ProEditorPanels.showTextOverlayPanel(this, s, frameToMs(timelineFrame)) {
                updatePreviewOverlay(); refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_subtitles), StudioIcon.SUBTITLES) {
            val s = engine ?: return@actionButton
            ProEditorPanels.showSubtitlesPanel(this, s, frameToMs(timelineFrame)) {
                updatePreviewOverlay(); refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_transition), StudioIcon.TRANSITION) {
            val s = engine ?: return@actionButton
            val c = currentSelectedClip() ?: run { Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show(); return@actionButton }
            ProEditorPanels.showTransitionPanel(this, s, c) {
                updatePreviewOverlay(); refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_tracks), StudioIcon.PIP) {
            val s = engine ?: return@actionButton
            ProEditorPanels.showTracksPanel(this, s, { targetTrack ->
                launchMediaPickerForTrack(targetTrack)
            }) {
                refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        proToolsRow.addView(actionButton(AppLanguage.text(R.string.action_beat_cut), StudioIcon.BEAT) {
            val s = engine ?: return@actionButton
            ProEditorPanels.showBeatAndSceneCutPanel(this, s, currentSelectedClip()) {
                refreshTimeline(); scheduleAutosave()
            }
        }, wrapButtonParams())
        // Keep every tool discoverable without requiring a horizontal swipe: two compact rows.
        val proToolsScroll = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val proToolsRows = mutableListOf<LinearLayout>()
        while (proToolsRow.childCount > 0) {
            if (proToolsRows.isEmpty() || proToolsRows.last().childCount >= 4)
                proToolsRows += LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            val child = proToolsRow.getChildAt(0)
            proToolsRow.removeViewAt(0)
            proToolsRows.last().addView(child, wrapButtonParams())
        }
        proToolsRows.forEach { proToolsScroll.addView(it, matchWrap()) }
        val contextBody = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(8),0,dp(8),0) }
        contextBody.addView(mediaText,matchWrap()); contextBody.addView(selectedClipText,matchWrap()); contextBody.addView(seekBar,matchWrap())
        workspacePanels.getOrPut("Media") { mutableListOf() }.addAll(listOf(mediaText,selectedClipText,seekBar))
        contextBody.addView(controls,matchWrap()); contextBody.addView(secondaryScroll,matchWrap())
        contextBody.addView(proToolsScroll,matchWrap())
        workspacePanels.getOrPut("Tools") { mutableListOf() }.add(secondaryScroll)
        workspacePanels.getOrPut("Tools") { mutableListOf() }.add(proToolsScroll)
        contextBody.addView(projectControls,matchWrap()); contextBody.addView(editHelpText,matchWrap())
        // root.removeAllViews detached Color too: reattach it to the final scrollable pane.
        contextBody.addView(colorPanel,matchWrap())
        contextBody.addView(deliverPanel,matchWrap()); contextBody.addView(flow,matchWrap())
        contextBody.addView(statusText,matchWrap()); contextBody.addView(engineStatusText,matchWrap())
        workspacePanels.getOrPut("Tools") { mutableListOf() }.add(statusText)
        val contextScroll = ScrollView(this).apply { addView(contextBody); background = StudioUi.shape(this@MainActivity,StudioUi.surface,8) }
        detailsPanel = contextScroll
        root.addView(contextScroll,LinearLayout.LayoutParams(-1,dp(152)))
        root.addView(navigation,matchWrap())
        showWorkspacePage(workspacePage)
        if (!compactFallback) { setContentView(root); return }
        val workspaceScroll = ScrollView(this).apply {
            isFillViewport = true
            clipToPadding = false
            addView(root, ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ))
        }
        setContentView(workspaceScroll)
    }

    // Referenced timeline sources only: no claim of a persistent unused-asset registry.
    private fun showMediaSources(page: Int = 0) {
        if (isDestroyed || isFinishing) return
        if (projectIoBusy || blockWhileMediaLoads()) return
        try {
            val assets = engine?.mediaAssets().orEmpty()
            val pageSize = 20
            val pageIndex = page.coerceIn(0, ((assets.size - 1).coerceAtLeast(0) / pageSize))
            val visible = assets.drop(pageIndex * pageSize).take(pageSize)
            val dialog = AlertDialog.Builder(this)
                .setTitle(AppLanguage.text(R.string.media_sources_title, assets.size))
                .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138), null)
            if (visible.isEmpty()) dialog.setMessage(AppLanguage.text(R.string.media_sources_empty))
            else {
                val labels = visible.mapIndexed { index, asset ->
                    val label = Uri.parse(asset.mediaId).lastPathSegment.orEmpty().replace('\n',' ').replace('\r',' ').take(64)
                    AppLanguage.text(R.string.media_source_item, pageIndex*pageSize+index+1,
                        label, asset.clipIds.size, formatTime(asset.requiredSourceEndMs))
                }.toTypedArray()
                dialog.setItems(labels) { _, index ->
                    if (!projectIoBusy && !blockWhileMediaLoads()) {
                        try {
                            val source = visible[index]
                            val clip = engine?.clips()?.filter { it.id in source.clipIds }?.minByOrNull { it.startFrame }
                            if (clip != null) selectClip(clip)
                        } catch(error: Exception) { showError(error) }
                    }
                }
                if ((pageIndex+1)*pageSize < assets.size)
                    dialog.setPositiveButton(AppLanguage.text(R.string.media_sources_next)) { _, _ ->
                        mainHandler.post { if(!isDestroyed) showMediaSources(pageIndex+1) }
                    }
                if (pageIndex > 0)
                    dialog.setNeutralButton(AppLanguage.text(R.string.media_sources_previous)) { _, _ ->
                        mainHandler.post { if(!isDestroyed) showMediaSources(pageIndex-1) }
                    }
            }
            val shown=dialog.create()
            mediaSourcesDialog?.dismiss()
            mediaSourcesDialog=shown
            shown.setOnDismissListener { if(mediaSourcesDialog === shown) mediaSourcesDialog=null }
            shown.show()
        } catch(error: Exception) { showError(error) }
    }

    private fun pageLabel(page: String) = AppLanguage.text(when(page) {
        "Color" -> R.string.page_color; "Tools" -> R.string.tools; "Edit" -> R.string.page_edit; "Media" -> R.string.page_media; "Flow" -> R.string.page_flow; else -> R.string.page_deliver
    })

    private fun showWorkspacePage(page: String) {
        workspacePage = page
        detailsPanel?.visibility = if(page == "Edit") android.view.View.GONE else android.view.View.VISIBLE
        workspaceTabs.forEach { (key, button) -> button.isSelected = key == page }
        for ((key, views) in workspacePanels) for (view in views)
            view.visibility = if (key == page) android.view.View.VISIBLE else android.view.View.GONE
        if (::statusText.isInitialized) statusText.visibility = if(page == "Edit" || page == "Flow") android.view.View.GONE else android.view.View.VISIBLE
        projectTitle?.text = "${projectName} · ${pageLabel(page)}"
    }

    private fun actionButton(label: String, icon: StudioIcon = StudioIcon.INFO, action: () -> Unit): Button = StudioButton(this).apply {
        this.icon = icon
        text = label
        setOnClickListener {
            if (projectIoBusy) Toast.makeText(this@MainActivity, AppLanguage.text(R.string.message_ccdca32722), Toast.LENGTH_SHORT).show()
            else action()
        }
    }

    private fun wrapButtonParams() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.WRAP_CONTENT,
        ViewGroup.LayoutParams.WRAP_CONTENT,
    ).apply { rightMargin = dp(4) }

    private fun showColorModuleSelfTest() {
        try {
            val text = ColorModuleDiagnostics.runSelfTest(64, 64)
            AlertDialog.Builder(this)
                .setTitle(AppLanguage.text(R.string.color_module_dialog_title))
                .setMessage(text)
                .setPositiveButton(AppLanguage.text(R.string.color_module_ok), null)
                .show()
        } catch (error: Exception) { showError(error) }
    }

    private fun showClipColorEditor() {
        if(blockWhileMediaLoads() || colorDialog?.isShowing == true) return
        val session=engine ?: return
        try {
            val clip=currentSelectedClip() ?: error(AppLanguage.text(R.string.message_f4e00f9522))
            pauseForTimelineScrub()
            mainGradePreview?.close();mainGradePreview=null
            val colorPosition=previewPositionMs.coerceIn(clip.sourceInMs,
                (clip.sourceOutMs-1).coerceAtLeast(clip.sourceInMs))
            val reference=if(!clip.gradeActive && playerPrepared && !previewSeeking && preview.isAvailable &&
                selectedUri?.toString()==clip.mediaId && previewPositionMs==colorPosition && preview.width>0 && preview.height>0) {
                runCatching {
                    val size=PreviewFrameSize.fit(preview.width,preview.height)
                    preview.getBitmap(size.width,size.height)
                }.getOrNull()
            } else null
            colorDialog=ClipColorEditor.show(this,clip,colorPosition,referenceFrame=reference,
                onClosed={ colorDialog=null;syncMainGradePreview() },previewDraft={ grade ->
                check(engine === session && !projectIoBusy && !isDestroyed) { AppLanguage.text(R.string.message_ccdca32722) }
                check(session.clips().firstOrNull { it.id==clip.id }==clip) { AppLanguage.text(R.string.color_source_changed) }
                liveColorDialog?.dismiss()
                // gradeActive is a saved native predicate, not used by the isolated shader plan.
                LiveColorPreview.show(this,clip.copy(grade=grade),colorPosition).also { liveColorDialog=it }
            }) { grade ->
                check(engine === session && !projectIoBusy && !isDestroyed) { AppLanguage.text(R.string.message_ccdca32722) }
                val current=requireNotNull(session.clips().firstOrNull { it.id==clip.id }) { AppLanguage.text(R.string.color_source_changed) }
                check(current==clip) { AppLanguage.text(R.string.color_source_changed) }
                if(grade != current.grade) {
                    session.setClipGrades(listOf(clip.id),grade)
                    refreshTimeline();scheduleAutosave();engineStatusText.text=session.status()
                    syncMainGradePreview()
                }
            }
        } catch(error: Exception) { showError(error);syncMainGradePreview() }
    }

    private fun refreshTimeline() {
        if (!::timelineContainer.isInitialized || !::selectedClipText.isInitialized) return
        val clips = try { engine?.clips().orEmpty().sortedBy { it.startFrame } } catch (error: Exception) {
            statusText.text = AppLanguage.text(R.string.message_19dafc9927, error.message); return
        }
        timelineClips = clips
        if (selectedClipId != null && clips.none { it.id == selectedClipId }) selectedClipId = null
        if (timelineView == null) {
            val canvas = TimelineView(this).apply {
                minimumWidth = (resources.displayMetrics.widthPixels - dp(152)).coerceAtLeast(dp(160))
                onSeek = { track, frame -> seekTimelineAt(track, frame) }
                onScrub = { frame, commit ->
                    if (!projectIoBusy) {
                        val startingScrub = !timelineScrubbing && !commit
                        timelineScrubbing = !commit
                        timelineFrame = frame; setPlayheadDisplay(frame)
                        if (commit) {
                            val track = timelineClips.firstOrNull { it.id == selectedClipId }?.trackId
                                ?: timelineClips.firstOrNull()?.trackId
                            if (track != null) seekTimelineAt(track, frame)
                        } else if (startingScrub) pauseForTimelineScrub()
                    }
                }
                onScrubCancel = { timelineScrubbing = false; updateTimelinePlayhead() }
                onEvaluateSnap = { clip, proposedFrame, pixelsPerFrame ->
                    val s = engine
                    s?.evaluateTimelineSnap(
                        clip.trackId, clip.id, proposedFrame, clip.durationFrames,
                        timelineFrame, pixelsPerFrame, snapEnabled = s.isSnapEnabled()
                    ) ?: SnapDecision(proposedFrame, false, -1L, false, false)
                }
                onPinchZoom = { scaleFactor, focusXPx ->
                    engine?.pinchTimelineZoom(scaleFactor, focusXPx) ?: 1.0
                }
                onMove = { clip, frame -> moveClipToFreeSlot(clip, clip.trackId, frame) }
                onMoveToTrack = { clip, trackId, frame -> moveClipToFreeSlot(clip, trackId, frame) }
                onTrimClip = { clip, leftEdge, deltaFrames -> trimClipByFrames(clip, leftEdge, deltaFrames) }
                onTwoFingerUndo = { runHistory(undo = true) }
                onThreeFingerRedo = { runHistory(undo = false) }
                onMarkerClick = { mk ->
                    Toast.makeText(this@MainActivity, "${mk.label} (${formatTime(mk.positionMs)})", Toast.LENGTH_SHORT).show()
                }
                onKeyframeClick = { clip, frame ->
                    selectedClipId = clip.id
                    timelineFrame = frame.coerceIn(clip.startFrame, clip.startFrame + clip.durationFrames)
                    refreshTimeline()
                }
                onEditRichBlock = { id, isSubtitle, startMs, endMs ->
                    val session = engine
                    if (session != null) {
                        session.mutateRichState(recordUndo = true) { doc ->
                            if (isSubtitle) doc.copy(subtitles = doc.subtitles.map {
                                if (it.id == id) it.copy(startMs = startMs, endMs = endMs.coerceAtLeast(startMs + 100L)) else it
                            }) else doc.copy(overlays = doc.overlays.map {
                                if (it.id == id) it.copy(startMs = startMs, durationMs = (endMs - startMs).coerceAtLeast(100L)) else it
                            })
                        }
                        scheduleAutosave()
                        refreshTimeline()
                    }
                }
            }
            timelineView = canvas
            timelineTrackHeaders = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setBackgroundColor(StudioUi.background)
            }
            timelineContainer.addView(timelineTrackHeaders, LinearLayout.LayoutParams(dp(120), -2))
            val viewport = object : HorizontalScrollView(this) {
                override fun onInterceptTouchEvent(event: MotionEvent): Boolean {
                    val intercepted = super.onInterceptTouchEvent(event)
                    if (intercepted && event.actionMasked == MotionEvent.ACTION_MOVE) {
                        followPlayhead = false; updateFollowButton()
                    }
                    return intercepted
                }
                override fun performAccessibilityAction(action: Int, arguments: Bundle?): Boolean {
                    if (action == android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ||
                        action == android.view.accessibility.AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD) {
                        followPlayhead = false; updateFollowButton()
                    }
                    return super.performAccessibilityAction(action, arguments)
                }
            }.apply {
                isHorizontalScrollBarEnabled = true
                addView(canvas, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))
            }
            timelineViewport = viewport
            timelineContainer.addView(viewport, LinearLayout.LayoutParams(0, -2, 1f))
        }
        val session = engine
        timelineView?.apply {
            setTimeline(clips, selectedClipId)
            if (session != null) {
                updateRich(session.richState(), session.tracks().map { it.id }, session)
            }
            isEnabled = !projectIoBusy
        }
        if (session != null) refreshTimelineTrackHeaders(session.tracks())
        updatePreviewOverlay()
        val selected = clips.firstOrNull { it.id == selectedClipId }
        if (::relinkButton.isInitialized) relinkButton.isEnabled = selected != null && !projectIoBusy && !selectedMediaLoading
        selectedClipText.text = selected?.let {
            AppLanguage.text(R.string.message_5bd8db9fcd, it.id, formatTime(it.sourceInMs), formatTime(it.sourceOutMs)) +
                if (it.gradeActive) "\n" + AppLanguage.text(R.string.color_saved_grade_viewer) else ""
        } ?: if (clips.isEmpty()) AppLanguage.text(R.string.message_f13c794447) else AppLanguage.text(R.string.message_083723004b, clips.size)
        updateTimelinePlayhead()
        if(playerPrepared) syncMainGradePreview()
        restoreTimelineScrollX?.let { saved ->
            timelineViewport?.post { timelineViewport?.scrollTo(saved, 0) }
            restoreTimelineScrollX = null
        }
    }

    private fun refreshTimelineTrackHeaders(tracks: List<TimelineTrackInfo>) {
        val headers = timelineTrackHeaders ?: return
        headers.removeAllViews()
        val orderedTracks = tracks.sortedWith(compareBy<TimelineTrackInfo>({ if (it.id.startsWith("a", true)) 1 else 0 }, { it.id }))
        // Align lane controls with the canvas ruler while keeping them pinned during horizontal scroll.
        headers.addView(View(this), LinearLayout.LayoutParams(-1, dp(34)))
        orderedTracks.forEach { track ->
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(3), 0, dp(3), 0)
                setBackgroundColor(if (track.locked) 0xFF252B38.toInt() else StudioUi.surface)
            }
            val name = TextView(this).apply {
                text = track.id.uppercase(java.util.Locale.ROOT)
                textSize = 10f
                maxLines = 1
                setTextColor(StudioUi.text)
                gravity = Gravity.CENTER_VERTICAL
            }
            row.addView(name, LinearLayout.LayoutParams(-1, dp(22)))
            val controls = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val mute = actionButton(if (track.muted) "M✓" else "M", if (track.muted) StudioIcon.MUTE else StudioIcon.AUDIO) {
                val current = engine?.tracks()?.firstOrNull { it.id == track.id } ?: return@actionButton
                engine?.setTrackState(track.id, !current.muted, current.locked)
                refreshTimeline(); scheduleAutosave()
            }.apply {
                isSelected = track.muted
                contentDescription = AppLanguage.text(if (track.muted) R.string.timeline_track_unmute else R.string.timeline_track_mute, track.id)
                tooltipText = contentDescription
            }
            val lock = actionButton(if (track.locked) "L✓" else "L", StudioIcon.LOCK) {
                val current = engine?.tracks()?.firstOrNull { it.id == track.id } ?: return@actionButton
                engine?.setTrackState(track.id, current.muted, !current.locked)
                refreshTimeline(); scheduleAutosave()
            }.apply {
                isSelected = track.locked
                contentDescription = AppLanguage.text(if (track.locked) R.string.timeline_track_unlock else R.string.timeline_track_lock, track.id)
                tooltipText = contentDescription
            }
            controls.addView(mute, LinearLayout.LayoutParams(dp(52), dp(48)).apply { rightMargin = dp(2) })
            controls.addView(lock, LinearLayout.LayoutParams(dp(52), dp(48)))
            row.addView(controls, LinearLayout.LayoutParams(-1, dp(48)))
            headers.addView(row, LinearLayout.LayoutParams(-1, dp(72)))
        }
        if (tracks.isEmpty()) headers.addView(View(this), LinearLayout.LayoutParams(-1, dp(72)))
        listOf(R.string.timeline_text_lane, R.string.timeline_subtitle_lane).forEach { labelId ->
            headers.addView(TextView(this).apply {
                text = AppLanguage.text(labelId)
                textSize = 10f
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(6), 0, dp(4), 0)
                setTextColor(StudioUi.muted)
                background = StudioUi.shape(this@MainActivity, StudioUi.surface, 4)
            }, LinearLayout.LayoutParams(-1, dp(34)))
        }
        headers.requestLayout()
    }

    private fun updateFollowButton() {
        if (::followPlayheadButton.isInitialized)
            followPlayheadButton.text = if (followPlayhead) AppLanguage.text(R.string.message_279f0e3cdf) else AppLanguage.text(R.string.message_c3dbf41a1e)
        if (::followPlayheadButton.isInitialized) followPlayheadButton.isSelected = followPlayhead
    }

    private fun setPlayheadDisplay(frame: Long) {
        timelineFrame = frame.coerceIn(0L, timelineView?.endFrame ?: 0L)
        timelineView?.setPlayhead(timelineFrame)
        if (::timelinePositionText.isInitialized)
            timelinePositionText.text = AppLanguage.text(R.string.message_e653e09701, TimelineGeometry.timecode(timelineFrame))
        updatePreviewOverlay()
        updatePreviewTransform()
    }

    private fun updatePreviewTransform() {
        preview.setOutputAspectRatio(engine?.richState()?.outputAspectRatio)
        if (preview.width <= 0 || preview.height <= 0) return
        val clip = timelineClips.firstOrNull { it.id == selectedClipId }
        val fx = if (clip != null && !clip.trackId.startsWith("a", true)) engine?.clipFx(clip.id) else null
        val localMs = clip?.let { frameToMs((timelineFrame - it.startFrame).coerceAtLeast(0L)) } ?: 0L
        val clipDurationMs = clip?.let { frameToMs(it.durationFrames) } ?: 0L
        val sample = if (clip != null && fx != null) fx.transformAt(localMs, clipDurationMs)
            else TransformSample(1f, 0f, 0f, 0f)
        updatePreviewPlaybackSpeed(if (clip != null && fx != null) fx.speedAt(localMs, clipDurationMs) else 1f)
        val cx = preview.width / 2f
        val cy = preview.height / 2f
        val matrix = android.graphics.Matrix().apply {
            setScale(sample.scale, sample.scale, cx, cy)
            postRotate(sample.rotationDeg, cx, cy)
            postTranslate(sample.panX * preview.width / 2f, -sample.panY * preview.height / 2f)
        }
        preview.setTransform(matrix)
    }

    private fun updatePreviewPlaybackSpeed(speed: Float) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M || !speed.isFinite()) return
        val target = speed.coerceIn(0.25f, 4f)
        if (kotlin.math.abs(target - lastPreviewSpeed) < 0.015f) return
        val current = player ?: return
        if (!playerPrepared) return
        val generation = playerGeneration
        mediaHandler.post {
            if (generation == playerGeneration && player === current && playerPrepared) {
                try {
                    val params = current.playbackParams
                    params.speed = target
                    current.playbackParams = params
                    lastPreviewSpeed = target
                } catch (_: IllegalStateException) { }
            }
        }
    }

    private fun updatePreviewOverlay() {
        val ov = overlayView ?: return
        val session = engine ?: return
        val richDoc = session.richState()
        val timeMs = frameToMs(timelineFrame)
        val v1Clip = timelineClips.firstOrNull {
            !it.trackId.startsWith("a", true) &&
                it.trackId == "v1" &&
                timelineFrame >= it.startFrame &&
                timelineFrame < it.startFrame + it.durationFrames
        }
        val v1SourceMs = if (v1Clip != null) {
            v1Clip.sourceInMs + frameToMs(timelineFrame - v1Clip.startFrame)
        } else previewPositionMs
        val v2Clip = timelineClips.firstOrNull {
            it.trackId == "v2" &&
                timelineFrame >= it.startFrame &&
                timelineFrame < it.startFrame + it.durationFrames
        }
        val v2SourceMs = if (v2Clip != null) {
            v2Clip.sourceInMs + frameToMs(timelineFrame - v2Clip.startFrame)
        } else 0L
        ov.updateState(timeMs, richDoc, v1Clip, v1SourceMs, v2Clip, v2SourceMs)
    }

    private fun applyClipPlaybackParams(applySpeedNow: Boolean = false) {
        val session = engine ?: return
        val clip = currentSelectedClip() ?: return
        val fx = session.clipFx(clip.id)
        val tracks = session.tracks().associateBy { it.id }
        val trackMuted = tracks[clip.trackId]?.muted == true
        val effectiveVol = if (fx.muted || trackMuted) 0f else fx.volume.coerceIn(0f, 1f)
        val speed = fx.speed.toFloat().coerceIn(0.25f, 4.0f)
        val current = player ?: return
        val gen = playerGeneration
        mediaHandler.post {
            if (gen == playerGeneration && player === current && playerPrepared) {
                try {
                    current.setVolume(effectiveVol, effectiveVol)
                    if (applySpeedNow && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && kotlin.math.abs(speed - 1.0f) > 0.02f) {
                        val params = current.playbackParams
                        params.speed = speed
                        current.playbackParams = params
                    }
                } catch (_: Throwable) {
                }
            }
        }
    }

    private fun trimClipByFrames(clip: ClipSnapshot, leftEdge: Boolean, deltaFrames: Long) {
        if (projectIoBusy || blockWhileMediaLoads() || deltaFrames == 0L) return
        val session = engine ?: return
        try {
            val deltaMs = frameToMs(kotlin.math.abs(deltaFrames)) * if (deltaFrames < 0L) -1L else 1L
            val boundaryMs = if (leftEdge) {
                (clip.sourceInMs + deltaMs).coerceIn(0L, clip.sourceOutMs - 40L)
            } else {
                (clip.sourceOutMs + deltaMs).coerceAtLeast(clip.sourceInMs + 40L)
            }
            session.trimClip(clip.id, leftEdge, boundaryMs)
            refreshTimeline()
            scheduleAutosave()
            engineStatusText.text = session.status()
            session.clips().firstOrNull { it.id == clip.id }?.let { selectClip(it) }
        } catch (error: Exception) {
            showError(error)
        }
    }

    private fun duplicateSelectedClip() {
        if (blockWhileMediaLoads()) return
        val session = engine ?: return
        try {
            val clip = currentSelectedClip() ?: throw IllegalStateException(AppLanguage.text(R.string.pro_select_clip_first))
            val newId = session.duplicateClip(clip.id)
            selectedClipId = newId
            refreshTimeline()
            scheduleAutosave()
            engineStatusText.text = session.status()
            session.clips().firstOrNull { it.id == newId }?.let { selectClip(it) }
        } catch (error: Exception) {
            showError(error)
        }
    }

    private fun freezeAtPlayhead() {
        if (blockWhileMediaLoads()) return
        val session = engine ?: return
        try {
            val clip = currentSelectedClip() ?: throw IllegalStateException(AppLanguage.text(R.string.pro_select_clip_first))
            val playheadMs = frameToMs(timelineFrame)
            val clipStartMs = frameToMs(clip.startFrame)
            val sourceMs = (clip.sourceInMs + (playheadMs - clipStartMs)).coerceIn(clip.sourceInMs, clip.sourceOutMs)
            session.setClipFx(clip.id) { fx ->
                fx.copy(freezeMs = if (fx.freezeMs >= 0L) -1L else sourceMs)
            }
            refreshTimeline()
            scheduleAutosave()
        } catch (error: Exception) {
            showError(error)
        }
    }

    private fun launchMediaPickerForTrack(trackId: String) {
        if (projectIoBusy) return
        pendingImportTrackId = trackId
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            if (trackId.startsWith("a", ignoreCase = true)) {
                type = "*/*"
                putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("audio/*", "video/*"))
            } else {
                type = "video/*"
            }
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        startActivityForResult(intent, REQUEST_VIDEO)
    }

    private fun updateTimelinePlayhead(follow: Boolean = false) {
        if (timelineScrubbing) return
        val clip = timelineClips.firstOrNull { it.id == selectedClipId }
        val frame = if (clip != null && !timelineGapPosition)
            TimelineGeometry.frameForSource(clip, previewPositionMs) else timelineFrame
        setPlayheadDisplay(frame)
        if (follow && followPlayhead) revealPlayhead()
    }

    private fun revealPlayhead() {
        val view = timelineView ?: return
        val viewport = timelineViewport ?: return
        if (viewport.width <= 0) return
        val x = view.xForFrame(timelineFrame).toInt()
        if (x < viewport.scrollX + dp(12) || x > viewport.scrollX + viewport.width - dp(32))
            viewport.scrollTo((x - viewport.width / 3).coerceAtLeast(0), 0)
    }

    private fun pauseForTimelineScrub() {
        mainGradePreview?.pause()
        mainHandler.removeCallbacks(playbackTicker)
        val current = player
        val generation = playerGeneration
        mediaHandler.post {
            if (current != null && player === current && generation == playerGeneration) {
                try { if (current.isPlaying) current.pause() } catch (_: IllegalStateException) { }
            }
        }
        playButton.text = AppLanguage.text(R.string.message_b5cdf40ef5)
    }

    private fun seekTimelineAt(track: String, frame: Long) {
        if (projectIoBusy || previewSeeking) { blockWhileMediaLoads(); updateTimelinePlayhead(); return }
        try {
            val session = engine ?: return
            val target = session.resolvePlayhead(track, frame)
            timelineScrubbing = false
            followPlayhead = true; updateFollowButton()
            if (target == null) {
                metadataRequestSequence += 1; selectedMediaLoading = false
                selectedClipId = null; selectedUri = null
                previewStartMs = 0L; previewEndMs = 0L; previewPositionMs = 0L
                seekBar.max = 1; seekBar.progress = 0; durationMs = 0L
                releasePlayer(); preview.alpha = 0f; timelineGapPosition = true; timelineFrame = frame
                refreshTimeline()
                revealPlayhead()
                mediaText.text = AppLanguage.text(R.string.message_5fa6a967dd)
                statusText.text = AppLanguage.text(R.string.message_05bda35a9b)
                return
            }
            val clip = session.clips().first { it.id == target.clipId }
            timelineGapPosition = false
            if (clip.id == selectedClipId && playerPrepared && !selectedMediaLoading) {
                seekPreviewTo(target.sourcePositionMs)
            } else selectClip(clip, sourcePositionMs = target.sourcePositionMs)
            setPlayheadDisplay(target.timelineFrame)
            revealPlayhead()
        } catch (error: Exception) { showError(error); updateTimelinePlayhead() }
    }

    private fun seekPreviewTo(sourceMs: Long, resumeIfPlaying: Boolean = false, startAfterSeek: Boolean = false) {
        mainGradePreview?.let { graded ->
            graded.seek(sourceMs,startAfterSeek || (resumeIfPlaying && graded.isPlaying));return
        }
        val current = player ?: return
        if (!playerPrepared || previewSeeking) return
        val target = sourceMs.coerceIn(previewStartMs, (previewEndMs - 1L).coerceAtLeast(previewStartMs))
        val serial = ++seekSerial
        val generation = playerGeneration
        previewSeeking = true; previewPositionMs = target
        seekBar.progress = target.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
        updateTimelinePlayhead(); playButton.isEnabled = false
        mainHandler.removeCallbacks(playbackTicker)
        mediaHandler.post {
            if (player !== current || generation != playerGeneration) return@post
            try {
                val resume = startAfterSeek || (resumeIfPlaying && current.isPlaying)
                if (current.isPlaying) current.pause()
                current.setOnSeekCompleteListener { ready ->
                    if (player === ready && generation == playerGeneration && serial == seekSerial) {
                        ready.setOnSeekCompleteListener(null); previewSeeking = false
                        if (resume) ready.start()
                        mainHandler.post {
                            if (player === ready && generation == playerGeneration && serial == seekSerial) {
                                previewPositionMs = target
                                seekBar.progress = target.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                                playButton.isEnabled = true
                                playButton.text = if (resume) AppLanguage.text(R.string.action_pause) else AppLanguage.text(R.string.message_b5cdf40ef5)
                                updateTimelinePlayhead(follow = resume)
                                if (resume) mainHandler.post(playbackTicker)
                            }
                        }
                    }
                }
                current.seekTo(target, MediaPlayer.SEEK_CLOSEST)
            } catch (error: IllegalStateException) {
                mainHandler.post {
                    if (generation == playerGeneration) {
                        previewSeeking = false; playButton.isEnabled = playerPrepared
                        statusText.text = AppLanguage.text(R.string.message_0336fc9f6e, error.message)
                    }
                }
            }
        }
    }

    private fun moveClipToFreeSlot(clip: ClipSnapshot, trackId: String, proposedStart: Long) {
        if (projectIoBusy || selectedMediaLoading || previewSeeking) return
        val session = engine ?: return
        try {
            val wasSelected = selectedClipId == clip.id
            val others = session.clips().filter { it.trackId == trackId && it.id != clip.id }
            val duration = clip.durationFrames
            fun isFree(start: Long): Boolean = others.none { other ->
                start < other.startFrame + other.durationFrames &&
                    other.startFrame < start + duration
            }
            val slots = buildList {
                add(proposedStart)
                add(0L)
                for (other in others) {
                    add((other.startFrame - duration).coerceAtLeast(0L))
                    add(other.startFrame + other.durationFrames)
                }
            }.distinct().filter(::isFree)
            val destination = slots.minByOrNull { kotlin.math.abs(it - proposedStart) }
            if (destination == clip.startFrame) return
            if (destination == null) {
                statusText.text = AppLanguage.text(R.string.message_cfa6f3d5ab)
                return
            }
            session.moveClip(clip.id, trackId, destination)
            selectedClipId = clip.id
            refreshTimeline()
            if (!wasSelected) session.clips().firstOrNull { it.id == clip.id }?.let { selectClip(it) }
            scheduleAutosave()
            updateEngineStatus()
            statusText.text = if (destination == proposedStart) AppLanguage.text(R.string.message_6b876b131b) else AppLanguage.text(R.string.message_a3ec0cc398)
        } catch (error: Exception) {
            showError(error)
        }
    }

    private fun selectClip(clip: ClipSnapshot, resumePlayback: Boolean = false, sourcePositionMs: Long = clip.sourceInMs) {
        if (projectIoBusy) return
        resumeAfterSelection = resumePlayback
        selectedClipId = clip.id
        releasePlayer()
        val uri = Uri.parse(clip.mediaId)
        selectedUri = uri
        previewStartMs = clip.sourceInMs
        previewEndMs = clip.sourceOutMs
        previewPositionMs = sourcePositionMs.coerceIn(clip.sourceInMs, clip.sourceOutMs - 1L)
        timelineGapPosition = false
        preview.alpha = 1f
        refreshTimeline()
        readMetadataAndRegister(uri, existingClipId = clip.id)
    }

    private fun probeMediaDurationMs(mediaId: String): Long {
        val retriever = MediaMetadataRetriever()
        return try {
            retriever.setDataSource(this, Uri.parse(mediaId))
            retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull()
                ?: error("Could not determine source duration; edit was not applied")
        } finally { runCatching { retriever.release() } }
    }

    private fun showSlipRollDialog(isRoll: Boolean) {
        val session = engine ?: return
        val selected = currentSelectedClip() ?: run {
            Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
            return
        }
        val neighbors = if (isRoll) session.clips().filter { it.trackId == selected.trackId }.let { clips ->
            clips.firstOrNull { it.startFrame == selected.startFrame + selected.durationFrames }
                ?: clips.firstOrNull { it.startFrame + it.durationFrames == selected.startFrame }
        } else null
        if (isRoll && neighbors == null) {
            Toast.makeText(this, AppLanguage.text(R.string.timeline_roll_requires_adjacent), Toast.LENGTH_SHORT).show()
            return
        }
        val offsets = listOf(-500L, -250L, 250L, 500L)
        val choices = offsets.map { value ->
            AppLanguage.text(R.string.timeline_offset_choice,
                AppLanguage.text(if (value < 0L) R.string.timeline_earlier else R.string.timeline_later), kotlin.math.abs(value))
        }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(if (isRoll) R.string.action_roll else R.string.action_slip))
            .setItems(choices) { _, index ->
                val delta = offsets[index]
                try {
                    if (!isRoll) {
                        val sourceDuration = probeMediaDurationMs(selected.mediaId)
                        require(selected.sourceInMs + delta >= 0L && selected.sourceOutMs + delta <= sourceDuration) {
                            "Slip would exceed source media bounds"
                        }
                        session.slipClip(selected.id, delta)
                    } else {
                        val adjacent = neighbors ?: return@setItems
                        val left = if (adjacent.startFrame < selected.startFrame) adjacent else selected
                        val right = if (left.id == selected.id) adjacent else selected
                        val leftDuration = probeMediaDurationMs(left.mediaId)
                        val rightDuration = probeMediaDurationMs(right.mediaId)
                        require(left.sourceOutMs + delta <= leftDuration && left.sourceOutMs + delta > left.sourceInMs &&
                            right.sourceInMs + delta >= 0L && right.sourceInMs + delta < right.sourceOutMs) {
                            "Roll would exceed source media bounds"
                        }
                        session.rollBoundary(left.id, right.id, delta)
                    }
                    refreshTimeline(); scheduleAutosave()
                } catch (error: Exception) { showError(error) }
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    private fun showJogDialDialog() {
        val startFrame = timelineFrame
        val dial = VirtualJogDialView(this)
        dial.onDelta = { delta, commit ->
            val frame = (startFrame + delta).coerceIn(0L, timelineView?.endFrame ?: 0L)
            setPlayheadDisplay(frame)
            if (commit) {
                val track = timelineClips.firstOrNull { it.id == selectedClipId }?.trackId
                    ?: timelineClips.firstOrNull()?.trackId
                if (track != null) seekTimelineAt(track, frame)
            }
        }
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.timeline_jog_dial))
            .setView(dial)
            .setPositiveButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    private fun showSlideDialog() {
        val session = engine ?: return
        val moving = currentSelectedClip() ?: run {
            Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
            return
        }
        val sameTrack = session.clips().filter { it.trackId == moving.trackId }
        val left = sameTrack.firstOrNull { it.startFrame + it.durationFrames == moving.startFrame }
        val right = sameTrack.firstOrNull { it.startFrame == moving.startFrame + moving.durationFrames }
        if (left == null || right == null) {
            Toast.makeText(this, AppLanguage.text(R.string.timeline_roll_requires_adjacent), Toast.LENGTH_SHORT).show()
            return
        }
        val offsets = listOf(-500L, -250L, 250L, 500L)
        val labels = offsets.map { value -> AppLanguage.text(R.string.timeline_offset_choice,
            AppLanguage.text(if (value < 0L) R.string.timeline_earlier else R.string.timeline_later), kotlin.math.abs(value)) }.toTypedArray()
        AlertDialog.Builder(this).setTitle(AppLanguage.text(R.string.action_slide)).setItems(labels) { _, index ->
            try {
                val leftDuration = probeMediaDurationMs(left.mediaId)
                val rightDuration = probeMediaDurationMs(right.mediaId)
                val delta = offsets[index]
                require(left.sourceOutMs + delta <= leftDuration && left.sourceOutMs + delta > left.sourceInMs &&
                    right.sourceInMs + delta >= 0L && right.sourceInMs + delta < right.sourceOutMs) {
                    "Slide would exceed source media bounds"
                }
                session.slideClips(left.id, moving.id, right.id, delta)
                refreshTimeline(); scheduleAutosave()
            } catch (error: Exception) { showError(error) }
        }.setNegativeButton(AppLanguage.text(R.string.pro_close), null).show()
    }

    private fun applyJLCut(videoClip: ClipSnapshot, isJCut: Boolean, offsetMs: Long) {
        val session = engine ?: error("No open project")
        require(offsetMs in 1L..5000L) { "Cut offset must be between 1 and 5000 ms" }
        require(!videoClip.trackId.startsWith("a", true)) { "Select a video clip" }
        val extraFrames = (offsetMs * TimelineGeometry.FPS + 999L) / 1000L
        val videoEnd = videoClip.startFrame + videoClip.durationFrames
        val proposedStart = if (isJCut) (videoClip.startFrame - extraFrames).coerceAtLeast(0L) else videoClip.startFrame
        val proposedEnd = if (isJCut) videoEnd else videoEnd + extraFrames
        var extendedSourceOut = videoClip.sourceOutMs
        if (isJCut) require(videoClip.sourceInMs >= offsetMs) { "The clip has insufficient source preroll for this J-cut" }
        else {
            val retriever = MediaMetadataRetriever()
            val mediaDuration = try {
                retriever.setDataSource(this, Uri.parse(videoClip.mediaId))
                retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull()
            } finally { runCatching { retriever.release() } }
            extendedSourceOut = videoClip.sourceOutMs + offsetMs
            require(mediaDuration != null && extendedSourceOut <= mediaDuration) { "The source has insufficient audio postroll for this L-cut" }
        }
        val detachedId = session.detachAudio(videoClip.id)
        val detached = session.clips().firstOrNull { it.id == detachedId } ?: error("Detached audio clip disappeared")
        val candidateTracks = session.tracks().filter { it.id.startsWith("a", true) }.map { it.id }.toMutableList()
        var target = candidateTracks.firstOrNull { track ->
            session.clips().filter { it.id != detachedId && it.trackId == track }.none { other ->
                proposedStart < other.startFrame + other.durationFrames && other.startFrame < proposedEnd
            }
        }
        if (target == null) {
            var n = 1
            while (session.tracks().any { it.id == "a$n" }) n++
            target = "a$n"
            session.ensureTrack(target, true)
        }
        val targetTrack = target ?: error("No audio track available for the cut")
        if (detached.trackId != targetTrack || detached.startFrame != videoClip.startFrame)
            session.moveClip(detachedId, targetTrack, videoClip.startFrame)
        if (isJCut) session.trimClip(detachedId, true, videoClip.sourceInMs - offsetMs)
        else session.trimClip(detachedId, false, extendedSourceOut)
        refreshTimeline(); scheduleAutosave()
    }

    private data class OfflineFrameSamples(val rgba: ByteArray, val width: Int, val height: Int, val count: Int)

    /** Decode a bounded 8-bit thumbnail sequence; never falls back to full-resolution API-26 frames. */
    private fun sampleOfflineVideoFrames(clip: ClipSnapshot, generation: Int): OfflineFrameSamples {
        require(Build.VERSION.SDK_INT >= 27) { AppLanguage.text(R.string.reframe_requires_api27) }
        val durationMs = (clip.sourceOutMs - clip.sourceInMs).coerceAtLeast(1L)
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(this, Uri.parse(clip.mediaId))
            var sourceWidth = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull()
                ?: error("Video width metadata unavailable")
            var sourceHeight = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull()
                ?: error("Video height metadata unavailable")
            val rotation = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
            if (rotation == 90 || rotation == 270) { val swap = sourceWidth; sourceWidth = sourceHeight; sourceHeight = swap }
            require(sourceWidth > 0 && sourceHeight > 0) { "Invalid source dimensions" }
            val sourceRatio = sourceWidth.toDouble() / sourceHeight.toDouble()
            val pixelBudget = 160 * 90
            val thumbWidth: Int
            val thumbHeight: Int
            if (sourceRatio >= 1.0) {
                thumbWidth = minOf(160, kotlin.math.sqrt(pixelBudget * sourceRatio).toInt()).coerceAtLeast(16)
                thumbHeight = (thumbWidth / sourceRatio).toInt().coerceIn(16, 160)
            } else {
                thumbHeight = minOf(160, kotlin.math.sqrt(pixelBudget / sourceRatio).toInt()).coerceAtLeast(16)
                thumbWidth = (thumbHeight * sourceRatio).toInt().coerceIn(16, 160)
            }
            require(thumbWidth * thumbHeight <= pixelBudget) { "Analysis thumbnail exceeds pixel budget" }
            val count = ((durationMs + 499L) / 500L + 1L).coerceIn(2L, 120L).toInt()
            val packed = ByteArray(count * thumbWidth * thumbHeight * 4)
            val argb = IntArray(thumbWidth * thumbHeight)
            for (index in 0 until count) {
                if (generation != reframeGeneration || isFinishing) throw CancellationException("Analysis cancelled")
                val localMs = (durationMs - 1L) * index / (count - 1)
                val bitmap = retriever.getScaledFrameAtTime(
                    (clip.sourceInMs + localMs) * 1000L,
                    MediaMetadataRetriever.OPTION_CLOSEST,
                    thumbWidth,
                    thumbHeight,
                ) ?: error("Could not decode a bounded video sample")
                try {
                    val thumbnail = if (bitmap.width == thumbWidth && bitmap.height == thumbHeight) bitmap
                        else android.graphics.Bitmap.createScaledBitmap(bitmap, thumbWidth, thumbHeight, true)
                    try {
                        thumbnail.getPixels(argb, 0, thumbWidth, 0, 0, thumbWidth, thumbHeight)
                        val base = index * thumbWidth * thumbHeight * 4
                        for (pixel in argb.indices) {
                            val color = argb[pixel]
                            val at = base + pixel * 4
                            packed[at] = (color shr 16).toByte()
                            packed[at + 1] = (color shr 8).toByte()
                            packed[at + 2] = color.toByte()
                            packed[at + 3] = (color ushr 24).toByte()
                        }
                    } finally { if (thumbnail !== bitmap) thumbnail.recycle() }
                } finally { bitmap.recycle() }
            }
            return OfflineFrameSamples(packed, thumbWidth, thumbHeight, count)
        } finally { retriever.release() }
    }

    private fun showOfflineReframeOptions() {
        val clip = currentSelectedClip() ?: run {
            Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
            return
        }
        if (clip.trackId.startsWith("a", true)) {
            Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
            return
        }
        val aspects = listOf(
            Triple(R.string.reframe_aspect_vertical, 9, 16),
            Triple(R.string.reframe_aspect_square, 1, 1),
            Triple(R.string.reframe_aspect_portrait, 4, 5),
            Triple(R.string.reframe_aspect_landscape, 16, 9),
            Triple(R.string.reframe_aspect_standard, 4, 3),
            Triple(R.string.reframe_aspect_photo, 3, 2),
            Triple(R.string.reframe_aspect_wide, 2, 1),
            Triple(R.string.reframe_aspect_cinema, 239, 100),
            Triple(R.string.reframe_aspect_classic_portrait, 5, 4),
            Triple(R.string.reframe_aspect_tall, 9, 21),
        )
        val labels = (listOf(AppLanguage.text(R.string.reframe_aspect_original)) +
            aspects.map { AppLanguage.text(it.first) } +
            AppLanguage.text(R.string.reframe_custom_resolution)).toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.reframe_choose_title))
            .setMessage(AppLanguage.text(R.string.reframe_guide))
            .setItems(labels) { _, which ->
                when {
                    which == 0 -> resetOfflineReframe(clip)
                    which == aspects.size + 1 -> showCustomOutputResolutionDialog(clip)
                    else -> {
                        val (_, numerator, denominator) = aspects[which - 1]
                        beginOfflineReframe(clip, numerator, denominator)
                    }
                }
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    private fun showOfflineStabilizationOptions() {
        val clip = currentSelectedClip() ?: run {
            Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
            return
        }
        if (clip.trackId.startsWith("a", true)) {
            Toast.makeText(this, AppLanguage.text(R.string.pro_select_clip_first), Toast.LENGTH_SHORT).show()
            return
        }
        val choices = listOf(
            Triple(R.string.stabilize_mode_smooth, 0.55f, false),
            Triple(R.string.stabilize_mode_strong, 0.85f, false),
            Triple(R.string.stabilize_mode_lock, 0.65f, true),
        )
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.stabilize_choose_title))
            .setMessage(AppLanguage.text(R.string.stabilize_guide))
            .setItems(choices.map { AppLanguage.text(it.first) }.toTypedArray()) { _, index ->
                val (_, smoothness, cameraLock) = choices[index]
                beginOfflineStabilization(clip, smoothness, cameraLock)
            }
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .show()
    }

    private fun beginOfflineStabilization(clip: ClipSnapshot, smoothness: Float, cameraLock: Boolean) {
        if (Build.VERSION.SDK_INT < 27) {
            Toast.makeText(this, AppLanguage.text(R.string.reframe_requires_api27), Toast.LENGTH_LONG).show()
            return
        }
        val sessionAtStart = engine ?: return
        val expectedEditGeneration = editGeneration
        val generation = ++reframeGeneration
        statusText.text = AppLanguage.text(R.string.reframe_analyzing)
        reframeExecutor.execute {
            try {
                val frames = sampleOfflineVideoFrames(clip, generation)
                val trajectory = NativeReframe.nativeAnalyzeStabilization(
                    frames.rgba, frames.width, frames.height, frames.count, smoothness, cameraLock, 1.35f,
                )
                require(trajectory.size == frames.count * 4) { "Native stabilizer returned incomplete samples" }
                if (generation != reframeGeneration || isFinishing) return@execute
                mainHandler.post {
                    if (generation != reframeGeneration || isFinishing || engine !== sessionAtStart ||
                        editGeneration != expectedEditGeneration || currentSelectedClip()?.id != clip.id) return@post
                    val duration = (clip.sourceOutMs - clip.sourceInMs).coerceAtLeast(1L)
                    val keys = (0 until frames.count).map { index ->
                        val at = index * 4
                        TransformKeyframe(
                            timeMs = (duration - 1L) * index / (frames.count - 1),
                            scale = trajectory[at + 3].coerceIn(1f, 1.5f),
                            panX = trajectory[at].coerceIn(-0.5f, 0.5f),
                            panY = trajectory[at + 1].coerceIn(-0.5f, 0.5f),
                            rotationDeg = (-Math.toDegrees(trajectory[at + 2].toDouble())).toFloat().coerceIn(-30f, 30f),
                        )
                    }
                    sessionAtStart.mutateRichState(recordUndo = true) { doc ->
                        val map = LinkedHashMap(doc.clipFx)
                        map[clip.id] = doc.fxFor(clip.id).copy(
                            scale = 1f, panX = 0f, panY = 0f, rotationDeg = 0f,
                            dynamicZoomStart = 1f, dynamicZoomEnd = 1f,
                            transformKeyframes = keys,
                        )
                        doc.copy(clipFx = map)
                    }
                    updatePreviewOverlay(); updatePreviewTransform(); refreshTimeline(); scheduleAutosave()
                    statusText.text = AppLanguage.text(R.string.stabilize_applied)
                    Toast.makeText(this, AppLanguage.text(R.string.stabilize_applied), Toast.LENGTH_LONG).show()
                }
            } catch (_: CancellationException) {
                // A newer analysis or Activity destruction invalidated this bounded worker.
            } catch (error: Exception) {
                if (generation == reframeGeneration && !isFinishing) mainHandler.post {
                    if (generation == reframeGeneration) {
                        statusText.text = AppLanguage.text(R.string.reframe_failed)
                        showError(error)
                    }
                }
            }
        }
    }

    private fun resetOfflineReframe(clip: ClipSnapshot) {
        val session = engine ?: return
        session.mutateRichState(recordUndo = true) { doc ->
            val current = doc.fxFor(clip.id)
            val map = LinkedHashMap(doc.clipFx)
            map[clip.id] = current.copy(
                scale = 1f, panX = 0f, panY = 0f, rotationDeg = 0f,
                dynamicZoomStart = 1f, dynamicZoomEnd = 1f, transformKeyframes = emptyList(),
            )
            doc.copy(clipFx = map, outputAspectRatio = null, outputWidth = null, outputHeight = null)
        }
        preview.setOutputAspectRatio(null)
        updatePreviewOverlay(); updatePreviewTransform(); refreshTimeline(); scheduleAutosave()
    }

    private fun showCustomOutputResolutionDialog(clip: ClipSnapshot) {
        val fields = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(20), dp(12), dp(20), dp(8))
        }
        val widthField = android.widget.EditText(this).apply {
            hint = AppLanguage.text(R.string.reframe_width_hint)
            inputType = android.text.InputType.TYPE_CLASS_NUMBER
            setText("1080")
            contentDescription = hint
        }
        val heightField = android.widget.EditText(this).apply {
            hint = AppLanguage.text(R.string.reframe_height_hint)
            inputType = android.text.InputType.TYPE_CLASS_NUMBER
            setText("1920")
            contentDescription = hint
        }
        fields.addView(widthField, LinearLayout.LayoutParams(0, dp(56), 1f))
        fields.addView(heightField, LinearLayout.LayoutParams(0, dp(56), 1f).apply { marginStart = dp(8) })
        val dialog = AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.reframe_custom_title))
            .setMessage(AppLanguage.text(R.string.reframe_custom_invalid))
            .setView(fields)
            .setPositiveButton(AppLanguage.text(R.string.pro_apply), null)
            .setNegativeButton(AppLanguage.text(R.string.pro_close), null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val width = widthField.text.toString().toIntOrNull()
                val height = heightField.text.toString().toIntOrNull()
                val valid = width != null && height != null &&
                    width in 144..3840 && height in 144..3840 && width % 2 == 0 && height % 2 == 0 &&
                    width.toLong() * height <= 8_294_400L && width.toFloat() / height in 0.3f..3.2f
                if (!valid) {
                    widthField.error = AppLanguage.text(R.string.reframe_custom_invalid)
                    heightField.error = AppLanguage.text(R.string.reframe_custom_invalid)
                    return@setOnClickListener
                }
                dialog.dismiss()
                val w = width!!
                val h = height!!
                val divisor = gcdPositive(w, h)
                beginOfflineReframe(clip, w / divisor, h / divisor, w, h)
            }
        }
        dialog.show()
    }

    private fun gcdPositive(a: Int, b: Int): Int {
        var x = a
        var y = b
        while (y != 0) { val remainder = x % y; x = y; y = remainder }
        return x.coerceAtLeast(1)
    }

    private fun beginOfflineReframe(
        clip: ClipSnapshot,
        aspectNum: Int,
        aspectDen: Int,
        outputWidth: Int? = null,
        outputHeight: Int? = null,
    ) {
        if (Build.VERSION.SDK_INT < 27) {
            Toast.makeText(this, AppLanguage.text(R.string.reframe_requires_api27), Toast.LENGTH_LONG).show()
            return
        }
        val sessionAtStart = engine ?: return
        val expectedEditGeneration = editGeneration
        val generation = ++reframeGeneration
        statusText.text = AppLanguage.text(R.string.reframe_analyzing)
        reframeExecutor.execute {
            try {
                val frames = sampleOfflineVideoFrames(clip, generation)
                val samples = NativeReframe.nativeAnalyzeRgba(
                    frames.rgba, frames.width, frames.height, frames.count, aspectNum, aspectDen, 0.82f,
                )
                require(samples.size == frames.count * 4) { "Native reframe returned incomplete samples" }
                if (generation != reframeGeneration || isFinishing) return@execute
                mainHandler.post {
                    if (generation != reframeGeneration || isFinishing || engine !== sessionAtStart ||
                        editGeneration != expectedEditGeneration || currentSelectedClip()?.id != clip.id) return@post
                    val session = sessionAtStart
                    val localDuration = (clip.sourceOutMs - clip.sourceInMs).coerceAtLeast(1L)
                    val keys = (0 until frames.count).map { index ->
                        val at = index * 4
                        val time = (localDuration - 1L) * index / (frames.count - 1)
                        TransformKeyframe(
                            timeMs = time,
                            scale = 1f,
                            panX = (samples[at] - 0.5f).coerceIn(-0.5f, 0.5f),
                            panY = (0.5f - samples[at + 1]).coerceIn(-0.5f, 0.5f),
                            rotationDeg = 0f,
                        )
                    }
                    session.mutateRichState(recordUndo = true) { doc ->
                        val current = doc.fxFor(clip.id)
                        val map = LinkedHashMap(doc.clipFx)
                        map[clip.id] = current.copy(
                            scale = 1f, panX = 0f, panY = 0f, rotationDeg = 0f,
                            dynamicZoomStart = 1f, dynamicZoomEnd = 1f,
                            transformKeyframes = keys,
                        )
                        doc.copy(
                            clipFx = map,
                            outputAspectRatio = aspectNum.toFloat() / aspectDen,
                            outputWidth = outputWidth,
                            outputHeight = outputHeight,
                        )
                    }
                    preview.setOutputAspectRatio(session.richState().outputAspectRatio)
                    updatePreviewOverlay(); updatePreviewTransform(); refreshTimeline(); scheduleAutosave()
                    statusText.text = AppLanguage.text(R.string.reframe_applied)
                    Toast.makeText(this, AppLanguage.text(R.string.reframe_applied), Toast.LENGTH_LONG).show()
                }
            } catch (_: CancellationException) {
                // A later request or Activity destruction invalidated this worker.
            } catch (error: Exception) {
                if (generation == reframeGeneration && !isFinishing) mainHandler.post {
                    if (generation == reframeGeneration) {
                        statusText.text = AppLanguage.text(R.string.reframe_failed)
                        showError(error)
                    }
                }
            }
        }
    }

    private fun toggleVoiceoverRecording() {
        val active = voiceoverRecorder
        if (active?.isRecording() == true) {
            finishVoiceoverRecording()
            return
        }
        if (checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(android.Manifest.permission.RECORD_AUDIO), voiceoverPermissionRequest)
            return
        }
        startVoiceoverRecording()
    }

    private fun startVoiceoverRecording() {
        try {
            val recorder = LiveVoiceoverRecorder(this)
            recorder.start()
            voiceoverRecorder = recorder
            voiceoverButton?.contentDescription = AppLanguage.text(R.string.voiceover_recording)
            statusText.text = AppLanguage.text(R.string.voiceover_recording)
        } catch (error: Exception) {
            voiceoverRecorder = null
            showError(error)
        }
    }

    /** Keep recorded media with its owning project instead of leaving a project clip pointing
     * into the recorder's temporary capture directory. Import/export-as-copy remains separate.
     */
    private fun storeVoiceoverBesideProject(source: File): File {
        val projectDirectory = File(projectPath).absoluteFile.parentFile
            ?: throw IllegalStateException("Project has no parent directory")
        val mediaDirectory = File(projectDirectory, "media/voiceovers")
        check(mediaDirectory.isDirectory || mediaDirectory.mkdirs()) { "Cannot create project voiceover directory" }
        val destination = File(mediaDirectory, "voice-${java.util.UUID.randomUUID()}.m4a")
        val staging = File(mediaDirectory, ".${destination.name}.partial")
        try {
            source.inputStream().use { input ->
                java.io.FileOutputStream(staging).use { output ->
                    input.copyTo(output)
                    output.fd.sync()
                }
            }
            check(staging.isFile && staging.length() > 0L && staging.renameTo(destination)) {
                "Cannot publish voiceover into project media"
            }
            source.delete()
            return destination
        } catch (error: Exception) {
            staging.delete()
            destination.delete()
            throw error
        }
    }

    private fun finishVoiceoverRecording() {
        val recorder = voiceoverRecorder ?: return
        voiceoverRecorder = null
        voiceoverButton?.contentDescription = AppLanguage.text(R.string.action_voiceover)
        try {
            val result = recorder.stop() ?: return
            val (temporaryFile, elapsedMs) = result
            val voiceoverFile = storeVoiceoverBesideProject(temporaryFile)
            val metadata = MediaMetadataRetriever()
            val measuredDuration = try {
                metadata.setDataSource(voiceoverFile.absolutePath)
                metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull()
            } catch (_: RuntimeException) { null } finally { runCatching { metadata.release() } }
            val durationMs = (measuredDuration ?: elapsedMs).coerceAtLeast(250L)
            val session = engine ?: throw IllegalStateException("No open project to receive the voiceover")
            val all = session.clips()
            val durationFrames = (durationMs * TimelineGeometry.FPS + 999L) / 1000L
            val tracks = session.tracks().filter { it.id.startsWith("a", true) }
            val targetTrack = tracks.firstOrNull { track ->
                all.none { clip -> clip.trackId == track.id && timelineFrame < clip.startFrame + clip.durationFrames &&
                    clip.startFrame < timelineFrame + durationFrames }
            }?.id ?: run {
                var n = 1
                while (session.tracks().any { it.id == "a$n" }) n++
                "a$n"
            }
            val id = session.addMediaClipToTrack(targetTrack, Uri.fromFile(voiceoverFile).toString(), durationMs, timelineFrame)
            selectedClipId = id
            refreshTimeline(); scheduleAutosave()
            Toast.makeText(this, AppLanguage.text(R.string.voiceover_saved, targetTrack.uppercase()), Toast.LENGTH_LONG).show()
        } catch (error: Exception) {
            showError(error)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == voiceoverPermissionRequest) {
            val granted = grantResults.firstOrNull() == android.content.pm.PackageManager.PERMISSION_GRANTED
            if (granted) startVoiceoverRecording()
            else Toast.makeText(this, AppLanguage.text(R.string.voiceover_permission), Toast.LENGTH_LONG).show()
        }
    }

    override fun onPause() {
        if (voiceoverRecorder?.isRecording() == true) finishVoiceoverRecording()
        super.onPause()
    }

    private fun currentSelectedClip(): ClipSnapshot? = engine?.clips()?.firstOrNull { it.id == selectedClipId }

    private fun advanceOrStopTimeline(expectedGeneration: Int, completedPlayer: MediaPlayer?) {
        if (expectedGeneration != playerGeneration || player !== completedPlayer) return
        val finishedClip = currentSelectedClip()
        val timelineEnd = finishedClip?.let { it.startFrame + it.durationFrames }
        val nextClip = if (finishedClip != null && timelineEnd != null) {
            engine?.clips()?.firstOrNull {
                it.trackId == finishedClip.trackId && it.startFrame == timelineEnd
            }
        } else null
        if (nextClip != null) {
            statusText.text = AppLanguage.text(R.string.message_9a98a6e051)
            selectClip(nextClip, resumePlayback = true)
        } else {
            mainHandler.removeCallbacks(playbackTicker)
            seekBar.progress = previewEndMs.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
            previewPositionMs = previewEndMs
            updateTimelinePlayhead(follow = true)
            playButton.text = AppLanguage.text(R.string.message_b5cdf40ef5)
            statusText.text = AppLanguage.text(R.string.message_d579843004)
        }
    }

    private fun showFirstClip(session: EngineSession) {
        val first = session.clips().sortedBy { it.startFrame }.firstOrNull()
        selectedClipId = first?.id
        refreshTimeline()
        if (first != null) selectClip(first) else {
            selectedUri = null
            previewStartMs = 0L
            previewEndMs = 0L
            previewPositionMs = 0L
            releasePlayer()
            mediaText.text = AppLanguage.text(R.string.message_d20e0c1b30)
        }
    }

    private fun splitAtPlayhead() {
        if (blockWhileMediaLoads()) return
        val session = engine ?: return
        try {
            val clip = currentSelectedClip() ?: throw IllegalStateException(AppLanguage.text(R.string.message_f4e00f9522))
            val playheadMs = seekBar.progress.toLong()
            val sourceOffsetMs = playheadMs - clip.sourceInMs
            if (sourceOffsetMs <= 0L || playheadMs >= clip.sourceOutMs)
                throw IllegalArgumentException(AppLanguage.text(R.string.message_81764a5673))
            val frameOffset = (sourceOffsetMs * 30L + 500L) / 1000L
            val cutFrame = clip.startFrame + frameOffset
            val rightId = "${clip.id}-r-${System.nanoTime()}"
            session.splitClip(clip.id, cutFrame, rightId)
            selectedClipId = rightId
            refreshTimeline()
            scheduleAutosave()
            engineStatusText.text = session.status()
            engine?.clips()?.firstOrNull { it.id == rightId }?.let { selectClip(it) }
        } catch (error: Exception) { showError(error) }
    }

    private fun trimAtPlayhead(leftEdge: Boolean) {
        if (blockWhileMediaLoads()) return
        val session = engine ?: return
        try {
            val clip = currentSelectedClip() ?: throw IllegalStateException(AppLanguage.text(R.string.message_f4e00f9522))
            session.trimClip(clip.id, leftEdge, seekBar.progress.toLong())
            refreshTimeline()
            scheduleAutosave()
            engineStatusText.text = session.status()
            engine?.clips()?.firstOrNull { it.id == clip.id }?.let { selectClip(it) }
        } catch (error: Exception) { showError(error) }
    }

    private fun deleteSelected(ripple: Boolean) {
        if (blockWhileMediaLoads()) return
        val session = engine ?: return
        try {
            val clip = currentSelectedClip() ?: throw IllegalStateException(AppLanguage.text(R.string.message_f4e00f9522))
            session.deleteClip(clip.id, ripple)
            val remaining = engine?.clips().orEmpty().sortedBy { it.startFrame }
            selectedClipId = remaining.firstOrNull()?.id
            refreshTimeline()
            scheduleAutosave()
            engineStatusText.text = session.status()
            remaining.firstOrNull()?.let { selectClip(it) } ?: run {
                selectedUri = null
                previewStartMs = 0L
                previewEndMs = 0L
                previewPositionMs = 0L
                releasePlayer()
                mediaText.text = AppLanguage.text(R.string.message_d20e0c1b30)
            }
        } catch (error: Exception) { showError(error) }
    }

    private fun runHistory(undo: Boolean) {
        if (blockWhileMediaLoads()) return
        val session = engine ?: return
        try {
            val anchorFrame = timelineFrame
            val anchorTrack = timelineClips.firstOrNull { it.id == selectedClipId }?.trackId ?: "v1"
            val changed = if (undo) session.undo() else session.redo()
            if (!changed) {
                Toast.makeText(this, if (undo) AppLanguage.text(R.string.message_b9cbcc0505) else AppLanguage.text(R.string.message_5e5162c1fe), Toast.LENGTH_SHORT).show()
                return
            }
            val clips = session.clips().sortedBy { it.startFrame }
            if (clips.none { it.id == selectedClipId }) selectedClipId = clips.firstOrNull()?.id
            refreshTimeline()
            scheduleAutosave()
            engineStatusText.text = session.status()
            if (clips.isEmpty()) showFirstClip(session)
            else seekTimelineAt(anchorTrack, anchorFrame)
        } catch (error: Exception) { showError(error) }
    }

    private fun scheduleAutosave() {
        val session = engine ?: return
        hasUnsavedChanges = true
        editGeneration += 1
        val scheduledGeneration = editGeneration
        val scheduledStore = managedStore
        pendingAutosave?.cancel(false)
        pendingAutosave = metadataExecutor.schedule({
            if (handingOffEngine) return@schedule
            try {
                scheduledStore?.autosave(session) { !handingOffEngine } ?: session.saveAutosave(projectPath)
                mainHandler.post { if (!isDestroyed && !projectIoBusy && editGeneration == scheduledGeneration) statusText.text = AppLanguage.text(R.string.message_80236dfc68) }
            } catch (error: Exception) {
                mainHandler.post { if (!isDestroyed) statusText.text = AppLanguage.text(R.string.message_eeae4b4c38, error.message) }
            }
        }, 600L, TimeUnit.MILLISECONDS)
    }

    private fun saveProject() {
        if (blockWhileMediaLoads()) return
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.message_50b34fb08a))
            .setMessage(AppLanguage.text(R.string.message_c2c36977a7))
            .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138), null)
            .setPositiveButton(AppLanguage.text(R.string.message_efc007a393)) { _, _ ->
                if (projectIoBusy || selectedMediaLoading) return@setPositiveButton
                val session = engine ?: return@setPositiveButton
                pendingAutosave?.cancel(false)
                projectIoBusy = true
                projectIoOperation = ProjectIoOperation.SAVE
                val generationAtSave = editGeneration
                statusText.text = AppLanguage.text(R.string.message_725e31767e)
                metadataExecutor.execute {
                    try {
                        if (managedStore != null) managedStore!!.checkpoint(session) else session.saveProject(projectPath)
                        session.clearAutosave(projectPath)
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                if (editGeneration == generationAtSave) hasUnsavedChanges = false
                                statusText.text = AppLanguage.text(R.string.message_46d696486d)
                            }
                        }
                    } catch (error: Exception) {
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                statusText.text = AppLanguage.text(R.string.message_1a9f905eb4, error.message)
                            }
                        }
                    }
                }
            }
            .show()
    }

    private fun confirmLoadProject() {
        if (blockWhileMediaLoads()) return
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.message_8edbd70ad6))
            .setMessage(AppLanguage.text(R.string.message_42f3763acb))
            .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138), null)
            .setPositiveButton(AppLanguage.text(R.string.message_cf9b77061f)) { _, _ ->
                if (projectIoBusy || selectedMediaLoading) return@setPositiveButton
                val session = engine ?: return@setPositiveButton
                pendingAutosave?.cancel(false)
                projectIoBusy = true
                projectIoOperation = ProjectIoOperation.LOAD
                statusText.text = AppLanguage.text(R.string.message_d36d2b9e2d)
                metadataExecutor.execute {
                    try {
                        if (managedStore != null) managedStore!!.autosave(session) else session.saveAutosave(projectPath)
                        if (managedStore != null) managedStore!!.load(session) else session.loadProject(projectPath)
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                hasUnsavedChanges = false
                                updateEngineStatus()
                                showFirstClip(session)
                                statusText.text = AppLanguage.text(R.string.message_63fa0fce55)
                            }
                        }
                    } catch (error: Exception) {
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                statusText.text = AppLanguage.text(R.string.message_7496096196, error.message)
                            }
                        }
                    }
                }
            }
            .show()
    }

    private fun confirmRepairCheckpoint() {
        if (isDestroyed || isFinishing) return
        if (projectIoBusy || blockWhileMediaLoads()) return
        val dialog=AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.checkpoint_repair))
            .setMessage(AppLanguage.text(R.string.checkpoint_repair_explanation))
            .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138),null)
            .setPositiveButton(AppLanguage.text(R.string.checkpoint_repair_accept)) { _, _ ->
                if (projectIoBusy || selectedMediaLoading) return@setPositiveButton
                val store=managedStore ?: return@setPositiveButton
                val session=engine ?: return@setPositiveButton
                projectIoBusy=true; projectIoOperation=ProjectIoOperation.SAVE
                metadataExecutor.execute {
                    try {
                        val repaired=store.repairCheckpoint(session)
                        mainHandler.post {
                            if(!isDestroyed) {
                                projectIoBusy=false; projectIoOperation=null
                                statusText.text=AppLanguage.text(if(repaired) R.string.checkpoint_repaired else R.string.checkpoint_no_repair)
                            }
                        }
                    } catch(error: Exception) {
                        mainHandler.post {
                            if(!isDestroyed) {
                                projectIoBusy=false; projectIoOperation=null
                                statusText.text=AppLanguage.text(R.string.message_11e7c90ac5,error.message)
                            }
                        }
                    }
                }
            }.create()
        checkpointRepairDialog?.dismiss()
        checkpointRepairDialog=dialog
        dialog.setOnDismissListener { if(checkpointRepairDialog === dialog) checkpointRepairDialog=null }
        dialog.show()
    }

    private fun confirmRecoverAutosave() {
        if (blockWhileMediaLoads()) return
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.message_fc80eb0ad5))
            .setMessage(AppLanguage.text(R.string.message_d795d2fb3e))
            .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138), null)
            .setPositiveButton(AppLanguage.text(R.string.message_a7f8686836)) { _, _ ->
                if (projectIoBusy || selectedMediaLoading) return@setPositiveButton
                val session = engine ?: return@setPositiveButton
                pendingAutosave?.cancel(false)
                projectIoBusy = true
                projectIoOperation = ProjectIoOperation.RECOVER
                statusText.text = AppLanguage.text(R.string.message_a94f180f47)
                metadataExecutor.execute {
                    try {
                        val recovered = managedStore?.recover(session) ?: session.loadAutosave(projectPath)
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                if (recovered) {
                                    hasUnsavedChanges = libraryId != null
                                    updateEngineStatus()
                                    showFirstClip(session)
                                    statusText.text = AppLanguage.text(R.string.message_3119322927)
                                } else statusText.text = AppLanguage.text(R.string.message_4651a020c4)
                            }
                        }
                    } catch (error: Exception) {
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                statusText.text = AppLanguage.text(R.string.message_ff3186302b, error.message)
                            }
                        }
                    }
                }
            }
            .show()
    }

    private fun blockWhileMediaLoads(): Boolean {
        if (previewSeeking) {
            Toast.makeText(this, AppLanguage.text(R.string.message_7c49e6e92c), Toast.LENGTH_SHORT).show()
            return true
        }
        if (projectIoBusy) {
            Toast.makeText(this, AppLanguage.text(R.string.message_98746c7c59), Toast.LENGTH_SHORT).show()
            return true
        }
        if (!selectedMediaLoading) return false
        Toast.makeText(this, AppLanguage.text(R.string.message_ba90d21bbd), Toast.LENGTH_SHORT).show()
        return true
    }

    private fun showError(error: Exception) {
        Toast.makeText(this, error.message ?: AppLanguage.text(R.string.message_36aba39eed), Toast.LENGTH_LONG).show()
    }

    private fun frameToMs(frames: Long): Long = Math.addExact(Math.multiplyExact(frames.coerceAtLeast(0L) / 30L, 1000L), (frames.coerceAtLeast(0L) % 30L * 1000L) / 30L)

    private fun launchExportPicker() {
        if (projectIoBusy || blockWhileMediaLoads()) return
        // Plan/grade validation belongs to startTimelineExport, which also presents the
        // explicit experimental route. A duplicate guard here made that dialog unreachable.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startTimelineExport(null)
        } else {
            launchExportDocumentPicker()
        }
    }

    private fun launchExportDocumentPicker() {
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "video/mp4"
            putExtra(Intent.EXTRA_TITLE, "Owncut-edited.mp4")
        }
        startActivityForResult(intent, REQUEST_EXPORT_VIDEO)
    }

    private fun startTimelineExport(destination: Uri?, route: ExportRoute=ExportRoute.SHIPPING) {
        if (projectIoBusy) return
        val exportClips=try { engine?.clips().orEmpty().toList() } catch(error: Exception) {
            showError(error)
            return
        }
        val primaryClips = exportClips
            .filter { it.trackId == "v1" }
            .ifEmpty { exportClips.filter { !it.trackId.startsWith("a", ignoreCase = true) } }
            .ifEmpty { exportClips }
        val plan = try {
            ExportRoutePolicy.requireAllowed(route,exportClips.any { it.gradeActive },Build.VERSION.SDK_INT)
            if(route==ExportRoute.EXPERIMENTAL_GRADED) TimelineRemuxExporter.candidateGradedTimingPlan(primaryClips)
            else TimelineRemuxExporter.makePlan(primaryClips)
        } catch (error: Exception) {
            showError(error)
            showExportFailureDetails("Owncut ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) revision=${BuildConfig.PREVIEW_REVISION}\n" +
                ExportFailureReport.details(error,ExportFailureReport.Stage.PREPARING,exportClips.size,
                    exportClips.count { it.gradeActive },engine?.richState()?.outputHeight ?: TimelineRenderExporter.OUTPUT_HEIGHT),
                if(route==ExportRoute.SHIPPING && destination==null && Build.VERSION.SDK_INT>=29 &&
                    exportClips.any { it.gradeActive }) ({ confirmExperimentalExport() }) else null)
            return
        }
        if (plan.omittedGapFrames > 0L) {
            val omittedGapMs = frameToMs(plan.omittedGapFrames)
            AlertDialog.Builder(this)
                .setTitle(AppLanguage.text(R.string.message_de5c2380cd))
                .setMessage(AppLanguage.text(R.string.message_73a9853a46, plan.omittedGapFrames, omittedGapMs))
                .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138)) { _, _ -> destination?.let { deleteFailedExportDocument(it) } }
                .setOnCancelListener { destination?.let { deleteFailedExportDocument(it) } }
                .setPositiveButton(AppLanguage.text(R.string.message_8cee3592bb)) { _, _ -> beginTimelineExport(destination, plan, route) }
                .show()
        } else {
            beginTimelineExport(destination, plan, route)
        }
    }

    private fun beginTimelineExport(destination: Uri?, plan: TimelineRemuxExporter.Plan,
        route: ExportRoute=ExportRoute.SHIPPING) {
        if (projectIoBusy) return
        val cancellation = AtomicBoolean(false)
        exportCancellation = cancellation
        projectIoBusy = true
        projectIoOperation = ProjectIoOperation.EXPORT
        timelineView?.isEnabled = false
        cancelExportButton.text = AppLanguage.text(R.string.message_90f2b63925)
        cancelExportButton.isEnabled = true
        statusText.text = AppLanguage.text(R.string.message_d7e376c6eb)
        exportExecutor.execute {
            val temporary = File(cacheDir, "owncut-export-${System.nanoTime()}.mp4")
            var resultMessage = if (plan.omittedGapFrames > 0L) {
                AppLanguage.text(R.string.message_21bed82a7e)
            } else {
                if (destination == null) AppLanguage.text(R.string.message_48a2f4125e)
                else AppLanguage.text(R.string.message_f5a66453bc)
            }
            var failureReport: String?=null
            var failureStage=ExportFailureReport.Stage.PREPARING
            var failureOutputHeight = engine?.richState()?.outputHeight ?: TimelineRenderExporter.OUTPUT_HEIGHT
            try {
                ExportRoutePolicy.requireAllowed(route,plan.clips.any { it.gradeActive },Build.VERSION.SDK_INT)
                if(route==ExportRoute.SHIPPING) TimelineRenderExporter.requireShippingExportSupported(plan)
                failureStage=ExportFailureReport.Stage.RENDERING
                val onProgress: (String) -> Unit = { message ->
                    mainHandler.post { if (!isDestroyed) statusText.text = message }
                }
                val snapEngine = engine
                val richDoc = snapEngine?.richState() ?: TimelineRichDocument()
                failureOutputHeight = richDoc.outputHeight ?: TimelineRenderExporter.OUTPUT_HEIGHT
                val allClipsSnapshot = snapEngine?.clips().orEmpty()
                val trackInfos = snapEngine?.tracks().orEmpty()
                val useRichPipeline = richDoc.outputAspectRatio != null || richDoc.outputWidth != null ||
                    richDoc.outputHeight != null || richDoc.overlays.isNotEmpty() ||
                    richDoc.subtitles.isNotEmpty() ||
                    allClipsSnapshot.any { it.trackId != "v1" } ||
                    plan.clips.any { c ->
                        val fx = richDoc.fxFor(c.id)
                        fx.hasVideoTransformOrFx() || fx.hasAudioEnvelope()
                    }
                if(route==ExportRoute.EXPERIMENTAL_GRADED) {
                    if (useRichPipeline) {
                        TimelineRenderExporter.exportRichGraded(
                            this@MainActivity, plan, "explicit-candidate-${System.nanoTime()}",
                            richDoc, allClipsSnapshot, trackInfos, temporary, cancellation, onProgress
                        )
                    } else {
                        TimelineRenderExporter.exportGradedCandidate(this@MainActivity,plan,
                            "explicit-candidate-${System.nanoTime()}",temporary,cancellation,onProgress)
                    }
                } else {
                    if (useRichPipeline) {
                        TimelineRenderExporter.exportRich(
                            this@MainActivity, plan, richDoc, allClipsSnapshot, trackInfos,
                            temporary, cancellation, onProgress
                        )
                    } else {
                        TimelineRenderExporter.export(this@MainActivity,plan,temporary,cancellation,onProgress)
                    }
                }
                if (destination == null) {
                    failureStage=ExportFailureReport.Stage.PUBLISHING_GALLERY
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        publishExportToGallery(temporary, cancellation)
                    } else {
                        error(AppLanguage.text(R.string.message_ebab316ada))
                    }
                }
                else {
                    failureStage=ExportFailureReport.Stage.COPYING_DOCUMENT
                    copyExportToDocument(temporary, destination, cancellation)
                }
            } catch (cancel: CancellationException) {
                val cleaned = destination == null || deleteFailedExportDocument(destination)
                resultMessage = if (destination == null) (cancel.message ?: AppLanguage.text(R.string.message_df54d9e4da))
                    else if (cleaned) AppLanguage.text(R.string.message_51d6ad5eff)
                    else AppLanguage.text(R.string.message_32ba7b117e)
            } catch (error: Exception) {
                val cleaned = destination == null || deleteFailedExportDocument(destination)
                failureReport="Owncut ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) revision=${BuildConfig.PREVIEW_REVISION} route=${route.name}\n" +
                    ExportFailureReport.details(error,failureStage,plan.clips.size,
                        plan.clips.count { it.gradeActive },failureOutputHeight)
                resultMessage = AppLanguage.text(R.string.message_38516603a3, error.message ?: error.javaClass.simpleName) +
                    if (cleaned) "" else AppLanguage.text(R.string.message_360e86bda3)
            } finally {
                temporary.delete()
                exportCancellation = null
                mainHandler.post {
                    if (!isDestroyed) {
                        projectIoBusy = false
                        projectIoOperation = null
                        cancelExportButton.isEnabled = false
                        timelineView?.isEnabled = true
                        cancelExportButton.text = AppLanguage.text(R.string.message_90f2b63925)
                        statusText.text = resultMessage
                        failureReport?.let { showExportFailureDetails(it) }
                    }
                }
            }
        }
    }

    private fun confirmExperimentalExport() {
        if(projectIoBusy || isFinishing || isDestroyed) return
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.export_candidate_title))
            .setMessage(AppLanguage.text(R.string.export_candidate_warning))
            .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138),null)
            .setPositiveButton(AppLanguage.text(R.string.export_candidate_start)) { _,_ ->
                startTimelineExport(null,ExportRoute.EXPERIMENTAL_GRADED)
            }.show()
    }

    private fun showExportFailureDetails(report: String, experimentalAction: (() -> Unit)?=null) {
        if(isFinishing || isDestroyed) return
        val builder=AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.export_error_details))
            .setMessage(report)
            .setNeutralButton(AppLanguage.text(R.string.color_copy_report)) { _,_ ->
                PreviewDiagnostics.copy(this,report)
            }
            .setPositiveButton(AppLanguage.text(R.string.color_details_close),null)
        if(experimentalAction!=null) builder.setNegativeButton(AppLanguage.text(R.string.export_candidate_title)) { _,_ ->
            experimentalAction()
        }
        val dialog=builder.show()
        dialog.findViewById<TextView>(android.R.id.message)?.setTextIsSelectable(true)
    }

    /** Publish only a validated MP4; keep the Gallery row hidden until the copy closes. */
    @android.annotation.TargetApi(Build.VERSION_CODES.Q)
    private fun publishExportToGallery(file: File, cancellation: AtomicBoolean) {
        ExportPublication.publish(file, cancellation) { GalleryPendingOutput.create(this) }
    }

    private fun deleteFailedExportDocument(destination: Uri): Boolean = try {
        DocumentsContract.deleteDocument(contentResolver, destination)
    } catch (_: Exception) {
        false
    }

    private fun copyExportToDocument(file: File, destination: Uri, cancellation: AtomicBoolean) {
        if (cancellation.get()) throw CancellationException(AppLanguage.text(R.string.message_2b042e5a6c))
        val output = contentResolver.openOutputStream(destination, "wt")
            ?: throw IllegalStateException(AppLanguage.text(R.string.message_9c49f1f1b6))
        output.buffered().use { target ->
            file.inputStream().buffered().use { input ->
                val buffer = ByteArray(64 * 1024)
                while (true) {
                    if (cancellation.get() || Thread.currentThread().isInterrupted)
                        throw CancellationException(AppLanguage.text(R.string.message_2b042e5a6c))
                    val count = input.read(buffer)
                    if (count < 0) break
                    target.write(buffer, 0, count)
                }
                target.flush()
            }
        }
    }

    private fun launchRelinkPicker() {
        if (projectIoBusy || blockWhileMediaLoads()) return
        val clipId = selectedClipId ?: run {
            Toast.makeText(this, AppLanguage.text(R.string.message_f4e00f9522), Toast.LENGTH_SHORT).show()
            return
        }
        if (engine?.clips()?.any { it.id == clipId } != true) return
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "video/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        startActivityForResult(intent, REQUEST_RELINK_MEDIA)
    }

    private fun launchProjectSavePicker() {
        if (projectIoBusy || blockWhileMediaLoads()) return
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_TITLE, "Owncut-project.owncut")
        }
        startActivityForResult(intent, REQUEST_PROJECT_SAVE)
    }

    private fun launchProjectOpenPicker() {
        if (projectIoBusy || blockWhileMediaLoads()) return
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*" // SAF providers do not consistently register the .owncut extension MIME type.
        }
        startActivityForResult(intent, REQUEST_PROJECT_OPEN)
    }

    private fun saveProjectToDocument(uri: Uri) {
        val session = engine ?: return
        pendingAutosave?.cancel(false)
        projectIoBusy = true
        projectIoOperation = ProjectIoOperation.SAVE
        val generationAtSave = editGeneration
        statusText.text = AppLanguage.text(R.string.message_18689bdb66)
        metadataExecutor.execute {
            val temporary = File(cacheDir, "project-export-${System.nanoTime()}.owncut")
            try {
                session.saveCopy(temporary.absolutePath)
                if (temporary.length() > ProjectDocumentIo.MAX_PROJECT_BYTES) throw IllegalStateException(AppLanguage.text(R.string.message_413c4a66bd))
                contentResolver.openOutputStream(uri, "wt")?.use { output ->
                    temporary.inputStream().buffered().use { input -> ProjectDocumentIo.copy(input, output) }
                } ?: throw IllegalStateException(AppLanguage.text(R.string.message_9c49f1f1b6))
                // External backup succeeded even if a managed primary has a conflict.
                val localError = try {
                    if (libraryId != null) { managedStore?.checkpoint(session) ?: session.saveProject(projectPath) }
                    session.clearAutosave(projectPath)
                    null
                } catch(error: Exception) { error }
                temporary.delete()
                mainHandler.post {
                    if (!isDestroyed) {
                        projectIoBusy = false
                        projectIoOperation = null
                        if (localError == null && editGeneration == generationAtSave) hasUnsavedChanges = false
                        statusText.text = if(localError == null) AppLanguage.text(R.string.message_59b7d93902) else AppLanguage.text(R.string.external_saved_only, localError.message)
                    }
                }
            } catch (error: Exception) {
                temporary.delete()
                mainHandler.post {
                    if (!isDestroyed) {
                        projectIoBusy = false
                        projectIoOperation = null
                        statusText.text = AppLanguage.text(R.string.message_f07be40718, error.message)
                    }
                }
            }
        }
    }

    private fun confirmOpenProjectDocument(uri: Uri) {
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.message_f13c98fc70))
            .setMessage(AppLanguage.text(R.string.message_2d4b4eb369))
            .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138), null)
            .setPositiveButton(AppLanguage.text(R.string.message_cf9b77061f)) { _, _ ->
                if (projectIoBusy || selectedMediaLoading) return@setPositiveButton
                val session = engine ?: return@setPositiveButton
                pendingAutosave?.cancel(false)
                projectIoBusy = true
                projectIoOperation = ProjectIoOperation.LOAD
                statusText.text = AppLanguage.text(R.string.message_315688be7e)
                metadataExecutor.execute {
                    val temporary = File(cacheDir, "project-import-${System.nanoTime()}.owncut")
                    try {
                        if (managedStore != null) managedStore!!.autosave(session) else session.saveAutosave(projectPath)
                        contentResolver.openInputStream(uri)?.buffered()?.use { input ->
                            temporary.outputStream().buffered().use { output -> ProjectDocumentIo.copy(input, output) }
                        } ?: throw IllegalStateException(AppLanguage.text(R.string.message_b89067ce69))
                        session.loadProject(temporary.absolutePath)
                        session.clearAutosave(projectPath)
                        temporary.delete()
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                hasUnsavedChanges = false
                                updateEngineStatus()
                                showFirstClip(session)
                                statusText.text = AppLanguage.text(R.string.message_027434fe36)
                            }
                        }
                    } catch (error: Exception) {
                        temporary.delete()
                        mainHandler.post {
                            if (!isDestroyed) {
                                projectIoBusy = false
                                projectIoOperation = null
                                statusText.text = AppLanguage.text(R.string.message_7496096196, error.message)
                            }
                        }
                    }
                }
            }
            .show()
    }

    private fun launchVideoPicker() {
        if (projectIoBusy) return
        pendingImportTrackId = null
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "video/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        startActivityForResult(intent, REQUEST_VIDEO)
    }

    @Deprecated("Uses platform Activity result API to avoid adding an AndroidX runtime dependency in the bootstrap.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK) {
            pendingImportTrackId = null
            return
        }
        val resultData = data ?: return
        val uri = resultData.data ?: return
        when (requestCode) {
            REQUEST_VIDEO -> {
                val targetTrack = pendingImportTrackId
                pendingImportTrackId = null
                val takeGrant = resultData.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0
                readMetadataAndRegister(uri, takeGrant, targetTrackId = targetTrack)
            }
            REQUEST_RELINK_MEDIA -> {
                val takeGrant = resultData.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0
                selectedClipId?.let { clipId -> readMetadataAndRegister(uri, takeGrant, relinkClipId = clipId) }
            }
            REQUEST_PROJECT_SAVE -> saveProjectToDocument(uri)
            REQUEST_PROJECT_OPEN -> confirmOpenProjectDocument(uri)
            REQUEST_EXPORT_VIDEO -> startTimelineExport(uri)
        }
    }

    private fun readMetadataAndRegister(
        uri: Uri,
        takePersistableGrant: Boolean = false,
        existingClipId: String? = null,
        relinkClipId: String? = null,
        targetTrackId: String? = null,
    ) {
        if (projectIoBusy) return
        val request = ++metadataRequestSequence
        selectedMediaLoading = true
        seekBar.isEnabled = false
        statusText.text = AppLanguage.text(R.string.message_a6dbb03662)
        val existingTrackIsAudio = existingClipId != null &&
            engine?.clips()?.firstOrNull { it.id == existingClipId }?.trackId?.startsWith("a", true) == true
        val allowAudioOnly = (targetTrackId?.startsWith("a", true) == true) || existingTrackIsAudio
        metadataExecutor.execute {
            var duration = 0L
            var displayWidth = 0
            var displayHeight = 0
            var persistedGrant = false
            var failure: Exception? = null
            val retriever = MediaMetadataRetriever()
            try {
                if (takePersistableGrant) {
                    try {
                        contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    } catch (_: SecurityException) {
                        // Some document providers grant only temporary access; the warning is shown below.
                    }
                }
                persistedGrant = contentResolver.persistedUriPermissions.any {
                    it.uri == uri && it.isReadPermission
                }
                retriever.setDataSource(this, uri)
                duration = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                    ?.toLongOrNull() ?: throw IllegalArgumentException(AppLanguage.text(R.string.message_24c6921f1f))
                if (duration <= 0L) throw IllegalArgumentException(AppLanguage.text(R.string.message_0b5dee2ad4))

                val width = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull() ?: 0
                val height = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull() ?: 0
                if ((width <= 0 || height <= 0) && !allowAudioOnly) throw IllegalArgumentException(AppLanguage.text(R.string.message_b7592acd38))
                val rotation = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
                if (width > 0 && height > 0) {
                    displayWidth = if (rotation == 90 || rotation == 270) height else width
                    displayHeight = if (rotation == 90 || rotation == 270) width else height
                } else {
                    displayWidth = 1280
                    displayHeight = 720
                }
            } catch (error: Exception) {
                failure = error
            } finally {
                try { retriever.release() } catch (_: RuntimeException) { }
            }

            mainHandler.post {
                if (request != metadataRequestSequence || isFinishing || isDestroyed) return@post
                val error = failure
                if (error != null) {
                    selectedMediaLoading = false
                    mediaText.text = AppLanguage.text(R.string.message_422600b830, error.message ?: AppLanguage.text(R.string.message_d766471f27))
                    statusText.text = AppLanguage.text(R.string.message_b4c2e22ddc)
                    if (existingClipId != null) {
                        resumeAfterSelection = false
                        previewEndMs = 0L
                        playButton.isEnabled = false
                        releasePlayer()
                    }
                    return@post
                }

                try {
                    selectedMediaLoading = false
                    val session = engine ?: throw IllegalStateException(AppLanguage.text(R.string.message_a77e61169d))
                    val existing = existingClipId?.let { id -> session.clips().firstOrNull { it.id == id } }
                    if (existingClipId != null && existing == null) throw IllegalStateException(AppLanguage.text(R.string.message_72f825c0e0))
                    if (existing != null && existing.sourceOutMs > duration)
                        throw IllegalArgumentException(AppLanguage.text(R.string.message_03862c58d4))
                    val relinkTarget = relinkClipId?.let { id -> session.clips().firstOrNull { it.id == id } }
                    if (relinkClipId != null && relinkTarget == null) throw IllegalStateException(AppLanguage.text(R.string.message_2313bf0ccc))
                    if (relinkTarget != null) session.relinkClipMedia(relinkTarget.id, uri.toString(), duration)
                    val clipId = relinkTarget?.id ?: existing?.id ?: if (targetTrackId != null) {
                        session.addMediaClipToTrack(targetTrackId, uri.toString(), duration, timelineFrame)
                    } else {
                        session.addMediaClip(uri.toString(), duration)
                    }
                    val addedNewClip = existing == null && relinkTarget == null
                    selectedClipId = clipId
                    previewStartMs = relinkTarget?.sourceInMs ?: existing?.sourceInMs ?: 0L
                    previewEndMs = relinkTarget?.sourceOutMs ?: existing?.sourceOutMs ?: duration
                    if (addedNewClip) previewPositionMs = 0L
                    previewPositionMs = previewPositionMs.coerceIn(previewStartMs,
                        (previewEndMs - 1L).coerceAtLeast(previewStartMs))
                    timelineGapPosition = false; preview.alpha = 1f
                    selectedUri = uri
                    durationMs = duration
                    preview.setVideoSize(displayWidth, displayHeight)
                    seekBar.max = duration.coerceAtMost(Int.MAX_VALUE.toLong()).toInt().coerceAtLeast(1)
                    seekBar.progress = previewPositionMs.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                    val grantInfo = if (persistedGrant) {
                        AppLanguage.text(R.string.message_8c9b5fc4b7)
                    } else {
                        AppLanguage.text(R.string.message_9c5367b9dd)
                    }
                    mediaText.text = "${uri.lastPathSegment ?: AppLanguage.text(R.string.generic_video)} · ${displayWidth}×${displayHeight} · ${formatTime(duration)} · ${clipId}\n${grantInfo}"
                    if (surfaceReady) preparePlayer(uri)
                    updateEngineStatus()
                    refreshTimeline()
                    if (addedNewClip || relinkTarget != null) scheduleAutosave()
                } catch (error: Exception) {
                    if (relinkClipId != null) statusText.text = AppLanguage.text(R.string.message_dd7aff38a4, error.message)
                    else mediaText.text = AppLanguage.text(R.string.message_8586291d13, error.message)
                }
            }
        }
    }

    private fun preparePlayer(uri: Uri) {
        val outputSurface = previewSurface ?: run {
            statusText.text = AppLanguage.text(R.string.message_97096c364c)
            return
        }
        releasePlayer()
        val generation = playerGeneration
        val savedGrade=currentSelectedClip()?.gradeActive==true
        preview.alpha=if(savedGrade) 0f else 1f
        statusText.text = AppLanguage.text(R.string.message_12ef116cca)
        playButton.isEnabled = false
        mediaHandler.post {
            if (generation != playerGeneration) return@post
            val candidate = MediaPlayer()
            if (generation != playerGeneration) {
                candidate.release()
                return@post
            }
            player = candidate
            if (generation != playerGeneration) {
                player = null
                candidate.release()
                return@post
            }
            try {
                candidate.setSurface(outputSurface)
                candidate.setOnPreparedListener { ready ->
                    if (generation != playerGeneration || player !== ready) return@setOnPreparedListener
                    val readyDuration = ready.duration
                    val startPosition = previewPositionMs.coerceIn(previewStartMs,
                        (minOf(previewEndMs, readyDuration.toLong()) - 1L).coerceAtLeast(previewStartMs))
                    val shouldResume = resumeAfterSelection
                    resumeAfterSelection = false
                    fun positioned() {
                        if (generation != playerGeneration || player !== ready) return
                        try {
                            ready.setOnSeekCompleteListener(null)
                            previewSeeking = false; playerPrepared = true
                            if (shouldResume && !savedGrade) ready.start()
                            mainHandler.post {
                                if (generation == playerGeneration && player === ready) {
                                    durationMs = readyDuration.toLong()
                                    previewPositionMs = startPosition
                                    seekBar.isEnabled = true
                                    seekBar.max = readyDuration.coerceAtLeast(1)
                                    seekBar.progress = startPosition.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                                    updateTimelinePlayhead(follow = shouldResume)
                                    playButton.isEnabled = true
                                    applyClipPlaybackParams(applySpeedNow = shouldResume && !savedGrade)
                                    syncMainGradePreview(shouldResume)
                                    if (shouldResume && !savedGrade) {
                                        playButton.text = AppLanguage.text(R.string.action_pause)
                                        scheduleFirstFrameDiagnostic(generation, ready)
                                        mainHandler.removeCallbacks(playbackTicker)
                                        mainHandler.post(playbackTicker)
                                    } else statusText.text = AppLanguage.text(R.string.message_e3c1664426)
                                }
                            }
                        } catch (error: IllegalStateException) {
                            playerPrepared = false; previewSeeking = false
                            mainHandler.post { if (generation == playerGeneration) statusText.text = AppLanguage.text(R.string.message_be209c24bc, error.message) }
                        }
                    }
                    try {
                        if (startPosition > 0L) {
                            previewSeeking = true
                            ready.setOnSeekCompleteListener { positioned() }
                            ready.seekTo(startPosition, MediaPlayer.SEEK_CLOSEST)
                        } else positioned()
                    } catch (error: IllegalStateException) {
                        previewSeeking = false
                        mainHandler.post { if (generation == playerGeneration) statusText.text = AppLanguage.text(R.string.message_1ac6986001, error.message) }
                    }
                }
                candidate.setOnCompletionListener { completed ->
                    if (generation == playerGeneration && player === completed) {
                        mainHandler.post { advanceOrStopTimeline(generation, completed) }
                    }
                }
                candidate.setOnErrorListener { failed, what, extra ->
                    if (generation == playerGeneration && player === failed) {
                        resumeAfterSelection = false
                        player = null
                        playerPrepared = false
                        previewSeeking = false
                        failed.release()
                        mainHandler.post {
                            clearFirstFrameDiagnostic()
                            mainHandler.removeCallbacks(playbackTicker)
                            playButton.isEnabled = false
                            playButton.text = AppLanguage.text(R.string.message_b5cdf40ef5)
                            statusText.text = AppLanguage.text(R.string.message_2668d112c9, what, extra)
                        }
                    }
                    true
                }
                candidate.setDataSource(this, uri)
                candidate.prepareAsync()
            } catch (error: Exception) {
                if (generation == playerGeneration) resumeAfterSelection = false
                if (player === candidate) {
                    player = null
                    playerPrepared = false
                    previewSeeking = false
                }
                candidate.release()
                mainHandler.post {
                    if (generation == playerGeneration) statusText.text = AppLanguage.text(R.string.message_aef7617ee6, error.message ?: AppLanguage.text(R.string.message_fea64f9a12))
                }
            }
        }
    }

    private fun scheduleFirstFrameDiagnostic(generation: Int, expectedPlayer: MediaPlayer) {
        clearFirstFrameDiagnostic()
        val check = Runnable {
            if (generation == playerGeneration && player === expectedPlayer && !firstVideoFrameRendered) {
                statusText.text = AppLanguage.text(R.string.message_0eaf8169fc)
            }
        }
        firstFrameDiagnostic = check
        mainHandler.postDelayed(check, 2_500L)
    }

    private fun clearFirstFrameDiagnostic() {
        val pending = firstFrameDiagnostic
        if (pending != null) mainHandler.removeCallbacks(pending)
        firstFrameDiagnostic = null
    }

    private fun syncMainGradePreview(startPlaying: Boolean=false) {
        if(colorDialog?.isShowing==true || isFinishing || isDestroyed) return
        val parent=gradeViewerParent ?: return
        val clip=currentSelectedClip()?.takeIf { it.mediaId==selectedUri?.toString() }
        if(mainGradePreview?.clip==clip && clip?.gradeActive==true) return
        val previous=mainGradePreview
        val resume=startPlaying || previous?.isPlaying==true
        previous?.close();mainGradePreview=null
        preview.alpha=1f
        if(clip==null || !clip.gradeActive || !playerPrepared) {
            if(previous!=null && clip!=null && playerPrepared) seekPreviewTo(previewPositionMs,startAfterSeek=resume)
            return
        }
        pauseForTimelineScrub()
        preview.alpha=0f // Never show the ungraded source as successful saved-grade playback.
        mainGradePreview=MainGradePreview(this,parent,clip,mainAssumedClipId==clip.id,previewPositionMs,resume,{ position,playing ->
            if(!isSeeking && !timelineScrubbing) {
                previewPositionMs=position
                seekBar.progress=position.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
                updateTimelinePlayhead(follow=playing)
            }
            playButton.text=AppLanguage.text(if(playing) R.string.action_pause else R.string.color_live_short)
        },{ advanceOrStopTimeline(playerGeneration,player) },{ error,canAssume ->
            playButton.isEnabled=false
            statusText.text=AppLanguage.text(R.string.color_preview_unavailable,PreviewDiagnostics.report(error))
            if(canAssume) offerMainInterpretMissing(clip)
        })
    }

    /** Explicit, per-clip consent only. Never automatic; cancel keeps the strict fail-closed behavior. */
    private fun offerMainInterpretMissing(clip: ClipSnapshot) {
        if(isFinishing || isDestroyed || colorDialog?.isShowing==true) return
        AlertDialog.Builder(this)
            .setTitle(AppLanguage.text(R.string.color_input_title))
            .setMessage(AppLanguage.text(R.string.color_input_warning))
            .setPositiveButton(AppLanguage.text(R.string.color_input_assume)) { _,_ ->
                mainAssumedClipId=clip.id
                mainGradePreview?.close();mainGradePreview=null
                syncMainGradePreview()
            }
            .setNegativeButton(AppLanguage.text(R.string.color_details_close),null)
            .show()
    }

    private fun togglePlayback() {
        mainGradePreview?.let { graded ->
            graded.toggle()
            playButton.text=AppLanguage.text(if(graded.isPlaying) R.string.action_pause else R.string.color_live_short)
            return
        }
        val current = player
        if (!playerPrepared || current == null || previewSeeking) {
            statusText.text = AppLanguage.text(R.string.message_7833cc82ff)
            return
        }
        val generation = playerGeneration
        val activeClip = currentSelectedClip()
        val fx = activeClip?.let { engine?.clipFx(it.id) }
        val trackMuted = activeClip != null && engine?.tracks()?.firstOrNull { it.id == activeClip.trackId }?.muted == true
        mediaHandler.post {
            if (generation != playerGeneration || player !== current) return@post
            try {
                if (current.isPlaying) {
                    current.pause()
                    mainHandler.post {
                        clearFirstFrameDiagnostic()
                        mainHandler.removeCallbacks(playbackTicker)
                        playButton.text = AppLanguage.text(R.string.message_b5cdf40ef5)
                        statusText.text = AppLanguage.text(R.string.action_pause)
                    }
                } else {
                    val position = current.currentPosition.toLong()
                    if (position < previewStartMs || position >= previewEndMs) {
                        mainHandler.post {
                            if (generation == playerGeneration && player === current)
                                seekPreviewTo(previewStartMs, startAfterSeek = true)
                        }
                        return@post
                    }
                    val vol = if (fx?.muted == true || trackMuted) 0f else (fx?.volume ?: 1f).coerceIn(0f, 1f)
                    current.setVolume(vol, vol)
                    val targetSpeed = fx?.speed?.toFloat()?.coerceIn(0.25f, 4.0f) ?: 1.0f
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && kotlin.math.abs(targetSpeed - 1.0f) > 0.02f) {
                        runCatching {
                            val params = current.playbackParams
                            params.speed = targetSpeed
                            current.playbackParams = params
                        }
                    }
                    current.start()
                    mainHandler.post {
                        playButton.text = AppLanguage.text(R.string.action_pause)
                        if (!firstVideoFrameRendered) {
                            statusText.text = AppLanguage.text(R.string.message_f836b31adb)
                            scheduleFirstFrameDiagnostic(generation, current)
                        }
                        mainHandler.removeCallbacks(playbackTicker)
                        mainHandler.post(playbackTicker)
                    }
                }
            } catch (error: IllegalStateException) {
                mainHandler.post { statusText.text = AppLanguage.text(R.string.message_6ad7121692, error.message) }
            }
        }
    }

    private fun releasePlayer() {
        mainGradePreview?.close();mainGradePreview=null
        if(::preview.isInitialized) preview.alpha=1f
        mainHandler.removeCallbacks(playbackTicker)
        clearFirstFrameDiagnostic()
        firstVideoFrameRendered = false
        previewSeeking = false
        seekSerial += 1
        val old = player
        player = null
        playerPrepared = false
        lastPreviewSpeed = Float.NaN
        isSeeking = false; timelineScrubbing = false
        if (::seekBar.isInitialized) seekBar.isEnabled = false
        playerGeneration += 1
        playButtonOrNull()?.let { it.isEnabled = false; it.text = AppLanguage.text(R.string.message_b5cdf40ef5) }
        if (old != null) mediaHandler.post {
            try { old.setSurface(null) } catch (_: IllegalStateException) { }
            try { old.release() } catch (_: IllegalStateException) { }
        }
    }

    private fun playButtonOrNull(): Button? = if (::playButton.isInitialized) playButton else null

    override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) {
        updatePreviewTransform()
        try {
            previewSurface = Surface(texture)
            surfaceReady = true
            if (!selectedMediaLoading) selectedUri?.let(::preparePlayer)
        } catch (error: RuntimeException) {
            showStartupFailure(error)
        }
    }

    override fun onSurfaceTextureSizeChanged(texture: SurfaceTexture, width: Int, height: Int) {
        val outputSurface = previewSurface ?: return
        val current = player ?: return
        val generation = playerGeneration
        mediaHandler.post {
            if (generation == playerGeneration && player === current) {
                try {
                    current.setSurface(outputSurface)
                } catch (_: IllegalStateException) {
                    mainHandler.post {
                        if (generation == playerGeneration && player === current) {
                            statusText.text = AppLanguage.text(R.string.message_b3e7f770ff)
                        }
                    }
                }
            }
        }
    }

    override fun onSurfaceTextureDestroyed(texture: SurfaceTexture): Boolean {
        surfaceReady = false
        releasePlayer()
        val oldSurface = previewSurface
        previewSurface = null
        mediaHandler.post {
            oldSurface?.release()
            texture.release()
        }
        // Keep the texture alive until MediaPlayer has detached on its own thread.
        return false
    }

    override fun onSurfaceTextureUpdated(texture: SurfaceTexture) {
        if(mainGradePreview!=null) return
        if (!firstVideoFrameRendered && playerPrepared && player != null) {
            firstVideoFrameRendered = true
            clearFirstFrameDiagnostic()
            statusText.text = AppLanguage.text(R.string.message_e5cd36b000)
        }
    }

    override fun onStart() {
        super.onStart()
        if (surfaceReady && !selectedMediaLoading && player == null) selectedUri?.let(::preparePlayer)
    }

    override fun onStop() {
        liveColorDialog?.dismiss();liveColorDialog=null
        releasePlayer()
        super.onStop()
    }

    @Suppress("DEPRECATION") // Retain the native session without adding a ViewModel dependency to this bootstrap.
    override fun onRetainNonConfigurationInstance(): Any? {
        if (projectIoBusy && projectIoOperation != ProjectIoOperation.EXPORT) return null
        // Render uses an immutable plan: cancel the old job but retain the editable native session.
        pendingAutosave?.cancel(false)
        val currentEngine = engine ?: return null
        val retained = RetainedEditorState(
            engine = currentEngine,
            projectPath = projectPath,
            managedStore = managedStore,
            selectedUri = selectedUri?.toString(),
            selectedClipId = selectedClipId,
            hasUnsavedChanges = hasUnsavedChanges,
            editGeneration = editGeneration,
            previewPositionMs = previewPositionMs,
            timelineFrame = timelineFrame,
            timelineGapPosition = timelineGapPosition,
            followPlayhead = followPlayhead,
            timelineScrollX = timelineViewport?.scrollX ?: 0,
            workspacePage = workspacePage,
        )
        handingOffEngine = true
        engine = null
        managedStore = null
        return retained
    }

    override fun onDestroy() {
        liveColorDialog?.dismiss();liveColorDialog=null
        colorDialog?.dismiss();colorDialog=null
        mediaSourcesDialog?.dismiss(); mediaSourcesDialog=null
        checkpointRepairDialog?.dismiss(); checkpointRepairDialog=null
        metadataRequestSequence += 1
        reframeGeneration += 1
        reframeExecutor.shutdownNow()
        if (!handingOffEngine) pendingAutosave?.cancel(false)
        if (::preview.isInitialized) preview.surfaceTextureListener = null
        releasePlayer()
        val surfaceToRelease = previewSurface
        previewSurface = null
        if (surfaceToRelease != null) mediaHandler.post { surfaceToRelease.release() }
        mediaThread.quitSafely()
        val closingSession = engine
        val operationAtShutdown = projectIoOperation
        val shouldWriteRecovery = hasUnsavedChanges &&
            operationAtShutdown != ProjectIoOperation.LOAD &&
            operationAtShutdown != ProjectIoOperation.RECOVER
        engine = null
        if (closingSession != null) {
            metadataExecutor.execute {
                try {
                    if (shouldWriteRecovery) { managedStore?.autosave(closingSession) ?: closingSession.saveAutosave(projectPath) }
                } catch (_: Exception) { }
                finally {
                    try { closingSession.close() } finally {
                        if (!handingOffEngine) { managedStore?.close(); managedStore = null }
                    }
                }
            }
        }
        metadataExecutor.shutdown()
        exportCancellation?.set(true)
        exportExecutor.shutdownNow()
        super.onDestroy()
    }

    private fun updateEngineStatus() {
        try { engineStatusText.text = engine?.status() ?: AppLanguage.text(R.string.message_08be239204) } catch (error: Exception) {
            engineStatusText.text = AppLanguage.text(R.string.message_c088ab45ec, error.message)
        }
    }

    private fun formatTime(milliseconds: Long): String {
        val totalSeconds = milliseconds.coerceAtLeast(0L) / 1000L
        return "%d:%02d".format(totalSeconds / 60L, totalSeconds % 60L)
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun matchWrap() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT,
    )

    companion object {
        internal const val EXTRA_LIBRARY_ID = "com.owncut.app.LIBRARY_PROJECT_ID"
        private const val REQUEST_VIDEO = 4101
        private const val REQUEST_PROJECT_SAVE = 4102
        private const val REQUEST_PROJECT_OPEN = 4103
        private const val REQUEST_RELINK_MEDIA = 4104
        private const val REQUEST_EXPORT_VIDEO = 4105
    }
}
