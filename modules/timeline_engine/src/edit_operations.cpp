#include "timeline/edit_operations.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace owncut::timeline {
namespace {

RationalTime resolved_source_duration(const SourceInsertSpec& spec) {
    if (spec.source_out > spec.source_in) return spec.source_out - spec.source_in;
    if (spec.media_duration > spec.source_in) return spec.media_duration - spec.source_in;
    return RationalTime(5, 1);
}

void overwrite_range_on_track(Track& track, const TimeRange& range) {
    if (track.locked || range.duration.num() <= 0) return;
    const RationalTime r_start = range.start;
    const RationalTime r_end = range.end();
    std::vector<ClipItem> next_clips;
    next_clips.reserve(track.clips.size() + 1);

    for (auto& c : track.clips) {
        const RationalTime c_start = c.timeline_start;
        const RationalTime c_end = c.timeline_end();
        if (c_end <= r_start || c_start >= r_end) {
            next_clips.push_back(std::move(c));
            continue;
        }
        // Left surviving segment
        if (c_start < r_start) {
            ClipItem left = c;
            const RationalTime kept_dur = r_start - c_start;
            left.timeline_duration = kept_dur;
            left.source_out = left.source_in + kept_dur;
            next_clips.push_back(std::move(left));
        }
        // Right surviving segment
        if (c_end > r_end) {
            ClipItem right = c;
            const RationalTime cut_offset = r_end - c_start;
            right.id = 0; // Needs fresh ID if split
            right.timeline_start = r_end;
            right.timeline_duration = c_end - r_end;
            right.source_in = c.source_in + cut_offset;
            next_clips.push_back(std::move(right));
        }
    }
    track.clips = std::move(next_clips);
}

void split_and_ripple_track(Timeline& tl, Track& track, const RationalTime& at_time, const RationalTime& shift_delta) {
    if (track.locked || shift_delta.num() == 0) return;
    std::vector<ClipItem> updated;
    updated.reserve(track.clips.size() + 1);
    for (auto& c : track.clips) {
        if (shift_delta.num() > 0 && c.timeline_start < at_time && c.timeline_end() > at_time) {
            ClipItem left = c;
            const RationalTime left_dur = at_time - c.timeline_start;
            left.timeline_duration = left_dur;
            left.source_out = left.source_in + left_dur;

            ClipItem right = c;
            right.id = tl.allocate_clip_id();
            right.timeline_start = at_time + shift_delta;
            right.timeline_duration = c.timeline_end() - at_time;
            right.source_in = c.source_in + left_dur;

            updated.push_back(std::move(left));
            updated.push_back(std::move(right));
        } else if (c.timeline_start >= at_time) {
            c.timeline_start = std::max(RationalTime::zero(), c.timeline_start + shift_delta);
            updated.push_back(std::move(c));
        } else {
            updated.push_back(std::move(c));
        }
    }
    track.clips = std::move(updated);
    track.sort_clips();
}

void assign_missing_clip_ids(Timeline& tl, Track& track) {
    for (auto& c : track.clips) {
        if (c.id == 0) c.id = tl.allocate_clip_id();
    }
    track.sort_clips();
}

ClipItem make_video_clip(Timeline& tl, uint64_t track_id, uint64_t link_group_id,
                         const SourceInsertSpec& spec, const RationalTime& tl_start,
                         const RationalTime& tl_dur, double speed = 1.0) {
    ClipItem c;
    c.id = tl.allocate_clip_id();
    c.media_id = spec.media_id;
    c.track_id = track_id;
    c.link_group_id = link_group_id;
    c.kind = spec.kind;
    c.media_role = TrackType::Video;
    c.name = spec.name.empty() ? ("Clip " + std::to_string(c.id)) : spec.name;
    c.timeline_start = tl_start;
    c.timeline_duration = tl_dur;
    c.source_in = spec.source_in;
    c.source_out = spec.source_in + resolved_source_duration(spec);
    c.source_total_duration = (spec.media_duration > RationalTime::zero())
                                  ? spec.media_duration
                                  : c.source_out;
    c.speed = speed;
    return c;
}

ClipItem make_audio_clip(Timeline& tl, uint64_t track_id, uint64_t link_group_id,
                         const SourceInsertSpec& spec, const RationalTime& v_tl_start,
                         const RationalTime& v_tl_dur, double speed = 1.0) {
    ClipItem a;
    a.id = tl.allocate_clip_id();
    a.media_id = spec.media_id;
    a.track_id = track_id;
    a.link_group_id = link_group_id;
    a.kind = spec.kind;
    a.media_role = TrackType::Audio;
    a.name = (spec.name.empty() ? ("Clip " + std::to_string(a.id)) : spec.name) + " (Audio)";
    const RationalTime a_start = std::max(RationalTime::zero(), v_tl_start + spec.audio_in_offset);
    const RationalTime a_end = std::max(a_start + RationalTime::from_frames(1, tl.fps()),
                                        v_tl_start + v_tl_dur + spec.audio_out_offset);
    a.timeline_start = a_start;
    a.timeline_duration = a_end - a_start;
    a.source_in = std::max(RationalTime::zero(), spec.source_in + spec.audio_in_offset);
    a.source_out = a.source_in + a.timeline_duration;
    a.source_total_duration = (spec.media_duration > RationalTime::zero())
                                  ? spec.media_duration
                                  : a.source_out;
    a.speed = speed;
    return a;
}

Track* resolve_track(Timeline& tl, TrackType role, std::optional<uint64_t> explicit_id) {
    if (explicit_id.has_value()) {
        if (Track* t = tl.find_track(*explicit_id)) return t;
    }
    return tl.primary_track(role);
}

} // namespace

