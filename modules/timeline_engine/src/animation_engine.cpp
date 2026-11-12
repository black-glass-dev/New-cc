#include "timeline/animation_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace owncut::timeline {
namespace {
constexpr double kPi = 3.14159265358979323846;

double interp_curve(KeyframeInterpolation mode, double u, const CubicBezierCurve& custom) noexcept {
    u = std::clamp(u, 0.0, 1.0);
    switch (mode) {
        case KeyframeInterpolation::Hold:
            return (u >= 1.0) ? 1.0 : 0.0;
        case KeyframeInterpolation::Linear:
            return u;
        case KeyframeInterpolation::EaseIn:
            return CubicBezierCurve{0.42, 0.0, 1.0, 1.0}.evaluate(u);
        case KeyframeInterpolation::EaseOut:
            return CubicBezierCurve{0.0, 0.0, 0.58, 1.0}.evaluate(u);
        case KeyframeInterpolation::EaseInOut:
            return CubicBezierCurve{0.42, 0.0, 0.58, 1.0}.evaluate(u);
        case KeyframeInterpolation::CustomBezier:
            return custom.evaluate(u);
    }
    return u;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::optional<std::string> extract_json_string(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            val.push_back(json[pos] == 'n' ? '\n' : json[pos]);
        } else {
            val.push_back(json[pos]);
        }
        ++pos;
    }
    return val;
}

std::optional<double> extract_json_number(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return std::nullopt;
    if (json.compare(pos, 4, "true") == 0) return 1.0;
    if (json.compare(pos, 5, "false") == 0) return 0.0;
    char* end_ptr = nullptr;
    const double val = std::strtod(json.c_str() + pos, &end_ptr);
    if (end_ptr == json.c_str() + pos) return std::nullopt;
    return val;
}

void sample_rgba_clamp(const VideoFrame& src, float x, float y, float out[4]) noexcept {
    if (x < 0.0f || y < 0.0f || x >= static_cast<float>(src.width) || y >= static_cast<float>(src.height)) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const int32_t y0 = static_cast<int32_t>(std::floor(y));
    const int32_t x1 = std::min(src.width - 1, x0 + 1);
    const int32_t y1 = std::min(src.height - 1, y0 + 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float w00 = (1.0f - fx) * (1.0f - fy);
    const float w10 = fx * (1.0f - fy);
    const float w01 = (1.0f - fx) * fy;
    const float w11 = fx * fy;
    const float* p00 = src.data() + y0 * src.stride_floats + x0 * 4;
    const float* p10 = src.data() + y0 * src.stride_floats + x1 * 4;
    const float* p01 = src.data() + y1 * src.stride_floats + x0 * 4;
    const float* p11 = src.data() + y1 * src.stride_floats + x1 * 4;
    for (int c = 0; c < 4; ++c) {
        out[c] = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
    }
}

} // namespace

void KeyframedProperty::add_or_update(const RationalTime& t, double value,
                                      KeyframeInterpolation interp, CubicBezierCurve bezier) {
    for (auto& kf : keyframes_) {
        if (kf.time == t) {
            kf.value = value;
            kf.interpolation = interp;
            kf.bezier = bezier;
            return;
        }
    }
    keyframes_.push_back(Keyframe{t, value, interp, bezier, false});
    std::sort(keyframes_.begin(), keyframes_.end(), [](const Keyframe& a, const Keyframe& b) {
        return a.time < b.time;
    });
}

bool KeyframedProperty::remove_at(const RationalTime& t, const RationalTime& tolerance) {
    auto it = std::find_if(keyframes_.begin(), keyframes_.end(), [&](const Keyframe& kf) {
        return (kf.time - t).abs() <= tolerance;
    });
    if (it == keyframes_.end()) return false;
    keyframes_.erase(it);
    return true;
}

