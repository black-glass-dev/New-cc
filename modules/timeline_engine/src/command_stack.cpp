#include "timeline/command_stack.hpp"

#include <utility>

namespace owncut::timeline {

SnapshotTimelineCommand::SnapshotTimelineCommand(std::string desc, ActionFn action, uint64_t coalesce_key)
    : description_(std::move(desc)), action_(std::move(action)), coalesce_key_(coalesce_key) {}

bool SnapshotTimelineCommand::execute(Timeline& timeline) {
    if (!captured_) {
        before_snapshot_ = timeline;
        if (!action_ || !action_(timeline)) {
            timeline = before_snapshot_;
            return false;
        }
        after_snapshot_ = timeline;
        captured_ = true;
        return true;
    }
    timeline = after_snapshot_;
    return true;
}

bool SnapshotTimelineCommand::undo(Timeline& timeline) {
    if (!captured_) return false;
    timeline = before_snapshot_;
    return true;
}

bool SnapshotTimelineCommand::try_coalesce(const ITimelineCommand& newer) {
    if (coalesce_key_ == 0 || newer.coalesce_id() != coalesce_key_) return false;
    const auto* rhs = dynamic_cast<const SnapshotTimelineCommand*>(&newer);
    if (!rhs || !rhs->captured_) return false;
    after_snapshot_ = rhs->after_snapshot_;
    description_ = rhs->description_;
    return true;
}

CompositeCommand::CompositeCommand(std::string desc) : description_(std::move(desc)) {}

void CompositeCommand::add(std::unique_ptr<ITimelineCommand> cmd) {
    if (cmd) commands_.push_back(std::move(cmd));
}

bool CompositeCommand::execute(Timeline& timeline) {
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        if (!commands_[i]->execute(timeline)) {
            for (std::size_t j = i; j > 0; --j) {
                commands_[j - 1]->undo(timeline);
            }
            return false;
        }
    }
    return true;
}

bool CompositeCommand::undo(Timeline& timeline) {
    for (std::size_t i = commands_.size(); i > 0; --i) {
        if (!commands_[i - 1]->undo(timeline)) return false;
    }
    return true;
}

bool CommandStack::execute(std::unique_ptr<ITimelineCommand> cmd, Timeline& timeline) {
    if (!cmd) return false;
    if (!cmd->execute(timeline)) return false;
    redo_stack_.clear();

    if (coalescing_enabled_ && !undo_stack_.empty() && cmd->coalesce_id() != 0 &&
        undo_stack_.back()->coalesce_id() == cmd->coalesce_id() &&
        undo_stack_.back()->try_coalesce(*cmd)) {
        return true;
    }

    undo_stack_.push_back(std::move(cmd));
    if (max_depth_ > 0 && undo_stack_.size() > max_depth_) {
        undo_stack_.erase(undo_stack_.begin());
    }
    return true;
}

bool CommandStack::undo(Timeline& timeline) {
    if (undo_stack_.empty()) return false;
    auto cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    if (!cmd->undo(timeline)) {
        undo_stack_.push_back(std::move(cmd));
        return false;
    }
    redo_stack_.push_back(std::move(cmd));
    return true;
}

bool CommandStack::redo(Timeline& timeline) {
    if (redo_stack_.empty()) return false;
    auto cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    if (!cmd->execute(timeline)) {
        redo_stack_.push_back(std::move(cmd));
        return false;
    }
    undo_stack_.push_back(std::move(cmd));
    return true;
}

void CommandStack::clear() {
    undo_stack_.clear();
    redo_stack_.clear();
}

std::vector<HistoryEntryInfo> CommandStack::history_entries() const {
    std::vector<HistoryEntryInfo> out;
    out.reserve(undo_stack_.size() + redo_stack_.size());
    for (std::size_t i = 0; i < undo_stack_.size(); ++i) {
        out.push_back(HistoryEntryInfo{undo_stack_[i]->description(), i + 1 == undo_stack_.size()});
    }
    for (auto it = redo_stack_.rbegin(); it != redo_stack_.rend(); ++it) {
        out.push_back(HistoryEntryInfo{(*it)->description(), false});
    }
    return out;
}

} // namespace owncut::timeline
