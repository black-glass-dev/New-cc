#include "editor/command_history.hpp"

#include <stdexcept>
#include <set>
#include <utility>

namespace editor {
AddClipCommand::AddClipCommand(std::string track_id, Clip clip)
    : track_id_(std::move(track_id)), clip_(std::move(clip)) {
    if (track_id_.empty() || clip_.id.empty() || clip_.media_id.empty())
        throw std::invalid_argument("add command requires track, clip and media ids");
}

void AddClipCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("add command is already applied");
    timeline.add_clip(track_id_, clip_);
    applied_ = true;
}

void AddClipCommand::undo(Timeline& timeline) {
    if (!applied_) throw std::logic_error("add command is not applied");
    (void)timeline.remove_clip(clip_.id);
    applied_ = false;
}

MoveClipCommand::MoveClipCommand(std::string clip_id, std::string destination_track_id,
                                 Timestamp destination_start)
    : clip_id_(std::move(clip_id)), destination_track_id_(std::move(destination_track_id)),
      destination_start_(destination_start) {
    if (clip_id_.empty() || destination_track_id_.empty())
        throw std::invalid_argument("move command requires clip and destination track ids");
}

void MoveClipCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("move command is already applied");
    if (!captured_) {
        before_ = timeline.locate_clip(clip_id_);
        captured_ = true;
    }
    timeline.move_clip(clip_id_, destination_track_id_, destination_start_);
    applied_ = true;
}

void MoveClipCommand::undo(Timeline& timeline) {
    if (!captured_ || !applied_) throw std::logic_error("move command is not applied");
    timeline.move_clip(clip_id_, before_.track_id, before_.clip.timeline_start);
    applied_ = false;
}

RelinkClipMediaCommand::RelinkClipMediaCommand(std::string clip_id, std::string replacement_media_id)
    : clip_id_(std::move(clip_id)), replacement_media_id_(std::move(replacement_media_id)) {
    if (clip_id_.empty() || replacement_media_id_.empty())
        throw std::invalid_argument("relink command requires clip and media ids");
}

void RelinkClipMediaCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("relink command is already applied");
    if (!captured_) {
        previous_media_id_ = timeline.locate_clip(clip_id_).clip.media_id;
        if (previous_media_id_ == replacement_media_id_)
            throw std::invalid_argument("clip already references the selected media");
        affected_clip_ids_ = timeline.clips_referencing_media(previous_media_id_);
        captured_ = true;
    }
    timeline.relink_media_references(affected_clip_ids_, previous_media_id_, replacement_media_id_);
    applied_ = true;
}

void RelinkClipMediaCommand::undo(Timeline& timeline) {
    if (!captured_ || !applied_) throw std::logic_error("relink command is not applied");
    timeline.relink_media_references(affected_clip_ids_, replacement_media_id_, previous_media_id_);
    applied_ = false;
}

SplitClipCommand::SplitClipCommand(std::string clip_id, Timestamp cut_at, std::string right_clip_id)
    : clip_id_(std::move(clip_id)), cut_at_(cut_at), right_clip_id_(std::move(right_clip_id)) {
    if (clip_id_.empty() || right_clip_id_.empty() || clip_id_ == right_clip_id_)
        throw std::invalid_argument("split command requires distinct non-empty clip ids");
}

void SplitClipCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("split command is already applied");
    if (!captured_) { before_ = timeline.locate_clip(clip_id_); captured_ = true; }
    (void)timeline.split_clip(clip_id_, cut_at_, right_clip_id_);
    applied_ = true;
}

void SplitClipCommand::undo(Timeline& timeline) {
    if (!captured_ || !applied_) throw std::logic_error("split command is not applied");
    (void)timeline.remove_clip(right_clip_id_);
    timeline.replace_clip(before_.clip);
    applied_ = false;
}

TrimClipCommand::TrimClipCommand(std::string clip_id, TrimEdge edge, Timestamp source_boundary)
    : clip_id_(std::move(clip_id)), edge_(edge), source_boundary_(source_boundary) {
    if (clip_id_.empty()) throw std::invalid_argument("trim command requires a clip id");
}

void TrimClipCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("trim command is already applied");
    if (!captured_) {
        before_ = timeline.locate_clip(clip_id_);
        after_ = before_.clip;
        captured_ = true;
    }
    if (edge_ == TrimEdge::left) {
        const auto delta = source_boundary_ - before_.clip.source_in;
        after_.timeline_start = before_.clip.timeline_start + delta;
        after_.source_in = source_boundary_;
    } else {
        after_.source_out = source_boundary_;
    }
    timeline.replace_clip(after_);
    applied_ = true;
}

