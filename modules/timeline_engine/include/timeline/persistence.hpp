#pragma once

#include "timeline/timeline_model.hpp"
#include "timeline/text_subtitle_engine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace owncut::timeline {

struct RichClipFx {
    std::string track_id = "v1";
    std::string clip_id;
    double speed = 1.0;
    bool reversed = false;
    int64_t freeze_ms = -1; // -1 = disabled; >= 0 = freeze frame offset within clip source
    double volume = 1.0;
    int64_t fade_in_ms = 0;
    int64_t fade_out_ms = 0;
    bool muted = false;
    double ducking_db = 0.0;
    double opacity = 1.0;
    std::string blend_mode = "normal";
    double scale = 1.0;
    double pan_x = 0.0;
    double pan_y = 0.0;
    double rotation_deg = 0.0;
    double dynamic_zoom_start = 1.0;
    double dynamic_zoom_end = 1.0;
    std::string transition_kind = "none";
    int64_t transition_ms = 0;
    std::string chroma_key_hex;
    double chroma_tolerance = 0.0;
    std::vector<double> audio_peaks;
};

struct RichTextOverlay {
    std::string id;
    std::string text;
    int64_t start_ms = 0;
    int64_t duration_ms = 2000;
    double x = 0.5;
    double y = 0.82;
    double scale = 1.0;
    double rotation_deg = 0.0;
    std::string fg_hex = "#FFFFFF";
    std::string bg_hex = "#B8000000";
    std::string style = "title";
    bool bold = true;
};

struct RichSubtitleCue {
    std::string id;
    int64_t start_ms = 0;
    int64_t end_ms = 2000;
    std::string text;
};

struct RichMarker {
    std::string id;
    int64_t position_ms = 0;
    std::string label;
    std::string color_hex = "#4F9CF9";
};

struct OwncutRichState {
    int32_t schema_version = 1;
    bool snap_enabled = true;
    double beat_bpm = 0.0;
    int64_t beat_offset_ms = 0;
    std::vector<RichClipFx> clip_fx;
    std::vector<RichTextOverlay> overlays;
    std::vector<RichSubtitleCue> subtitles;
    std::vector<RichMarker> markers;
};

class TimelineSerializer {
public:
    static std::string escape_json(const std::string& raw);
    static std::string to_json(const Timeline& tl);
    static bool from_json(const std::string& json, Timeline& out_tl);

    static std::string state_to_json(const OwncutRichState& state);
    static bool state_from_json(const std::string& json, OwncutRichState& out_state);

    static bool write_atomic_utf8(const std::string& path, const std::string& content);
    static bool read_utf8_file(const std::string& path, std::string& out_content);
};

} // namespace owncut::timeline