std::vector<SnapTarget> MagneticSnapEngine::collect_targets(const Timeline& tl,
                                                            std::optional<uint64_t> exclude_clip_id) const {
    std::vector<SnapTarget> targets;
    targets.push_back(SnapTarget{SnapTarget::Kind::Playhead, tl.playhead(), 0});
    if (tl.in_point().has_value()) {
        targets.push_back(SnapTarget{SnapTarget::Kind::InOutPoint, *tl.in_point(), 0});
    }
    if (tl.out_point().has_value()) {
        targets.push_back(SnapTarget{SnapTarget::Kind::InOutPoint, *tl.out_point(), 0});
    }
    for (const auto& m : tl.markers()) {
        targets.push_back(SnapTarget{SnapTarget::Kind::Marker, m.time, m.id});
    }
    for (const auto& tr : tl.tracks()) {
        for (const auto& c : tr.clips) {
            if (exclude_clip_id.has_value() && c.id == *exclude_clip_id) continue;
            targets.push_back(SnapTarget{SnapTarget::Kind::ClipBoundary, c.timeline_start, c.id});
            targets.push_back(SnapTarget{SnapTarget::Kind::ClipBoundary, c.timeline_end(), c.id});
        }
    }
    for (const auto& b : beat_grid_) {
        targets.push_back(SnapTarget{SnapTarget::Kind::BeatGrid, b, 0});
    }
    return targets;
}

SnapResult MagneticSnapEngine::snap(const Timeline& tl,
                                    const RationalTime& candidate,
                                    std::optional<uint64_t> exclude_clip_id,
                                    bool trigger_haptic) const {
    SnapResult res{false, candidate, SnapTarget::Kind::ClipBoundary};
    if (!enabled_) return res;
    RationalTime best_dist = threshold_;
    for (const auto& t : collect_targets(tl, exclude_clip_id)) {
        const RationalTime dist = (t.time - candidate).abs();
        if (dist <= best_dist) {
            best_dist = dist;
            res.snapped = true;
            res.snapped_time = t.time;
            res.target_kind = t.kind;
        }
    }
    if (res.snapped && trigger_haptic && haptic_cb_) {
        haptic_cb_(res);
    }
    return res;
}