double KeyframedProperty::evaluate(const RationalTime& t) const noexcept {
    if (keyframes_.empty()) return default_value_;
    if (keyframes_.size() == 1) return keyframes_.front().value;

    double query_s = t.to_seconds();
    const double t_first = keyframes_.front().time.to_seconds();
    const double t_last = keyframes_.back().time.to_seconds();
    const double span = t_last - t_first;
    double offset_accum = 0.0;

    if (query_s > t_last && span > 1e-9 && loop_mode_ != KeyframeLoopMode::None) {
        const double elapsed = query_s - t_first;
        const int64_t cycle = static_cast<int64_t>(std::floor(elapsed / span));
        const double rem = elapsed - static_cast<double>(cycle) * span;
        if (loop_mode_ == KeyframeLoopMode::Loop) {
            query_s = t_first + rem;
        } else if (loop_mode_ == KeyframeLoopMode::PingPong) {
            query_s = (cycle % 2 == 0) ? (t_first + rem) : (t_last - rem);
        } else if (loop_mode_ == KeyframeLoopMode::OffsetRepeat) {
            query_s = t_first + rem;
            offset_accum = static_cast<double>(cycle) * (keyframes_.back().value - keyframes_.front().value);
        }
    }

    if (query_s <= t_first) return keyframes_.front().value;
    if (query_s >= t_last) return keyframes_.back().value + offset_accum;

    for (std::size_t i = 0; i + 1 < keyframes_.size(); ++i) {
        const double t0 = keyframes_[i].time.to_seconds();
        const double t1 = keyframes_[i + 1].time.to_seconds();
        if (query_s >= t0 && query_s <= t1) {
            const double dt = std::max(1e-9, t1 - t0);
            const double u = (query_s - t0) / dt;
            const double k = interp_curve(keyframes_[i].interpolation, u, keyframes_[i].bezier);
            return keyframes_[i].value + (keyframes_[i + 1].value - keyframes_[i].value) * k + offset_accum;
        }
    }
    return keyframes_.back().value + offset_accum;
}

AnimatedTransformRig::AnimatedTransformRig() = default;

TransformSnapshot AnimatedTransformRig::evaluate(const RationalTime& local_t,
                                                 const RationalTime& clip_duration) const noexcept {
    TransformSnapshot snap;
    snap.pos_x = pos_x.evaluate(local_t);
    snap.pos_y = pos_y.evaluate(local_t);
    snap.scale_x = scale_x.evaluate(local_t);
    snap.scale_y = scale_y.evaluate(local_t);
    snap.rotation_deg = rotation_deg.evaluate(local_t);
    snap.anchor_x = anchor_x.evaluate(local_t);
    snap.anchor_y = anchor_y.evaluate(local_t);
    snap.crop_left = crop_left.evaluate(local_t);
    snap.crop_right = crop_right.evaluate(local_t);
    snap.crop_top = crop_top.evaluate(local_t);
    snap.crop_bottom = crop_bottom.evaluate(local_t);
    snap.opacity = std::clamp(opacity.evaluate(local_t), 0.0, 1.0);
    snap.pitch = pitch.evaluate(local_t);
    snap.yaw = yaw.evaluate(local_t);

    if (dynamic_zoom.enabled && clip_duration.num() > 0) {
        const double dur_s = std::max(1e-6, clip_duration.to_seconds());
        double u = std::clamp(local_t.to_seconds() / dur_s, 0.0, 1.0);
        if (dynamic_zoom.ease_in_out) {
            u = CubicBezierCurve{0.42, 0.0, 0.58, 1.0}.evaluate(u);
        }
        const double dz_scale = dynamic_zoom.start_scale + (dynamic_zoom.end_scale - dynamic_zoom.start_scale) * u;
        const double dz_ox = dynamic_zoom.start_offset_x + (dynamic_zoom.end_offset_x - dynamic_zoom.start_offset_x) * u;
        const double dz_oy = dynamic_zoom.start_offset_y + (dynamic_zoom.end_offset_y - dynamic_zoom.start_offset_y) * u;
        snap.scale_x *= dz_scale;
        snap.scale_y *= dz_scale;
        snap.pos_x += dz_ox;
        snap.pos_y += dz_oy;
    }
    return snap;
}

