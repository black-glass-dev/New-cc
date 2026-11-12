#include "timeline/persistence.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace owncut::timeline {
namespace {

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::map<std::string, JsonValue> obj_val;

    const JsonValue* get(const std::string& k) const {
        if (kind != Kind::Object) return nullptr;
        const auto it = obj_val.find(k);
        return it == obj_val.end() ? nullptr : &it->second;
    }
    std::string as_str(const std::string& def = "") const {
        return kind == Kind::String ? str_val : def;
    }
    double as_num(double def = 0.0) const {
        return kind == Kind::Number ? num_val : def;
    }
    int64_t as_i64(int64_t def = 0) const {
        return kind == Kind::Number ? static_cast<int64_t>(std::llround(num_val)) : def;
    }
    int32_t as_i32(int32_t def = 0) const {
        return kind == Kind::Number ? static_cast<int32_t>(std::llround(num_val)) : def;
    }
    bool as_bool(bool def = false) const {
        return kind == Kind::Bool ? bool_val : def;
    }
};

void append_utf8_cp(std::string& out, uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : s_(src) {}

    bool parse(JsonValue& out) {
        skip_ws();
        if (!parse_value(out, 0)) return false;
        skip_ws();
        return pos_ == s_.size();
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) {
            ++pos_;
        }
    }

    bool parse_value(JsonValue& out, int depth) {
        if (depth > 64 || pos_ >= s_.size()) return false;
        skip_ws();
        if (pos_ >= s_.size()) return false;
        const char ch = s_[pos_];
        if (ch == '"') {
            out = JsonValue{};
            out.kind = JsonValue::Kind::String;
            return parse_string(out.str_val);
        }
        if (ch == '{') return parse_object(out, depth + 1);
        if (ch == '[') return parse_array(out, depth + 1);
        if (ch == 't' && s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = JsonValue{};
            out.kind = JsonValue::Kind::Bool;
            out.bool_val = true;
            return true;
        }
        if (ch == 'f' && s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = JsonValue{};
            out.kind = JsonValue::Kind::Bool;
            out.bool_val = false;
            return true;
        }
        if (ch == 'n' && s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out = JsonValue{};
            out.kind = JsonValue::Kind::Null;
            return true;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return parse_number(out);
        }
        return false;
    }

    bool parse_hex4(uint32_t& cp) {
        if (pos_ + 4 > s_.size()) return false;
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') cp |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parse_string(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                const char esc = s_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!parse_hex4(cp)) return false;
                        if (cp >= 0xD800u && cp <= 0xDBFFu && pos_ + 2 <= s_.size() &&
                            s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = 0;
                            if (!parse_hex4(low)) return false;
                            if (low >= 0xDC00u && low <= 0xDFFFu) {
                                cp = 0x10000u + (((cp - 0xD800u) << 10) | (low - 0xDC00u));
                            }
                        }
                        append_utf8_cp(out, cp);
                        break;
                    }
                    default:
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parse_number(JsonValue& out) {
        const std::size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return false;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return false;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return false;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        const std::string token = s_.substr(start, pos_ - start);
        char* endp = nullptr;
        const double v = std::strtod(token.c_str(), &endp);
        if (endp == token.c_str() || !std::isfinite(v)) return false;
        out = JsonValue{};
        out.kind = JsonValue::Kind::Number;
        out.num_val = v;
        return true;
    }

    bool parse_array(JsonValue& out, int depth) {
        if (s_[pos_] != '[') return false;
        ++pos_;
        out = JsonValue{};
        out.kind = JsonValue::Kind::Array;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            JsonValue elem;
            if (!parse_value(elem, depth)) return false;
            out.arr_val.push_back(std::move(elem));
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                skip_ws();
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
        return false;
    }

    bool parse_object(JsonValue& out, int depth) {
        if (s_[pos_] != '{') return false;
        ++pos_;
        out = JsonValue{};
        out.kind = JsonValue::Kind::Object;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            JsonValue val;
            if (!parse_value(val, depth)) return false;
            out.obj_val.insert_or_assign(std::move(key), std::move(val));
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
        return false;
    }
};

std::string track_type_str(TrackType t) {
    switch (t) {
        case TrackType::Video: return "video";
        case TrackType::Audio: return "audio";
        case TrackType::Subtitle: return "subtitle";
        case TrackType::Adjustment: return "adjustment";
    }
    return "video";
}

