package com.owncut.app

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import java.util.Locale

/** App-owned language on API 26+, independent of the device language. No editor data is rewritten. */
internal object AppLanguage {
    private const val PREFS = "owncut_preferences"
    private const val KEY = "language"
    @Volatile private var selected = "en"
    @Volatile private var localizedResources: Resources? = null
    fun hasChoice(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).contains(KEY)
    fun code(context: Context): String = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        .getString(KEY, "en").let { if (it == "bn") "bn" else "en" }
    fun wrap(context: Context): Context {
        val code = code(context)
        val config = Configuration(context.resources.configuration)
        config.setLocale(Locale.forLanguageTag(code))
        return context.createConfigurationContext(config)
    }
    fun initialize(context: Context) {
        selected = code(context)
        localizedResources = wrap(context).resources
    }
    fun select(context: Context, code: String) {
        require(code == "en" || code == "bn")
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY, code).apply()
        initialize(context.applicationContext)
    }
    fun locale(): Locale = Locale.forLanguageTag(selected)
    fun text(id: Int, vararg args: Any?): String {
        val resources = checkNotNull(localizedResources) { "Application language not initialized" }
        return if (args.isEmpty()) resources.getString(id) else resources.getString(id, *args)
    }
}

class OwncutApplication : Application() {
    override fun onCreate() { super.onCreate(); AppLanguage.initialize(this); StudioUi.initialize(this) }
    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig); AppLanguage.initialize(this)
    }
}

open class LocalizedActivity : Activity() {
    override fun onCreate(savedInstanceState: android.os.Bundle?) {
        setTheme(when(StudioUi.theme(this)) { "ember" -> R.style.AppThemeEmber; "midnight" -> R.style.AppThemeMidnight; else -> R.style.AppTheme })
        super.onCreate(savedInstanceState)
        window.statusBarColor = StudioUi.background
        window.navigationBarColor = StudioUi.background
    }
    override fun attachBaseContext(newBase: Context) { super.attachBaseContext(AppLanguage.wrap(newBase)) }
}
