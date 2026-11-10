package com.owncut.app

import android.view.TextureView
import android.view.View
import android.view.ViewGroup
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MainActivityLaunchTest {
    @Test
    fun editorOpensAndCreatesPreviewTextureView() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { launchedActivity ->
                check(!launchedActivity.isFinishing) { "MainActivity should remain open after launch" }
                val preview = findView(launchedActivity.window.decorView)
                    ?: throw AssertionError("Preview TextureView is missing from the initial workspace")
                check(preview.surfaceTextureListener === launchedActivity) {
                    "Activity is not connected to the preview surface callbacks"
                }
            }
        }
    }

    private fun findView(root: View): TextureView? {
        if (root is TextureView) return root
        if (root is ViewGroup) {
            for (index in 0 until root.childCount) {
                val found = findView(root.getChildAt(index))
                if (found != null) return found
            }
        }
        return null
    }
}