TrackType parse_track_type(const std::string& s) {
    if (s == "audio") return TrackType::Audio;
    if (s == "subtitle") return TrackType::Subtitle;
    if (s == "adjustment") return TrackType::Adjustment;
    return TrackType::Video;
}

} // namespace

std::string TimelineSerializer::escape_json(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (unsigned char c : raw) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20u) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string TimelineSerializer::to_json(const Timeline& tl) {
    std::ostringstream os;
    os << "{\"format\":\"owncut_timeline_v1\","
       << "\"id\":" << tl.id() << ","
       << "\"name\":\"" << escape_json(tl.name()) << "\","
       << "\"fps_num\":" << tl.fps().num << ","
       << "\"fps_den\":" << tl.fps().den << ","
       << "\"width\":" << tl.width() << ","
       << "\"height\":" << tl.height() << ","
       << "\"playhead_ms\":" << tl.playhead().to_milliseconds() << ","
       << "\"tracks\":[";
    bool first_tr = true;
    for (const auto& tr : tl.tracks()) {
        if (!first_tr) os << ',';
        first_tr = false;
        os << "{\"id\":" << tr.id << ","
           << "\"type\":\"" << track_type_str(tr.type) << "\","
           << "\"index\":" << tr.index << ","
           << "\"name\":\"" << escape_json(tr.name) << "\","
           << "\"locked\":" << (tr.locked ? "true" : "false") << ","
           << "\"muted\":" << (tr.muted ? "true" : "false") << ","
           << "\"soloed\":" << (tr.soloed ? "true" : "false") << ","
           << "\"clips\":[";
        bool first_cl = true;
        for (const auto& c : tr.clips) {
            if (!first_cl) os << ',';
            first_cl = false;
            os << "{\"id\":" << c.id << ","
               << "\"media_id\":" << c.media_id << ","
               << "\"link_group_id\":" << c.link_group_id << ","
               << "\"name\":\"" << escape_json(c.name) << "\","
               << "\"timeline_start_num\":" << c.timeline_start.num() << ","
               << "\"timeline_start_den\":" << c.timeline_start.den() << ","
               << "\"timeline_dur_num\":" << c.timeline_duration.num() << ","
               << "\"timeline_dur_den\":" << c.timeline_duration.den() << ","
               << "\"source_in_num\":" << c.source_in.num() << ","
               << "\"source_in_den\":" << c.source_in.den() << ","
               << "\"source_out_num\":" << c.source_out.num() << ","
               << "\"source_out_den\":" << c.source_out.den() << ","
               << "\"speed\":" << c.speed << ","
               << "\"reversed\":" << (c.reversed ? "true" : "false") << ","
               << "\"opacity\":" << c.opacity << ","
               << "\"blend_mode\":" << static_cast<int>(c.blend_mode) << ","
               << "\"gain_db\":" << c.gain_db << ","
               << "\"muted\":" << (c.muted ? "true" : "false") << ","
               << "\"metadata\":{";
            bool first_m = true;
            for (const auto& kv : c.metadata) {
                if (!first_m) os << ',';
                first_m = false;
                os << "\"" << escape_json(kv.first) << "\":\"" << escape_json(kv.second) << "\"";
            }
            os << "}}";
        }
        os << "]}";
    }
    os << "],\"markers\":[";
    bool first_mk = true;
    for (const auto& m : tl.markers()) {
        if (!first_mk) os << ',';
        first_mk = false;
        os << "{\"id\":" << m.id << ","
           << "\"pos_ms\":" << m.time.to_milliseconds() << ","
           << "\"name\":\"" << escape_json(m.name) << "\","
           << "\"note\":\"" << escape_json(m.note) << "\"}";
    }
    os << "]}";
    return os.str();
}

