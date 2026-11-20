// Host harness for the native 16-bit FFV1 writer. It includes the real JNI source so the actual
// Ffv1Writer class runs (no JNI environment needed). Built and run by tests/native/run_ffv1_host_check.py
// against a host FFmpeg. This is NOT the Android build; it checks the writer logic and the codec output.
#include "../../platform/android/jni/ffv1_writer_jni.cpp"

#include <cstdio>
#include <cstdlib>

namespace {

// Deterministic value in [-0.5, 1.5]: exercises clamping at both ends. The Python checker uses the same formula.
float sample(int x, int y, int frame, int channel) {
    const int v = (x * 7 + y * 13 + frame * 29 + channel * 53) % 400;
    return static_cast<float>(v) / 200.0f - 0.5f;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s out.mkv width height frames\n", argv[0]);
        return 2;
    }
    const char* out = argv[1];
    const int width = std::atoi(argv[2]);
    const int height = std::atoi(argv[3]);
    const int frames = std::atoi(argv[4]);
    try {
        Ffv1Writer writer(out, width, height, 30, 1);
        std::vector<float> buffer(static_cast<std::size_t>(width) * height * 4);
        for (int f = 0; f < frames; ++f) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    for (int c = 0; c < 4; ++c) {
                        buffer[(static_cast<std::size_t>(y) * width + x) * 4 + c] = sample(x, y, f, c);
                    }
                }
            }
            writer.write(buffer.data(), buffer.size());
        }
        writer.finish();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "writer failed: %s\n", error.what());
        return 1;
    }
    std::printf("wrote %s\n", out);
    return 0;
}
