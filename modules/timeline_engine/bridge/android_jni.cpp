#if defined(__ANDROID__) || defined(OWNCUT_HAS_JNI)
#include <jni.h>
#include "timeline_c_api.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::string jstring_to_utf8(JNIEnv* env, jstring value, const char* fallback = "") {
    if (!env || !value) return std::string(fallback);
    const jsize len = env->GetStringLength(value);
    if (len <= 0) return std::string();
    const jchar* chars = env->GetStringChars(value, nullptr);
    if (!chars) return std::string(fallback);

    std::string out;
    out.reserve(static_cast<std::size_t>(len));
    for (jsize i = 0; i < len; ++i) {
        uint32_t cp = static_cast<uint32_t>(chars[i]);
        if (cp >= 0xD800u && cp <= 0xDBFFu && (i + 1) < len) {
            const uint32_t low = static_cast<uint32_t>(chars[i + 1]);
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                cp = 0x10000u + (((cp - 0xD800u) << 10) | (low - 0xDC00u));
                ++i;
            } else {
                cp = 0xFFFDu;
            }
        } else if (cp >= 0xD800u && cp <= 0xDFFFu) {
            cp = 0xFFFDu;
        }

        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    env->ReleaseStringChars(value, chars);
    return out;
}

jstring utf8_to_jstring(JNIEnv* env, const std::string& utf8) {
    if (!env) return nullptr;
    std::vector<jchar> utf16;
    utf16.reserve(utf8.size());
    const std::size_t n = utf8.size();
    for (std::size_t i = 0; i < n;) {
        const uint8_t b0 = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0xFFFDu;
        if (b0 <= 0x7Fu) {
            cp = b0;
            ++i;
        } else if ((b0 & 0xE0u) == 0xC0u && i + 1 < n) {
            cp = (static_cast<uint32_t>(b0 & 0x1Fu) << 6) |
                 static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((b0 & 0xF0u) == 0xE0u && i + 2 < n) {
            cp = (static_cast<uint32_t>(b0 & 0x0Fu) << 12) |
                 (static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 6) |
                 static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((b0 & 0xF8u) == 0xF0u && i + 3 < n) {
            cp = (static_cast<uint32_t>(b0 & 0x07u) << 18) |
                 (static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 12) |
                 (static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu) << 6) |
                 static_cast<uint32_t>(static_cast<uint8_t>(utf8[i + 3]) & 0x3Fu);
            i += 4;
        } else {
            ++i;
        }
        if (cp <= 0xFFFFu) {
            utf16.push_back(static_cast<jchar>(cp));
        } else {
            const uint32_t adj = cp - 0x10000u;
            utf16.push_back(static_cast<jchar>(0xD800u + (adj >> 10)));
            utf16.push_back(static_cast<jchar>(0xDC00u + (adj & 0x3FFu)));
        }
    }
    return env->NewString(utf16.data(), static_cast<jsize>(utf16.size()));
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeCreate(JNIEnv*, jclass,
                                                     jint fpsNum, jint fpsDen,
                                                     jint width, jint height) {
    return reinterpret_cast<jlong>(owncut_tl_create(fpsNum, fpsDen, width, height));
}

JNIEXPORT void JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    owncut_tl_destroy(reinterpret_cast<OwnCutTimelineEngine*>(handle));
}

JNIEXPORT jlong JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeAddSyntheticMedia(JNIEnv* env, jclass,
                                                                 jlong handle, jstring name,
                                                                 jint width, jint height,
                                                                 jlong durationFrames) {
    const std::string cname = jstring_to_utf8(env, name, "Clip");
    const uint64_t id = owncut_tl_add_synthetic_media(
        reinterpret_cast<OwnCutTimelineEngine*>(handle), cname.c_str(), width, height, durationFrames);
    return static_cast<jlong>(id);
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativePerformEdit(JNIEnv*, jclass, jlong handle,
                                                          jint editMode, jlong mediaId,
                                                          jlong srcInFrames, jlong srcOutFrames) {
    return owncut_tl_perform_edit(reinterpret_cast<OwnCutTimelineEngine*>(handle),
                                  editMode, static_cast<uint64_t>(mediaId),
                                  srcInFrames, srcOutFrames) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeBladeSplit(JNIEnv*, jclass, jlong handle, jlong frameIndex) {
    return owncut_tl_blade_split(reinterpret_cast<OwnCutTimelineEngine*>(handle), frameIndex) != 0
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeUndo(JNIEnv*, jclass, jlong handle) {
    return owncut_tl_undo(reinterpret_cast<OwnCutTimelineEngine*>(handle)) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeRedo(JNIEnv*, jclass, jlong handle) {
    return owncut_tl_redo(reinterpret_cast<OwnCutTimelineEngine*>(handle)) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeTwoFingerUndo(JNIEnv*, jclass, jlong handle) {
    return owncut_tl_gesture_two_finger_undo(reinterpret_cast<OwnCutTimelineEngine*>(handle)) != 0
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeThreeFingerRedo(JNIEnv*, jclass, jlong handle) {
    return owncut_tl_gesture_three_finger_redo(reinterpret_cast<OwnCutTimelineEngine*>(handle)) != 0
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeSetPlayheadFrames(JNIEnv*, jclass, jlong handle, jlong frame) {
    owncut_tl_set_playhead_frames(reinterpret_cast<OwnCutTimelineEngine*>(handle), frame);
}

JNIEXPORT jlong JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeGetDurationFrames(JNIEnv*, jclass, jlong handle) {
    return owncut_tl_get_duration_frames(reinterpret_cast<const OwnCutTimelineEngine*>(handle));
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeSyncClips(JNIEnv* env, jclass, jlong handle,
                                                        jobjectArray ids,
                                                        jobjectArray trackIds,
                                                        jobjectArray mediaIds,
                                                        jlongArray startFrames,
                                                        jlongArray durationFrames,
                                                        jlongArray sourceInMs,
                                                        jlongArray sourceOutMs,
                                                        jbooleanArray gradeActives) {
    auto* engine = reinterpret_cast<OwnCutTimelineEngine*>(handle);
    if (!engine || !ids || !trackIds || !mediaIds || !startFrames || !durationFrames ||
        !sourceInMs || !sourceOutMs || !gradeActives) {
        return JNI_FALSE;
    }

    const jsize count = env->GetArrayLength(ids);
    if (count < 0 ||
        env->GetArrayLength(trackIds) != count ||
        env->GetArrayLength(mediaIds) != count ||
        env->GetArrayLength(startFrames) != count ||
        env->GetArrayLength(durationFrames) != count ||
        env->GetArrayLength(sourceInMs) != count ||
        env->GetArrayLength(sourceOutMs) != count ||
        env->GetArrayLength(gradeActives) != count) {
        return JNI_FALSE;
    }

    if (count == 0) {
        return owncut_tl_sync_clips(engine, nullptr, 0) != 0 ? JNI_TRUE : JNI_FALSE;
    }

    std::vector<jlong> starts(static_cast<std::size_t>(count));
    std::vector<jlong> durations(static_cast<std::size_t>(count));
    std::vector<jlong> src_ins(static_cast<std::size_t>(count));
    std::vector<jlong> src_outs(static_cast<std::size_t>(count));
    std::vector<jboolean> grades(static_cast<std::size_t>(count));

    env->GetLongArrayRegion(startFrames, 0, count, starts.data());
    env->GetLongArrayRegion(durationFrames, 0, count, durations.data());
    env->GetLongArrayRegion(sourceInMs, 0, count, src_ins.data());
    env->GetLongArrayRegion(sourceOutMs, 0, count, src_outs.data());
    env->GetBooleanArrayRegion(gradeActives, 0, count, grades.data());
    if (env->ExceptionCheck()) return JNI_FALSE;

    std::vector<std::string> id_bufs(static_cast<std::size_t>(count));
    std::vector<std::string> track_bufs(static_cast<std::size_t>(count));
    std::vector<std::string> media_bufs(static_cast<std::size_t>(count));
    std::vector<OwnCutBridgeClipSpec> specs(static_cast<std::size_t>(count));

    for (jsize i = 0; i < count; ++i) {
        auto jid = static_cast<jstring>(env->GetObjectArrayElement(ids, i));
        auto jtr = static_cast<jstring>(env->GetObjectArrayElement(trackIds, i));
        auto jmed = static_cast<jstring>(env->GetObjectArrayElement(mediaIds, i));
        if (env->ExceptionCheck()) {
            if (jid) env->DeleteLocalRef(jid);
            if (jtr) env->DeleteLocalRef(jtr);
            if (jmed) env->DeleteLocalRef(jmed);
            return JNI_FALSE;
        }

        const auto idx = static_cast<std::size_t>(i);
        id_bufs[idx] = jstring_to_utf8(env, jid);
        track_bufs[idx] = jstring_to_utf8(env, jtr, "v1");
        media_bufs[idx] = jstring_to_utf8(env, jmed);

        if (jid) env->DeleteLocalRef(jid);
        if (jtr) env->DeleteLocalRef(jtr);
        if (jmed) env->DeleteLocalRef(jmed);

        specs[idx].id = id_bufs[idx].c_str();
        specs[idx].track_id = track_bufs[idx].c_str();
        specs[idx].media_id = media_bufs[idx].c_str();
        specs[idx].start_frame = static_cast<int64_t>(starts[idx]);
        specs[idx].duration_frames = static_cast<int64_t>(durations[idx]);
        specs[idx].source_in_ms = static_cast<int64_t>(src_ins[idx]);
        specs[idx].source_out_ms = static_cast<int64_t>(src_outs[idx]);
        specs[idx].grade_active = (grades[idx] == JNI_TRUE) ? 1 : 0;
    }

    return owncut_tl_sync_clips(engine, specs.data(), static_cast<int32_t>(count)) != 0
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlongArray JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeEvaluateSnap(JNIEnv* env, jclass, jlong handle,
                                                           jstring trackId,
                                                           jstring clipId,
                                                           jlong proposedFrame,
                                                           jlong durationFrames,
                                                           jlong playheadFrame,
                                                           jdouble pixelsPerFrame,
                                                           jboolean snapEnabled) {
    auto* engine = reinterpret_cast<OwnCutTimelineEngine*>(handle);
    if (!env || !engine) return nullptr;

    const std::string track = jstring_to_utf8(env, trackId, "v1");
    const std::string clip = jstring_to_utf8(env, clipId, "");
    OwnCutSnapOutcome outcome{};
    if (owncut_tl_evaluate_snap(engine,
                                track.c_str(),
                                clip.c_str(),
                                static_cast<int64_t>(proposedFrame),
                                static_cast<int64_t>(durationFrames),
                                static_cast<int64_t>(playheadFrame),
                                static_cast<double>(pixelsPerFrame),
                                snapEnabled == JNI_TRUE ? 1 : 0,
                                &outcome) == 0) {
        return nullptr;
    }

    jlongArray arr = env->NewLongArray(5);
    if (!arr) return nullptr;
    const jlong values[5] = {
        static_cast<jlong>(outcome.final_frame),
        static_cast<jlong>(outcome.snapped),
        static_cast<jlong>(outcome.snap_guide_frame),
        static_cast<jlong>(outcome.slot_adjusted),
        static_cast<jlong>(outcome.haptic_pulse)
    };
    env->SetLongArrayRegion(arr, 0, 5, values);
    return arr;
}

JNIEXPORT jdouble JNICALL
Java_com_owncut_timeline_TimelineBridge_nativePinchZoom(JNIEnv*, jclass, jlong handle,
                                                        jdouble scaleFactor, jdouble focusXPx) {
    return static_cast<jdouble>(owncut_tl_gesture_pinch_scale(
        reinterpret_cast<OwnCutTimelineEngine*>(handle),
        static_cast<double>(scaleFactor),
        static_cast<double>(focusXPx)));
}

JNIEXPORT void JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeResetZoom(JNIEnv*, jclass, jlong handle) {
    owncut_tl_reset_zoom(reinterpret_cast<OwnCutTimelineEngine*>(handle));
}

JNIEXPORT jdouble JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeGetZoomScale(JNIEnv*, jclass, jlong handle) {
    return static_cast<jdouble>(owncut_tl_get_zoom_scale(
        reinterpret_cast<const OwnCutTimelineEngine*>(handle)));
}

JNIEXPORT jstring JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeGetCapabilitiesJson(JNIEnv* env, jclass, jlong handle) {
    auto* engine = reinterpret_cast<const OwnCutTimelineEngine*>(handle);
    if (!env || !engine) return nullptr;
    const int32_t needed = owncut_tl_get_capabilities_json(engine, nullptr, 0);
    if (needed <= 0) return utf8_to_jstring(env, "{}");
    std::string buf(static_cast<std::size_t>(needed), '\0');
    owncut_tl_get_capabilities_json(engine, buf.data(), needed);
    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    return utf8_to_jstring(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeExportStateJson(JNIEnv* env, jclass, jlong handle) {
    auto* engine = reinterpret_cast<const OwnCutTimelineEngine*>(handle);
    if (!env || !engine) return nullptr;
    const int32_t needed = owncut_tl_export_state_json(engine, nullptr, 0);
    if (needed <= 0) return utf8_to_jstring(env, "{}");
    std::string buf(static_cast<std::size_t>(needed), '\0');
    owncut_tl_export_state_json(engine, buf.data(), needed);
    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    return utf8_to_jstring(env, buf);
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeImportStateJson(JNIEnv* env, jclass, jlong handle, jstring json) {
    auto* engine = reinterpret_cast<OwnCutTimelineEngine*>(handle);
    if (!env || !engine || !json) return JNI_FALSE;
    const std::string str = jstring_to_utf8(env, json);
    return owncut_tl_import_state_json(engine, str.c_str()) != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeSetSnapEnabled(JNIEnv*, jclass, jlong handle, jboolean enabled) {
    owncut_tl_set_snap_enabled(reinterpret_cast<OwnCutTimelineEngine*>(handle), enabled == JNI_TRUE ? 1 : 0);
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeGetSnapEnabled(JNIEnv*, jclass, jlong handle) {
    return owncut_tl_get_snap_enabled(reinterpret_cast<const OwnCutTimelineEngine*>(handle)) != 0
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeSetBeatGrid(JNIEnv*, jclass, jlong handle, jdouble bpm, jlong offsetMs) {
    owncut_tl_set_beat_grid(reinterpret_cast<OwnCutTimelineEngine*>(handle),
                            static_cast<double>(bpm), static_cast<int64_t>(offsetMs));
}

JNIEXPORT void JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeUpsertMarker(JNIEnv* env, jclass, jlong handle,
                                                           jstring id, jlong positionMs,
                                                           jstring label, jstring colorHex) {
    const std::string cid = jstring_to_utf8(env, id);
    const std::string clabel = jstring_to_utf8(env, label);
    const std::string ccolor = jstring_to_utf8(env, colorHex, "#4F9CF9");
    owncut_tl_upsert_marker(reinterpret_cast<OwnCutTimelineEngine*>(handle),
                            cid.c_str(), static_cast<int64_t>(positionMs),
                            clabel.c_str(), ccolor.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeRemoveMarker(JNIEnv* env, jclass, jlong handle, jstring id) {
    const std::string cid = jstring_to_utf8(env, id);
    return owncut_tl_remove_marker(reinterpret_cast<OwnCutTimelineEngine*>(handle), cid.c_str()) != 0
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeImportSrt(JNIEnv* env, jclass, jlong handle, jstring srtText) {
    const std::string srt = jstring_to_utf8(env, srtText);
    return owncut_tl_import_srt(reinterpret_cast<OwnCutTimelineEngine*>(handle), srt.c_str()) != 0
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeExportSrt(JNIEnv* env, jclass, jlong handle) {
    auto* engine = reinterpret_cast<const OwnCutTimelineEngine*>(handle);
    if (!env || !engine) return nullptr;
    const int32_t needed = owncut_tl_export_srt(engine, nullptr, 0);
    if (needed <= 0) return utf8_to_jstring(env, "");
    std::string buf(static_cast<std::size_t>(needed), '\0');
    owncut_tl_export_srt(engine, buf.data(), needed);
    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    return utf8_to_jstring(env, buf);
}

JNIEXPORT jdoubleArray JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeAnalyzePcm16(JNIEnv* env, jclass,
                                                           jshortArray pcmInterleaved,
                                                           jint channels,
                                                           jint sampleRate,
                                                           jint numPeaks) {
    if (!env || !pcmInterleaved || channels <= 0 || sampleRate <= 0 || numPeaks <= 0) return nullptr;
    const jsize total_shorts = env->GetArrayLength(pcmInterleaved);
    const int32_t sample_frames = static_cast<int32_t>(total_shorts) / channels;
    if (sample_frames <= 0) return nullptr;

    std::vector<jshort> samples(static_cast<std::size_t>(total_shorts));
    env->GetShortArrayRegion(pcmInterleaved, 0, total_shorts, samples.data());
    if (env->ExceptionCheck()) return nullptr;

    std::vector<float> peaks(static_cast<std::size_t>(numPeaks), 0.0f);
    double bpm = 0.0;
    double lufs = -70.0;
    const int32_t written = owncut_tl_analyze_pcm16(
        reinterpret_cast<const int16_t*>(samples.data()),
        sample_frames, channels, sampleRate, numPeaks,
        peaks.data(), &bpm, &lufs);
    if (written <= 0) return nullptr;

    std::vector<jdouble> result(static_cast<std::size_t>(written + 2));
    result[0] = static_cast<jdouble>(bpm);
    result[1] = static_cast<jdouble>(lufs);
    for (int32_t i = 0; i < written; ++i) {
        result[static_cast<std::size_t>(i + 2)] = static_cast<jdouble>(peaks[static_cast<std::size_t>(i)]);
    }
    jdoubleArray arr = env->NewDoubleArray(written + 2);
    if (!arr) return nullptr;
    env->SetDoubleArrayRegion(arr, 0, written + 2, result.data());
    return arr;
}

JNIEXPORT jintArray JNICALL
Java_com_owncut_timeline_TimelineBridge_nativeDetectSceneCuts(JNIEnv* env, jclass,
                                                              jbyteArray rgbaFrames,
                                                              jint numFrames,
                                                              jint width,
                                                              jint height,
                                                              jdouble threshold) {
    if (!env || !rgbaFrames || numFrames < 2 || width <= 0 || height <= 0) return nullptr;
    const jsize byte_len = env->GetArrayLength(rgbaFrames);
    const jsize expected = static_cast<jsize>(numFrames) * width * height * 4;
    if (byte_len < expected) return nullptr;

    std::vector<jbyte> raw(static_cast<std::size_t>(byte_len));
    env->GetByteArrayRegion(rgbaFrames, 0, byte_len, raw.data());
    if (env->ExceptionCheck()) return nullptr;

    std::vector<int32_t> indices(static_cast<std::size_t>(numFrames), 0);
    const int32_t count = owncut_tl_detect_scene_cuts(
        reinterpret_cast<const uint8_t*>(raw.data()),
        numFrames, width, height, static_cast<double>(threshold),
        indices.data(), numFrames);
    const int32_t safe_count = std::min(count, static_cast<int32_t>(numFrames));
    jintArray out = env->NewIntArray(safe_count);
    if (!out) return nullptr;
    if (safe_count > 0) {
        std::vector<jint> jvals(static_cast<std::size_t>(safe_count));
        for (int32_t i = 0; i < safe_count; ++i) jvals[static_cast<std::size_t>(i)] = indices[static_cast<std::size_t>(i)];
        env->SetIntArrayRegion(out, 0, safe_count, jvals.data());
    }
    return out;
}

} // extern "C"
#endif
