#pragma once

#include "editor/time.hpp"
#include "editor/color.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor {

enum class TrackKind { video = 0, audio = 1 };

struct Clip final {
    std::string id;
    std::string media_id;
    Timestamp timeline_start;
    Timestamp source_in;
    Timestamp source_out; // exclusive
    color::Grade grade{}; // value-owned by stable clip identity, never by media URI
};

struct Track final {
    std::string id;
    TrackKind kind;
    bool muted{false};
    bool locked{false};
    std::vector<Clip> clips;
};

struct ClipLocation final {
    std::string track_id;
    Clip clip;
};

struct PlayheadPosition final {
    std::string clip_id;
    Timestamp timeline_position;
    Timestamp source_position;
};

struct RippleDeleteResult final {
    ClipLocation removed;
    std::size_t original_index{0};
    std::vector<ClipLocation> shifted_before;
};

// A non-destructive value model. Clips refer to source ranges; source media is never modified.
class Timeline final {
public:
    explicit Timeline(TimeBase frame_time_base);
    void add_track(Track track);
    void add_clip(const std::string& track_id, Clip clip);
    [[nodiscard]] ClipLocation remove_clip(const std::string& clip_id);
    void replace_clip(const Clip& replacement);
    void set_clip_grade(const std::string& clip_id, const color::Grade& grade);
    [[nodiscard]] std::vector<std::string> clips_referencing_media(const std::string& media_id) const;
    void relink_media_references(const std::vector<std::string>& clip_ids,
                                 const std::string& expected_media_id,
                                 const std::string& replacement_media_id);
    [[nodiscard]] Clip split_clip(const std::string& clip_id, Timestamp cut_at,
                                  const std::string& right_clip_id);
    [[nodiscard]] RippleDeleteResult ripple_delete(const std::string& clip_id);
    void restore_ripple_delete(const RippleDeleteResult& result);
    void set_track_state(const std::string& track_id, bool muted, bool locked);
    void move_clip(const std::string& clip_id, const std::string& destination_track_id,
                   Timestamp destination_start);
    // Point lookup uses [start,end); gaps return null, ambiguous overlaps are rejected.
    [[nodiscard]] std::optional<PlayheadPosition> resolve_playhead(
        const std::string& track_id, Timestamp position) const;
    [[nodiscard]] ClipLocation locate_clip(const std::string& clip_id) const;
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept { return tracks_; }
    [[nodiscard]] TimeBase frame_time_base() const noexcept { return frame_time_base_; }

private:
    TimeBase frame_time_base_;
    std::vector<Track> tracks_;
};

} // namespace editor
