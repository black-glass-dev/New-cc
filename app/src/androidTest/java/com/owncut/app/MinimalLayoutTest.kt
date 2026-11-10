package com.owncut.app

import android.content.Context
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test

class MinimalLayoutTest {
    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun children(view: View): List<View> = listOf(view) + if(view is ViewGroup) (0 until view.childCount).flatMap { children(view.getChildAt(it)) } else emptyList()
    @Test fun quietEditAndContextualToolsKeepViewerAndTimelineAcrossThreeThemes() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val prefs = context.getSharedPreferences("owncut_preferences",Context.MODE_PRIVATE)
        val previous = prefs.getString("theme",null)
        try {
            for(theme in listOf("graphite","midnight","ember")) {
                StudioUi.select(context,theme)
                assertEquals(theme,StudioUi.theme(context))
                ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                    scenario.onActivity { activity ->
                        val preview = field("preview").get(activity) as View
                        val timeline = field("timelineContainer").get(activity) as View
                        val metadata = field("mediaText").get(activity) as TextView
                        val sourceBar = field("seekBar").get(activity) as View
                        val status = field("statusText").get(activity) as TextView
                        assertFalse(metadata.isShown); assertFalse(sourceBar.isShown); assertFalse(status.isShown)
                        val buttons = children(activity.findViewById(android.R.id.content)).filterIsInstance<StudioButton>().filter { it.isShown }
                        assertFalse(buttons.any { it.icon == StudioIcon.SAVE || it.icon == StudioIcon.TRIM_LEFT || it.icon == StudioIcon.RECOVER })
                        assertEquals(1, buttons.count { it.icon == StudioIcon.IMPORT })
                        assertEquals(1, buttons.count { it.icon == StudioIcon.PLAY })
                        fun page(value: String) = MainActivity::class.java.getDeclaredMethod("showWorkspacePage",String::class.java).apply { isAccessible = true }.invoke(activity,value)
                        page("Media"); assertTrue(metadata.isShown); assertTrue(sourceBar.isShown)
                        page("Tools"); assertFalse(metadata.isShown)
                        assertTrue(children(activity.findViewById(android.R.id.content)).filterIsInstance<StudioButton>().any { it.icon == StudioIcon.TRIM_LEFT && it.isShown })
                        page("Color")
                        val colorActions=children(activity.findViewById(android.R.id.content))
                            .filterIsInstance<android.widget.Button>()
                            .filter { it !is StudioButton }
                        for(label in listOf(R.string.color_editor_title,R.string.color_live_title)) {
                            val button=colorActions.single { it.text.toString()==AppLanguage.text(label) }
                            assertTrue("Color action must be attached and shown",button.isAttachedToWindow && button.isShown)
                        }
                        page("Edit")
                        assertTrue(colorActions.none { it.isShown })
                        assertSame(preview,field("preview").get(activity)); assertSame(timeline,field("timelineContainer").get(activity))
                    }
                    scenario.recreate()
                    scenario.onActivity { assertEquals(theme,StudioUi.theme(it)); assertFalse((field("mediaText").get(it) as View).isShown) }
                }
            }
        } finally {
            prefs.edit().apply { if(previous == null) remove("theme") else putString("theme",previous) }.commit()
            StudioUi.initialize(context)
        }
    }
    @Test fun quietStatusDoesNotSuppressImportantFailures() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().runOnMainSync {
            val messages = mutableListOf<String>()
            val status = EditorStatusView(context).apply { onImportant = { messages += it } }
            status.text = "A video frame has reached the preview"
            assertTrue(messages.isEmpty())
            status.text = "Could not open source"
            status.text = "Could not open source"
            assertEquals(1,messages.size)
            status.text = "Project saved"
            status.text = "Could not open source"
            assertEquals(2,messages.size)
            status.text = "ভিডিও decode হয়নি"
            assertEquals(3,messages.size)
        }
    }
}