bool TimelineSerializer::from_json(const std::string& json, Timeline& out_tl) {
    JsonValue root;
    if (!JsonParser(json).parse(root) || root.kind != JsonValue::Kind::Object) return false;
    const auto* fmt = root.get("format");
    if (!fmt || fmt->as_str() != "owncut_timeline_v1") return false;

    const uint64_t id = static_cast<uint64_t>(std::max<int64_t>(1, root.get("id") ? root.get("id")->as_i64(1) : 1));
    const std::string name = root.get("name") ? root.get("name")->as_str("Timeline 1") : "Timeline 1";
    const int32_t fps_num = root.get("fps_num") ? root.get("fps_num")->as_i32(30) : 30;
    const int32_t fps_den = std::max<int32_t>(1, root.get("fps_den") ? root.get("fps_den")->as_i32(1) : 1);
    const int32_t w = std::max<int32_t>(16, root.get("width") ? root.get("width")->as_i32(1920) : 1920);
    const int32_t h = std::max<int32_t>(16, root.get("height") ? root.get("height")->as_i32(1080) : 1080);

    Timeline tl(id, name, FrameRate{fps_num, fps_den}, w, h);
    if (const auto* ph = root.get("playhead_ms")) {
        tl.set_playhead(RationalTime::from_milliseconds(ph->as_i64(0)));
    }

    if (const auto* trs = root.get("tracks"); trs && trs->kind == JsonValue::Kind::Array) {
        for (const auto& tj : trs->arr_val) {
            if (tj.kind != JsonValue::Kind::Object) continue;
            const TrackType tt = parse_track_type(tj.get("type") ? tj.get("type")->as_str("video") : "video");
            const std::string tname = tj.get("name") ? tj.get("name")->as_str() : "";
            Track& tr = tl.add_track(tt, tname);
            if (const auto* lk = tj.get("locked")) tr.locked = lk->as_bool(false);
            if (const auto* mt = tj.get("muted")) tr.muted = mt->as_bool(false);
            if (const auto* sl = tj.get("soloed")) tr.soloed = sl->as_bool(false);

            if (const auto* cls = tj.get("clips"); cls && cls->kind == JsonValue::Kind::Array) {
                for (const auto& cj : cls->arr_val) {
                    if (cj.kind != JsonValue::Kind::Object) continue;
                    ClipItem c;
                    c.id = cj.get("id") ? static_cast<uint64_t>(std::max<int64_t>(0, cj.get("id")->as_i64(0))) : 0;
                    if (c.id == 0) c.id = tl.allocate_clip_id();
                    c.media_id = cj.get("media_id") ? static_cast<uint64_t>(std::max<int64_t>(0, cj.get("media_id")->as_i64(0))) : 0;
                    c.link_group_id = cj.get("link_group_id") ? static_cast<uint64_t>(std::max<int64_t>(0, cj.get("link_group_id")->as_i64(0))) : 0;
                    c.name = cj.get("name") ? cj.get("name")->as_str() : "";

                    auto read_rt = [&](const char* num_k, const char* den_k) -> RationalTime {
                        const int64_t num = cj.get(num_k) ? cj.get(num_k)->as_i64(0) : 0;
                        const int64_t den = std::max<int64_t>(1, cj.get(den_k) ? cj.get(den_k)->as_i64(1000) : 1000);
                        return RationalTime{num, den};
                    };
                    c.timeline_start = read_rt("timeline_start_num", "timeline_start_den");
                    c.timeline_duration = read_rt("timeline_dur_num", "timeline_dur_den");
                    c.source_in = read_rt("source_in_num", "source_in_den");
                    c.source_out = read_rt("source_out_num", "source_out_den");
                    c.speed = cj.get("speed") ? cj.get("speed")->as_num(1.0) : 1.0;
                    c.reversed = cj.get("reversed") ? cj.get("reversed")->as_bool(false) : false;
                    c.opacity = static_cast<float>(cj.get("opacity") ? cj.get("opacity")->as_num(1.0) : 1.0);
                    c.blend_mode = static_cast<BlendMode>(cj.get("blend_mode") ? cj.get("blend_mode")->as_i32(0) : 0);
                    c.gain_db = static_cast<float>(cj.get("gain_db") ? cj.get("gain_db")->as_num(0.0) : 0.0);
                    c.muted = cj.get("muted") ? cj.get("muted")->as_bool(false) : false;

                    if (const auto* meta = cj.get("metadata"); meta && meta->kind == JsonValue::Kind::Object) {
                        for (const auto& kv : meta->obj_val) {
                            if (kv.second.kind == JsonValue::Kind::String) {
                                c.metadata.insert_or_assign(kv.first, kv.second.str_val);
                            }
                        }
                    }
                    tr.clips.push_back(std::move(c));
                }
                tr.sort_clips();
            }
        }
    }

    if (const auto* mks = root.get("markers"); mks && mks->kind == JsonValue::Kind::Array) {
        for (const auto& mj : mks->arr_val) {
            if (mj.kind != JsonValue::Kind::Object) continue;
            Marker m;
            m.id = mj.get("id") ? static_cast<uint64_t>(std::max<int64_t>(0, mj.get("id")->as_i64(0))) : 0;
            m.time = RationalTime::from_milliseconds(mj.get("pos_ms") ? mj.get("pos_ms")->as_i64(0) : 0);
            m.name = mj.get("name") ? mj.get("name")->as_str() : "";
            m.note = mj.get("note") ? mj.get("note")->as_str() : (mj.get("comment") ? mj.get("comment")->as_str() : "");
            tl.add_marker(std::move(m));
        }
    }

    out_tl = std::move(tl);
    return true;
}