void TrimClipCommand::undo(Timeline& timeline) {
    if (!captured_ || !applied_) throw std::logic_error("trim command is not applied");
    timeline.replace_clip(before_.clip);
    applied_ = false;
}

DeleteClipCommand::DeleteClipCommand(std::string clip_id) : clip_id_(std::move(clip_id)) {
    if (clip_id_.empty()) throw std::invalid_argument("delete command requires a clip id");
}

void DeleteClipCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("delete command is already applied");
    if (!captured_) { before_ = timeline.locate_clip(clip_id_); captured_ = true; }
    (void)timeline.remove_clip(clip_id_);
    applied_ = true;
}

void DeleteClipCommand::undo(Timeline& timeline) {
    if (!captured_ || !applied_) throw std::logic_error("delete command is not applied");
    timeline.add_clip(before_.track_id, before_.clip);
    applied_ = false;
}

RippleDeleteClipCommand::RippleDeleteClipCommand(std::string clip_id) : clip_id_(std::move(clip_id)) {
    if (clip_id_.empty()) throw std::invalid_argument("ripple delete requires a clip id");
}

void RippleDeleteClipCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("ripple delete command is already applied");
    auto result = timeline.ripple_delete(clip_id_);
    if (!captured_) { result_ = std::move(result); captured_ = true; }
    applied_ = true;
}

void RippleDeleteClipCommand::undo(Timeline& timeline) {
    if (!captured_ || !applied_) throw std::logic_error("ripple delete command is not applied");
    timeline.restore_ripple_delete(result_);
    applied_ = false;
}

SetClipGradesCommand::SetClipGradesCommand(std::vector<std::string> ids, color::Grade grade)
    : ids_(std::move(ids)), grade_(std::move(grade)) {
    color::validate(grade_);
    if (ids_.empty()) throw std::invalid_argument("grade command requires target clips");
    std::set<std::string> unique;
    for (const auto& id : ids_) if (id.empty() || !unique.insert(id).second)
        throw std::invalid_argument("grade command requires unique nonempty clip ids");
}
void SetClipGradesCommand::apply(Timeline& timeline) {
    if (applied_) throw std::logic_error("grade command is already applied");
    auto updated = timeline;
    std::vector<color::Grade> before;
    if (!captured_) before.reserve(ids_.size());
    for (const auto& id : ids_) {
        if (!captured_) before.push_back(updated.locate_clip(id).clip.grade);
        updated.set_clip_grade(id,grade_);
    }
    if (!captured_) { before_ = std::move(before); captured_ = true; }
    timeline = std::move(updated); // publish only after every target succeeds
    applied_ = true;
}
void SetClipGradesCommand::undo(Timeline& timeline) {
    if (!applied_) throw std::logic_error("grade command is not applied");
    auto updated = timeline;
    for (std::size_t i=0;i<ids_.size();++i) updated.set_clip_grade(ids_[i],before_[i]);
    timeline = std::move(updated);
    applied_ = false;
}

CommandHistory::CommandHistory(std::size_t max_depth) : max_depth_(max_depth) {
    if (max_depth_ == 0) throw std::invalid_argument("command history depth must be positive");
}

void CommandHistory::execute(Timeline& timeline, std::unique_ptr<EditCommand> command) {
    if (!command) throw std::invalid_argument("cannot execute a null command");
    undo_.push_back(std::move(command));
    try {
        undo_.back()->apply(timeline);
    } catch (...) {
        undo_.pop_back();
        throw;
    }
    redo_.clear();
    if (undo_.size() > max_depth_)
        undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - max_depth_));
}

bool CommandHistory::undo(Timeline& timeline) {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    try {
        redo_.back()->undo(timeline);
    } catch (...) {
        undo_.push_back(std::move(redo_.back()));
        redo_.pop_back();
        throw;
    }
    return true;
}

bool CommandHistory::redo(Timeline& timeline) {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    try {
        undo_.back()->apply(timeline);
    } catch (...) {
        redo_.push_back(std::move(undo_.back()));
        undo_.pop_back();
        throw;
    }
    return true;
}

void CommandHistory::clear() noexcept {
    undo_.clear();
    redo_.clear();
}
} // namespace editor
