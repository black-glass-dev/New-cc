#pragma once

#include "timeline/color_hook.hpp"
#include "timeline/media_pool.hpp"
#include "timeline/rational_time.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace owncut::timeline {

enum class TrackType : uint8_t {
    Video = 0,
    Audio = 1,
    Subtitle = 2,
    Adjustment = 3
};

enum class ClipKind : uint8_t {
    SourceClip = 0,
    CompoundClip = 1,
    AdjustmentClip = 2,
    MulticamClip = 3,
    GeneratorClip = 4,
    TitleClip = 5
};

enum class BlendMode : uint8_t {
    Normal = 0,
    Add = 1,
    Multiply = 2,
    Screen = 3,
    Overlay = 4,
    Darken = 5,
    Lighten = 6,
    ColorDodge = 7,
    ColorBurn = 8,
    HardLight = 9,
    SoftLight = 10,
    Difference = 11,
    Exclusion = 12,
    Subtract = 13,
    Hue = 14,
    Saturation = 15,
    Color = 16,
    Luminosity = 17
};

struct GeneratorSpec {
    std::string kind = "solid"; // solid, bars, gradient, grid, countdown
    float color_rgba[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float secondary_rgba[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MulticamAngle {
    uint64_t media_id = 0;
    std::string label;
    RationalTime sync_offset{};
};

struct Marker {
    uint64_t id = 0;
    RationalTime time{};
    RationalTime duration{};
    ClipFlagColor color = ClipFlagColor::Blue;
    std::string name;
    std::string note;
    std::string custom_data;
};

struct PresentationAnnotation {
    uint64_t id = 0;
    RationalTime time{};
    std::string author;
    std::string text;
    float norm_x = 0.5f;
    float norm_y = 0.5f;
    float norm_w = 0.1f;
    float norm_h = 0.1f;
    bool resolved = false;
};

struct ClipItem {
    uint64_t id = 0;
    uint64_t media_id = 0; // 0 for generator/title/adjustment
    uint64_t track_id = 0;
    uint64_t link_group_id = 0; // Non-zero links A/V clips together (supports J/L independent offsets)
    uint64_t color_group_id = 0; // Passed directly to IColorPipelineHook
    ClipKind kind = ClipKind::SourceClip;
    TrackType media_role = TrackType::Video;
    std::string name;

    RationalTime timeline_start{};
    RationalTime timeline_duration{};
    RationalTime source_in{};
    RationalTime source_out{};
    RationalTime source_total_duration{}; // Bounds for Safe Trimming Mode

    double speed = 1.0;
    bool reversed = false;
    bool enabled = true;
    bool locked = false;

    BlendMode blend_mode = BlendMode::Normal;
    float opacity = 1.0f;

    // Compound / Multicam / Generator fields
    uint64_t compound_timeline_id = 0;
    std::size_t multicam_active_angle = 0;
    std::vector<MulticamAngle> multicam_angles;
    GeneratorSpec generator_spec{};

    // Audio basics (extended in AudioEngine)
    float gain_db = 0.0f;
    float pan = 0.0f; // -1.0 left .. +1.0 right
    bool muted = false;

    std::map<std::string, std::string> metadata;

    [[nodiscard]] RationalTime timeline_end() const { return timeline_start + timeline_duration; }
    [[nodiscard]] TimeRange timeline_range() const { return TimeRange{timeline_start, timeline_duration}; }
    [[nodiscard]] bool contains_time(const RationalTime& t) const {
        return t >= timeline_start && t < timeline_end();
    }
    [[nodiscard]] RationalTime map_timeline_to_source(const RationalTime& timeline_t) const;
};

struct Track {
    uint64_t id = 0;
    TrackType type = TrackType::Video;
    int32_t index = 0; // 0 = V1/A1, 1 = V2/A2...
    std::string name;
    bool locked = false;
    bool muted = false;
    bool soloed = false;
    bool enabled = true;
    bool auto_track_selector = true;
    int32_t height_px = 68;
    std::vector<ClipItem> clips;

    void sort_clips();
    [[nodiscard]] ClipItem* find_clip(uint64_t clip_id);
    [[nodiscard]] const ClipItem* find_clip(uint64_t clip_id) const;
    [[nodiscard]] const ClipItem* clip_at(const RationalTime& t) const;
    [[nodiscard]] ClipItem* clip_at(const RationalTime& t);
    [[nodiscard]] std::vector<TimeRange> find_gaps(const RationalTime& up_to) const;
    [[nodiscard]] RationalTime duration() const;
};

struct FrameLayerDescriptor {
    uint64_t track_id = 0;
    int32_t track_index = 0;
    uint64_t clip_id = 0;
    uint64_t media_id = 0;
    uint64_t color_group_id = 0;
    ClipKind kind = ClipKind::SourceClip;
    BlendMode blend_mode = BlendMode::Normal;
    float opacity = 1.0f;
    RationalTime source_time{};
};

struct FrameRenderPlan {
    RationalTime timeline_time{};
    std::vector<FrameLayerDescriptor> video_layers; // Bottom-to-top
    std::vector<uint64_t> active_adjustment_clip_ids;
};

class Timeline {
public:
    Timeline() = default;
    explicit Timeline(uint64_t id, std::string name, FrameRate fps = FrameRate::fps_30(),
                      int32_t width = 1920, int32_t height = 1080);

    [[nodiscard]] uint64_t id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }
    [[nodiscard]] FrameRate fps() const noexcept { return fps_; }
    void set_fps(FrameRate r) { fps_ = r; }
    [[nodiscard]] int32_t width() const noexcept { return width_; }
    [[nodiscard]] int32_t height() const noexcept { return height_; }
    void set_resolution(int32_t w, int32_t h) { width_ = w; height_ = h; }

    Track& add_track(TrackType type, std::string name = "");
    bool remove_track(uint64_t track_id);
    Track* find_track(uint64_t track_id);
    const Track* find_track(uint64_t track_id) const;
    Track* primary_track(TrackType type);
    const Track* primary_track(TrackType type) const;
    std::vector<Track*> tracks_of_type(TrackType type);
    std::vector<const Track*> tracks_of_type(TrackType type) const;
    std::vector<Track>& tracks() noexcept { return tracks_; }
    const std::vector<Track>& tracks() const noexcept { return tracks_; }

    ClipItem* find_clip(uint64_t clip_id, uint64_t* out_track_id = nullptr);
    const ClipItem* find_clip(uint64_t clip_id, uint64_t* out_track_id = nullptr) const;
    std::vector<ClipItem*> find_linked_clips(uint64_t link_group_id);

    uint64_t allocate_clip_id() noexcept { return next_clip_id_++; }
    uint64_t allocate_link_group_id() noexcept { return next_link_group_id_++; }

    // Playhead and In/Out marks
    void set_playhead(const RationalTime& t) { playhead_ = std::max(RationalTime::zero(), t); }
    [[nodiscard]] const RationalTime& playhead() const noexcept { return playhead_; }
    void set_in_point(std::optional<RationalTime> t) { in_point_ = t; }
    void set_out_point(std::optional<RationalTime> t) { out_point_ = t; }
    [[nodiscard]] const std::optional<RationalTime>& in_point() const noexcept { return in_point_; }
    [[nodiscard]] const std::optional<RationalTime>& out_point() const noexcept { return out_point_; }

    // Markers & Annotations
    uint64_t add_marker(Marker m);
    bool remove_marker(uint64_t marker_id);
    const std::vector<Marker>& markers() const noexcept { return markers_; }

    uint64_t add_annotation(PresentationAnnotation a);
    bool resolve_annotation(uint64_t annotation_id, bool resolved = true);
    const std::vector<PresentationAnnotation>& annotations() const noexcept { return annotations_; }

    [[nodiscard]] RationalTime duration() const;
    [[nodiscard]] FrameRenderPlan build_render_plan(const RationalTime& t) const;

    // Decodes active clip(s) at `t`, composites layers, and invokes IColorPipelineHook per clip/group & adjustment.
    bool render_frame_with_color_hook(const RationalTime& t,
                                      const MediaPool& media_pool,
                                      VideoFrame& out_frame,
                                      IColorPipelineHook* color_hook = nullptr,
                                      FrameMemoryPool* memory_pool = nullptr) const;

    void set_compound_timeline(uint64_t compound_id, Timeline sub);
    const Timeline* find_compound_timeline(uint64_t compound_id) const;

private:
    uint64_t id_ = 1;
    std::string name_ = "Timeline 1";
    FrameRate fps_ = FrameRate::fps_30();
    int32_t width_ = 1920;
    int32_t height_ = 1080;
    uint64_t next_track_id_ = 1;
    uint64_t next_clip_id_ = 1;
    uint64_t next_link_group_id_ = 1;
    uint64_t next_marker_id_ = 1;
    uint64_t next_annotation_id_ = 1;

    std::vector<Track> tracks_;
    RationalTime playhead_{};
    std::optional<RationalTime> in_point_;
    std::optional<RationalTime> out_point_;
    std::vector<Marker> markers_;
    std::vector<PresentationAnnotation> annotations_;
    std::map<uint64_t, Timeline> compound_timelines_;
};

class Project {
public:
    explicit Project(std::string name = "Untitled Project");
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }
    MediaPool& media_pool() noexcept { return media_pool_; }
    const MediaPool& media_pool() const noexcept { return media_pool_; }

    Timeline& create_timeline(const std::string& name, FrameRate fps = FrameRate::fps_30(),
                              int32_t width = 1920, int32_t height = 1080);
    Timeline* find_timeline(uint64_t id);
    const Timeline* find_timeline(uint64_t id) const;
    std::vector<Timeline>& timelines() noexcept { return timelines_; }
    const std::vector<Timeline>& timelines() const noexcept { return timelines_; }

private:
    std::string name_;
    MediaPool media_pool_;
    uint64_t next_timeline_id_ = 1;
    std::vector<Timeline> timelines_;
};

// Tabbed & Stacked Timelines (Cut/Edit multi-sequence workflow)
class TabbedStackedTimelineManager {
public:
    void open_tab(uint64_t timeline_id);
    void close_tab(uint64_t timeline_id);
    bool set_active_tab(uint64_t timeline_id);
    void enable_stacked_view(bool enabled, std::optional<uint64_t> secondary_timeline_id = std::nullopt);
    [[nodiscard]] const std::vector<uint64_t>& open_tabs() const noexcept { return open_tabs_; }
    [[nodiscard]] uint64_t active_tab() const noexcept { return active_tab_; }
    [[nodiscard]] bool stacked_enabled() const noexcept { return stacked_enabled_; }
    [[nodiscard]] std::optional<uint64_t> secondary_tab() const noexcept { return secondary_tab_; }

private:
    std::vector<uint64_t> open_tabs_;
    uint64_t active_tab_ = 0;
    bool stacked_enabled_ = false;
    std::optional<uint64_t> secondary_tab_;
};

enum class DiffKind : uint8_t {
    Added = 0,
    Removed = 1,
    Moved = 2,
    Trimmed = 3
};

struct TimelineDiffEntry {
    DiffKind kind = DiffKind::Added;
    uint64_t clip_id = 0;
    std::string clip_name;
    int32_t track_index = 0;
    TrackType track_type = TrackType::Video;
    RationalTime before_start{};
    RationalTime before_duration{};
    RationalTime after_start{};
    RationalTime after_duration{};
};

std::vector<TimelineDiffEntry> compare_timelines(const Timeline& baseline, const Timeline& current);

// Cut-Page Dual Timeline (Overview + Detail synchronized zoom)
class DualTimelineController {
public:
    void set_viewport_duration(const RationalTime& detail_span);
    void sync_to_playhead(const Timeline& tl);
    void scrub_overview(Timeline& tl, double normalized_pos);
    void scrub_detail(Timeline& tl, double normalized_pos);
    [[nodiscard]] TimeRange overview_range(const Timeline& tl) const;
    [[nodiscard]] TimeRange detail_range(const Timeline& tl) const;

private:
    RationalTime detail_span_ = RationalTime(10, 1);
    RationalTime detail_start_{};
};

// Cut-Page Source Tape: stitches selected MediaItems into a continuous searchable tape
struct SourceTapeSegment {
    uint64_t media_id = 0;
    RationalTime tape_start{};
    RationalTime media_duration{};
};

class SourceTapeController {
public:
    void rebuild(const std::vector<const MediaItem*>& items);
    [[nodiscard]] RationalTime total_duration() const;
    [[nodiscard]] std::optional<std::pair<uint64_t, RationalTime>> map_tape_time(const RationalTime& tape_t) const;
    void set_tape_in(const RationalTime& t) { tape_in_ = t; }
    void set_tape_out(const RationalTime& t) { tape_out_ = t; }
    [[nodiscard]] const std::optional<RationalTime>& tape_in() const noexcept { return tape_in_; }
    [[nodiscard]] const std::optional<RationalTime>& tape_out() const noexcept { return tape_out_; }
    [[nodiscard]] const std::vector<SourceTapeSegment>& segments() const noexcept { return segments_; }

private:
    std::vector<SourceTapeSegment> segments_;
    std::optional<RationalTime> tape_in_;
    std::optional<RationalTime> tape_out_;
};

struct SmartIndicatorResult {
    uint64_t target_track_id = 0;
    RationalTime insert_time{};
    bool will_ripple = false;
    bool snapped_to_edit_point = false;
};

SmartIndicatorResult compute_smart_indicator(const Timeline& tl, TrackType role = TrackType::Video);

// Helper used across compositor & transition engines
void composite_rgba32f_layer(VideoFrame& dst, const VideoFrame& src, BlendMode mode, float opacity);

} // namespace owncut::timeline
