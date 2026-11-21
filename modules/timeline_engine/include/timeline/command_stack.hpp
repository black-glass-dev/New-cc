#pragma once

#include "timeline/timeline_model.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace owncut::timeline {

class ITimelineCommand {
public:
    virtual ~ITimelineCommand() = default;
    virtual bool execute(Timeline& timeline) = 0;
    virtual bool undo(Timeline& timeline) = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual uint64_t coalesce_id() const noexcept { return 0; }
    virtual bool try_coalesce(const ITimelineCommand& newer) {
        (void)newer;
        return false;
    }
};

// Snapshot-backed reversible command guaranteeing exact bit-for-bit restoration across multi-track edits.
class SnapshotTimelineCommand final : public ITimelineCommand {
public:
    using ActionFn = std::function<bool(Timeline&)>;

    SnapshotTimelineCommand(std::string desc, ActionFn action, uint64_t coalesce_key = 0);

    bool execute(Timeline& timeline) override;
    bool undo(Timeline& timeline) override;
    [[nodiscard]] std::string description() const override { return description_; }
    [[nodiscard]] uint64_t coalesce_id() const noexcept override { return coalesce_key_; }
    bool try_coalesce(const ITimelineCommand& newer) override;

private:
    std::string description_;
    ActionFn action_;
    uint64_t coalesce_key_ = 0;
    bool captured_ = false;
    Timeline before_snapshot_;
    Timeline after_snapshot_;
};

class CompositeCommand final : public ITimelineCommand {
public:
    explicit CompositeCommand(std::string desc);
    void add(std::unique_ptr<ITimelineCommand> cmd);
    bool execute(Timeline& timeline) override;
    bool undo(Timeline& timeline) override;
    [[nodiscard]] std::string description() const override { return description_; }

private:
    std::string description_;
    std::vector<std::unique_ptr<ITimelineCommand>> commands_;
};

struct HistoryEntryInfo {
    std::string description;
    bool is_current = false;
};

class CommandStack {
public:
    explicit CommandStack(std::size_t max_depth = 256) : max_depth_(max_depth) {}

    bool execute(std::unique_ptr<ITimelineCommand> cmd, Timeline& timeline);
    bool undo(Timeline& timeline);
    bool redo(Timeline& timeline);
    void clear();

    [[nodiscard]] bool can_undo() const noexcept { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_stack_.empty(); }
    [[nodiscard]] std::size_t undo_count() const noexcept { return undo_stack_.size(); }
    [[nodiscard]] std::size_t redo_count() const noexcept { return redo_stack_.size(); }
    [[nodiscard]] std::vector<HistoryEntryInfo> history_entries() const;

    void set_coalescing_enabled(bool enabled) noexcept { coalescing_enabled_ = enabled; }
    [[nodiscard]] bool coalescing_enabled() const noexcept { return coalescing_enabled_; }

private:
    std::size_t max_depth_ = 256;
    bool coalescing_enabled_ = true;
    std::vector<std::unique_ptr<ITimelineCommand>> undo_stack_;
    std::vector<std::unique_ptr<ITimelineCommand>> redo_stack_;
};

} // namespace owncut::timeline
