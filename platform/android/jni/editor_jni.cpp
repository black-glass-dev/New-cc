#include <jni.h>

#include "editor/session_coordinator.hpp"
#include "editor/session_registry.hpp"
#include <charconv>
#include "editor/unicode.hpp"
#include "editor/project.hpp"
#include "editor/time.hpp"
#include "editor/timeline.hpp"

#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <type_traits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace editor;

const TimeBase kFrameTimeBase{1, 30};
const TimeBase kMilliseconds{1, 1000};

using EngineSession = SessionCoordinator;

SessionRegistry sessions;

static_assert(std::is_same_v<jchar,std::uint16_t>);
std::string from_java(JNIEnv* env, jstring value) {
    if (value == nullptr) throw std::invalid_argument("string argument is null");
    const auto length=env->GetStringLength(value);
    const jchar* chars=env->GetStringChars(value,nullptr);
    if (chars == nullptr) throw std::runtime_error("could not access Java string");
    struct Guard final {
        JNIEnv* env; jstring value; const jchar* chars;
        ~Guard() { env->ReleaseStringChars(value,chars); }
    } guard{env,value,chars};
    auto result=utf8_from_utf16({chars,static_cast<std::size_t>(length)});
    if (result.empty()) throw std::invalid_argument("string argument must not be empty");
    if (result.find('\0') != std::string::npos) throw std::invalid_argument("string argument contains NUL");
    return result;
}
jstring to_java(JNIEnv* env, std::string_view text) {
    if(text.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
        throw std::length_error("native string exceeds Java size limit");
    const auto utf16=utf16_from_utf8(text);
    static constexpr jchar empty=0;
    return env->NewString(utf16.empty() ? &empty : utf16.data(),static_cast<jsize>(utf16.size()));
}

void throw_java_argument(JNIEnv* env, const char* message) noexcept {
    if (env->ExceptionCheck()) return;
    jclass type=env->FindClass("java/lang/IllegalArgumentException");
    if (type == nullptr) return;
    try {
        const auto constructor=env->GetMethodID(type,"<init>","(Ljava/lang/String;)V");
        if(constructor != nullptr) {
            jstring text=to_java(env,message);
            if(text != nullptr) {
                jobject error=env->NewObject(type,constructor,text);
                if(error != nullptr) { env->Throw(static_cast<jthrowable>(error)); env->DeleteLocalRef(error); }
                env->DeleteLocalRef(text);
            }
        }
    } catch (...) {
        if(!env->ExceptionCheck()) env->ThrowNew(type,"Native operation failed"); // ASCII fallback only
    }
    env->DeleteLocalRef(type);
}

std::shared_ptr<EngineSession> require_session(jlong handle) {
    return sessions.acquire(handle);
}

void append_json_string(std::string& output, const std::string& value) {
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const char raw_byte : value) {
        const auto byte = static_cast<unsigned char>(raw_byte);
        switch (byte) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (byte < 0x20) {
                    output += "\\u00";
                    output.push_back(hex[(byte >> 4) & 0x0f]);
                    output.push_back(hex[byte & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(byte));
                }
        }
    }
    output.push_back('"');
}

void append_clip(std::string& output, const ClipLocation& location) {
    const auto& clip = location.clip;
    const auto start = clip.timeline_start.rescaled(kFrameTimeBase).ticks;
    const auto duration = (clip.source_out - clip.source_in).rescaled(kFrameTimeBase).ticks;
    const auto source_in_ms = clip.source_in.rescaled(kMilliseconds).ticks;
    const auto source_out_ms = clip.source_out.rescaled(kMilliseconds).ticks;
    output += "{\"id\":";
    append_json_string(output, clip.id);
    output += ",\"trackId\":";
    append_json_string(output, location.track_id);
    output += ",\"mediaId\":";
    append_json_string(output, clip.media_id);
    output += ",\"startFrame\":" + std::to_string(start);
    output += ",\"durationFrames\":" + std::to_string(duration);
    output += ",\"sourceInMs\":" + std::to_string(source_in_ms);
    output += ",\"sourceOutMs\":" + std::to_string(source_out_ms);
    output += ",\"gradeActive\":" + std::string(color::Pipeline(clip.grade).identity() ? "false" : "true");
    output += ",\"grade\":{\"enabled\":" + std::string(clip.grade.enabled ? "true" : "false") + ",\"parameters\":[";
    bool first = true;
    for (const auto value : color::parameters(clip.grade)) {
        if (!first) output.push_back(',');
        first = false;
        char number[64];
        const auto result = std::to_chars(number,number+sizeof(number),value,std::chars_format::general,
                                          std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) throw std::runtime_error("could not serialize clip grade");
        output.append(number,result.ptr);
    }
    output += "]}}";
}