void AnimatedTransformRig::apply_transform(const VideoFrame& src, const TransformSnapshot& xf,
                                           VideoFrame& dst, FrameMemoryPool* pool) {
    if (!src.valid()) return;
    dst = VideoFrame::allocate(src.width, src.height, pool);
    if (!dst.valid()) return;
    dst.pts = src.pts;

    const float w = static_cast<float>(src.width);
    const float h = static_cast<float>(src.height);
    const float cx = static_cast<float>(xf.anchor_x) * w;
    const float cy = static_cast<float>(xf.anchor_y) * h;
    const float rad = static_cast<float>(-xf.rotation_deg * kPi / 180.0);
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);
    const float inv_sx = 1.0f / static_cast<float>(std::max(1e-4, xf.scale_x));
    const float inv_sy = 1.0f / static_cast<float>(std::max(1e-4, xf.scale_y));
    const float tx = static_cast<float>(xf.pos_x) * w;
    const float ty = static_cast<float>(xf.pos_y) * h;
    const float op = static_cast<float>(std::clamp(xf.opacity, 0.0, 1.0));

    const float crop_x0 = static_cast<float>(xf.crop_left) * w;
    const float crop_x1 = w - static_cast<float>(xf.crop_right) * w;
    const float crop_y0 = static_cast<float>(xf.crop_top) * h;
    const float crop_y1 = h - static_cast<float>(xf.crop_bottom) * h;

    float* dp = dst.data();
    for (int32_t y = 0; y < dst.height; ++y) {
        float* row = dp + y * dst.stride_floats;
        for (int32_t x = 0; x < dst.width; ++x) {
            const float dx = static_cast<float>(x) - cx - tx;
            const float dy = static_cast<float>(y) - cy - ty;
            const float rx = (dx * cs - dy * sn) * inv_sx + cx;
            const float ry = (dx * sn + dy * cs) * inv_sy + cy;

            if (rx < crop_x0 || rx >= crop_x1 || ry < crop_y0 || ry >= crop_y1) {
                row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = row[x * 4 + 3] = 0.0f;
                continue;
            }
            float rgba[4]{};
            sample_rgba_clamp(src, rx, ry, rgba);
            row[x * 4 + 0] = rgba[0];
            row[x * 4 + 1] = rgba[1];
            row[x * 4 + 2] = rgba[2];
            row[x * 4 + 3] = rgba[3] * op;
        }
    }
}

std::optional<RationalTime> KeyframeTrayController::next_keyframe(const KeyframedProperty& prop, const RationalTime& after_t) {
    for (const auto& kf : prop.keyframes()) {
        if (kf.time > after_t) return kf.time;
    }
    return std::nullopt;
}

std::optional<RationalTime> KeyframeTrayController::prev_keyframe(const KeyframedProperty& prop, const RationalTime& before_t) {
    for (auto it = prop.keyframes().rbegin(); it != prop.keyframes().rend(); ++it) {
        if (it->time < before_t) return it->time;
    }
    return std::nullopt;
}

std::size_t KeyframeTrayController::shift_selected(KeyframedProperty& prop, const RationalTime& delta) {
    std::size_t count = 0;
    for (auto& kf : prop.keyframes()) {
        if (kf.selected) {
            kf.time = std::max(RationalTime::zero(), kf.time + delta);
            ++count;
        }
    }
    std::sort(prop.keyframes().begin(), prop.keyframes().end(), [](const Keyframe& a, const Keyframe& b) {
        return a.time < b.time;
    });
    return count;
}

std::size_t KeyframeTrayController::set_selected_interpolation(KeyframedProperty& prop, KeyframeInterpolation interp) {
    std::size_t count = 0;
    for (auto& kf : prop.keyframes()) {
        if (kf.selected) {
            kf.interpolation = interp;
            ++count;
        }
    }
    return count;
}

std::string InspectorPreset::to_json() const {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"name\": \"" << json_escape(name) << "\",\n"
        << "  \"category\": \"" << json_escape(category) << "\",\n"
        << "  \"blend_mode\": " << static_cast<int>(blend_mode) << ",\n"
        << "  \"pos_x\": " << static_transform.pos_x << ",\n"
        << "  \"pos_y\": " << static_transform.pos_y << ",\n"
        << "  \"scale_x\": " << static_transform.scale_x << ",\n"
        << "  \"scale_y\": " << static_transform.scale_y << ",\n"
        << "  \"rotation_deg\": " << static_transform.rotation_deg << ",\n"
        << "  \"opacity\": " << static_transform.opacity << ",\n"
        << "  \"dynamic_zoom_enabled\": " << (dynamic_zoom.enabled ? "true" : "false") << ",\n"
        << "  \"dynamic_zoom_start_scale\": " << dynamic_zoom.start_scale << ",\n"
        << "  \"dynamic_zoom_end_scale\": " << dynamic_zoom.end_scale << "\n"
        << "}";
    return oss.str();
}

std::optional<InspectorPreset> InspectorPreset::from_json(const std::string& json) {
    auto n = extract_json_string(json, "name");
    if (!n.has_value()) return std::nullopt;
    InspectorPreset p;
    p.name = *n;
    if (auto c = extract_json_string(json, "category")) p.category = *c;
    if (auto bm = extract_json_number(json, "blend_mode")) {
        p.blend_mode = static_cast<BlendMode>(static_cast<int>(*bm));
    }
    if (auto v = extract_json_number(json, "pos_x")) p.static_transform.pos_x = *v;
    if (auto v = extract_json_number(json, "pos_y")) p.static_transform.pos_y = *v;
    if (auto v = extract_json_number(json, "scale_x")) p.static_transform.scale_x = *v;
    if (auto v = extract_json_number(json, "scale_y")) p.static_transform.scale_y = *v;
    if (auto v = extract_json_number(json, "rotation_deg")) p.static_transform.rotation_deg = *v;
    if (auto v = extract_json_number(json, "opacity")) p.static_transform.opacity = *v;
    if (auto v = extract_json_number(json, "dynamic_zoom_enabled")) p.dynamic_zoom.enabled = (*v > 0.5);
    if (auto v = extract_json_number(json, "dynamic_zoom_start_scale")) p.dynamic_zoom.start_scale = *v;
    if (auto v = extract_json_number(json, "dynamic_zoom_end_scale")) p.dynamic_zoom.end_scale = *v;
    return p;
}

