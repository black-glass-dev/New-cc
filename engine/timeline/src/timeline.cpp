#include "editor/timeline.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace editor {
Timeline::Timeline(TimeBase frame_time_base) : frame_time_base_(frame_time_base) {}

std::optional<PlayheadPosition> Timeline::resolve_playhead(
    const std::string& track_id, Timestamp position) const {
    if (position < Timestamp{0, frame_time_base_})
        throw std::invalid_argument("playhead cannot be before timeline zero");
    const auto track = std::ranges::find_if(tracks_, [&](const Track& value) { return value.id == track_id; });
    if (track == tracks_.end()) throw std::invalid_argument("unknown playhead track");
    position = position.rescaled(frame_time_base_);
    std::optional<PlayheadPosition> result;
    for (const auto& clip : track->clips) {
        const auto end = clip.timeline_start + (clip.source_out - clip.source_in).rescaled(frame_time_base_);
        if (position < clip.timeline_start || !(position < end)) continue;
        if (result) throw std::logic_error("overlapping clips require a compositor for seeking");
        result = PlayheadPosition{clip.id, position, clip.source_in + (position - clip.timeline_start)};
    }
    return result;
}

void Timeline::add_track(Track track) {
    if (track.id.empty()) throw std::invalid_argument("track id must not be empty");
    if (track.kind != TrackKind::video && track.kind != TrackKind::audio)
        throw std::invalid_argument("unsupported track kind");
    if (!track.clips.empty()) throw std::invalid_argument("tracks must be added before clips");
    if (std::ranges::any_of(tracks_, [&](const Track& existing) { return existing.id == track.id; }))
        throw std::invalid_argument("duplicate track id");
    tracks_.push_back(std::move(track));
}

void Timeline::add_clip(const std::string& track_id, Clip clip) {
    color::validate(clip.grade);
    auto track = std::ranges::find_if(tracks_, [&](const Track& value) { return value.id == track_id; });
    if (track == tracks_.end()) throw std::invalid_argument("unknown track id");
    if (track->locked) throw std::logic_error("cannot edit a locked track");
    if (track->kind != TrackKind::video && clip.grade != color::Grade{})
        throw std::invalid_argument("color grading requires a video track");
    if (clip.id.empty() || clip.media_id.empty()) throw std::invalid_argument("clip and media ids are required");
    for (const auto& existing_track : tracks_) {
        if (std::ranges::any_of(existing_track.clips, [&](const Clip& existing) { return existing.id == clip.id; }))
            throw std::invalid_argument("duplicate clip id");
    }
    if (clip.source_in < Timestamp{0, clip.source_in.time_base} || !(clip.source_in < clip.source_out))
        throw std::invalid_argument("clip source range must be non-negative and have positive duration");
    if (clip.timeline_start < Timestamp{0, frame_time_base_})
        throw std::invalid_argument("clip cannot start before timeline zero");
    clip.timeline_start = clip.timeline_start.rescaled(frame_time_base_);
    track->clips.push_back(std::move(clip));
}

ClipLocation Timeline::remove_clip(const std::string& clip_id) {
    for (auto& track : tracks_) {
        auto clip = std::ranges::find_if(track.clips, [&](const Clip& value) { return value.id == clip_id; });
        if (clip == track.clips.end()) continue;
        if (track.locked) throw std::logic_error("cannot edit a locked track");
        ClipLocation removed{track.id, *clip};
        track.clips.erase(clip);
        return removed;
    }
    throw std::invalid_argument("unknown clip id");
}

std::vector<std::string> Timeline::clips_referencing_media(const std::string& media_id) const {
    std::vector<std::string> result;
    for (const auto& track : tracks_)
        for (const auto& clip : track.clips)
            if (clip.media_id == media_id) result.push_back(clip.id);
    return result;
}

