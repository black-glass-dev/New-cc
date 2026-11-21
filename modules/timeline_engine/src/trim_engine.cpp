#include "timeline/trim_engine.hpp"

#include <algorithm>
#include <cmath>

namespace owncut::timeline {
namespace {

RationalTime clip_min_duration(const Timeline& tl) {
    return RationalTime::from_frames(TrimEngine::kDefaultMinFrames, tl.fps());
}

TrimClampReport apply_single_trim(Timeline& tl, uint64_t clip_id, TrimMode mode,
                                  const RationalTime& delta, bool ripple, bool safe_trimming) {
    TrimClampReport rep;
    rep.requested_delta = delta;

    uint64_t tr_id = 0;
    ClipItem* c = tl.find_clip(clip_id, &tr_id);
    if (!c) return rep;
    Track* tr = tl.find_track(tr_id);
    if (!tr || tr->locked) return rep;

    tr->sort_clips();
    std::size_t idx = 0;
    for (; idx < tr->clips.size(); ++idx) {
        if (tr->clips[idx].id == clip_id) break;
    }
    if (idx >= tr->clips.size()) return rep;

    const RationalTime min_dur = clip_min_duration(tl);
    ClipItem& clip = tr->clips[idx];
    const RationalTime prev_end = (idx > 0) ? tr->clips[idx - 1].timeline_end() : RationalTime::zero();
    const bool has_next = (idx + 1 < tr->clips.size());
    const RationalTime next_start = has_next ? tr->clips[idx + 1].timeline_start : RationalTime(1000000000, 1);

    if (mode == TrimMode::TrimIn) {
        // delta > 0 trims In forward (shortens clip); delta < 0 extends head earlier.
        RationalTime d = delta;
        const RationalTime max_forward = clip.timeline_duration - min_dur;
        if (d > max_forward) {
            d = max_forward;
            rep.clamped_by_min_duration = true;
        }
        if (d.num() < 0) {
            const RationalTime max_left_timeline = clip.timeline_start - RationalTime::zero();
            if (-d > max_left_timeline) {
                d = -max_left_timeline;
                rep.clamped_by_neighbor = true;
            }
            if (!ripple && -d > (clip.timeline_start - prev_end)) {
                d = -(clip.timeline_start - prev_end);
                rep.clamped_by_neighbor = true;
            }
            if (safe_trimming && -d > clip.source_in) {
                d = -clip.source_in;
                rep.clamped_by_media_start = true;
            }
        }
        if (ripple) {
            // Ripple TrimIn shifts this clip's start only if it stayed anchored at timeline_start:
            // Standard NLE Ripple Trim-In pulls clip start back to prev position and shifts remaining clips by -d.
            clip.source_in += d;
            clip.timeline_duration -= d;
            for (std::size_t k = idx + 1; k < tr->clips.size(); ++k) {
                tr->clips[k].timeline_start = std::max(RationalTime::zero(), tr->clips[k].timeline_start - d);
            }
        } else {
            clip.timeline_start += d;
            clip.source_in += d;
            clip.timeline_duration -= d;
        }
        rep.applied_delta = d;
        return rep;
    }

    if (mode == TrimMode::TrimOut) {
        // delta > 0 extends tail; delta < 0 shortens tail.
        RationalTime d = delta;
        const RationalTime min_delta = -(clip.timeline_duration - min_dur);
        if (d < min_delta) {
            d = min_delta;
            rep.clamped_by_min_duration = true;
        }
        if (d.num() > 0) {
            if (!ripple && has_next) {
                const RationalTime gap = next_start - clip.timeline_end();
                if (d > gap) {
                    d = gap;
                    rep.clamped_by_neighbor = true;
                }
            }
            if (safe_trimming && clip.source_total_duration > RationalTime::zero()) {
                const RationalTime tail_avail = clip.source_total_duration - clip.source_out;
                if (d > tail_avail) {
                    d = std::max(RationalTime::zero(), tail_avail);
                    rep.clamped_by_media_end = true;
                }
            }
        }
        clip.timeline_duration += d;
        clip.source_out += d;
        if (ripple) {
            for (std::size_t k = idx + 1; k < tr->clips.size(); ++k) {
                tr->clips[k].timeline_start = std::max(RationalTime::zero(), tr->clips[k].timeline_start + d);
            }
        }
        rep.applied_delta = d;
        return rep;
    }

    if (mode == TrimMode::RollEdit) {
        // Rolls the cut point between clip[idx] (outgoing) and clip[idx+1] (incoming)
        if (!has_next) return rep;
        ClipItem& out_c = tr->clips[idx];
        ClipItem& in_c = tr->clips[idx + 1];
        RationalTime d = delta;

        // Lower bound (moving edit point left): out_c duration >= min_dur, in_c head <= source_in
        RationalTime min_d = -(out_c.timeline_duration - min_dur);
        if (safe_trimming && -in_c.source_in > min_d) {
            if (d < -in_c.source_in) rep.clamped_by_media_start = true;
            min_d = -in_c.source_in;
        }
        // Upper bound (moving edit point right): in_c duration >= min_dur, out_c tail <= total
        RationalTime max_d = in_c.timeline_duration - min_dur;
        if (safe_trimming && out_c.source_total_duration > RationalTime::zero()) {
            const RationalTime avail = std::max(RationalTime::zero(), out_c.source_total_duration - out_c.source_out);
            if (avail < max_d) {
                if (d > avail) rep.clamped_by_media_end = true;
                max_d = avail;
            }
        }

        if (d < min_d) {
            d = min_d;
            if (!rep.clamped_by_media_start) rep.clamped_by_min_duration = true;
        }
        if (d > max_d) {
            d = max_d;
            if (!rep.clamped_by_media_end) rep.clamped_by_min_duration = true;
        }

        out_c.timeline_duration += d;
        out_c.source_out += d;
        in_c.timeline_start += d;
        in_c.timeline_duration -= d;
        in_c.source_in += d;
        rep.applied_delta = d;
        return rep;
    }

    if (mode == TrimMode::SlipEdit) {
        // Shifts source_in/source_out inside fixed timeline footprint [start, end)
        RationalTime d = delta;
        if (safe_trimming) {
            if (d.num() < 0 && -d > clip.source_in) {
                d = -clip.source_in;
                rep.clamped_by_media_start = true;
            }
            if (d.num() > 0 && clip.source_total_duration > RationalTime::zero()) {
                const RationalTime avail = std::max(RationalTime::zero(), clip.source_total_duration - clip.source_out);
                if (d > avail) {
                    d = avail;
                    rep.clamped_by_media_end = true;
                }
            }
        }
        clip.source_in += d;
        clip.source_out += d;
        rep.applied_delta = d;
        return rep;
    }

    if (mode == TrimMode::SlideEdit) {
        // Slides clip left/right along track, trimming adjacent outgoing tail & incoming head to keep neighbors locked
        RationalTime d = delta;
        if (idx > 0) {
            ClipItem& left_n = tr->clips[idx - 1];
            const RationalTime max_left_pull = left_n.timeline_duration - min_dur;
            if (d.num() < 0 && -d > max_left_pull) {
                d = -max_left_pull;
                rep.clamped_by_min_duration = true;
            }
            if (safe_trimming && d.num() > 0 && left_n.source_total_duration > RationalTime::zero()) {
                const RationalTime avail = std::max(RationalTime::zero(), left_n.source_total_duration - left_n.source_out);
                if (d > avail) {
                    d = avail;
                    rep.clamped_by_media_end = true;
                }
            }
        } else if (d.num() < 0 && -d > clip.timeline_start) {
            d = -clip.timeline_start;
            rep.clamped_by_neighbor = true;
        }
        if (has_next) {
            ClipItem& right_n = tr->clips[idx + 1];
            const RationalTime max_right_push = right_n.timeline_duration - min_dur;
            if (d.num() > 0 && d > max_right_push) {
                d = max_right_push;
                rep.clamped_by_min_duration = true;
            }
            if (safe_trimming && d.num() < 0 && -d > right_n.source_in) {
                d = -right_n.source_in;
                rep.clamped_by_media_start = true;
            }
        }

        if (idx > 0 && tr->clips[idx - 1].timeline_end() == clip.timeline_start) {
            tr->clips[idx - 1].timeline_duration += d;
            tr->clips[idx - 1].source_out += d;
        }
        if (has_next && tr->clips[idx + 1].timeline_start == clip.timeline_end()) {
            tr->clips[idx + 1].timeline_start += d;
            tr->clips[idx + 1].timeline_duration -= d;
            tr->clips[idx + 1].source_in += d;
        }
        clip.timeline_start += d;
        rep.applied_delta = d;
        return rep;
    }

    return rep;
}

} // namespace

TrimClampReport TrimEngine::trim_clip(Timeline& tl, CommandStack& stack,
                                      uint64_t clip_id, TrimMode mode,
                                      const RationalTime& delta,
                                      bool ripple, bool safe_trimming,
                                      uint64_t coalesce_key) {
    TrimClampReport report;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "TrimClip",
        [&report, clip_id, mode, delta, ripple, safe_trimming](Timeline& target) -> bool {
            report = apply_single_trim(target, clip_id, mode, delta, ripple, safe_trimming);
            return true;
        },
        coalesce_key);
    stack.execute(std::move(cmd), tl);
    return report;
}

