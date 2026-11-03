package com.owncut.app

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.widget.*
import androidx.media3.common.util.UnstableApi
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** Original wheel and drag-value grade editor. C++ validates/grades; UI only transports values.
 * A single in-flight still render plus one latest pending revision bounds work. Saving
 * validated settings is independent of decoder availability; never claim unavailable preview.
 */
@UnstableApi
internal object ClipColorEditor {
    fun show(activity: Activity, clip: ClipSnapshot, positionMs: Long, previewDraft: ((ColorGrade) -> AlertDialog)?=null, referenceFrame: Bitmap?=null, commit: (ColorGrade) -> Unit): AlertDialog {
        require(clip.sourceInMs>=0 && positionMs>=clip.sourceInMs && positionMs<clip.sourceOutMs)
        require(referenceFrame==null || (referenceFrame.width<=PreviewFrameSize.MAX_WIDTH && referenceFrame.height<=PreviewFrameSize.MAX_HEIGHT))
        val main=Handler(Looper.getMainLooper())
        val worker=Executors.newSingleThreadExecutor()
        val closed=AtomicBoolean(false)
        fun dp(value: Int)=(activity.resources.displayMetrics.density*value).toInt()
        fun textLabel(value: String,size: Float,secondary: Boolean=false)=StudioUi.label(activity,value,size,secondary).apply {
            typeface=android.graphics.Typeface.create("sans-serif",android.graphics.Typeface.NORMAL)
        }
        fun action(label: Int)=Button(activity).apply {
            text=AppLanguage.text(label);isAllCaps=false;textSize=12f
            typeface=android.graphics.Typeface.create("sans-serif-medium",android.graphics.Typeface.NORMAL)
            maxLines=1;ellipsize=android.text.TextUtils.TruncateAt.END
            minWidth=0;minimumWidth=0;minHeight=dp(48);minimumHeight=dp(48)
            setPadding(dp(8),dp(4),dp(8),dp(4));setTextColor(StudioUi.text)
            stateListAnimator=null;elevation=0f
            backgroundTintList=null;background=StudioUi.shape(activity,StudioUi.surface,8)
        }
        val root=LinearLayout(activity).apply {
            orientation=LinearLayout.VERTICAL;setPadding(dp(12),dp(8),dp(12),dp(8))
            setBackgroundColor(StudioUi.background)
        }
        // A bounded viewer is outside the controls scroller. Landscape uses two columns.
        val landscape=activity.resources.configuration.screenWidthDp>activity.resources.configuration.screenHeightDp
        val header=LinearLayout(activity).apply { gravity=android.view.Gravity.CENTER_VERTICAL }
        val heading=LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL }
        heading.addView(textLabel(AppLanguage.text(R.string.color_editor_title),18f))
        val sourceTime=String.format(java.util.Locale.ROOT,"%02d:%02d.%03d",positionMs/60000,(positionMs/1000)%60,positionMs%1000)
        heading.addView(textLabel(AppLanguage.text(R.string.color_source_time,sourceTime),11f,true))
        header.addView(heading,LinearLayout.LayoutParams(0,-2,1f))
        val reset=action(R.string.color_reset_draft).apply { text="↺";textSize=22f;contentDescription=AppLanguage.text(R.string.color_reset_draft) };header.addView(reset,LinearLayout.LayoutParams(-2,dp(48)))
        root.addView(header)
        val body=LinearLayout(activity).apply { orientation=if(landscape) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL }
        root.addView(body,LinearLayout.LayoutParams(-1,0,1f))
        val viewerSide=LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL }
        body.addView(viewerSide,if(landscape) LinearLayout.LayoutParams(0,-1,1f).apply { marginEnd=dp(12) } else LinearLayout.LayoutParams(-1,-2))
        val viewport=FrameLayout(activity).apply { setBackgroundColor(android.graphics.Color.BLACK) }
        val preview=ImageView(activity).apply { scaleType=ImageView.ScaleType.FIT_CENTER;contentDescription=AppLanguage.text(R.string.color_frame_viewer) }
        val videoTexture=AspectRatioTextureView(activity)
        viewport.addView(videoTexture,FrameLayout.LayoutParams(-1,-1,android.view.Gravity.CENTER))
        viewport.addView(preview,FrameLayout.LayoutParams(-1,-1))
        val empty=LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL;gravity=android.view.Gravity.CENTER;setPadding(dp(20),dp(12),dp(20),dp(12)) }
        val loading=ProgressBar(activity);empty.addView(loading,LinearLayout.LayoutParams(dp(28),dp(28)))
        val emptyText=textLabel(AppLanguage.text(R.string.color_frame_loading),13f,true).apply { gravity=android.view.Gravity.CENTER }
        empty.addView(emptyText);viewport.addView(empty,FrameLayout.LayoutParams(-1,-1))
        val viewerHeight=(activity.resources.configuration.screenHeightDp*0.27).toInt().coerceIn(100,240)
        viewerSide.addView(viewport,if(landscape) LinearLayout.LayoutParams(-1,0,1f) else LinearLayout.LayoutParams(-1,dp(viewerHeight)))
        val caption=textLabel(AppLanguage.text(if(referenceFrame!=null) R.string.color_frame_reference else R.string.color_frame_loading),11f,true)
        caption.maxLines=1;caption.ellipsize=android.text.TextUtils.TruncateAt.END;viewerSide.addView(caption)
        val statusRow=LinearLayout(activity).apply { gravity=android.view.Gravity.CENTER_VERTICAL }
        val status=TextView(activity).apply {
            text=AppLanguage.text(R.string.color_processing);setTextColor(StudioUi.muted);textSize=12f
            maxLines=1;ellipsize=android.text.TextUtils.TruncateAt.END
        }
        statusRow.addView(status,LinearLayout.LayoutParams(0,dp(24),1f))
        val details=action(R.string.color_error_details).apply { visibility=android.view.View.GONE }
        statusRow.addView(details,LinearLayout.LayoutParams(-2,dp(48)));viewerSide.addView(statusRow)
        val buttons=LinearLayout(activity)
        val render=action(R.string.color_refresh_frame).apply { isEnabled=false;text="↻";textSize=22f;contentDescription=AppLanguage.text(R.string.color_refresh_frame) }
        val compare=ToggleButton(activity).apply {
            textOn=AppLanguage.text(R.string.color_before);textOff=AppLanguage.text(R.string.color_after)
            isChecked=false;isEnabled=false;isAllCaps=false;textSize=12f
            minWidth=0;minimumWidth=0;setTextColor(StudioUi.text)
            stateListAnimator=null;elevation=0f
            backgroundTintList=null;background=StudioUi.shape(activity,StudioUi.surface,8)
        }
        val live=action(R.string.color_live_short).apply { isEnabled=previewDraft!=null }
        val inputColor=action(R.string.color_input_short).apply { isEnabled=false;contentDescription=AppLanguage.text(R.string.color_input_title) }
        for(button in listOf(compare,render,live,inputColor)) buttons.addView(button,LinearLayout.LayoutParams(0,dp(48),1f).apply { setMargins(dp(2),0,dp(2),0) })
        viewerSide.addView(buttons)
        val controls=LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL }
        body.addView(controls,if(landscape) LinearLayout.LayoutParams(0,-1,1f) else LinearLayout.LayoutParams(-1,0,1f))
        val tabs=LinearLayout(activity);controls.addView(tabs)
        val controlBody=LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL;setPadding(0,dp(8),0,dp(12)) }
        val controlScroll=ScrollView(activity).apply { isFillViewport=false;addView(controlBody) }
        controls.addView(controlScroll,LinearLayout.LayoutParams(-1,0,1f))
        val enabled=Switch(activity).apply {
            text=AppLanguage.text(R.string.color_grade_short);contentDescription=AppLanguage.text(R.string.color_enabled)
            isChecked=clip.grade.enabled;setTextColor(StudioUi.muted);textSize=11f;minHeight=dp(48)
            typeface=android.graphics.Typeface.create("sans-serif",android.graphics.Typeface.NORMAL)
            setPadding(dp(4),0,0,0)
        }
        val values=clip.grade.parametersV1()
        val names=activity.resources.getStringArray(R.array.color_parameter_names)
        check(names.size==values.size)
        val scales=values.mapIndexed { index,value -> ColorSliderScale.forParameter(index,value) }
        val inputs=mutableListOf<GradeDragValue>()
        val wheels=mutableListOf<GradeColorWheel>()
        val wheelReadouts=mutableListOf<TextView>()
        val groups=List(3) { LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL } }
        val tabButtons=mutableListOf<Button>()
        fun showGroup(group: Int) {
            groups.forEachIndexed { index,view -> view.visibility=if(index==group) android.view.View.VISIBLE else android.view.View.GONE }
            tabButtons.forEachIndexed { index,button ->
                button.isSelected=index==group
                button.setTextColor(if(index==group) StudioUi.accent else StudioUi.muted)
                button.background=StudioUi.shape(activity,if(index==group) StudioUi.raised else StudioUi.background,8)
            }
            val incoming=groups[group]
            incoming.animate().cancel()
            if(incoming.isAttachedToWindow && android.animation.ValueAnimator.areAnimatorsEnabled()) {
                incoming.alpha=0.4f;incoming.animate().alpha(1f).setDuration(120).start()
            } else incoming.alpha=1f
            controlScroll.scrollTo(0,0)
        }
        listOf(R.string.color_group_primary,R.string.color_group_wheels,R.string.color_group_channels).forEachIndexed { index,label ->
            val button=action(label).apply { setOnClickListener { showGroup(index) } }
            tabButtons.add(button);tabs.addView(button,LinearLayout.LayoutParams(0,dp(48),1f).apply { setMargins(dp(2),dp(6),dp(2),0) })
        }
        tabs.addView(enabled,LinearLayout.LayoutParams(dp(82),dp(54)))
        // One focused wheel rather than four competing cards; selectors retain48dp targets.
        val wheelSelectors=LinearLayout(activity)
        groups[1].background=StudioUi.shape(activity,StudioUi.surface,16)
        groups[1].setPadding(dp(6),dp(6),dp(6),dp(10))
        groups[1].addView(wheelSelectors)
        val wheelPanels=mutableListOf<LinearLayout>()
        val wheelButtons=mutableListOf<Button>()
        fun selectWheel(selected: Int) {
            wheelPanels.forEachIndexed { index,view -> view.visibility=if(index==selected) android.view.View.VISIBLE else android.view.View.GONE }
            wheelButtons.forEachIndexed { index,button ->
                button.isSelected=index==selected
                button.setTextColor(if(index==selected) StudioUi.accent else StudioUi.muted)
                button.background=StudioUi.shape(activity,if(index==selected) StudioUi.raised else StudioUi.surface,8)
            }
        }
        val wheelTitles=listOf(R.string.color_wheel_balance,R.string.color_wheel_lift,R.string.color_wheel_gamma,R.string.color_wheel_gain)
        wheelTitles.forEachIndexed { index,title ->
            val selector=action(title).apply { textSize=11f;setOnClickListener { selectWheel(index) } }
            wheelButtons.add(selector);wheelSelectors.addView(selector,LinearLayout.LayoutParams(0,dp(48),1f))
            val panel=LinearLayout(activity).apply { orientation=LinearLayout.VERTICAL }
            wheelPanels.add(panel);groups[1].addView(panel)
            val wheel=GradeColorWheel(activity).apply { contentDescription=AppLanguage.text(title) }
            wheels.add(wheel);panel.addView(wheel,LinearLayout.LayoutParams(-1,dp(180)))
            val readout=textLabel("",11f,true).apply { typeface=android.graphics.Typeface.MONOSPACE;gravity=android.view.Gravity.CENTER }
            wheelReadouts.add(readout);panel.addView(readout)
        }
        selectWheel(0)
        for(index in listOf(0,2)) {
            groups[index].background=StudioUi.shape(activity,StudioUi.surface,16)
            groups[index].setPadding(dp(4),dp(4),dp(4),dp(4))
        }
        values.forEachIndexed { index,value ->
            val row=LinearLayout(activity).apply {
                gravity=android.view.Gravity.CENTER_VERTICAL
                setPadding(dp(10),dp(2),dp(4),dp(2))
            }
            row.addView(textLabel(names[index].substringBefore('(').trim(),13f),LinearLayout.LayoutParams(0,-2,1f))
            val input=GradeDragValue(activity,scales[index]).apply { parameterName=names[index];bind(value) }
            inputs.add(input)
            row.addView(input,LinearLayout.LayoutParams(dp(78),dp(48)))
            val minus=action(R.string.color_fine_minus).apply {
                text="−";background=android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT);contentDescription=AppLanguage.text(R.string.color_step_down,names[index])
                setOnClickListener { input.step(-1) }
            }
            val plus=action(R.string.color_fine_plus).apply {
                text="+";background=android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT);contentDescription=AppLanguage.text(R.string.color_step_up,names[index])
                setOnClickListener { input.step(1) }
            }
            row.addView(minus,LinearLayout.LayoutParams(dp(48),dp(48)))
            row.addView(plus,LinearLayout.LayoutParams(dp(48),dp(48)))
            groups[if(index<5) 0 else 2].addView(row,LinearLayout.LayoutParams(-1,-2).apply { bottomMargin=dp(4) })
        }
        groups.forEach { controlBody.addView(it) };showGroup(1)
        fun labels() {
            inputs.forEachIndexed { index,input -> input.bind(values[index]) }
            val settings=ColorGrade.fromParametersV1(values.copyOf(),enabled.isChecked)
            wheels.forEachIndexed { group,wheel ->
                wheel.bind(ColorEngine.wheelPosition(settings,group))
                val offset=5+group*3
                val text=String.format(java.util.Locale.ROOT,"R %.2f  G %.2f  B %.2f",values[offset],values[offset+1],values[offset+2])
                if(wheelReadouts[group].text.toString()!=text) wheelReadouts[group].text=text
            }
        }
        labels()
        val dialog=AlertDialog.Builder(activity).setView(root)
            .setPositiveButton(AppLanguage.text(R.string.color_apply),null)
            .setNegativeButton(AppLanguage.text(R.string.color_cancel),null).create()
        var draftPlayer: AlertDialog?=null
        var hasReference=referenceFrame!=null
        var diagnosticsDialog: AlertDialog?=null
        var lastFailure: String?=null
        if(referenceFrame!=null) { preview.setImageBitmap(referenceFrame);empty.visibility=android.view.View.GONE }
        fun displayReference(frame: Bitmap) {
            hasReference=true;preview.setImageBitmap(frame);empty.visibility=android.view.View.GONE
            caption.text=AppLanguage.text(R.string.color_frame_reference)
        }
        fun showFailure(error: Exception, previewRelated: Boolean=true) {
            statusRow.visibility=android.view.View.VISIBLE
            lastFailure=ColorPreviewFailure.describe(error)
            status.text=AppLanguage.text(if(previewRelated) R.string.color_preview_unavailable else R.string.color_error,lastFailure)
            details.visibility=android.view.View.VISIBLE
            if(previewRelated) loading.visibility=android.view.View.GONE
            if(previewRelated && !hasReference) { empty.visibility=android.view.View.VISIBLE;emptyText.text=AppLanguage.text(R.string.color_frame_empty) }
        }
        details.setOnClickListener {
            diagnosticsDialog?.dismiss()
            diagnosticsDialog=AlertDialog.Builder(activity).setTitle(AppLanguage.text(R.string.color_error_details))
                .setMessage(lastFailure ?: status.text).setPositiveButton(AppLanguage.text(R.string.color_details_close),null).show()
            diagnosticsDialog?.findViewById<TextView>(android.R.id.message)?.setTextIsSelectable(true)
        }
        var inline: InlineGradePreview?=null
        var inlineSource: VideoSourceMetadata?=null
        var probedSource: VideoSourceMetadata?=null
        var interpretMissing=false
        fun videoCaption(before: Boolean)=AppLanguage.text(if(before && interpretMissing) R.string.color_assumed_before else if(before) R.string.color_frame_before else if(interpretMissing) R.string.color_assumed_frame else R.string.color_inline_frame)
        var original: Bitmap?=null
        var graded: Bitmap?=null
        var source: IntArray?=null // extraction/render worker only
        var revision=0L
        var busy=false
        var pending=false
        var presentedRevision=-1L
        val pulse=PreviewPulse()
        fun draft()=ColorGrade.fromParametersV1(values.copyOf(),enabled.isChecked)
        fun validatedDraft(): ColorGrade {
            val settings=draft()
            ColorEngine.shaderSources(settings,ColorEngine.TextureEncoding.MEDIA3_SDR_709)
            return settings
        }
        lateinit var debounced: Runnable
        fun updateButtons() { render.isEnabled=(original!=null || inlineSource!=null) && !busy }
        fun startInline(metadata: VideoSourceMetadata) {
            if(closed.get()) return
            revision++;original=null
            inlineSource=metadata;probedSource=metadata
            videoTexture.alpha=0f;preview.visibility=android.view.View.VISIBLE
            caption.text=AppLanguage.text(if(interpretMissing) R.string.color_assumed_frame else R.string.color_frame_loading)
            inputColor.isEnabled=PreviewColorPolicy.canInterpret(metadata.color)
            statusRow.visibility=android.view.View.VISIBLE
            compare.isEnabled=false
            inline?.close()
            val controller=InlineGradePreview(activity,videoTexture,clip,metadata,positionMs,{ frameBefore,waiting ->
                if(!closed.get()) {
                    videoTexture.alpha=1f
                    preview.visibility=android.view.View.INVISIBLE;empty.visibility=android.view.View.GONE
                    compare.isEnabled=true;lastFailure=null;details.visibility=android.view.View.GONE
                    caption.text=if(waiting && !interpretMissing) AppLanguage.text(R.string.color_frame_updating) else videoCaption(frameBefore)
                    status.text=AppLanguage.text(if(waiting) R.string.color_processing else R.string.color_inline_ready)
                    statusRow.visibility=android.view.View.GONE
                    live.text=AppLanguage.text(if(inline?.isPlaying==true) R.string.action_pause else R.string.color_live_short)
                    updateButtons()
                }
            },{ error ->
                inline?.close();inline=null
                videoTexture.alpha=0f
                caption.text=AppLanguage.text(if(hasReference) R.string.color_frame_reference else R.string.color_frame_empty)
                preview.visibility=android.view.View.VISIBLE;compare.isEnabled=false
                if(!closed.get()) { showFailure(error);updateButtons() }
            },interpretMissing)
            inline=controller
            status.text=AppLanguage.text(R.string.color_inline_loading)
            controller.update(draft(),compare.isChecked)
            updateButtons()
        }
        inputColor.setOnClickListener {
            val metadata=probedSource ?: return@setOnClickListener
            diagnosticsDialog?.dismiss()
            diagnosticsDialog=AlertDialog.Builder(activity).setTitle(AppLanguage.text(R.string.color_input_title))
                .setMessage(AppLanguage.text(R.string.color_input_warning)+"\n\n"+metadata.color.toString())
                .setPositiveButton(AppLanguage.text(R.string.color_input_assume)) { _,_ -> interpretMissing=true;startInline(metadata) }
                .setNeutralButton(AppLanguage.text(R.string.color_input_tagged)) { _,_ -> interpretMissing=false;startInline(metadata) }
                .setNegativeButton(AppLanguage.text(R.string.color_cancel),null).show()
        }
        fun requestRender() {
            if(pulse.request()) main.postDelayed(debounced,if(inline!=null) 66 else 80)
        }
        fun startRender() {
            if(closed.get()) return
            inline?.let { video ->
                try {
                    video.update(validatedDraft(),compare.isChecked)
                } catch(error: Exception) { showFailure(error) }
                return
            }
            val frame=original ?: return
            if(busy) { pending=true;return }
            try {
                val settings=validatedDraft()
                val captured=revision
                pending=false;busy=true;compare.isEnabled=graded!=null;updateButtons()
                lastFailure=null;details.visibility=android.view.View.GONE
                status.text=AppLanguage.text(R.string.color_processing)
                worker.execute {
                    try {
                        check(!closed.get())
                        val pixels=ColorFrameBuffers.render(source ?: error("Missing color frame"),frame.width,frame.height,settings)
                        check(!closed.get())
                        val result=Bitmap.createBitmap(pixels,frame.width,frame.height,Bitmap.Config.ARGB_8888)
                        main.post {
                            if(!closed.get()) {
                                busy=false
                                if(inline==null && original===frame && captured>=presentedRevision) {
                                    presentedRevision=captured
                                    graded=result;preview.setImageBitmap(if(compare.isChecked) original else result)
                                    compare.isEnabled=true
                                    status.text=AppLanguage.text(if(captured==revision) R.string.color_auto_preview_ready else R.string.color_draft_changed)
                                    caption.text=AppLanguage.text(if(compare.isChecked) R.string.color_frame_before else if(captured==revision) R.string.color_frame_graded else R.string.color_frame_updating)
                                    statusRow.visibility=android.view.View.GONE
                                }
                                updateButtons()
                                if(pending || captured!=revision) requestRender()
                            }
                        }
                    } catch(error: Exception) { main.post {
                        if(!closed.get()) {
                            busy=false;updateButtons()
                            if(captured==revision) showFailure(error)
                            if(pending || captured!=revision) requestRender()
                        }
                    } }
                }
            } catch(error: Exception) { showFailure(error) }
        }
        debounced=Runnable { if(pulse.take()) startRender() }
        fun changed() {
            revision++;if(inline==null) compare.isEnabled=graded!=null
            if(inline!=null || original!=null || lastFailure==null) status.text=AppLanguage.text(R.string.color_draft_changed)
            if(original!=null || inline!=null) caption.text=AppLanguage.text(if(interpretMissing) R.string.color_assumed_frame else R.string.color_frame_updating)
            requestRender()
        }
        fun parameterChanged() {
            // Editing is an After action, not a hidden edit behind comparison/bypass.
            if(compare.isChecked) compare.isChecked=false
            if(!enabled.isChecked) enabled.isChecked=true
            labels();changed()
        }
        inputs.forEachIndexed { index,input -> input.onValue={ value -> values[index]=value;parameterChanged() } }
        wheels.forEachIndexed { group,wheel ->
            var base=draft()
            wheel.onBegin={ base=draft() }
            wheel.onPoint={ x,y ->
                try {
                    ColorEngine.wheel(base,group,x,y).parametersV1().copyInto(values)
                    parameterChanged()
                } catch(error: Exception) { showFailure(error,false) }
            }
        }
        enabled.setOnCheckedChangeListener { _,_ -> changed() }
        reset.setOnClickListener {
            ColorGrade().parametersV1().copyInto(values)
            enabled.isChecked=true;labels();changed()
        }
        compare.setOnCheckedChangeListener { _,before ->
            if(inline!=null) { requestRender();return@setOnCheckedChangeListener }
            preview.setImageBitmap(if(before) original else graded ?: original)
            caption.text=AppLanguage.text(if(before) R.string.color_frame_before else R.string.color_frame_graded)
        }
        render.setOnClickListener {
            val metadata=inlineSource
            if(inline==null && metadata!=null) startInline(metadata) else requestRender()
        }
        live.setOnClickListener {
            try {
                val video=inline
                if(video!=null) {
                    video.togglePlaying()
                    live.text=AppLanguage.text(if(video.isPlaying) R.string.action_pause else R.string.color_live_short)
                } else {
                    val metadata=probedSource
                    if(metadata!=null) { startInline(metadata);inline?.togglePlaying() }
                    else { draftPlayer?.dismiss();draftPlayer=previewDraft?.invoke(validatedDraft()) }
                }
            }
            catch(error: Exception) { showFailure(error,false) }
        }
        dialog.setOnDismissListener {
            closed.set(true);pulse.close();main.removeCallbacks(debounced);draftPlayer?.dismiss();draftPlayer=null
            diagnosticsDialog?.dismiss();diagnosticsDialog=null
            inline?.close();inline=null
            worker.shutdownNow();preview.setImageDrawable(null);original=null;graded=null
        }
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                try {
                    commit(validatedDraft())
                    Toast.makeText(activity,AppLanguage.text(R.string.color_saved_notice),Toast.LENGTH_LONG).show()
                    dialog.dismiss()
                } catch(error: Exception) { showFailure(error,false) }
            }
        }
        // AlertDialog footer stays fixed; only the controls column is scrollable.
        root.setOnApplyWindowInsetsListener { view,insets ->
            if(Build.VERSION.SDK_INT>=30) {
                val bars=insets.getInsets(android.view.WindowInsets.Type.systemBars() or android.view.WindowInsets.Type.displayCutout())
                view.setPadding(dp(12)+bars.left,dp(8)+bars.top,dp(12)+bars.right,dp(8)+bars.bottom)
            }
            insets
        }
        dialog.show()
        dialog.window?.apply {
            setBackgroundDrawable(android.graphics.drawable.ColorDrawable(StudioUi.background))
            setLayout(android.view.ViewGroup.LayoutParams.MATCH_PARENT,android.view.ViewGroup.LayoutParams.MATCH_PARENT)
        }
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).apply { setTextColor(StudioUi.accent);textSize=15f }
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setTextColor(StudioUi.muted)
        root.requestApplyInsets()
        worker.execute {
            var metadataForVideo: VideoSourceMetadata?=null
            try {
                val metadata=VideoSourceProbe.read(activity.applicationContext,clip.mediaId) { check(!closed.get()) }
                metadataForVideo=metadata
                main.post { if(!closed.get()) {
                    probedSource=metadata;live.isEnabled=true;inputColor.isEnabled=PreviewColorPolicy.canInterpret(metadata.color)
                } }
                check(!closed.get())
                val requested=PreviewFrameSize.fit(metadata.width,metadata.height,metadata.rotationDegrees)
                if(Build.VERSION.SDK_INT<27) require(PreviewFrameSize.fitsUnscaled(metadata.width,metadata.height,metadata.rotationDegrees)) {
                    AppLanguage.text(R.string.color_scaled_preview_requires_27)
                }
                val retriever=MediaMetadataRetriever()
                val frame=try {
                    retriever.setDataSource(activity.applicationContext,Uri.parse(clip.mediaId))
                    check(!closed.get())
                    val timeUs=Math.multiplyExact(positionMs,1000L)
                    val decoded=requireNotNull(if(Build.VERSION.SDK_INT>=27)
                        retriever.getScaledFrameAtTime(timeUs,MediaMetadataRetriever.OPTION_CLOSEST,requested.width,requested.height)
                        else retriever.getFrameAtTime(timeUs,MediaMetadataRetriever.OPTION_CLOSEST)) {
                            "Platform still retriever returned no frame; video-decoder preview required"
                        }
                    check(!closed.get())
                    require(decoded.width<=PreviewFrameSize.MAX_WIDTH && decoded.height<=PreviewFrameSize.MAX_HEIGHT) {
                        "Decoder returned a bitmap beyond the requested preview budget"
                    }
                    decoded
                } finally { retriever.release() }
                check(!closed.get())
                main.post { if(!closed.get() && inline==null) displayReference(frame) }
                // A platform reference can be shown even when grading admission fails.
                // Never feed this (or the supplied timeline capture) into native grading
                // until source/bitmap color provenance is explicitly admitted.
                ColorEngine.validateVideoSource(metadata.color)
                check(!closed.get())
                require(frame.colorSpace?.isSrgb == true && frame.config==Bitmap.Config.ARGB_8888) { "Still grading requires an sRGB ARGB8888 bitmap" }
                val pixels=IntArray(frame.width*frame.height)
                frame.getPixels(pixels,0,frame.width,0,0,frame.width,frame.height) // straight ARGB sRGB ints
                source=pixels
                main.post { if(!closed.get() && inline==null) { original=frame;preview.setImageBitmap(frame);startRender() } }
            } catch(error: Exception) {
                val metadata=metadataForVideo
                main.post { if(!closed.get() && inline==null) {
                    if(metadata!=null) {
                        statusRow.visibility=android.view.View.VISIBLE
                        lastFailure=ColorPreviewFailure.describe(error)
                        startInline(metadata)
                    } else showFailure(error)
                } }
            }
        }
        return dialog
    }
}
