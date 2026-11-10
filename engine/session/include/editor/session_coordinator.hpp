#pragma once

#include "editor/command_history.hpp"
#include "editor/project.hpp"
#include <cstdint>
#include <mutex>

namespace editor {
struct SessionSnapshot final {
    Project project; // owned value: callers cannot mutate the live project
    std::uint64_t revision;
};
struct ClipPage final {
    std::vector<ClipLocation> clips;
    std::size_t total, next_offset;
    std::uint64_t revision;
};
struct MediaAssetSummary final {
    std::string media_id;
    std::vector<std::string> clip_ids;
    std::int64_t required_source_end_ms;
};
struct SessionStatus final {
    std::string name;
    std::size_t tracks, clips;
    std::uint64_t revision;
    bool dirty, can_undo, can_redo;
};

// One native session owner. All entry points serialize, including blocking I/O.
// Caller must keep this object alive until outstanding calls finish. This is not
// a cross-session filesystem lease or an Android lifecycle/job executor.
class SessionCoordinator final {
public:
    explicit SessionCoordinator(std::string id, TimeBase base = {1, 30});
    [[nodiscard]] SessionSnapshot snapshot() const;
    [[nodiscard]] SessionStatus status() const;
    [[nodiscard]] std::vector<MediaAssetSummary> media_assets() const;
    static constexpr std::size_t max_page_size = 256;
    [[nodiscard]] ClipPage clip_page(std::size_t offset, std::size_t limit,
        std::optional<std::uint64_t> expected_revision = std::nullopt) const;
    [[nodiscard]] std::optional<PlayheadPosition> resolve_playhead(const std::string& track, Timestamp at) const;
    void ensure_track(const std::string& track_id, TrackKind kind);
    void set_track_state(const std::string& track_id, bool muted, bool locked);
    [[nodiscard]] std::string add_media(const std::string& media, std::int64_t duration_ms);
    [[nodiscard]] std::string add_media_to_track(const std::string& track_id, const std::string& media,
        std::int64_t duration_ms, std::optional<Timestamp> start_at = std::nullopt);
    void execute(std::unique_ptr<EditCommand> command);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void save(const std::filesystem::path& path);
    void checkpoint(const std::filesystem::path& path);
    [[nodiscard]] bool repair_checkpoint(const std::filesystem::path& path);
    void load(const std::filesystem::path& path);
    void save_copy(const std::filesystem::path& path);
    void restore_draft(const std::filesystem::path& path);
    void autosave(const std::filesystem::path& primary);
    [[nodiscard]] bool recover(const std::filesystem::path& primary);
    void clear_recovery(const std::filesystem::path& primary);
private:
    void require_revision_capacity() const;
    void execute_locked(std::unique_ptr<EditCommand> command);
    mutable std::mutex mutex_;
    Project project_;
    CommandHistory history_;
    std::uint64_t revision_{0}, saved_revision_{0}, next_clip_id_{1};
};
} // namespace editor
