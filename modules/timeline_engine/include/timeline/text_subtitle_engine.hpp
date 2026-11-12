#pragma once

#include "timeline/animation_engine.hpp"
#include "timeline/media_pool.hpp"
#include "timeline/rational_time.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace owncut::timeline {

enum class TextAlignment : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2
};

struct TextStyle {
    std::string font_family = "Inter";
    float font_size_pt = 42.0f;
    bool bold = false;
    bool italic = false;
    float color_rgba[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float outline_width = 2.0f;
    float outline_rgba[4]{0.0f, 0.0f, 0.0f, 0.9f};
    float shadow_offset_x = 2.0f;
    float shadow_offset_y = 2.0f;
    float shadow_rgba[4]{0.0f, 0.0f, 0.0f, 0.6f};
    float background_rgba[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float background_padding_px = 10.0f;
    float tracking = 0.0f;
    float line_spacing = 1.2f;
    TextAlignment alignment = TextAlignment::Center;
};

struct TextLayer {
    uint64_t id = 0;
    std::string utf8_text; // Supports English, Bangla (UTF-8), emojis, RTL/LTR scripts
    TextStyle style{};
    AnimatedTransformRig transform{};
    bool visible = true;
};

class MultiTextEngine {
public:
    uint64_t add_layer(TextLayer layer);
    bool remove_layer(uint64_t layer_id);
    TextLayer* find_layer(uint64_t layer_id);
    const std::vector<TextLayer>& layers() const noexcept { return layers_; }

    // Software RGBA32F compositor rendering stylized text boxes / glyph blocks cleanly without external FreeType
    void composite_layers(VideoFrame& canvas, const RationalTime& local_t, const RationalTime& clip_dur) const;

private:
    uint64_t next_id_ = 1;
    std::vector<TextLayer> layers_;
};

struct SubtitleCue {
    uint64_t id = 0;
    RationalTime start{};
    RationalTime end{};
    std::string text;
    std::string speaker;
    std::string style_tag;
};

class SubtitleEngine {
public:
    void add_cue(SubtitleCue cue);
    bool remove_cue(uint64_t id);
    void clear() { cues_.clear(); }
    [[nodiscard]] const std::vector<SubtitleCue>& cues() const noexcept { return cues_; }
    [[nodiscard]] std::vector<const SubtitleCue*> active_cues_at(const RationalTime& t) const;

    bool import_srt(const std::string& srt_content);
    [[nodiscard]] std::string export_srt() const;

    bool import_vtt(const std::string& vtt_content);
    [[nodiscard]] std::string export_vtt() const;

    bool import_ttml(const std::string& ttml_content);
    [[nodiscard]] std::string export_ttml() const;

    // Composite active cues at timeline_t directly into a RGBA32F VideoFrame
    void composite_cues(VideoFrame& canvas, const RationalTime& timeline_t, const TextStyle& style = {}) const;

private:
    uint64_t next_id_ = 1;
    std::vector<SubtitleCue> cues_;
};

enum class MotionTemplateKind : uint8_t {
    Lottie = 0,
    OGraf = 1
};

struct MotionTemplateHandle {
    uint64_t id = 0;
    MotionTemplateKind kind = MotionTemplateKind::Lottie;
    std::string name;
    double duration_sec = 3.0;
    double fps = 30.0;
    int32_t width = 1920;
    int32_t height = 1080;
    std::map<std::string, std::string> dynamic_slots; // Dynamic text/color overrides
};

class IMotionGraphicsRenderer {
public:
    virtual ~IMotionGraphicsRenderer() = default;
    virtual bool load_template(MotionTemplateKind kind, const std::string& json_or_manifest,
                               MotionTemplateHandle& out_handle) = 0;
    virtual bool render_template_frame(const MotionTemplateHandle& handle,
                                       const RationalTime& local_t,
                                       VideoFrame& out_frame,
                                       FrameMemoryPool* pool) = 0;
};

class BuiltinMotionGraphicsRenderer final : public IMotionGraphicsRenderer {
public:
    bool load_template(MotionTemplateKind kind, const std::string& json_or_manifest,
                       MotionTemplateHandle& out_handle) override;
    bool render_template_frame(const MotionTemplateHandle& handle,
                               const RationalTime& local_t,
                               VideoFrame& out_frame,
                               FrameMemoryPool* pool) override;

private:
    uint64_t next_id_ = 1;
};

} // namespace owncut::timeline
