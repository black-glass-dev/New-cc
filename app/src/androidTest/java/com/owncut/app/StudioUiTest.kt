package com.owncut.app

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Test

class StudioUiTest {
    private fun views(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { views(view.getChildAt(it)) } else emptyList()
    private fun root(activity: android.app.Activity) = activity.findViewById<View>(android.R.id.content)
    private fun waitFor(condition: () -> Boolean) {
        val deadline = System.currentTimeMillis() + 15000
        while (System.currentTimeMillis() < deadline) { if (condition()) return; Thread.sleep(50) }
        fail("Timed out waiting for localized Activity")
    }
    @Test fun firstRunChoicePersistsAndHomeSettingsCanChangeIt() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val prefs = context.getSharedPreferences("owncut_preferences", Context.MODE_PRIVATE)
        val previous = prefs.getString("language", null)
        prefs.edit().remove("language").commit(); AppLanguage.initialize(context)
        try {
            ActivityScenario.launch(ProjectLibraryActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    assertFalse(AppLanguage.hasChoice(activity))
                    val english = views(root(activity)).filterIsInstance<Button>().single { it.text.toString() == "English" }
                    val bengali = views(root(activity)).filterIsInstance<Button>().single { it.text.toString() == "বাংলা" }
                    assertTrue(english.isShown); bengali.performClick()
                }
                waitFor { var ready = false; scenario.onActivity { ready = it.resources.configuration.locales[0].language == "bn" }; ready }
                scenario.onActivity { activity ->
                    assertTrue(AppLanguage.hasChoice(activity)); assertEquals("bn", AppLanguage.code(activity))
                    assertEquals("সম্পাদনা", activity.getString(R.string.page_edit))
                }
                scenario.recreate()
                scenario.onActivity { assertEquals("bn", it.resources.configuration.locales[0].language) }
                // Invoke the exact Settings action; choose English in its real platform dialog.
                scenario.onActivity { activity ->
                    ProjectLibraryActivity::class.java.getDeclaredMethod("showSettings").apply { isAccessible = true }.invoke(activity)
                }
                scenario.onActivity { activity ->
                    val dialog = ProjectLibraryActivity::class.java.getDeclaredField("languageDialog").apply { isAccessible = true }.get(activity) as android.app.AlertDialog
                    assertTrue(dialog.isShowing)
                    val list = dialog.listView
                    assertTrue(list.performItemClick(list.getChildAt(0), 0, list.adapter.getItemId(0)))
                }
                waitFor { var ready = false; scenario.onActivity { ready = it.resources.configuration.locales[0].language == "en" }; ready }
                scenario.onActivity { assertEquals("en", AppLanguage.code(it)); assertEquals("Edit", it.getString(R.string.page_edit)) }
            }
        } finally {
            prefs.edit().apply { if(previous == null) remove("language") else putString("language",previous) }.commit()
            AppLanguage.initialize(context)
        }
    }
    @Test fun editorUsesAccessibleIconActionsInBothLanguagesWithoutChangingSession() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val prefs = context.getSharedPreferences("owncut_preferences", Context.MODE_PRIVATE)
        val previous = prefs.getString("language", null)
        try {
            for (language in listOf("en", "bn")) {
                AppLanguage.select(context, language)
                ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                    scenario.onActivity { activity ->
                        val buttons = views(root(activity)).filterIsInstance<StudioButton>()
                        assertTrue(buttons.size >= 18)
                        assertTrue(buttons.all { !it.contentDescription.isNullOrBlank() && !it.tooltipText.isNullOrBlank() })
                        assertTrue(buttons.all { it.minimumWidth >= StudioUi.dp(activity,48) && it.minimumHeight >= StudioUi.dp(activity,48) })
                        val split = buttons.single { it.icon == StudioIcon.SPLIT }
                        assertEquals(if(language == "bn") "কাটুন (Split)" else "Split", split.contentDescription.toString())
                        val a = StudioButton(activity).apply { icon = StudioIcon.SPLIT; text = "A" }
                        val b = StudioButton(activity).apply { icon = StudioIcon.SPLIT; text = "A much longer semantic label" }
                        fun bitmap(button: StudioButton): Bitmap {
                            val size = StudioUi.dp(activity,52)
                            button.measure(View.MeasureSpec.makeMeasureSpec(size,View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(size,View.MeasureSpec.EXACTLY))
                            button.layout(0,0,size,size)
                            return Bitmap.createBitmap(size,size,Bitmap.Config.ARGB_8888).also { button.draw(Canvas(it)) }
                        }
                        assertTrue("Semantic labels must not be rendered as text", bitmap(a).sameAs(bitmap(b)))
                        val engine = MainActivity::class.java.getDeclaredField("engine").apply { isAccessible = true }.get(activity) as EngineSession
                        val before = engine.clips()
                        for (page in listOf("Media","Flow","Deliver","Edit"))
                            MainActivity::class.java.getDeclaredMethod("showWorkspacePage",String::class.java).apply { isAccessible = true }.invoke(activity,page)
                        assertEquals(before,engine.clips())
                    }
                }
            }
        } finally {
            prefs.edit().apply { if(previous == null) remove("language") else putString("language",previous) }.commit()
            AppLanguage.initialize(context)
        }
    }
}
