import com.owncut.app.ExrFloatWriter
import java.io.File

// Writes a deterministic 5x3 float RGBA image for an independent EXR reader to verify (see PROGRESS.md).
// Values include >1.0 and negatives to prove the writer does not clamp.
fun main(args: Array<String>) {
    val out = File(args[0])
    val w = 5
    val h = 3
    val rgba = FloatArray(w * h * 4)
    for (y in 0 until h) for (x in 0 until w) {
        val i = (y * w + x) * 4
        rgba[i] = x * 0.75f - 1.0f         // R: negatives and values above 1 (max 2.0)
        rgba[i + 1] = y * 0.25f + 0.125f  // G
        rgba[i + 2] = (x + y) * 0.1f      // B
        rgba[i + 3] = 1.0f                // A
    }
    out.outputStream().use { ExrFloatWriter.write(it, w, h, rgba) }
    val bytes = out.readBytes()
    check(bytes[0] == 0x76.toByte() && bytes[1] == 0x2f.toByte() && bytes[2] == 0x31.toByte() && bytes[3] == 0x01.toByte()) { "bad magic" }
    println("EXR written: ${bytes.size} bytes")
}