bool TrimEngine::asymmetric_multi_track_trim(Timeline& tl, CommandStack& stack,
                                             const std::vector<TrackTrimAction>& actions,
                                             bool ripple, bool safe_trimming) {
    if (actions.empty()) return false;
    auto cmd = std::make_unique<SnapshotTimelineCommand>(
        "AsymmetricMultiTrackTrim",
        [actions, ripple, safe_trimming](Timeline& target) -> bool {
            for (const auto& act : actions) {
                apply_single_trim(target, act.clip_id, act.mode, act.delta, ripple, safe_trimming);
            }
            return true;
        });
    return stack.execute(std::move(cmd), tl);
}

std::optional<ABTrimHandles> TrimEngine::inspect_ab_handles(const Timeline& tl,
                                                            uint64_t outgoing_clip_id,
                                                            uint64_t incoming_clip_id) {
    const ClipItem* out_c = tl.find_clip(outgoing_clip_id);
    const ClipItem* in_c = tl.find_clip(incoming_clip_id);
    if (!out_c || !in_c) return std::nullopt;

    ABTrimHandles h;
    h.outgoing_clip_id = outgoing_clip_id;
    h.incoming_clip_id = incoming_clip_id;
    h.outgoing_head_available = out_c->source_in;
    h.outgoing_tail_available = (out_c->source_total_duration > out_c->source_out)
                                    ? (out_c->source_total_duration - out_c->source_out)
                                    : RationalTime::zero();
    h.incoming_head_available = in_c->source_in;
    h.incoming_tail_available = (in_c->source_total_duration > in_c->source_out)
                                    ? (in_c->source_total_duration - in_c->source_out)
                                    : RationalTime::zero();
    h.edit_point = out_c->timeline_end();
    h.outgoing_frame_time = (out_c->source_out > RationalTime::from_frames(1, tl.fps()))
                                ? (out_c->source_out - RationalTime::from_frames(1, tl.fps()))
                                : out_c->source_in;
    h.incoming_frame_time = in_c->source_in;
    return h;
}

