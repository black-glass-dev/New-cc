package com.owncut.app

import android.app.AlertDialog
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.EditText
import android.widget.SeekBar
import androidx.media3.common.util.UnstableApi
import androidx.test.core.app.ActivityScenario
import org.junit.Assert.*
import org.junit.Test

/** Source compiled by CI; actual UI execution is an opt-in device check, not host proof. */
@UnstableApi
class ColorEditorInteractionTest {
    private fun children(view: View): List<View> = listOf(view)+if(view is ViewGroup)
        (0 until view.childCount).flatMap { children(view.getChildAt(it)) } else emptyList()
    private val missing=ClipSnapshot("test","v1","file:///owncut-nonexistent-test-source.mp4",0,30,0,1000)

    @Test fun closeCallbackFollowsCommitAndAlsoRunsOnCancel() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            val events=mutableListOf<String>() // accessed only on Activity main thread
            scenario.onActivity { activity ->
                val applied=ClipColorEditor.show(activity,missing,0,
                    onClosed={ events.add("closed") }) { events.add("committed") }
                applied.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
            }
            androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().waitForIdleSync()
            scenario.onActivity { activity ->
                assertEquals(listOf("committed","closed"),events)
                val cancelled=ClipColorEditor.show(activity,missing,0,
                    onClosed={ events.add("cancelled-close") }) { error("cancel must not commit") }
                cancelled.dismiss()
            }
            androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().waitForIdleSync()
            scenario.onActivity { assertEquals(listOf("committed","closed","cancelled-close"),events) }
        }
    }

    @Test fun dragControlsAndApplyDoNotRequireAWorkingStillDecoder() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                var saved: ColorGrade?=null
                val dialog=ClipColorEditor.show(activity,missing,0) { saved=it }
                try {
                    val views=children(requireNotNull(dialog.window).decorView)
                    assertTrue(views.none { it is EditText })
                    assertTrue(views.none { it is SeekBar })
                    assertEquals(4,views.filterIsInstance<GradeColorWheel>().size)
                    assertEquals(1,views.filterIsInstance<android.widget.Button>().count {
                        it.text==AppLanguage.text(R.string.color_group_channels)
                    })
                    val sliders=views.filterIsInstance<GradeDragValue>()
                    assertEquals(17,sliders.size)
                    val argument=Bundle().apply { putFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE,2f) }
                    assertTrue(sliders.first().performAccessibilityAction(
                        AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.id,argument))
                    assertTrue(dialog.getButton(AlertDialog.BUTTON_POSITIVE).isEnabled)
                    dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                    assertEquals(2.0,requireNotNull(saved).exposureStops,0.000001)
                    assertFalse(dialog.isShowing)
                } finally { dialog.dismiss() }
            }
        }
    }

    @Test fun referenceIsImmediatelyVisibleOutsideControlsScrollerAndFineStepsSave() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val reference=android.graphics.Bitmap.createBitmap(64,36,android.graphics.Bitmap.Config.ARGB_8888)
                reference.eraseColor(android.graphics.Color.DKGRAY)
                var saved: ColorGrade?=null
                val dialog=ClipColorEditor.show(activity,missing,0,referenceFrame=reference) { saved=it }
                try {
                    val views=children(requireNotNull(dialog.window).decorView)
                    val image=views.filterIsInstance<android.widget.ImageView>().single {
                        it.contentDescription==AppLanguage.text(R.string.color_frame_viewer)
                    }
                    assertSame(reference,(image.drawable as android.graphics.drawable.BitmapDrawable).bitmap)
                    var parent=image.parent
                    while(parent is View) {
                        assertFalse("Viewer must not scroll with controls",parent is android.widget.ScrollView)
                        parent=parent.parent
                    }
                    assertTrue(views.none { it is SeekBar })
                    val sliders=views.filterIsInstance<GradeDragValue>()
                    assertEquals(17,sliders.size)
                    sliders.first().step(1)
                    dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                    assertEquals(0.004,requireNotNull(saved).exposureStops,0.000001)
                } finally { dialog.dismiss() }
            }
        }
    }


    @Test fun wheelTapDoesNotChangeGradeAndCancelStopsTransport() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val wheel=GradeColorWheel(activity)
                wheel.layout(0,0,240,240)
                wheel.bind(doubleArrayOf(0.2,-0.1))
                var changes=0
                wheel.onPoint={ _,_ -> changes++ }
                fun event(action: Int,x: Float,y: Float) {
                    val value=android.view.MotionEvent.obtain(0,0,action,x,y,0)
                    try { wheel.onTouchEvent(value) } finally { value.recycle() }
                }
                event(android.view.MotionEvent.ACTION_DOWN,150f,120f)
                event(android.view.MotionEvent.ACTION_UP,150f,120f)
                assertEquals(0,changes)
                event(android.view.MotionEvent.ACTION_DOWN,120f,120f)
                event(android.view.MotionEvent.ACTION_MOVE,130f,120f)
                assertEquals(1,changes)
                event(android.view.MotionEvent.ACTION_CANCEL,130f,120f)
                event(android.view.MotionEvent.ACTION_MOVE,160f,120f)
                assertEquals(1,changes)
            }
        }
    }

    @Test fun primaryDragCancelRejectsFurtherMotion() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val input=GradeDragValue(activity,ColorSliderScale.forParameter(0,0.0))
                input.layout(0,0,200,48);input.bind(0.0)
                var changes=0
                input.onValue={ changes++ }
                fun send(action: Int,x: Float) {
                    val event=android.view.MotionEvent.obtain(0,0,action,x,20f,0)
                    try { input.onTouchEvent(event) } finally { event.recycle() }
                }
                send(android.view.MotionEvent.ACTION_DOWN,20f)
                send(android.view.MotionEvent.ACTION_MOVE,100f)
                assertEquals(1,changes)
                send(android.view.MotionEvent.ACTION_CANCEL,100f)
                send(android.view.MotionEvent.ACTION_MOVE,180f)
                assertEquals(1,changes)
            }
        }
    }

    @Test fun focusedWheelSelectorShowsOneWheelAndEditsLeaveBefore() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val dialog=ClipColorEditor.show(activity,missing,0) { }
                try {
                    val views=children(requireNotNull(dialog.window).decorView)
                    val wheels=views.filterIsInstance<GradeColorWheel>()
                    assertEquals(4,wheels.size)
                    assertEquals(1,wheels.count { it.isShown })
                    views.filterIsInstance<android.widget.Button>().single {
                        it.text==AppLanguage.text(R.string.color_wheel_gain)
                    }.performClick()
                    assertTrue(wheels[3].isShown)
                    assertEquals(1,wheels.count { it.isShown })
                    val compare=views.filterIsInstance<android.widget.ToggleButton>().single()
                    compare.isChecked=true
                    views.filterIsInstance<GradeDragValue>().first().step(1)
                    assertFalse(compare.isChecked)
                } finally { dialog.dismiss() }
            }
        }
    }

    @Test fun liveRecipeChangesKeepTheEffectAndProcessorIdentity() {
        val initial=ColorEngine.shaderSources(ColorGrade(),ColorEngine.TextureEncoding.MEDIA3_SDR_709)
        val live=GradedFramePipeline.LivePrepared(initial) { }
        val effect=live.prepared.effect
        val factory=live.prepared.factory
        live.update(ColorEngine.shaderSources(ColorGrade(exposureStops=1.0),ColorEngine.TextureEncoding.MEDIA3_SDR_709)) { }
        assertSame(effect,live.prepared.effect)
        assertSame(factory,live.prepared.factory)
    }

    @Test fun failedCommitDoesNotPretendToSaveOrDismiss() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val dialog=ClipColorEditor.show(activity,missing,0) { error("mock commit rejected") }
                try {
                    dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
                    assertTrue(dialog.isShowing)
                    assertTrue(children(requireNotNull(dialog.window).decorView)
                        .filterIsInstance<android.widget.TextView>().any { it.text.contains("mock commit rejected") })
                } finally { dialog.dismiss() }
            }
        }
    }
}
