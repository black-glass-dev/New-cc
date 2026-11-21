#pragma once

#include "timeline/retime_engine.hpp"
#include "timeline/timeline_model.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace owncut::timeline {

enum class KeyframeInterpolation : uint8_t {
    Linear = 0,
    Hold = 1,
    EaseIn = 2,
    EaseOut = 3,
    EaseInOut = 4,
    CustomBezier = 5
};

enum class KeyframeLoopMode : uint8_t {
    None = 0,
    Loop = 1,
    PingPong = 2,
    OffsetRepeat = 3
};

struct Keyframe {
    RationalTime time{}; // Relative to clip start
    double value = 0.0;
    KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
    CubicBezierCurve bezier{};
    bool selected = false;
};

class KeyframedProperty {
public:
    explicit KeyframedProperty(double default_val = 0.0) : default_value_(default_val) {}

    void set_default(double v) noexcept { default_value_ = v; }
    [[nodiscard]] double default_value() const noexcept { return default_value_; }
    void set_loop_mode(KeyframeLoopMode mode) noexcept { loop_mode_ = mode; }
    [[nodiscard]] KeyframeLoopMode loop_mode() const noexcept { return loop_mode_; }

    void add_or_update(const RationalTime& t, double value,
                       KeyframeInterpolation interp = KeyframeInterpolation::Linear,
                       CubicBezierCurve bezier = {});
    bool remove_at(const RationalTime& t, const RationalTime& tolerance = RationalTime(1, 1000));
    void clear() { keyframes_.clear(); }

    [[nodiscard]] double evaluate(const RationalTime& t) const noexcept;
    [[nodiscard]] std::vector<Keyframe>& keyframes() noexcept { return keyframes_; }
    [[nodiscard]] const std::vector<Keyframe>& keyframes() const noexcept { return keyframes_; }

private:
    double default_value_ = 0.0;
    KeyframeLoopMode loop_mode_ = KeyframeLoopMode::None;
    std::vector<Keyframe> keyframes_;
};

struct TransformSnapshot {
    double pos_x = 0.0;       // Normalized (-1..+1 relative to width) or pixels
    double pos_y = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation_deg = 0.0;
    double anchor_x = 0.5;
    double anchor_y = 0.5;
    double crop_left = 0.0;   // 0..1
    double crop_right = 0.0;  // 0..1
    double crop_top = 0.0;    // 0..1
    double crop_bottom = 0.0; // 0..1
    double opacity = 1.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

struct DynamicZoomSpec {
    bool enabled = false;
    bool ease_in_out = true;
    double start_scale = 1.0;
    double end_scale = 1.25;
    double start_offset_x = 0.0;
    double start_offset_y = 0.0;
    double end_offset_x = 0.0;
    double end_offset_y = 0.0;
};

class AnimatedTransformRig {
public:
    AnimatedTransformRig();

    KeyframedProperty pos_x{0.0};
    KeyframedProperty pos_y{0.0};
    KeyframedProperty scale_x{1.0};
    KeyframedProperty scale_y{1.0};
    KeyframedProperty rotation_deg{0.0};
    KeyframedProperty anchor_x{0.5};
    KeyframedProperty anchor_y{0.5};
    KeyframedProperty crop_left{0.0};
    KeyframedProperty crop_right{0.0};
    KeyframedProperty crop_top{0.0};
    KeyframedProperty crop_bottom{0.0};
    KeyframedProperty opacity{1.0};
    KeyframedProperty pitch{0.0};
    KeyframedProperty yaw{0.0};
    DynamicZoomSpec dynamic_zoom{};

    [[nodiscard]] TransformSnapshot evaluate(const RationalTime& local_t,
                                             const RationalTime& clip_duration) const noexcept;
    static void apply_transform(const VideoFrame& src, const TransformSnapshot& xf,
                                VideoFrame& dst, FrameMemoryPool* pool = nullptr);
};

// Mobile Keyframe Tray navigation & batch editing
class KeyframeTrayController {
public:
    static std::optional<RationalTime> next_keyframe(const KeyframedProperty& prop, const RationalTime& after_t);
    static std::optional<RationalTime> prev_keyframe(const KeyframedProperty& prop, const RationalTime& before_t);
    static std::size_t shift_selected(KeyframedProperty& prop, const RationalTime& delta);
    static std::size_t set_selected_interpolation(KeyframedProperty& prop, KeyframeInterpolation interp);
};

// Inspector Preset with lossless JSON serialization (no third-party JSON library needed)
struct InspectorPreset {
    std::string name;
    std::string category = "Transform";
    BlendMode blend_mode = BlendMode::Normal;
    TransformSnapshot static_transform{};
    DynamicZoomSpec dynamic_zoom{};
    std::map<std::string, double> custom_params;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static std::optional<InspectorPreset> from_json(const std::string& json);
};

enum class TransitionType : uint8_t {
    CrossDissolve = 0,
    DipToBlack = 1,
    DipToWhite = 2,
    WipeLeftToRight = 3,
    WipeTopToBottom = 4,
    PushLeft = 5,
    ZoomIn = 6
};

class TransitionEngine {
public:
    static bool render_transition(const VideoFrame& outgoing,
                                  const VideoFrame& incoming,
                                  float progress,
                                  TransitionType type,
                                  VideoFrame& out_frame,
                                  FrameMemoryPool* pool = nullptr);
};

} // namespace owncut::timeline