bool EditOperations::perform_edit(Timeline& tl, CommandStack& stack, EditMode mode, const SourceInsertSpec& spec) {
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "EditMode:" + std::to_string(static_cast<int>(mode)),
        [mode, spec](Timeline& target) -> bool {
            Track* v_track = resolve_track(target, TrackType::Video, spec.target_video_track_id);
            if (!v_track || v_track->locked) return false;
            Track* a_track = spec.include_audio ? resolve_track(target, TrackType::Audio, spec.target_audio_track_id) : nullptr;
            if (a_track && a_track->locked) a_track = nullptr;

            const RationalTime src_dur = resolved_source_duration(spec);
            if (src_dur.num() <= 0) return false;
            const uint64_t link_id = (spec.include_audio && a_track) ? target.allocate_link_group_id() : 0;

            switch (mode) {
                case EditMode::SmartInsert: {
                    const SmartIndicatorResult ind = compute_smart_indicator(target, TrackType::Video);
                    const RationalTime ins_t = ind.insert_time;
                    split_and_ripple_track(target, *v_track, ins_t, src_dur);
                    v_track->clips.push_back(make_video_clip(target, v_track->id, link_id, spec, ins_t, src_dur));
                    v_track->sort_clips();
                    if (a_track) {
                        split_and_ripple_track(target, *a_track, ins_t, src_dur);
                        a_track->clips.push_back(make_audio_clip(target, a_track->id, link_id, spec, ins_t, src_dur));
                        a_track->sort_clips();
                    }
                    target.set_playhead(ins_t + src_dur);
                    return true;
                }
                case EditMode::AppendAtEnd: {
                    RationalTime end_t = v_track->duration();
                    if (a_track) end_t = std::max(end_t, a_track->duration());
                    v_track->clips.push_back(make_video_clip(target, v_track->id, link_id, spec, end_t, src_dur));
                    v_track->sort_clips();
                    if (a_track) {
                        a_track->clips.push_back(make_audio_clip(target, a_track->id, link_id, spec, end_t, src_dur));
                        a_track->sort_clips();
                    }
                    target.set_playhead(end_t + src_dur);
                    return true;
                }
                case EditMode::PlaceOnTop: {
                    const RationalTime ph = target.playhead();
                    const uint64_t base_v_id = v_track->id;
                    const uint64_t base_a_id = a_track ? a_track->id : 0;
                    uint64_t top_track_id = 0;
                    for (Track* vt : target.tracks_of_type(TrackType::Video)) {
                        if (vt->id == base_v_id || vt->locked) continue;
                        bool overlaps = false;
                        for (const auto& c : vt->clips) {
                            if (c.timeline_range().overlaps(TimeRange{ph, src_dur})) {
                                overlaps = true;
                                break;
                            }
                        }
                        if (!overlaps) {
                            top_track_id = vt->id;
                            break;
                        }
                    }
                    if (top_track_id == 0) {
                        top_track_id = target.add_track(TrackType::Video).id;
                    }
                    if (Track* top_track = target.find_track(top_track_id)) {
                        top_track->clips.push_back(make_video_clip(target, top_track->id, link_id, spec, ph, src_dur));
                        top_track->sort_clips();
                    }
                    if (base_a_id != 0) {
                        uint64_t audio_overlay_id = 0;
                        for (Track* at : target.tracks_of_type(TrackType::Audio)) {
                            if (at->id == base_a_id || at->locked) continue;
                            bool overlaps = false;
                            for (const auto& c : at->clips) {
                                if (c.timeline_range().overlaps(TimeRange{ph, src_dur})) { overlaps = true; break; }
                            }
                            if (!overlaps) { audio_overlay_id = at->id; break; }
                        }
                        if (audio_overlay_id == 0) {
                            audio_overlay_id = target.add_track(TrackType::Audio).id;
                        }
                        if (Track* audio_overlay = target.find_track(audio_overlay_id)) {
                            audio_overlay->clips.push_back(make_audio_clip(target, audio_overlay->id, link_id, spec, ph, src_dur));
                            audio_overlay->sort_clips();
                        }
                    }
                    target.set_playhead(ph + src_dur);
                    return true;
                }
                case EditMode::Overwrite: {
                    const RationalTime ph = target.playhead();
                    overwrite_range_on_track(*v_track, TimeRange{ph, src_dur});
                    assign_missing_clip_ids(target, *v_track);
                    v_track->clips.push_back(make_video_clip(target, v_track->id, link_id, spec, ph, src_dur));
                    v_track->sort_clips();
                    if (a_track) {
                        ClipItem ac = make_audio_clip(target, a_track->id, link_id, spec, ph, src_dur);
                        overwrite_range_on_track(*a_track, ac.timeline_range());
                        assign_missing_clip_ids(target, *a_track);
                        a_track->clips.push_back(std::move(ac));
                        a_track->sort_clips();
                    }
                    target.set_playhead(ph + src_dur);
                    return true;
                }
                case EditMode::RippleOverwrite: {
                    const RationalTime ph = target.playhead();
                    ClipItem* victim = v_track->clip_at(ph);
                    if (!victim) {
                        // Fall back to In/Out range or overwrite at playhead
                        if (target.in_point().has_value() && target.out_point().has_value() &&
                            *target.out_point() > *target.in_point()) {
                            const RationalTime r_start = *target.in_point();
                            const RationalTime r_dur = *target.out_point() - r_start;
                            overwrite_range_on_track(*v_track, TimeRange{r_start, r_dur});
                            assign_missing_clip_ids(target, *v_track);
                            split_and_ripple_track(target, *v_track, r_start + r_dur, src_dur - r_dur);
                            v_track->clips.push_back(make_video_clip(target, v_track->id, link_id, spec, r_start, src_dur));
                            v_track->sort_clips();
                            return true;
                        }
                        return false;
                    }
                    const RationalTime v_start = victim->timeline_start;
                    const RationalTime old_dur = victim->timeline_duration;
                    const uint64_t old_link = victim->link_group_id;
                    const RationalTime delta = src_dur - old_dur;
                    v_track->clips.erase(
                        std::remove_if(v_track->clips.begin(), v_track->clips.end(),
                                       [id = victim->id](const ClipItem& c) { return c.id == id; }),
                        v_track->clips.end());
                    for (auto& c : v_track->clips) {
                        if (c.timeline_start >= v_start + old_dur) {
                            c.timeline_start += delta;
                        }
                    }
                    v_track->clips.push_back(make_video_clip(target, v_track->id, link_id, spec, v_start, src_dur));
                    v_track->sort_clips();

                    if (a_track) {
                        if (old_link != 0) {
                            a_track->clips.erase(
                                std::remove_if(a_track->clips.begin(), a_track->clips.end(),
                                               [old_link](const ClipItem& c) { return c.link_group_id == old_link; }),
                                a_track->clips.end());
                        } else {
                            overwrite_range_on_track(*a_track, TimeRange{v_start, old_dur});
                            assign_missing_clip_ids(target, *a_track);
                        }
                        for (auto& c : a_track->clips) {
                            if (c.timeline_start >= v_start + old_dur) {
                                c.timeline_start = std::max(RationalTime::zero(), c.timeline_start + delta);
                            }
                        }
                        a_track->clips.push_back(make_audio_clip(target, a_track->id, link_id, spec, v_start, src_dur));
                        a_track->sort_clips();
                    }
                    target.set_playhead(v_start + src_dur);
                    return true;
                }
                case EditMode::Replace: {
                    const RationalTime ph = target.playhead();
                    ClipItem* under = v_track->clip_at(ph);
                    if (!under) return false;
                    const RationalTime offset_into_clip = ph - under->timeline_start;
                    const RationalTime new_source_in = std::max(RationalTime::zero(), spec.source_in - offset_into_clip);
                    under->media_id = spec.media_id;
                    if (!spec.name.empty()) under->name = spec.name;
                    under->source_in = new_source_in;
                    under->source_out = new_source_in + under->timeline_duration;
                    if (spec.media_duration > RationalTime::zero()) {
                        under->source_total_duration = spec.media_duration;
                    }
                    if (under->link_group_id != 0) {
                        for (ClipItem* lc : target.find_linked_clips(under->link_group_id)) {
                            if (lc->id != under->id) {
                                lc->media_id = spec.media_id;
                                lc->source_in = new_source_in;
                                lc->source_out = new_source_in + lc->timeline_duration;
                            }
                        }
                    }
                    return true;
                }
                case EditMode::FitToFill: {
                    RationalTime fill_start = target.playhead();
                    RationalTime fill_dur = RationalTime::zero();
                    if (target.in_point().has_value() && target.out_point().has_value() &&
                        *target.out_point() > *target.in_point()) {
                        fill_start = *target.in_point();
                        fill_dur = *target.out_point() - fill_start;
                    } else if (ClipItem* under = v_track->clip_at(target.playhead())) {
                        fill_start = under->timeline_start;
                        fill_dur = under->timeline_duration;
                    } else {
                        return false;
                    }
                    if (fill_dur.num() <= 0) return false;
                    const double computed_speed = src_dur.to_seconds() / fill_dur.to_seconds();
                    overwrite_range_on_track(*v_track, TimeRange{fill_start, fill_dur});
                    assign_missing_clip_ids(target, *v_track);
                    v_track->clips.push_back(make_video_clip(target, v_track->id, link_id, spec, fill_start, fill_dur, computed_speed));
                    v_track->sort_clips();
                    if (a_track) {
                        overwrite_range_on_track(*a_track, TimeRange{fill_start, fill_dur});
                        assign_missing_clip_ids(target, *a_track);
                        ClipItem ac = make_audio_clip(target, a_track->id, link_id, spec, fill_start, fill_dur, computed_speed);
                        ac.timeline_start = fill_start;
                        ac.timeline_duration = fill_dur;
                        a_track->clips.push_back(std::move(ac));
                        a_track->sort_clips();
                    }
                    return true;
                }
            }
            return false;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::blade_split_at(Timeline& tl, CommandStack& stack, const RationalTime& time,
                                    std::optional<uint64_t> clip_id) {
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "BladeSplit",
        [time, clip_id](Timeline& target) -> bool {
            bool split_any = false;
            for (auto& tr : target.tracks()) {
                if (tr.locked) continue;
                if (!clip_id.has_value() && !tr.auto_track_selector) continue;
                std::vector<ClipItem> next_clips;
                next_clips.reserve(tr.clips.size() + 1);
                for (auto& c : tr.clips) {
                    const bool match = clip_id.has_value() ? (c.id == *clip_id) : true;
                    if (match && c.timeline_start < time && c.timeline_end() > time) {
                        const RationalTime left_dur = time - c.timeline_start;
                        const RationalTime right_dur = c.timeline_end() - time;
                        const RationalTime split_src = c.map_timeline_to_source(time);

                        ClipItem left = c;
                        left.timeline_duration = left_dur;
                        left.source_out = split_src;
                        if (left.link_group_id != 0) left.link_group_id = c.link_group_id;

                        ClipItem right = c;
                        right.id = target.allocate_clip_id();
                        right.timeline_start = time;
                        right.timeline_duration = right_dur;
                        right.source_in = split_src;
                        if (right.link_group_id != 0) right.link_group_id = 0;

                        next_clips.push_back(std::move(left));
                        next_clips.push_back(std::move(right));
                        split_any = true;
                    } else {
                        next_clips.push_back(std::move(c));
                    }
                }
                tr.clips = std::move(next_clips);
                tr.sort_clips();
            }
            return split_any;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::ripple_delete_clip(Timeline& tl, CommandStack& stack, uint64_t clip_id) {
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "RippleDeleteClip",
        [clip_id](Timeline& target) -> bool {
            uint64_t tr_id = 0;
            ClipItem* c = target.find_clip(clip_id, &tr_id);
            if (!c) return false;
            Track* tr = target.find_track(tr_id);
            if (!tr || tr->locked) return false;

            const RationalTime del_start = c->timeline_start;
            const RationalTime del_dur = c->timeline_duration;
            const uint64_t link_id = c->link_group_id;

            auto remove_and_ripple = [&](Track& track, uint64_t target_clip_id) {
                track.clips.erase(
                    std::remove_if(track.clips.begin(), track.clips.end(),
                                   [target_clip_id](const ClipItem& item) { return item.id == target_clip_id; }),
                    track.clips.end());
                for (auto& item : track.clips) {
                    if (item.timeline_start > del_start) {
                        item.timeline_start = std::max(RationalTime::zero(), item.timeline_start - del_dur);
                    }
                }
                track.sort_clips();
            };

            if (link_id != 0) {
                std::vector<std::pair<uint64_t, uint64_t>> linked;
                for (auto& t : target.tracks()) {
                    if (t.locked) continue;
                    for (auto& item : t.clips) {
                        if (item.link_group_id == link_id) {
                            linked.emplace_back(t.id, item.id);
                        }
                    }
                }
                for (auto [tid, cid] : linked) {
                    if (Track* lt = target.find_track(tid)) {
                        remove_and_ripple(*lt, cid);
                    }
                }
            } else {
                remove_and_ripple(*tr, clip_id);
            }
            return true;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::delete_gap(Timeline& tl, CommandStack& stack, uint64_t track_id, const TimeRange& gap_range) {
    if (gap_range.duration.num() <= 0) return false;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "DeleteGap",
        [track_id, gap_range](Timeline& target) -> bool {
            Track* tr = target.find_track(track_id);
            if (!tr || tr->locked) return false;
            bool moved = false;
            for (auto& c : tr->clips) {
                if (c.timeline_start >= gap_range.end()) {
                    c.timeline_start = std::max(gap_range.start, c.timeline_start - gap_range.duration);
                    moved = true;
                }
            }
            tr->sort_clips();
            return moved;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::close_all_gaps(Timeline& tl, CommandStack& stack, std::optional<uint64_t> track_id) {
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "CloseAllGaps",
        [track_id](Timeline& target) -> bool {
            bool changed = false;
            for (auto& tr : target.tracks()) {
                if (tr.locked) continue;
                if (track_id.has_value() && tr.id != *track_id) continue;
                tr.sort_clips();
                RationalTime cursor = RationalTime::zero();
                for (auto& c : tr.clips) {
                    if (c.timeline_start != cursor) {
                        c.timeline_start = cursor;
                        changed = true;
                    }
                    cursor += c.timeline_duration;
                }
            }
            return changed;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::lift_range(Timeline& tl, CommandStack& stack, const TimeRange& range,
                                std::optional<uint64_t> track_id) {
    if (range.duration.num() <= 0) return false;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "LiftRange",
        [range, track_id](Timeline& target) -> bool {
            bool touched = false;
            for (auto& tr : target.tracks()) {
                if (tr.locked) continue;
                if (track_id.has_value() && tr.id != *track_id) continue;
                if (!track_id.has_value() && !tr.auto_track_selector) continue;
                const auto prev_size = tr.clips.size();
                overwrite_range_on_track(tr, range);
                assign_missing_clip_ids(target, tr);
                if (tr.clips.size() != prev_size) touched = true;
                else {
                    // Check if any clip was trimmed
                    touched = true;
                }
            }
            return touched;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::extract_range(Timeline& tl, CommandStack& stack, const TimeRange& range,
                                   std::optional<uint64_t> track_id) {
    if (range.duration.num() <= 0) return false;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "ExtractRange",
        [range, track_id](Timeline& target) -> bool {
            bool touched = false;
            for (auto& tr : target.tracks()) {
                if (tr.locked) continue;
                if (track_id.has_value() && tr.id != *track_id) continue;
                if (!track_id.has_value() && !tr.auto_track_selector) continue;
                overwrite_range_on_track(tr, range);
                assign_missing_clip_ids(target, tr);
                for (auto& c : tr.clips) {
                    if (c.timeline_start >= range.end()) {
                        c.timeline_start = std::max(range.start, c.timeline_start - range.duration);
                    }
                }
                tr.sort_clips();
                touched = true;
            }
            return touched;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::swap_clips(Timeline& tl, CommandStack& stack, uint64_t clip_a_id, uint64_t clip_b_id) {
    if (clip_a_id == clip_b_id) return false;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "SwapClips",
        [clip_a_id, clip_b_id](Timeline& target) -> bool {
            uint64_t ta_id = 0, tb_id = 0;
            ClipItem* a = target.find_clip(clip_a_id, &ta_id);
            ClipItem* b = target.find_clip(clip_b_id, &tb_id);
            if (!a || !b) return false;
            Track* ta = target.find_track(ta_id);
            Track* tb = target.find_track(tb_id);
            if (!ta || !tb || ta->locked || tb->locked) return false;

            if (ta_id == tb_id && a->timeline_start != b->timeline_start) {
                // Swap contiguous/same-track positions cleanly by repacking around earlier clip
                ClipItem* first = (a->timeline_start < b->timeline_start) ? a : b;
                ClipItem* second = (first == a) ? b : a;
                const RationalTime first_start = first->timeline_start;
                const RationalTime delta = second->timeline_duration - first->timeline_duration;
                const RationalTime mid_start = first->timeline_end();
                const RationalTime second_start = second->timeline_start;

                first->timeline_start = second_start + delta;
                second->timeline_start = first_start;
                for (auto& c : ta->clips) {
                    if (c.id == first->id || c.id == second->id) continue;
                    if (c.timeline_start >= mid_start && c.timeline_start < second_start) {
                        c.timeline_start = std::max(RationalTime::zero(), c.timeline_start + delta);
                    }
                }
                ta->sort_clips();
                return true;
            }

            std::swap(a->timeline_start, b->timeline_start);
            ta->sort_clips();
            tb->sort_clips();
            return true;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::shuffle_track_clips(Timeline& tl, CommandStack& stack, uint64_t track_id,
                                         const std::vector<uint64_t>& new_order_clip_ids) {
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "ShuffleTrackClips",
        [track_id, new_order_clip_ids](Timeline& target) -> bool {
            Track* tr = target.find_track(track_id);
            if (!tr || tr->locked || new_order_clip_ids.size() != tr->clips.size()) return false;
            std::vector<ClipItem> reordered;
            reordered.reserve(tr->clips.size());
            RationalTime cursor = tr->clips.empty() ? RationalTime::zero() : tr->clips.front().timeline_start;
            for (uint64_t id : new_order_clip_ids) {
                ClipItem* found = tr->find_clip(id);
                if (!found) return false;
                ClipItem copy = *found;
                copy.timeline_start = cursor;
                cursor += copy.timeline_duration;
                reordered.push_back(std::move(copy));
            }
            tr->clips = std::move(reordered);
            return true;
        });
    return stack.execute(std::move(cmd), tl);
}

bool EditOperations::set_av_split_offsets(Timeline& tl, CommandStack& stack, uint64_t video_clip_id,
                                          const RationalTime& audio_start_delta,
                                          const RationalTime& audio_end_delta) {
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "SetAVSplitOffsets",
        [video_clip_id, audio_start_delta, audio_end_delta](Timeline& target) -> bool {
            ClipItem* vc = target.find_clip(video_clip_id);
            if (!vc || vc->link_group_id == 0) return false;
            bool updated = false;
            for (ClipItem* lc : target.find_linked_clips(vc->link_group_id)) {
                if (lc->id == vc->id || lc->media_role != TrackType::Audio) continue;
                const RationalTime new_start = std::max(RationalTime::zero(), vc->timeline_start + audio_start_delta);
                const RationalTime new_end = std::max(new_start + RationalTime::from_frames(1, target.fps()),
                                                      vc->timeline_end() + audio_end_delta);
                const RationalTime start_shift = new_start - lc->timeline_start;
                lc->timeline_start = new_start;
                lc->timeline_duration = new_end - new_start;
                lc->source_in = std::max(RationalTime::zero(), lc->source_in + start_shift);
                lc->source_out = lc->source_in + lc->timeline_duration;
                if (Track* atr = target.find_track(lc->track_id)) {
                    const uint64_t lc_id = lc->id;
                    for (auto& other : atr->clips) {
                        if (other.id == lc_id) continue;
                        if (other.timeline_start < new_start && other.timeline_end() > new_start) {
                            other.timeline_duration = new_start - other.timeline_start;
                            other.source_out = other.source_in + other.timeline_duration;
                        }
                        if (other.timeline_start < new_end && other.timeline_end() > new_end &&
                            other.timeline_start >= new_start) {
                            const RationalTime cut = new_end - other.timeline_start;
                            other.timeline_start = new_end;
                            other.timeline_duration = other.timeline_duration - cut;
                            other.source_in += cut;
                        }
                    }
                    atr->sort_clips();
                }
                updated = true;
            }
            return updated;
        });
    return stack.execute(std::move(cmd), tl);
}

ClipboardPayload EditOperations::copy_clips(const Timeline& tl, const std::vector<uint64_t>& clip_ids) {
    ClipboardPayload payload;
    RationalTime min_start{};
    bool first = true;
    for (uint64_t id : clip_ids) {
        if (const ClipItem* c = tl.find_clip(id)) {
            if (first || c->timeline_start < min_start) {
                min_start = c->timeline_start;
                first = false;
            }
            payload.clips.push_back(*c);
        }
    }
    payload.anchor_time = min_start;
    payload.has_data = !payload.clips.empty();
    return payload;
}

ClipboardPayload EditOperations::cut_clips(Timeline& tl, CommandStack& stack, const std::vector<uint64_t>& clip_ids,
                                           bool ripple) {
    ClipboardPayload payload = copy_clips(tl, clip_ids);
    if (!payload.has_data) return payload;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "CutClips",
        [clip_ids, ripple](Timeline& target) -> bool {
            std::set<uint64_t> ids(clip_ids.begin(), clip_ids.end());
            bool any = false;
            for (auto& tr : target.tracks()) {
                if (tr.locked) continue;
                for (auto it = tr.clips.begin(); it != tr.clips.end();) {
                    if (ids.count(it->id)) {
                        const RationalTime rm_start = it->timeline_start;
                        const RationalTime rm_dur = it->timeline_duration;
                        it = tr.clips.erase(it);
                        if (ripple) {
                            for (auto& other : tr.clips) {
                                if (other.timeline_start >= rm_start) {
                                    other.timeline_start = std::max(RationalTime::zero(), other.timeline_start - rm_dur);
                                }
                            }
                        }
                        any = true;
                    } else {
                        ++it;
                    }
                }
                tr.sort_clips();
            }
            return any;
        });
    if (!stack.execute(std::move(cmd), tl)) {
        return ClipboardPayload{};
    }
    return payload;
}

bool EditOperations::paste_clips(Timeline& tl, CommandStack& stack, const ClipboardPayload& payload,
                                 const RationalTime& target_time, bool ripple_insert) {
    if (!payload.has_data || payload.clips.empty()) return false;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "PasteClips",
        [payload, target_time, ripple_insert](Timeline& target) -> bool {
            const RationalTime offset = target_time - payload.anchor_time;
            RationalTime total_span = RationalTime::zero();
            for (const auto& c : payload.clips) {
                total_span = std::max(total_span, (c.timeline_end() - payload.anchor_time));
            }
            for (const auto& orig : payload.clips) {
                Track* dest = target.find_track(orig.track_id);
                if (!dest || dest->type != orig.media_role) {
                    dest = target.primary_track(orig.media_role);
                }
                if (!dest || dest->locked) continue;

                ClipItem pasted = orig;
                pasted.id = target.allocate_clip_id();
                pasted.track_id = dest->id;
                pasted.timeline_start = std::max(RationalTime::zero(), orig.timeline_start + offset);
                if (ripple_insert) {
                    split_and_ripple_track(target, *dest, pasted.timeline_start, pasted.timeline_duration);
                } else {
                    overwrite_range_on_track(*dest, pasted.timeline_range());
                    assign_missing_clip_ids(target, *dest);
                }
                dest->clips.push_back(std::move(pasted));
                dest->sort_clips();
            }
            (void)total_span;
            return true;
        });
    return stack.execute(std::move(cmd), tl);
}

uint64_t EditOperations::create_compound_clip(Timeline& tl, CommandStack& stack,
                                              const std::vector<uint64_t>& clip_ids,
                                              const std::string& compound_name) {
    if (clip_ids.empty()) return 0;
    uint64_t created_clip_id = 0;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "CreateCompoundClip",
        [clip_ids, compound_name, &created_clip_id](Timeline& target) -> bool {
            std::set<uint64_t> ids(clip_ids.begin(), clip_ids.end());
            std::vector<ClipItem> gathered;
            RationalTime min_s{};
            RationalTime max_e{};
            bool first = true;
            for (const auto& tr : target.tracks()) {
                for (const auto& c : tr.clips) {
                    if (ids.count(c.id)) {
                        if (first) {
                            min_s = c.timeline_start;
                            max_e = c.timeline_end();
                            first = false;
                        } else {
                            min_s = std::min(min_s, c.timeline_start);
                            max_e = std::max(max_e, c.timeline_end());
                        }
                        gathered.push_back(c);
                    }
                }
            }
            if (gathered.empty()) return false;

            const uint64_t compound_id = target.allocate_clip_id() + 100000;
            Timeline sub(compound_id, compound_name, target.fps(), target.width(), target.height());
            for (auto g : gathered) {
                g.timeline_start = g.timeline_start - min_s;
                Track* st = sub.primary_track(g.media_role);
                if (!st) st = &sub.add_track(g.media_role);
                g.track_id = st->id;
                st->clips.push_back(std::move(g));
            }
            for (auto& st : sub.tracks()) st.sort_clips();
            target.set_compound_timeline(compound_id, std::move(sub));

            for (auto& tr : target.tracks()) {
                tr.clips.erase(std::remove_if(tr.clips.begin(), tr.clips.end(),
                                              [&ids](const ClipItem& c) { return ids.count(c.id) > 0; }),
                               tr.clips.end());
            }

            Track* primary_v = target.primary_track(TrackType::Video);
            if (!primary_v) return false;
            ClipItem cc;
            cc.id = target.allocate_clip_id();
            cc.track_id = primary_v->id;
            cc.kind = ClipKind::CompoundClip;
            cc.media_role = TrackType::Video;
            cc.name = compound_name;
            cc.timeline_start = min_s;
            cc.timeline_duration = max_e - min_s;
            cc.source_in = RationalTime::zero();
            cc.source_out = cc.timeline_duration;
            cc.source_total_duration = cc.timeline_duration;
            cc.compound_timeline_id = compound_id;
            created_clip_id = cc.id;
            primary_v->clips.push_back(std::move(cc));
            primary_v->sort_clips();
            return true;
        });
    if (!stack.execute(std::move(cmd), tl)) return 0;
    return created_clip_id;
}

uint64_t EditOperations::switch_multicam_angle(Timeline& tl, CommandStack& stack, uint64_t clip_id,
                                               std::size_t angle_index) {
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "SwitchMulticamAngle",
        [clip_id, angle_index](Timeline& target) -> bool {
            ClipItem* c = target.find_clip(clip_id);
            if (!c || c->kind != ClipKind::MulticamClip || angle_index >= c->multicam_angles.size()) {
                return false;
            }
            c->multicam_active_angle = angle_index;
            c->media_id = c->multicam_angles[angle_index].media_id;
            return true;
        });
    return stack.execute(std::move(cmd), tl) ? clip_id : 0;
}

} // namespace owncut::timeline