bool TransitionEngine::render_transition(const VideoFrame& outgoing,
                                         const VideoFrame& incoming,
                                         float progress,
                                         TransitionType type,
                                         VideoFrame& out_frame,
                                         FrameMemoryPool* pool) {
    if (!outgoing.valid() || !incoming.valid() ||
        outgoing.width != incoming.width || outgoing.height != incoming.height) {
        return false;
    }
    const float t = std::clamp(progress, 0.0f, 1.0f);
    out_frame = VideoFrame::allocate(outgoing.width, outgoing.height, pool);
    if (!out_frame.valid()) return false;

    const int32_t w = outgoing.width;
    const int32_t h = outgoing.height;
    float* dst = out_frame.data();

    for (int32_t y = 0; y < h; ++y) {
        const float* r_out = outgoing.data() + y * outgoing.stride_floats;
        const float* r_in = incoming.data() + y * incoming.stride_floats;
        float* rd = dst + y * out_frame.stride_floats;
        for (int32_t x = 0; x < w; ++x) {
            if (type == TransitionType::CrossDissolve) {
                for (int c = 0; c < 4; ++c) {
                    rd[x * 4 + c] = r_out[x * 4 + c] * (1.0f - t) + r_in[x * 4 + c] * t;
                }
            } else if (type == TransitionType::DipToBlack || type == TransitionType::DipToWhite) {
                const float dip_val = (type == TransitionType::DipToWhite) ? 1.0f : 0.0f;
                if (t < 0.5f) {
                    const float a = t * 2.0f;
                    for (int c = 0; c < 3; ++c) rd[x * 4 + c] = r_out[x * 4 + c] * (1.0f - a) + dip_val * a;
                    rd[x * 4 + 3] = r_out[x * 4 + 3];
                } else {
                    const float a = (t - 0.5f) * 2.0f;
                    for (int c = 0; c < 3; ++c) rd[x * 4 + c] = dip_val * (1.0f - a) + r_in[x * 4 + c] * a;
                    rd[x * 4 + 3] = r_in[x * 4 + 3];
                }
            } else if (type == TransitionType::WipeLeftToRight) {
                const bool use_in = (static_cast<float>(x) / static_cast<float>(std::max(1, w))) <= t;
                const float* src = use_in ? r_in : r_out;
                for (int c = 0; c < 4; ++c) rd[x * 4 + c] = src[x * 4 + c];
            } else if (type == TransitionType::WipeTopToBottom) {
                const bool use_in = (static_cast<float>(y) / static_cast<float>(std::max(1, h))) <= t;
                const float* src = use_in ? r_in : r_out;
                for (int c = 0; c < 4; ++c) rd[x * 4 + c] = src[x * 4 + c];
            } else if (type == TransitionType::PushLeft) {
                const int32_t split_x = static_cast<int32_t>(std::round((1.0f - t) * static_cast<float>(w)));
                if (x < split_x) {
                    const int32_t sx = std::min(w - 1, x + static_cast<int32_t>(std::round(t * static_cast<float>(w))));
                    for (int c = 0; c < 4; ++c) rd[x * 4 + c] = r_out[sx * 4 + c];
                } else {
                    const int32_t sx = std::clamp(x - split_x, 0, w - 1);
                    for (int c = 0; c < 4; ++c) rd[x * 4 + c] = r_in[sx * 4 + c];
                }
            } else { // ZoomIn
                const float zoom = 1.0f + t * 0.35f;
                const float sx = (static_cast<float>(x) - w * 0.5f) / zoom + w * 0.5f;
                const float sy = (static_cast<float>(y) - h * 0.5f) / zoom + h * 0.5f;
                float zoomed_out[4]{};
                sample_rgba_clamp(outgoing, sx, sy, zoomed_out);
                for (int c = 0; c < 4; ++c) {
                    rd[x * 4 + c] = zoomed_out[c] * (1.0f - t) + r_in[x * 4 + c] * t;
                }
            }
        }
    }
    return true;
}

} // namespace owncut::timeline
