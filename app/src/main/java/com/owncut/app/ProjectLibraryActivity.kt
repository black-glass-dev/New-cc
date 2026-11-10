package com.owncut.app

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.text.DateFormat
import java.util.Date
import java.util.concurrent.Executors

/** Launcher/project shelf. No native editing session or decoder survives behind the editor. */
class ProjectLibraryActivity : LocalizedActivity() {
    private val worker = Executors.newSingleThreadExecutor()
    private lateinit var library: ProjectLibrary
    private lateinit var body: LinearLayout
    private lateinit var status: TextView
    private var languageDialog: AlertDialog? = null
    private var busy = false
    private var nameDialogOpen = false
    private var listing = LibraryListing(emptyList(), emptyList())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        library = ProjectLibrary(filesDir)
        body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(18), dp(18), dp(24))
            setBackgroundColor(StudioUi.background)
            fitsSystemWindows = true
        }
        setContentView(ScrollView(this).apply {
            setBackgroundColor(StudioUi.background)
            addView(body)
            setOnApplyWindowInsetsListener { view, insets ->
                @Suppress("DEPRECATION")
                if (android.os.Build.VERSION.SDK_INT >= 30) {
                    val bars = insets.getInsets(android.view.WindowInsets.Type.systemBars() or android.view.WindowInsets.Type.displayCutout())
                    view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                } else {
                    @Suppress("DEPRECATION")
                    view.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
                }
                insets
            }
        })
        if (AppLanguage.hasChoice(this)) render() else renderLanguageChoice()
    }
    override fun onResume() { super.onResume(); if (AppLanguage.hasChoice(this) && !nameDialogOpen) refresh() }

    private fun renderLanguageChoice() {
        body.removeAllViews()
        label("OWNCUT", 32f)
        label(AppLanguage.text(R.string.message_0d444672e0), 26f)
        label(AppLanguage.text(R.string.message_b4ce151ed6), 15f)
        for ((code, title) in listOf("en" to "English", "bn" to AppLanguage.text(R.string.message_a821f95ecf))) {
            body.addView(Button(this).apply {
                text = title; isAllCaps = false; textSize = 20f
                setTextColor(StudioUi.text)
                background = StudioUi.shape(this@ProjectLibraryActivity, StudioUi.raised)
                setOnClickListener { AppLanguage.select(this@ProjectLibraryActivity, code); recreate() }
            }, LinearLayout.LayoutParams(-1, dp(72)).apply { topMargin = dp(16) })
        }
    }

    private fun showPreferences() {
        AlertDialog.Builder(this).setTitle(AppLanguage.text(R.string.settings))
            .setItems(arrayOf(AppLanguage.text(R.string.language),AppLanguage.text(R.string.appearance),AppLanguage.text(R.string.message_0a0b1b77c1))) { _, index ->
                when(index) { 0 -> showSettings(); 1 -> showThemes(); else -> refresh() }
            }.show()
    }
    private fun showThemes() {
        val ids = listOf("graphite","midnight","ember")
        AlertDialog.Builder(this).setTitle(AppLanguage.text(R.string.appearance))
            .setSingleChoiceItems(arrayOf(AppLanguage.text(R.string.theme_graphite),AppLanguage.text(R.string.theme_midnight),AppLanguage.text(R.string.theme_ember)),ids.indexOf(StudioUi.theme(this))) { dialog,index ->
                dialog.dismiss(); StudioUi.select(this,ids[index]); recreate()
            }.setNegativeButton(AppLanguage.text(R.string.message_ca21daf138),null).show()
    }

    private fun showSettings() {
        if (languageDialog?.isShowing == true) return
        val dialog = AlertDialog.Builder(this).setTitle(AppLanguage.text(R.string.message_d10c8a8dd8))
            .setSingleChoiceItems(arrayOf("English", AppLanguage.text(R.string.message_a821f95ecf)), if (AppLanguage.code(this) == "bn") 1 else 0) { dialog, which ->
                dialog.dismiss()
                AppLanguage.select(this, if (which == 1) "bn" else "en")
                recreate()
            }.setNegativeButton(AppLanguage.text(R.string.message_ca21daf138), null).create()
        languageDialog = dialog
        dialog.setOnDismissListener { languageDialog = null }
        dialog.show()
    }

    private fun refresh() = task(AppLanguage.text(R.string.message_3ea73f3e83)) {
        val migrationWarning = try { library.migrateLegacy(); emptyList() }
            catch (error: Exception) { listOf(AppLanguage.text(R.string.message_d2046a8d8f, error.message)) }
        val result = library.list()
        val next = result.copy(warnings = migrationWarning + result.warnings)
        ui { listing = next; render() }
    }
    private fun render() {
        body.removeAllViews()
        val heading = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = android.view.Gravity.CENTER_VERTICAL }
        heading.addView(StudioUi.label(this, "OWNCUT", 28f).apply {
            setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL))
        }, LinearLayout.LayoutParams(0, -2, 1f))
        heading.addView(StudioButton(this).apply { icon = StudioIcon.SETTINGS; text = AppLanguage.text(R.string.message_d10c8a8dd8); setOnClickListener { if (!busy) showPreferences() } }, LinearLayout.LayoutParams(dp(52), dp(52)))
        heading.addView(StudioButton(this).apply { icon = StudioIcon.INFO; text = AppLanguage.text(R.string.message_e5c32c0cb4); setOnClickListener { showCapabilities() } },LinearLayout.LayoutParams(dp(52),dp(52)))
        body.addView(heading)
        label(AppLanguage.text(R.string.message_f4693deac0), 28f)
        label(AppLanguage.text(R.string.message_2cd5fdd10c), 15f)
        button(AppLanguage.text(R.string.message_871c85ca8f), StudioIcon.PLUS) { askName(AppLanguage.text(R.string.message_871c85ca8f), "") { name ->
            task(AppLanguage.text(R.string.message_521ec4f4e0)) { val project = library.create(name); ui { open(project) } }
        } }
        button(AppLanguage.text(R.string.message_5afa17a3d6), StudioIcon.IMPORT) {
            if (!busy) {
                @Suppress("DEPRECATION")
                startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE)
                }, IMPORT)
            }
        }
        status = label(if (busy) AppLanguage.text(R.string.message_8c5e3ee801) else AppLanguage.text(R.string.message_ef9732b90d, listing.projects.size), 13f)
        if (listing.projects.isEmpty()) label(AppLanguage.text(R.string.message_49396f3024), 16f)
        if (listing.projects.isNotEmpty()) label(AppLanguage.text(R.string.recent_projects), 19f)
        for (project in listing.projects) {
            val card = StudioUi.card(this)
            card.addView(StudioUi.label(this, project.name, 21f))
            val date = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT, AppLanguage.locale()).format(Date(project.updatedAt))
            card.addView(StudioUi.label(this, AppLanguage.text(R.string.message_6b104d83c9, date), 12f, true))
            if (project.recovery) card.addView(StudioUi.label(this, AppLanguage.text(R.string.message_4cebb485c3), 13f, true))
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = android.view.Gravity.END }
            row.addView(StudioButton(this).apply { icon = StudioIcon.OPEN; text = AppLanguage.text(R.string.message_79e2cae0eb, project.name); setOnClickListener { if (!busy) open(project) } }, LinearLayout.LayoutParams(dp(52), dp(52)))
            row.addView(StudioButton(this).apply { icon = StudioIcon.RENAME; text = AppLanguage.text(R.string.message_0ec264cf76, project.name); setOnClickListener {
                askName(AppLanguage.text(R.string.message_2c34f0e27b), project.name) { name ->
                    task(AppLanguage.text(R.string.message_8a2e50b708)) {
                        library.rename(project.id, name)
                        val next = library.list()
                        ui { listing = next; render() }
                    }
                }
            } }, LinearLayout.LayoutParams(dp(52), dp(52)).apply { leftMargin = dp(8) })
            card.addView(row)
            body.addView(card, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) })
        }
        for(id in listing.repairable) button(AppLanguage.text(R.string.repair_catalog, id.take(8)),StudioIcon.RECOVER) {
            askName(AppLanguage.text(R.string.repair_catalog_title), "") { name ->
                task(AppLanguage.text(R.string.repair_catalog_title)) {
                    library.repair(id,name)
                    val next = library.list()
                    ui { listing = next; render() }
                }
            }
        }
        listing.warnings.forEach { label(it, 12f) }
    }
    private fun showCapabilities() {
            AlertDialog.Builder(this).setTitle(AppLanguage.text(R.string.message_c82ca42c8d))
                .setMessage(AppLanguage.text(R.string.message_35d6f4944d))
                .setPositiveButton(AppLanguage.text(R.string.message_b7722d9327), null).show()
    }
    private fun open(project: LibraryProject) {
        startActivity(Intent(this, MainActivity::class.java).putExtra(MainActivity.EXTRA_LIBRARY_ID, project.id))
    }
    private fun askName(title: String, initial: String, action: (String) -> Unit) {
        if (busy) return
        val input = EditText(this).apply { setSingleLine(); setText(initial); hint = AppLanguage.text(R.string.message_c684969e0e) }
        nameDialogOpen = true
        val dialog = AlertDialog.Builder(this).setTitle(title).setView(input)
            .setNegativeButton(AppLanguage.text(R.string.message_ca21daf138), null).setPositiveButton(AppLanguage.text(R.string.message_2762c8b9a2), null).create()
        dialog.setOnDismissListener { nameDialogOpen = false }
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val value = input.text.toString().trim()
                if (value.isEmpty() || value.length > 80 || value.any { it.isISOControl() }) input.error = AppLanguage.text(R.string.message_ce04a46ef1)
                else { dialog.dismiss(); action(value) }
            }
        }
        dialog.show()
    }
    private fun task(message: String, action: () -> Unit) {
        if (busy) return
        busy = true; status.text = message
        worker.execute {
            try { action(); ui { busy = false; status.text = AppLanguage.text(R.string.message_ef9732b90d, listing.projects.size) } }
            catch (error: Exception) { ui { busy = false; status.text = AppLanguage.text(R.string.message_11e7c90ac5, error.message) } }
            catch (error: LinkageError) { ui { busy = false; status.text = AppLanguage.text(R.string.message_f1a8e5b456, error.javaClass.simpleName, error.message) } }
        }
    }
    private fun ui(action: () -> Unit) = runOnUiThread { if (!isDestroyed && !isFinishing) action() }
    private fun label(text: String, size: Float) = TextView(this).apply {
        this.text = text; textSize = size; setTextColor(StudioUi.text); setPadding(0, dp(8), 0, dp(6))
        body.addView(this, ViewGroup.LayoutParams(-1, -2))
    }
    private fun button(text: String, icon: StudioIcon, action: () -> Unit) {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = android.view.Gravity.CENTER_VERTICAL
            background = StudioUi.shape(this@ProjectLibraryActivity)
            setPadding(dp(12),dp(6),dp(8),dp(6))
        }
        row.addView(StudioUi.label(this, text, 15f), LinearLayout.LayoutParams(0,-2,1f))
        row.addView(StudioButton(this).apply { this.icon = icon; this.text = text; setOnClickListener { if (!busy) action() } }, LinearLayout.LayoutParams(dp(52),dp(52)))
        body.addView(row, LinearLayout.LayoutParams(-1,-2).apply { topMargin = dp(8) })
    }
    @Deprecated("Legacy SAF result adapter")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri: Uri = data?.data ?: return
        if (requestCode != IMPORT || resultCode != RESULT_OK) return
        askName(AppLanguage.text(R.string.message_8be8c7f189), AppLanguage.text(R.string.message_13801b7401)) { name ->
            task(AppLanguage.text(R.string.message_05b0f1f053)) {
                val temporary = File.createTempFile("library-import-", ".owncut", cacheDir)
                try {
                    contentResolver.openInputStream(uri)?.use { input -> temporary.outputStream().use { ProjectDocumentIo.copy(input, it) } }
                        ?: error(AppLanguage.text(R.string.message_136066f1d5))
                    val project = library.create(name, temporary)
                    ui { open(project) }
                } finally { temporary.delete() }
            }
        }
    }
    override fun onDestroy() { languageDialog?.dismiss(); worker.shutdown(); super.onDestroy() }
    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    companion object { private const val IMPORT = 5201 }
}