void Timeline::relink_media_references(const std::vector<std::string>& clip_ids,
                                       const std::string& expected_media_id,
                                       const std::string& replacement_media_id) {
    if (clip_ids.empty() || expected_media_id.empty() || replacement_media_id.empty())
        throw std::invalid_argument("media relink requires affected clips and non-empty media ids");
    if (expected_media_id == replacement_media_id)
        throw std::invalid_argument("replacement media is unchanged");
    auto updated = tracks_; // Allocate before changing live state for an all-or-nothing operation.
    for (const auto& clip_id : clip_ids) {
        bool found = false;
        for (auto& track : updated) {
            auto clip = std::ranges::find_if(track.clips, [&](const Clip& value) { return value.id == clip_id; });
            if (clip == track.clips.end()) continue;
            if (found) throw std::logic_error("duplicate clip id while relinking media");
            found = true;
            if (track.locked) throw std::logic_error("cannot edit a locked track");
            if (clip->media_id != expected_media_id)
                throw std::logic_error("media reference changed before relink");
            clip->media_id = replacement_media_id;
        }
        if (!found) throw std::invalid_argument("unknown clip id while relinking media");
    }
    tracks_.swap(updated);
}

void Timeline::set_clip_grade(const std::string& clip_id, const color::Grade& grade) {
    color::validate(grade);
    for (auto& track : tracks_) for (auto& clip : track.clips) {
        if (clip.id != clip_id) continue;
        if (track.locked) throw std::logic_error("cannot grade a locked track");
        if (track.kind != TrackKind::video) throw std::invalid_argument("color grading requires a video track");
        clip.grade = grade;
        return;
    }
    throw std::invalid_argument("unknown clip id");
}

void Timeline::replace_clip(const Clip& replacement) {
    for (auto& track : tracks_) {
        auto clip = std::ranges::find_if(track.clips, [&](const Clip& value) { return value.id == replacement.id; });
        if (clip == track.clips.end()) continue;
        if (track.locked) throw std::logic_error("cannot edit a locked track");
        if (replacement.media_id != clip->media_id)
            throw std::invalid_argument("clip media reference cannot be changed by a trim");
        if (replacement.source_in < Timestamp{0, replacement.source_in.time_base} ||
            !(replacement.source_in < replacement.source_out))
            throw std::invalid_argument("clip source range must be non-negative and have positive duration");
        if (replacement.timeline_start < Timestamp{0, frame_time_base_})
            throw std::invalid_argument("clip cannot start before timeline zero");
        clip->timeline_start = replacement.timeline_start.rescaled(frame_time_base_);
        clip->source_in = replacement.source_in;
        clip->source_out = replacement.source_out;
        return;
    }
    throw std::invalid_argument("unknown clip id");
}

Clip Timeline::split_clip(const std::string& clip_id, Timestamp cut_at, const std::string& right_clip_id) {
    if (right_clip_id.empty()) throw std::invalid_argument("split requires a right clip id");
    const auto original = locate_clip(clip_id);
    const auto track = std::ranges::find_if(tracks_, [&](const Track& value) { return value.id == original.track_id; });
    if (track->locked) throw std::logic_error("cannot edit a locked track");
    cut_at = cut_at.rescaled(frame_time_base_);
    const auto duration = (original.clip.source_out - original.clip.source_in).rescaled(frame_time_base_);
    const auto end = original.clip.timeline_start + duration;
    if (!(original.clip.timeline_start < cut_at) || !(cut_at < end))
        throw std::invalid_argument("split point must be strictly inside the clip");
    const auto source_offset = cut_at - original.clip.timeline_start;
    const auto source_cut = original.clip.source_in + source_offset;
    if (!(original.clip.source_in < source_cut) || !(source_cut < original.clip.source_out))
        throw std::invalid_argument("split point does not map to an interior source timestamp");

    Clip right = original.clip;
    right.id = right_clip_id;
    right.timeline_start = cut_at;
    right.source_in = source_cut;
    add_clip(original.track_id, right); // Allocation/validation happens before changing the left side.
    auto left = original.clip;
    left.source_out = source_cut;
    replace_clip(left);
    return right;
}