std::string make_timeline_snapshot(const EngineSession& session) {
    std::string output = "{\"clips\":[";
    bool first = true;
    std::size_t offset = 0;
    std::optional<std::uint64_t> revision;
    for (;;) {
        const auto page = session.clip_page(offset, SessionCoordinator::max_page_size, revision);
        revision = page.revision;
        for (const auto& location : page.clips) {
            if (!first) output.push_back(',');
            first = false;
            append_clip(output, location);
        }
        offset = page.next_offset;
        if (offset == page.total) break;
    }
    output += "]}";
    return output;
}

std::string make_status(const EngineSession& session) {
    const auto status = session.status();
    return "C++ engine ready · project=" + status.name +
        " · tracks=" + std::to_string(status.tracks) +
        " · clips=" + std::to_string(status.clips) +
        " · undo=" + (status.can_undo ? "yes" : "no") +
        " · redo=" + (status.can_redo ? "yes" : "no");
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_owncut_app_EngineSession_nativeCreate(JNIEnv* env, jobject, jstring project_id) {
    try {
        return sessions.create(from_java(env,project_id));
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return 0;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeGetStatus(JNIEnv* env, jobject, jlong handle) {
    try {
        auto session = require_session(handle);
        const auto status = make_status(*session);
        return to_java(env,status);
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeGetTimelineSnapshot(JNIEnv* env, jobject, jlong handle) {
    try {
        auto session = require_session(handle);
        const auto snapshot = make_timeline_snapshot(*session);
        return to_java(env,snapshot);
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeResolvePlayhead(JNIEnv* env, jobject, jlong handle,
                                                        jstring track_id, jlong frame) {
    try {
        auto session = require_session(handle);
        const auto track = from_java(env, track_id);
        const auto resolved = session->resolve_playhead(track, Timestamp{frame, kFrameTimeBase});
        if (!resolved) return nullptr;
        std::string json{"{\"clipId\":"};
        append_json_string(json, resolved->clip_id);
        json += ",\"timelineFrame\":" + std::to_string(resolved->timeline_position.ticks);
        json += ",\"sourcePositionMs\":" + std::to_string(resolved->source_position.rescaled(kMilliseconds).ticks) + "}";
        return to_java(env,json);
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); return nullptr; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeAddMediaClip(JNIEnv* env, jobject, jlong handle,
                                                      jstring media_id, jlong duration_ms) {
    try {
        auto session = require_session(handle);
        auto media = from_java(env, media_id);
        const auto id = session->add_media(media, duration_ms);
        return to_java(env,id);
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeRelinkClipMedia(JNIEnv* env, jobject, jlong handle,
                                                          jstring clip_id, jstring replacement_media_id) {
    try {
        auto session = require_session(handle);
        auto clip = from_java(env, clip_id);
        auto media = from_java(env, replacement_media_id);
        session->execute(
            std::make_unique<RelinkClipMediaCommand>(clip, media));
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeMoveClip(JNIEnv* env, jobject, jlong handle,
                                                  jstring clip_id, jstring destination_track_id,
                                                  jlong destination_start_frame) {
    try {
        auto session = require_session(handle);
        auto clip = from_java(env, clip_id);
        auto destination = from_java(env, destination_track_id);
        session->execute(
            std::make_unique<MoveClipCommand>(clip, destination,
                                               Timestamp{destination_start_frame, kFrameTimeBase}));
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeSplitClip(JNIEnv* env, jobject, jlong handle,
                                                   jstring clip_id, jlong cut_frame, jstring right_clip_id) {
    try {
        auto session = require_session(handle);
        auto clip = from_java(env, clip_id);
        auto right = from_java(env, right_clip_id);
        session->execute(
            std::make_unique<SplitClipCommand>(clip, Timestamp{cut_frame, kFrameTimeBase}, right));
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeTrimClip(JNIEnv* env, jobject, jlong handle,
                                                  jstring clip_id, jboolean left_edge, jlong boundary_ms) {
    try {
        auto session = require_session(handle);
        auto clip = from_java(env, clip_id);
        session->execute(
            std::make_unique<TrimClipCommand>(clip, left_edge == JNI_TRUE ? TrimEdge::left : TrimEdge::right,
                                               Timestamp{boundary_ms, kMilliseconds}));
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeDeleteClip(JNIEnv* env, jobject, jlong handle,
                                                    jstring clip_id, jboolean ripple) {
    try {
        auto session = require_session(handle);
        auto clip = from_java(env, clip_id);
        if (ripple == JNI_TRUE) {
            session->execute(std::make_unique<RippleDeleteClipCommand>(clip));
        } else {
            session->execute(std::make_unique<DeleteClipCommand>(clip));
        }
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_owncut_app_EngineSession_nativeUndo(JNIEnv* env, jobject, jlong handle) {
    try {
        auto session = require_session(handle);
        return session->undo() ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_owncut_app_EngineSession_nativeRedo(JNIEnv* env, jobject, jlong handle) {
    try {
        auto session = require_session(handle);
        return session->redo() ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeSaveProject(JNIEnv* env, jobject, jlong handle, jstring path) {
    try {
        auto session = require_session(handle);
        auto file_path = from_java(env, path);
        session->save(file_path);
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeLoadProject(JNIEnv* env, jobject, jlong handle, jstring path) {
    try {
        auto session = require_session(handle);
        auto file_path = from_java(env, path);
        session->load(file_path);
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeDestroy(JNIEnv* env, jobject, jlong handle) {
    try { sessions.close(handle); }
    catch (const std::exception& error) { throw_java_argument(env,error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeSaveAutosave(JNIEnv* env, jobject, jlong handle, jstring path) {
    try {
        auto session = require_session(handle);
        auto file_path = from_java(env, path);
        session->autosave(file_path);
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_owncut_app_EngineSession_nativeLoadAutosave(JNIEnv* env, jobject, jlong handle, jstring path) {
    try {
        auto session = require_session(handle);
        auto file_path = from_java(env, path);
        return session->recover(file_path) ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeClearAutosave(JNIEnv* env, jobject, jlong handle, jstring path) {
    try {
        require_session(handle)->clear_recovery(from_java(env, path));
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeSaveCopy(JNIEnv* env, jobject, jlong handle, jstring path) {
    try { require_session(handle)->save_copy(from_java(env, path)); }
    catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}
extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeRestoreDraft(JNIEnv* env, jobject, jlong handle, jstring path) {
    try { require_session(handle)->restore_draft(from_java(env, path)); }
    catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeCheckpointProject(JNIEnv* env, jobject, jlong handle, jstring path) {
    try { require_session(handle)->checkpoint(from_java(env,path)); }
    catch (const std::exception& error) { throw_java_argument(env,error.what()); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeGetMediaAssets(JNIEnv* env, jobject, jlong handle) {
    try {
        const auto assets = require_session(handle)->media_assets();
        std::string json = "{\"assets\":[";
        bool first_asset = true;
        for (const auto& asset : assets) {
            if (!first_asset) json += ',';
            first_asset = false;
            json += "{\"mediaId\":"; append_json_string(json,asset.media_id);
            json += ",\"requiredSourceEndMs\":" + std::to_string(asset.required_source_end_ms) + ",\"clipIds\":[";
            bool first_clip = true;
            for (const auto& id : asset.clip_ids) {
                if (!first_clip) json += ',';
                first_clip = false;
                append_json_string(json,id);
            }
            json += "]}";
        }
        json += "]}";
        return to_java(env,json);
    } catch (const std::exception& error) { throw_java_argument(env,error.what()); return nullptr; }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_owncut_app_EngineSession_nativeRepairCheckpoint(JNIEnv* env, jobject, jlong handle, jstring path) {
    try { return require_session(handle)->repair_checkpoint(from_java(env,path)) ? JNI_TRUE : JNI_FALSE; }
    catch (const std::exception& error) { throw_java_argument(env,error.what()); return JNI_FALSE; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeGetRevision(JNIEnv* env, jobject, jlong handle) {
    try { return to_java(env, std::to_string(require_session(handle)->status().revision)); }
    catch (const std::exception& error) { throw_java_argument(env,error.what()); return nullptr; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeGetClipPage(JNIEnv* env, jobject, jlong handle,
                                                 jint offset, jint limit, jstring expected) {
    try {
        if (offset < 0 || limit <= 0) throw std::invalid_argument("invalid snapshot page range");
        std::optional<std::uint64_t> revision;
        if (expected != nullptr) {
            const auto text = from_java(env,expected);
            std::uint64_t value = 0;
            const auto result = std::from_chars(text.data(),text.data()+text.size(),value);
            if (text.empty() || result.ec != std::errc{} || result.ptr != text.data()+text.size())
                throw std::invalid_argument("invalid snapshot revision");
            revision = value;
        }
        const auto page = require_session(handle)->clip_page(static_cast<std::size_t>(offset),
                                                            static_cast<std::size_t>(limit),revision);
        // Decimal string preserves every uint64 revision without JSON/Java double rounding.
        std::string json = "{\"revision\":\"" + std::to_string(page.revision) +
            "\",\"total\":" + std::to_string(page.total) +
            ",\"nextOffset\":" + std::to_string(page.next_offset) + ",\"clips\":[";
        bool first = true;
        for (const auto& location : page.clips) {
            if (!first) json.push_back(',');
            first = false;
            append_clip(json,location);
        }
        json += "]}";
        return to_java(env,json);
    } catch (const std::exception& error) { throw_java_argument(env,error.what()); return nullptr; }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeSetClipGrades(JNIEnv* env, jobject, jlong handle,
                                                     jobjectArray targets, jdoubleArray values, jboolean enabled) {
    try {
        auto session = require_session(handle);
        if (!targets || !values || env->GetArrayLength(values) != static_cast<jsize>(color::parameter_count))
            throw std::invalid_argument("grade command requires targets and 17 parameters");
        const auto count = env->GetArrayLength(targets);
        if (count <= 0 || count > 8192) throw std::invalid_argument("invalid grade target count");
        color::ParameterBlock parameters;
        env->GetDoubleArrayRegion(values,0,static_cast<jsize>(parameters.size()),parameters.data());
        if (env->ExceptionCheck()) return;
        std::vector<std::string> ids;
        ids.reserve(static_cast<std::size_t>(count));
        for (jsize i=0;i<count;++i) {
            auto value = static_cast<jstring>(env->GetObjectArrayElement(targets,i));
            if (env->ExceptionCheck()) return;
            try { ids.push_back(from_java(env,value)); }
            catch (...) { if (value) env->DeleteLocalRef(value); throw; }
            if (value) env->DeleteLocalRef(value);
        }
        session->execute(std::make_unique<SetClipGradesCommand>(std::move(ids),color::from_parameters(parameters,enabled != JNI_FALSE)));
    } catch (const std::exception& error) { throw_java_argument(env,error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeEnsureTrack(JNIEnv* env, jobject, jlong handle,
                                                    jstring track_id, jboolean is_audio) {
    try {
        auto session = require_session(handle);
        auto id = from_java(env, track_id);
        session->ensure_track(id, is_audio == JNI_TRUE ? TrackKind::audio : TrackKind::video);
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeAddMediaToTrack(JNIEnv* env, jobject, jlong handle,
                                                          jstring track_id, jstring media_id,
                                                          jlong duration_ms, jlong start_frame) {
    try {
        auto session = require_session(handle);
        auto tr = from_java(env, track_id);
        auto media = from_java(env, media_id);
        std::optional<Timestamp> start_at;
        if (start_frame >= 0) start_at = Timestamp{start_frame, kFrameTimeBase};
        const auto id = session->add_media_to_track(tr, media, duration_ms, start_at);
        return to_java(env, id);
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_EngineSession_nativeSetTrackState(JNIEnv* env, jobject, jlong handle,
                                                      jstring track_id, jboolean muted, jboolean locked) {
    try {
        auto session = require_session(handle);
        auto id = from_java(env, track_id);
        session->set_track_state(id, muted == JNI_TRUE, locked == JNI_TRUE);
    } catch (const std::exception& error) { throw_java_argument(env, error.what()); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_EngineSession_nativeGetTracksJson(JNIEnv* env, jobject, jlong handle) {
    try {
        auto session = require_session(handle);
        const auto snap = session->snapshot();
        std::string json = "{\"tracks\":[";
        bool first = true;
        for (const auto& tr : snap.project.timeline.tracks()) {
            if (!first) json.push_back(',');
            first = false;
            json += "{\"id\":";
            append_json_string(json, tr.id);
            json += ",\"kind\":";
            append_json_string(json, tr.kind == TrackKind::audio ? "audio" : "video");
            json += ",\"muted\":" + std::string(tr.muted ? "true" : "false");
            json += ",\"locked\":" + std::string(tr.locked ? "true" : "false");
            json += ",\"clipCount\":" + std::to_string(tr.clips.size()) + "}";
        }
        json += "]}";
        return to_java(env, json);
    } catch (const std::exception& error) {
        throw_java_argument(env, error.what());
        return nullptr;
    }
}