void DynamicJKLLoopTrim::set_shuttle_j() {
    if (speed_ >= 0.0) speed_ = -1.0;
    else speed_ = std::max(-8.0, speed_ * 2.0);
}

void DynamicJKLLoopTrim::set_shuttle_k() {
    speed_ = 0.0;
}

void DynamicJKLLoopTrim::set_shuttle_l() {
    if (speed_ <= 0.0) speed_ = 1.0;
    else speed_ = std::min(8.0, speed_ * 2.0);
}

void DynamicJKLLoopTrim::step_k_plus_j(Timeline& tl) {
    speed_ = 0.0;
    const RationalTime step = RationalTime::from_frames(1, tl.fps());
    tl.set_playhead(tl.playhead() > step ? (tl.playhead() - step) : RationalTime::zero());
}

void DynamicJKLLoopTrim::step_k_plus_l(Timeline& tl) {
    speed_ = 0.0;
    const RationalTime step = RationalTime::from_frames(1, tl.fps());
    tl.set_playhead(tl.playhead() + step);
}

void DynamicJKLLoopTrim::configure_loop(const RationalTime& edit_point,
                                        const RationalTime& pre_roll,
                                        const RationalTime& post_roll) {
    loop_center_ = edit_point;
    pre_roll_ = pre_roll;
    post_roll_ = post_roll;
}

void DynamicJKLLoopTrim::tick(Timeline& tl, const RationalTime& host_dt) {
    if (speed_ == 0.0 || host_dt.num() <= 0) return;
    const int64_t scaled = std::llround(speed_ * 1000.0);
    RationalTime next = tl.playhead() + scale_rational(host_dt, scaled, 1000);
    if (next < RationalTime::zero()) next = RationalTime::zero();

    if (loop_enabled_) {
        const RationalTime loop_start = (loop_center_ > pre_roll_) ? (loop_center_ - pre_roll_) : RationalTime::zero();
        const RationalTime loop_end = loop_center_ + post_roll_;
        if (speed_ > 0.0 && next >= loop_end) {
            next = loop_start;
        } else if (speed_ < 0.0 && next <= loop_start) {
            next = loop_end;
        }
    }
    tl.set_playhead(next);
}

} // namespace owncut::timeline
