package com.owncut.app

import java.io.File
import java.io.RandomAccessFile
import java.nio.channels.FileLock
import java.security.MessageDigest

/** Single-writer platform storage seam; native engine still owns codec and edit semantics.
 * Acquire/use/close on workers. Retain the same owner across Activity recreation.
 */
internal class ManagedProjectStore private constructor(private val file: File, private val lockFile: RandomAccessFile, private val lease: FileLock, private val identity: String) : AutoCloseable {
    private var expected: String? = null
    private var loaded = false
    private var closed = false
    @Synchronized fun load(session: EngineSession) {
        checkOpen()
        stableSnapshot(file) { snapshot, hash ->
            session.loadProject(snapshot.absolutePath) // native parse before live-state replacement
            session.loadRichSidecarFromFile(TimelineRichDocument.sidecarFor(file.absolutePath))
            expected = hash
            loaded = true
        }
    }
    @Synchronized fun checkpoint(session: EngineSession) {
        checkOpen(); check(loaded) { AppLanguage.text(R.string.project_not_loaded) }
        check(expected == fingerprint()) { AppLanguage.text(R.string.project_conflict) }
        try { session.checkpointProject(file.absolutePath) }
        catch(error: Exception) {
            // A failed acknowledgment may follow a durable primary rename. Only accept
            // our exact current bytes, never an arbitrary changed external document.
            runCatching { reconcileOwnWrite(session) }
            throw error
        }
        // Native journal validates the primary before recording commit/clearing recovery.
        expected = fingerprint()
        session.clearAutosave(file.absolutePath)
    }
    private fun reconcileOwnWrite(session: EngineSession) {
        val copy=File.createTempFile("checkpoint-compare-", ".owncut", file.parentFile)
        try {
            session.saveCopy(copy.absolutePath)
            val actual=fingerprint()
            if (actual != null && actual == fingerprint(copy)) expected=actual
        } finally {
            copy.delete()
            TimelineRichDocument.sidecarFor(copy.absolutePath).delete()
        }
    }
    @Synchronized fun repairCheckpoint(session: EngineSession): Boolean {
        checkOpen(); check(loaded)
        check(expected == fingerprint()) { AppLanguage.text(R.string.project_conflict) }
        return session.repairCheckpoint(file.absolutePath)
    }
    @Synchronized fun autosave(session: EngineSession, canWrite: () -> Boolean = { true }) {
        checkOpen(); check(loaded)
        if (canWrite()) session.saveAutosave(file.absolutePath)
    }
    @Synchronized fun recover(session: EngineSession): Boolean {
        checkOpen(); check(loaded)
        val recovery = File(file.absolutePath + ".autosave")
        if (!recovery.exists()) return false
        stableSnapshot(recovery) { snapshot, _ ->
            session.restoreDraft(snapshot.absolutePath)
            session.loadRichSidecarFromFile(TimelineRichDocument.sidecarFor(recovery.absolutePath))
        }
        return true
    }
    private fun checkOpen() { check(!closed) { "Managed store closed" } }
    /** No live-state mutation until the exact bounded snapshot passes revision checks. */
    private fun stableSnapshot(source: File, consume: (File, String) -> Unit) {
        val before = requireNotNull(fingerprint(source)) { "Project file is missing" }
        val snapshot = File.createTempFile("read-", ".owncut", file.parentFile)
        try {
            source.inputStream().buffered().use { input ->
                snapshot.outputStream().buffered().use { output -> ProjectDocumentIo.copy(input,output) }
            }
            check(before == fingerprint(source) && before == fingerprint(snapshot)) { AppLanguage.text(R.string.project_conflict) }
            consume(snapshot,before)
        } finally { snapshot.delete() }
    }
    private fun fingerprint(file: File = this.file): String? {
        if (!file.exists()) return null
        require(file.length() <= ProjectDocumentIo.MAX_PROJECT_BYTES) { "Project size exceeds safety limit" }
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(64*1024)
            var total = 0L
            while(true) { val count = input.read(buffer); if(count < 0) break; total += count
                require(total <= ProjectDocumentIo.MAX_PROJECT_BYTES); digest.update(buffer,0,count) }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }
    @Synchronized override fun close() {
        if(closed) return
        closed = true
        synchronized(writers) {
            try { lease.release() } finally {
                try { lockFile.close() } finally { writers.remove(identity) }
            }
        }
    }
    companion object {
        // POSIX fcntl locks may be released by closing *any* descriptor for that inode.
        // Refuse same-process duplicates before opening another descriptor, not after tryLock.
        private val writers = mutableSetOf<String>()
        fun acquire(file: File): ManagedProjectStore = synchronized(writers) {
            val anchor = File(file.parentFile,"writer.lock").canonicalFile
            val identity = anchor.path
            check(identity !in writers) { AppLanguage.text(R.string.project_already_open) }
            val owner = RandomAccessFile(anchor,"rw")
            try {
                val lease = try { owner.channel.tryLock() } catch (_: java.nio.channels.OverlappingFileLockException) { null }
                check(lease != null) { AppLanguage.text(R.string.project_already_open) }
                writers.add(identity)
                ManagedProjectStore(file,owner,lease,identity)
            } catch(error: Exception) { owner.close(); throw error }
        }
    }
}
