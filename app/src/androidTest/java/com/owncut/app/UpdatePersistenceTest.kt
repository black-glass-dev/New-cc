package com.owncut.app

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/** Invoked explicitly before/after adb install -r by CI; excluded from normal suite via assume. */
class UpdatePersistenceTest {
    private fun context() = ApplicationProvider.getApplicationContext<Context>()
    private fun selected(method: String) = InstrumentationRegistry.getArguments().getString("owncutUpgradeStage") == method
    @Test fun seed() {
        assumeTrue(selected("seed"))
        val context = context()
        EngineSession().use {
            val clip = it.addMediaClip("content://owncut/upgrade-fixture",4000)
            it.splitClip(clip,60,"upgrade-right")
            it.saveProject(File(context.filesDir,"upgrade-fixture.owncut").absolutePath)
        }
        assertTrue(context.getSharedPreferences("owncut_preferences",Context.MODE_PRIVATE).edit().putString("language","bn").putString("theme","ember").putInt("schema",1).commit())
    }
    @Test fun verify() {
        assumeTrue(selected("verify"))
        val context = context()
        EngineSession().use {
            it.loadProject(File(context.filesDir,"upgrade-fixture.owncut").absolutePath)
            assertEquals(2,it.clips().size)
            assertEquals(60L,it.clips().single { clip -> clip.id == "upgrade-right" }.startFrame)
        }
        val prefs = context.getSharedPreferences("owncut_preferences",Context.MODE_PRIVATE)
        assertEquals("bn",AppLanguage.code(context)); assertEquals("ember",StudioUi.theme(context))
        assertEquals("bn",prefs.getString("language",null)); assertEquals("ember",prefs.getString("theme",null)); assertEquals(1,prefs.getInt("schema",0))
    }
}