RippleDeleteResult Timeline::ripple_delete(const std::string& clip_id) {
    for (auto& track : tracks_) {
        auto target = std::ranges::find_if(track.clips, [&](const Clip& value) { return value.id == clip_id; });
        if (target == track.clips.end()) continue;
        if (track.locked) throw std::logic_error("cannot edit a locked track");

        RippleDeleteResult result;
        result.removed = {track.id, *target};
        result.original_index = static_cast<std::size_t>(target - track.clips.begin());
        const auto duration = (target->source_out - target->source_in).rescaled(frame_time_base_);
        const auto end = target->timeline_start + duration;
        for (const auto& clip : track.clips) {
            if (clip.id != clip_id && !(clip.timeline_start < end))
                result.shifted_before.push_back({track.id, clip});
        }

        auto updated = track.clips;
        updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(result.original_index));
        for (auto& clip : updated) {
            if (!(clip.timeline_start < end)) clip.timeline_start = clip.timeline_start - duration;
        }
        track.clips.swap(updated); // All potentially throwing work completed before commit.
        return result;
    }
    throw std::invalid_argument("unknown clip id");
}

void Timeline::restore_ripple_delete(const RippleDeleteResult& result) {
    auto track = std::ranges::find_if(tracks_, [&](const Track& value) { return value.id == result.removed.track_id; });
    if (track == tracks_.end()) throw std::invalid_argument("unknown ripple-delete track");
    if (track->locked) throw std::logic_error("cannot edit a locked track");
    for (const auto& value : tracks_) {
        if (std::ranges::any_of(value.clips, [&](const Clip& clip) { return clip.id == result.removed.clip.id; }))
            throw std::invalid_argument("cannot restore a clip id that is already present");
    }

    auto restored = track->clips;
    if (result.original_index > restored.size()) throw std::logic_error("ripple-delete history no longer matches timeline");
    restored.insert(restored.begin() + static_cast<std::ptrdiff_t>(result.original_index), result.removed.clip);
    for (const auto& prior : result.shifted_before) {
        if (prior.track_id != result.removed.track_id)
            throw std::logic_error("ripple-delete history contains a mismatched track");
        auto clip = std::ranges::find_if(restored, [&](const Clip& value) { return value.id == prior.clip.id; });
        if (clip == restored.end()) throw std::logic_error("ripple-delete history no longer matches timeline");
        clip->timeline_start = prior.clip.timeline_start;
    }
    track->clips.swap(restored);
}

void Timeline::set_track_state(const std::string& track_id, bool muted, bool locked) {
    auto track = std::ranges::find_if(tracks_, [&](const Track& value) { return value.id == track_id; });
    if (track == tracks_.end()) throw std::invalid_argument("unknown track id");
    track->muted = muted;
    track->locked = locked;
}

ClipLocation Timeline::locate_clip(const std::string& clip_id) const {
    for (const auto& track : tracks_) {
        const auto clip = std::ranges::find_if(track.clips, [&](const Clip& value) { return value.id == clip_id; });
        if (clip != track.clips.end()) return {track.id, *clip};
    }
    throw std::invalid_argument("unknown clip id");
}

void Timeline::move_clip(const std::string& clip_id, const std::string& destination_track_id,
                         Timestamp destination_start) {
    auto destination = std::ranges::find_if(tracks_, [&](const Track& value) { return value.id == destination_track_id; });
    if (destination == tracks_.end()) throw std::invalid_argument("unknown destination track id");
    if (destination->locked) throw std::logic_error("cannot edit a locked track");
    if (destination_start < Timestamp{0, frame_time_base_})
        throw std::invalid_argument("clip cannot start before timeline zero");
    destination_start = destination_start.rescaled(frame_time_base_);

    auto source = tracks_.end();
    auto clip = std::vector<Clip>::iterator{};
    for (auto track = tracks_.begin(); track != tracks_.end(); ++track) {
        auto candidate = std::ranges::find_if(track->clips, [&](const Clip& value) { return value.id == clip_id; });
        if (candidate != track->clips.end()) { source = track; clip = candidate; break; }
    }
    if (source == tracks_.end()) throw std::invalid_argument("unknown clip id");
    if (source->locked) throw std::logic_error("cannot edit a locked track");

    if (source == destination) {
        clip->timeline_start = destination_start;
        return;
    }
    // Copy into the destination first so allocation failure leaves the source untouched.
    if (destination->kind != TrackKind::video && clip->grade != color::Grade{})
        throw std::invalid_argument("graded clips require a video track");
    Clip moved = *clip;
    moved.timeline_start = destination_start;
    destination->clips.push_back(std::move(moved));
    source->clips.erase(clip);
}
} // namespace editor