std::string TimelineSerializer::state_to_json(const OwncutRichState& st) {
    std::ostringstream os;
    os << "{\"format\":\"owncut_tl_state_v1\","
       << "\"version\":" << st.schema_version << ","
       << "\"snap_enabled\":" << (st.snap_enabled ? "true" : "false") << ","
       << "\"beat_bpm\":" << st.beat_bpm << ","
       << "\"beat_offset_ms\":" << st.beat_offset_ms << ","
       << "\"clip_fx\":[";
    for (std::size_t i = 0; i < st.clip_fx.size(); ++i) {
        const auto& f = st.clip_fx[i];
        if (i > 0) os << ',';
        os << "{\"track_id\":\"" << escape_json(f.track_id) << "\","
           << "\"clip_id\":\"" << escape_json(f.clip_id) << "\","
           << "\"speed\":" << f.speed << ","
           << "\"reversed\":" << (f.reversed ? "true" : "false") << ","
           << "\"freeze_ms\":" << f.freeze_ms << ","
           << "\"volume\":" << f.volume << ","
           << "\"fade_in_ms\":" << f.fade_in_ms << ","
           << "\"fade_out_ms\":" << f.fade_out_ms << ","
           << "\"muted\":" << (f.muted ? "true" : "false") << ","
           << "\"ducking_db\":" << f.ducking_db << ","
           << "\"opacity\":" << f.opacity << ","
           << "\"blend_mode\":\"" << escape_json(f.blend_mode) << "\","
           << "\"scale\":" << f.scale << ","
           << "\"pan_x\":" << f.pan_x << ","
           << "\"pan_y\":" << f.pan_y << ","
           << "\"rotation_deg\":" << f.rotation_deg << ","
           << "\"dynamic_zoom_start\":" << f.dynamic_zoom_start << ","
           << "\"dynamic_zoom_end\":" << f.dynamic_zoom_end << ","
           << "\"transition_kind\":\"" << escape_json(f.transition_kind) << "\","
           << "\"transition_ms\":" << f.transition_ms << ","
           << "\"chroma_key_hex\":\"" << escape_json(f.chroma_key_hex) << "\","
           << "\"chroma_tolerance\":" << f.chroma_tolerance << ","
           << "\"audio_peaks\":[";
        for (std::size_t p = 0; p < f.audio_peaks.size(); ++p) {
            if (p > 0) os << ',';
            os << f.audio_peaks[p];
        }
        os << "]}";
    }
    os << "],\"overlays\":[";
    for (std::size_t i = 0; i < st.overlays.size(); ++i) {
        const auto& o = st.overlays[i];
        if (i > 0) os << ',';
        os << "{\"id\":\"" << escape_json(o.id) << "\","
           << "\"text\":\"" << escape_json(o.text) << "\","
           << "\"start_ms\":" << o.start_ms << ","
           << "\"duration_ms\":" << o.duration_ms << ","
           << "\"x\":" << o.x << ","
           << "\"y\":" << o.y << ","
           << "\"scale\":" << o.scale << ","
           << "\"rotation_deg\":" << o.rotation_deg << ","
           << "\"fg_hex\":\"" << escape_json(o.fg_hex) << "\","
           << "\"bg_hex\":\"" << escape_json(o.bg_hex) << "\","
           << "\"style\":\"" << escape_json(o.style) << "\","
           << "\"bold\":" << (o.bold ? "true" : "false") << "}";
    }
    os << "],\"subtitles\":[";
    for (std::size_t i = 0; i < st.subtitles.size(); ++i) {
        const auto& s = st.subtitles[i];
        if (i > 0) os << ',';
        os << "{\"id\":\"" << escape_json(s.id) << "\","
           << "\"start_ms\":" << s.start_ms << ","
           << "\"end_ms\":" << s.end_ms << ","
           << "\"text\":\"" << escape_json(s.text) << "\"}";
    }
    os << "],\"markers\":[";
    for (std::size_t i = 0; i < st.markers.size(); ++i) {
        const auto& m = st.markers[i];
        if (i > 0) os << ',';
        os << "{\"id\":\"" << escape_json(m.id) << "\","
           << "\"position_ms\":" << m.position_ms << ","
           << "\"label\":\"" << escape_json(m.label) << "\","
           << "\"color_hex\":\"" << escape_json(m.color_hex) << "\"}";
    }
    os << "]}";
    return os.str();
}

