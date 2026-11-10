package com.owncut.app

import android.util.AtomicFile
import org.json.JSONObject
import java.io.File
import java.util.UUID

internal data class LibraryProject(val id: String, val name: String, val updatedAt: Long, val recovery: Boolean)
internal data class LibraryListing(val projects: List<LibraryProject>, val warnings: List<String>, val repairable: List<String> = emptyList())

/** App-private catalog metadata only; native EngineSession owns project contents and validation.
 * All calls are blocking and must run on a worker. No media bytes are copied into this catalog.
 */
internal class ProjectLibrary(private val filesDir: File) {
    private val root = File(filesDir, "projects")
    private fun directory(id: String): File {
        require(Regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}").matches(id)) { AppLanguage.text(R.string.message_8bd7483212) }
        return File(root, id)
    }
    fun projectFile(id: String) = File(directory(id), "project.owncut")
    fun recoveryFile(id: String) = File(directory(id), "project.owncut.autosave")
    fun read(id: String): LibraryProject = synchronized(lock) { readUnlocked(id) }
    private fun readUnlocked(id: String): LibraryProject {
        val file = AtomicFile(File(directory(id), "metadata.json"))
        val bytes = file.openRead().use { input ->
            val output = java.io.ByteArrayOutputStream()
            ProjectDocumentIo.copy(input, output, 16L * 1024L)
            output.toByteArray()
        }
        val json = JSONObject(String(bytes, Charsets.UTF_8))
        require(json.getInt("version") == 1 && json.getString("id") == id) { AppLanguage.text(R.string.message_0766a0344b) }
        val name = validName(json.getString("name"))
        val updated = json.getLong("updatedAt")
        require(updated >= 0) { AppLanguage.text(R.string.message_8f454e8f99) }
        return LibraryProject(id, name, updated, recoveryFile(id).isFile)
    }
    fun list(): LibraryListing = synchronized(lock) { listUnlocked() }
    private fun listUnlocked(): LibraryListing {
        if (!root.exists()) return LibraryListing(emptyList(), emptyList())
        val dirs = root.listFiles() ?: error(AppLanguage.text(R.string.message_04e2fb0e61))
        require(dirs.size <= 1000) { AppLanguage.text(R.string.message_5b44580b10) }
        val warnings = mutableListOf<String>()
        val repairable = mutableListOf<String>()
        val items = dirs.filter { it.isDirectory }.mapNotNull { dir ->
            try { read(dir.name) } catch (_: Exception) {
                if (runCatching { projectFile(dir.name).isFile }.getOrDefault(false)) repairable += dir.name
                warnings += AppLanguage.text(R.string.message_ea1cce60a0, dir.name); null
            }
        }
        return LibraryListing(items.sortedWith(compareByDescending<LibraryProject> { it.updatedAt }.thenBy { it.id }), warnings, repairable)
    }
    fun create(name: String, source: File? = null): LibraryProject = synchronized(lock) { createUnlocked(name, source) }
    private fun createUnlocked(name: String, source: File?): LibraryProject {
        val safeName = validName(name)
        if (root.exists()) {
            val entries = root.listFiles() ?: error(AppLanguage.text(R.string.message_04e2fb0e61))
            require(entries.size < 1000) { AppLanguage.text(R.string.message_ee066bee3d) }
        }
        val id = UUID.randomUUID().toString()
        val dir = directory(id)
        check(dir.mkdirs()) { AppLanguage.text(R.string.message_6354e4e3d6) }
        try {
            EngineSession().use { session ->
                if (source != null) session.loadProject(source.absolutePath)
                session.saveProject(projectFile(id).absolutePath)
            }
            write(id, safeName)
            return read(id)
        } catch (error: Exception) {
            // Leave any successfully written native document available for manual recovery.
            throw IllegalStateException(AppLanguage.text(R.string.message_20c86043bf, dir.name), error)
        }
    }
    fun repair(id: String, name: String): LibraryProject = synchronized(lock) {
        val label = validName(name)
        val dir = directory(id)
        // Never replace an unknown-future catalog version through the repair UI.
        val metadata = File(dir,"metadata.json")
        val existing = if(metadata.length() <= 16L*1024L) runCatching { org.json.JSONObject(metadata.readText()).getInt("version") }.getOrNull() else null
        require(existing == null || existing == 1) { AppLanguage.text(R.string.unsupported_repair) }
        ManagedProjectStore.acquire(projectFile(id)).use { store ->
            EngineSession().use { store.load(it) }
            val stamp = java.util.UUID.randomUUID().toString()
            for (filename in listOf("metadata.json","metadata.json.bak")) {
                val damaged = File(dir,filename)
                if(damaged.isFile) damaged.copyTo(File(dir,"$filename.preserved-$stamp"))
            }
            write(id,label)
        }
        read(id)
    }
    fun rename(id: String, name: String): LibraryProject = synchronized(lock) { read(id); write(id, validName(name)); read(id) }
    fun touch(id: String) = synchronized(lock) { val old = read(id); write(id, old.name) }
    private fun write(id: String, name: String) {
        val file = AtomicFile(File(directory(id), "metadata.json"))
        val json = JSONObject().put("version", 1).put("id", id).put("name", name).put("updatedAt", System.currentTimeMillis())
        val stream = file.startWrite()
        try { stream.write(json.toString().toByteArray(Charsets.UTF_8)); file.finishWrite(stream) }
        catch (error: Exception) { file.failWrite(stream); throw error }
    }
    /** One-time, non-destructive discovery of the pre-library workspace. Recover before migration
     * only by explicit choice in the editor; the primary document is never silently overwritten. */
    fun migrateLegacy() = synchronized(lock) { migrateLegacyUnlocked() }
    private fun migrateLegacyUnlocked() {
        val saved = File(filesDir, "active.owncut")
        val recovery = File(filesDir, "active.owncut.autosave")
        if (!saved.isFile && !recovery.isFile) return
        val id = "00000000-0000-0000-0000-000000000001"
        val dir = directory(id)
        if (File(dir, "metadata.json").exists() || File(dir, "metadata.json.bak").exists()) return
        check(dir.isDirectory || dir.mkdirs())
        if (!projectFile(id).exists()) {
            EngineSession().use { session ->
                if (saved.isFile) session.loadProject(saved.absolutePath)
                session.saveProject(projectFile(id).absolutePath)
            }
        }
        if (recovery.isFile && !recoveryFile(id).exists()) {
            val temporary = File.createTempFile("recovery-migration-", ".tmp", dir)
            try {
                recovery.inputStream().use { input -> java.io.FileOutputStream(temporary).use { output ->
                    ProjectDocumentIo.copy(input, output); output.fd.sync()
                } }
                java.nio.file.Files.move(temporary.toPath(), recoveryFile(id).toPath(), java.nio.file.StandardCopyOption.ATOMIC_MOVE)
            } finally { temporary.delete() }
        }
        write(id, AppLanguage.text(R.string.message_b0ed78beaa))
    }
    companion object { private val lock = Any() }
    private fun validName(name: String): String {
        val value = name.trim()
        require(value.isNotEmpty() && value.length <= 80 && value.none { it.isISOControl() }) { AppLanguage.text(R.string.message_6cf81c7ca3) }
        return value
    }
}
