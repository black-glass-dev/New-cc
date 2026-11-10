#include "editor/session_coordinator.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace editor {
SessionCoordinator::SessionCoordinator(std::string id, TimeBase base)
    : project_(std::move(id), "Untitled", base) {
    project_.timeline.add_track({"v1", TrackKind::video, false, false, {}});
    project_.timeline.add_track({"a1", TrackKind::audio, false, false, {}});
}
void SessionCoordinator::require_revision_capacity() const {
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("session revision exhausted");
}
SessionSnapshot SessionCoordinator::snapshot() const {
    std::lock_guard lock(mutex_);
    return {project_, revision_};
}
SessionStatus SessionCoordinator::status() const {
    std::lock_guard lock(mutex_);
    std::size_t clips = 0;
    for (const auto& track : project_.timeline.tracks()) clips += track.clips.size();
    return {project_.name, project_.timeline.tracks().size(), clips, revision_,
            revision_ != saved_revision_, history_.can_undo(), history_.can_redo()};
}
std::vector<MediaAssetSummary> SessionCoordinator::media_assets() const {
    std::lock_guard lock(mutex_);
    std::vector<MediaAssetSummary> assets;
    std::unordered_map<std::string,std::size_t> indices;
    for (const auto& track : project_.timeline.tracks()) {
        for (const auto& clip : track.clips) {
            const auto [found,inserted] = indices.emplace(clip.media_id,assets.size());
            if (inserted) assets.push_back({clip.media_id,{},0});
            auto& asset = assets[found->second];
            asset.clip_ids.push_back(clip.id);
            asset.required_source_end_ms = std::max(asset.required_source_end_ms,
                                                    clip.source_out.rescaled({1,1000}).ticks);
        }
    }
    return assets;
}
ClipPage SessionCoordinator::clip_page(std::size_t offset, std::size_t limit,
                                      std::optional<std::uint64_t> expected_revision) const {
    std::lock_guard lock(mutex_);
    if (limit == 0 || limit > max_page_size) throw std::invalid_argument("invalid snapshot page size");
    if (expected_revision && *expected_revision != revision_)
        throw std::runtime_error("snapshot revision changed; restart read");
    std::size_t total = 0;
    for (const auto& track : project_.timeline.tracks()) total += track.clips.size();
    if (offset > total) throw std::invalid_argument("snapshot offset out of range");
    ClipPage result{{}, total, offset, revision_};
    result.clips.reserve(std::min(limit, total - offset));
    // Skip whole tracks and address their vector directly. Walking every preceding
    // clip on every page made a complete paged read quadratic in clip count.
    auto skip = offset;
    for (const auto& track : project_.timeline.tracks()) {
        if (skip >= track.clips.size()) { skip -= track.clips.size(); continue; }
        const auto count = std::min(limit - result.clips.size(), track.clips.size() - skip);
        for (std::size_t i = 0; i < count; ++i)
            result.clips.push_back({track.id, track.clips[skip + i]});
        skip = 0;
        if (result.clips.size() == limit) break;
    }
    result.next_offset += result.clips.size();
    return result;
}
std::optional<PlayheadPosition> SessionCoordinator::resolve_playhead(const std::string& track, Timestamp at) const {
    std::lock_guard lock(mutex_);
    return project_.timeline.resolve_playhead(track, at);
}
void SessionCoordinator::execute_locked(std::unique_ptr<EditCommand> command) {
    require_revision_capacity();
    auto before = project_.timeline;
    try { history_.execute(project_.timeline, std::move(command)); }
    catch (...) { project_.timeline = std::move(before); throw; }
    ++revision_;
}
void SessionCoordinator::execute(std::unique_ptr<EditCommand> command) {
    std::lock_guard lock(mutex_);
    execute_locked(std::move(command));
}
void SessionCoordinator::ensure_track(const std::string& track_id, TrackKind kind) {
    std::lock_guard lock(mutex_);
    const auto& tracks = project_.timeline.tracks();
    const auto existing = std::find_if(tracks.begin(), tracks.end(),
        [&](const Track& t) { return t.id == track_id; });
    if (existing != tracks.end()) return;
    require_revision_capacity();
    project_.timeline.add_track({track_id, kind, false, false, {}});
    ++revision_;
}
void SessionCoordinator::set_track_state(const std::string& track_id, bool muted, bool locked) {
    std::lock_guard lock(mutex_);
    require_revision_capacity();
    project_.timeline.set_track_state(track_id, muted, locked);
    ++revision_;
}
std::string SessionCoordinator::add_media(const std::string& media, std::int64_t duration_ms) {
    std::lock_guard lock(mutex_);
    if (duration_ms <= 0) throw std::invalid_argument("media duration must be positive");
    const auto& tracks = project_.timeline.tracks();
    const auto track = std::find_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.kind == TrackKind::video; });
    if (track == tracks.end()) throw std::logic_error("project has no video track");
    const auto base = project_.timeline.frame_time_base();
    Timestamp append_at{0, base};
    for (const auto& clip : track->clips) {
        const auto end = clip.timeline_start + (clip.source_out - clip.source_in).rescaled(base);
        if (append_at < end) append_at = end;
    }
    std::string id;
    for (;;) {
        if (next_clip_id_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("clip identifier exhausted");
        id = "clip-" + std::to_string(next_clip_id_++);
        try { (void)project_.timeline.locate_clip(id); }
        catch (const std::invalid_argument&) { break; }
    }
    execute_locked(std::make_unique<AddClipCommand>(track->id,
        Clip{id, media, append_at, {0, {1, 1000}}, {duration_ms, {1, 1000}}}));
    return id;
}
std::string SessionCoordinator::add_media_to_track(const std::string& track_id, const std::string& media,
                                                   std::int64_t duration_ms, std::optional<Timestamp> start_at) {
    std::lock_guard lock(mutex_);
    if (duration_ms <= 0) throw std::invalid_argument("media duration must be positive");
    const auto& tracks = project_.timeline.tracks();
    const auto track = std::find_if(tracks.begin(), tracks.end(),
        [&](const Track& t) { return t.id == track_id; });
    if (track == tracks.end()) throw std::invalid_argument("unknown track id");
    const auto base = project_.timeline.frame_time_base();
    Timestamp target_start{0, base};
    if (start_at.has_value()) {
        target_start = start_at->rescaled(base);
    } else {
        for (const auto& clip : track->clips) {
            const auto end = clip.timeline_start + (clip.source_out - clip.source_in).rescaled(base);
            if (target_start < end) target_start = end;
        }
    }
    std::string id;
    for (;;) {
        if (next_clip_id_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("clip identifier exhausted");
        id = "clip-" + std::to_string(next_clip_id_++);
        try { (void)project_.timeline.locate_clip(id); }
        catch (const std::invalid_argument&) { break; }
    }
    execute_locked(std::make_unique<AddClipCommand>(track->id,
        Clip{id, media, target_start, {0, {1, 1000}}, {duration_ms, {1, 1000}}}));
    return id;
}
bool SessionCoordinator::undo() {
    std::lock_guard lock(mutex_);
    if (!history_.can_undo()) return false;
    require_revision_capacity();
    auto before = project_.timeline;
    try { if (!history_.undo(project_.timeline)) return false; }
    catch (...) { project_.timeline = std::move(before); throw; }
    ++revision_; return true;
}
bool SessionCoordinator::redo() {
    std::lock_guard lock(mutex_);
    if (!history_.can_redo()) return false;
    require_revision_capacity();
    auto before = project_.timeline;
    try { if (!history_.redo(project_.timeline)) return false; }
    catch (...) { project_.timeline = std::move(before); throw; }
    ++revision_; return true;
}
void SessionCoordinator::save(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    ProjectStore::save_atomic(path, project_);
    saved_revision_ = revision_; // only after native save/fsync succeeds
}
void SessionCoordinator::checkpoint(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    CheckpointJournal::commit(path,project_,revision_);
    saved_revision_ = revision_;
}
bool SessionCoordinator::repair_checkpoint(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    return CheckpointJournal::archive_corrupt(path).has_value();
}
void SessionCoordinator::load(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    require_revision_capacity();
    auto loaded = ProjectStore::load(path);
    project_ = std::move(loaded);
    history_.clear();
    saved_revision_ = ++revision_;
}
void SessionCoordinator::save_copy(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    ProjectStore::save_atomic(path, project_); // backup/export does not mark primary clean
}
void SessionCoordinator::restore_draft(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    require_revision_capacity();
    auto draft = ProjectStore::load(path);
    project_ = std::move(draft);
    history_.clear();
    ++revision_;
}
void SessionCoordinator::autosave(const std::filesystem::path& primary) {
    std::lock_guard lock(mutex_);
    try { (void)CheckpointJournal::inspect(primary); }
    catch(const CorruptCheckpointReceipt&) { /* permit saving a known-format draft for repair */ }
    ProjectStore::save_autosave(primary, project_);
}
bool SessionCoordinator::recover(const std::filesystem::path& primary) {
    std::lock_guard lock(mutex_);
    require_revision_capacity();
    auto recovered = ProjectStore::load_autosave(primary);
    if (!recovered) return false;
    project_ = std::move(*recovered);
    history_.clear();
    ++revision_; // recovery is a dirty draft; never silently promotes primary
    return true;
}
void SessionCoordinator::clear_recovery(const std::filesystem::path& primary) {
    std::lock_guard lock(mutex_);
    ProjectStore::clear_autosave(primary);
}
} // namespace editor