bool TimelineSerializer::state_from_json(const std::string& json, OwncutRichState& out_state) {
    JsonValue root;
    if (!JsonParser(json).parse(root) || root.kind != JsonValue::Kind::Object) return false;
    const auto* fmt = root.get("format");
    if (!fmt || fmt->as_str() != "owncut_tl_state_v1") return false;

    OwncutRichState st;
    st.schema_version = root.get("version") ? root.get("version")->as_i32(1) : 1;
    st.snap_enabled = root.get("snap_enabled") ? root.get("snap_enabled")->as_bool(true) : true;
    st.beat_bpm = root.get("beat_bpm") ? root.get("beat_bpm")->as_num(0.0) : 0.0;
    st.beat_offset_ms = root.get("beat_offset_ms") ? root.get("beat_offset_ms")->as_i64(0) : 0;

    if (const auto* fxs = root.get("clip_fx"); fxs && fxs->kind == JsonValue::Kind::Array) {
        for (const auto& fj : fxs->arr_val) {
            if (fj.kind != JsonValue::Kind::Object) continue;
            RichClipFx f;
            f.track_id = fj.get("track_id") ? fj.get("track_id")->as_str("v1") : "v1";
            f.clip_id = fj.get("clip_id") ? fj.get("clip_id")->as_str() : "";
            f.speed = fj.get("speed") ? fj.get("speed")->as_num(1.0) : 1.0;
            f.reversed = fj.get("reversed") ? fj.get("reversed")->as_bool(false) : false;
            f.freeze_ms = fj.get("freeze_ms") ? fj.get("freeze_ms")->as_i64(-1) : -1;
            f.volume = fj.get("volume") ? fj.get("volume")->as_num(1.0) : 1.0;
            f.fade_in_ms = fj.get("fade_in_ms") ? fj.get("fade_in_ms")->as_i64(0) : 0;
            f.fade_out_ms = fj.get("fade_out_ms") ? fj.get("fade_out_ms")->as_i64(0) : 0;
            f.muted = fj.get("muted") ? fj.get("muted")->as_bool(false) : false;
            f.ducking_db = fj.get("ducking_db") ? fj.get("ducking_db")->as_num(0.0) : 0.0;
            f.opacity = fj.get("opacity") ? fj.get("opacity")->as_num(1.0) : 1.0;
            f.blend_mode = fj.get("blend_mode") ? fj.get("blend_mode")->as_str("normal") : "normal";
            f.scale = fj.get("scale") ? fj.get("scale")->as_num(1.0) : 1.0;
            f.pan_x = fj.get("pan_x") ? fj.get("pan_x")->as_num(0.0) : 0.0;
            f.pan_y = fj.get("pan_y") ? fj.get("pan_y")->as_num(0.0) : 0.0;
            f.rotation_deg = fj.get("rotation_deg") ? fj.get("rotation_deg")->as_num(0.0) : 0.0;
            f.dynamic_zoom_start = fj.get("dynamic_zoom_start") ? fj.get("dynamic_zoom_start")->as_num(1.0) : 1.0;
            f.dynamic_zoom_end = fj.get("dynamic_zoom_end") ? fj.get("dynamic_zoom_end")->as_num(1.0) : 1.0;
            f.transition_kind = fj.get("transition_kind") ? fj.get("transition_kind")->as_str("none") : "none";
            f.transition_ms = fj.get("transition_ms") ? fj.get("transition_ms")->as_i64(0) : 0;
            f.chroma_key_hex = fj.get("chroma_key_hex") ? fj.get("chroma_key_hex")->as_str() : "";
            f.chroma_tolerance = fj.get("chroma_tolerance") ? fj.get("chroma_tolerance")->as_num(0.0) : 0.0;
            if (const auto* pk = fj.get("audio_peaks"); pk && pk->kind == JsonValue::Kind::Array) {
                for (const auto& pv : pk->arr_val) {
                    f.audio_peaks.push_back(pv.as_num(0.0));
                }
            }
            if (!f.clip_id.empty()) st.clip_fx.push_back(std::move(f));
        }
    }

    if (const auto* ovs = root.get("overlays"); ovs && ovs->kind == JsonValue::Kind::Array) {
        for (const auto& oj : ovs->arr_val) {
            if (oj.kind != JsonValue::Kind::Object) continue;
            RichTextOverlay o;
            o.id = oj.get("id") ? oj.get("id")->as_str() : "";
            o.text = oj.get("text") ? oj.get("text")->as_str() : "";
            o.start_ms = oj.get("start_ms") ? oj.get("start_ms")->as_i64(0) : 0;
            o.duration_ms = oj.get("duration_ms") ? oj.get("duration_ms")->as_i64(2000) : 2000;
            o.x = oj.get("x") ? oj.get("x")->as_num(0.5) : 0.5;
            o.y = oj.get("y") ? oj.get("y")->as_num(0.82) : 0.82;
            o.scale = oj.get("scale") ? oj.get("scale")->as_num(1.0) : 1.0;
            o.rotation_deg = oj.get("rotation_deg") ? oj.get("rotation_deg")->as_num(0.0) : 0.0;
            o.fg_hex = oj.get("fg_hex") ? oj.get("fg_hex")->as_str("#FFFFFF") : "#FFFFFF";
            o.bg_hex = oj.get("bg_hex") ? oj.get("bg_hex")->as_str("#B8000000") : "#B8000000";
            o.style = oj.get("style") ? oj.get("style")->as_str("title") : "title";
            o.bold = oj.get("bold") ? oj.get("bold")->as_bool(true) : true;
            if (!o.id.empty()) st.overlays.push_back(std::move(o));
        }
    }

    if (const auto* sbs = root.get("subtitles"); sbs && sbs->kind == JsonValue::Kind::Array) {
        for (const auto& sj : sbs->arr_val) {
            if (sj.kind != JsonValue::Kind::Object) continue;
            RichSubtitleCue s;
            s.id = sj.get("id") ? sj.get("id")->as_str() : "";
            s.start_ms = sj.get("start_ms") ? sj.get("start_ms")->as_i64(0) : 0;
            s.end_ms = sj.get("end_ms") ? sj.get("end_ms")->as_i64(2000) : 2000;
            s.text = sj.get("text") ? sj.get("text")->as_str() : "";
            if (!s.id.empty()) st.subtitles.push_back(std::move(s));
        }
    }

    if (const auto* mks = root.get("markers"); mks && mks->kind == JsonValue::Kind::Array) {
        for (const auto& mj : mks->arr_val) {
            if (mj.kind != JsonValue::Kind::Object) continue;
            RichMarker m;
            m.id = mj.get("id") ? mj.get("id")->as_str() : "";
            m.position_ms = mj.get("position_ms") ? mj.get("position_ms")->as_i64(0) : 0;
            m.label = mj.get("label") ? mj.get("label")->as_str() : "";
            m.color_hex = mj.get("color_hex") ? mj.get("color_hex")->as_str("#4F9CF9") : "#4F9CF9";
            if (!m.id.empty()) st.markers.push_back(std::move(m));
        }
    }

    out_state = std::move(st);
    return true;
}

bool TimelineSerializer::write_atomic_utf8(const std::string& path, const std::string& content) {
    if (path.empty()) return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.flush();
        if (!ofs.good()) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool TimelineSerializer::read_utf8_file(const std::string& path, std::string& out_content) {
    if (path.empty()) return false;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out_content = ss.str();
    return !ifs.bad();
}

} // namespace owncut::timeline
