#pragma once

#include "editor/timeline.hpp"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <stdexcept>

namespace editor {

struct Project final {
    std::string id;
    std::string name;
    Timeline timeline;

    Project(std::string project_id, std::string project_name, TimeBase frame_time_base);
};

// Versioned, little-endian binary project format. v0 is read-only and migrates
// missing track mute/lock fields to false; v0/v1 clips migrate to neutral grades; writes always emit the current schema.
class UnsupportedProjectVersion final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
class ProjectCodec final {
public:
    static constexpr unsigned current_version = 2;
    static void write(const Project& project, std::ostream& output);
    [[nodiscard]] static Project read(std::istream& input);
};

// Shared durable same-directory write primitive; callbacks must not retain the stream.
class AtomicFileStore final {
public:
    static void write(const std::filesystem::path& path,
                      const std::function<void(std::ostream&)>& writer);
};

class CorruptCheckpointReceipt final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
class UnsupportedCheckpointVersion final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
enum class CheckpointPhase : unsigned char { prepared = 0, committed = 1, clean = 2 };
enum class CheckpointStep { recovery_written, prepared, primary_written, committed, recovery_cleared, clean };
struct CheckpointReceipt final {
    std::uint64_t generation, session_revision;
    CheckpointPhase phase;
    std::string project_id;
};
// Caller owns the writer lease. Receipt is diagnostic state, never permission to
// delete/promote recovery automatically. Main document and autosave use the current project schema.
class CheckpointJournal final {
public:
    static std::filesystem::path path_for(const std::filesystem::path& primary);
    static std::optional<CheckpointReceipt> inspect(const std::filesystem::path& primary);
    // Explicit repair only: returns an archive path; valid/missing receipts need no repair.
    static std::optional<std::filesystem::path> archive_corrupt(const std::filesystem::path& primary);
    static void commit(const std::filesystem::path& primary, const Project& project,
                       std::uint64_t session_revision,
                       const std::function<void(CheckpointStep)>& after_step = {});
};

// Same-directory temp write + rename. Platform storage adapters should use this
// codec with their own atomic document-provider strategy on Android SAF URIs.
class ProjectStore final {
public:
    static void save_atomic(const std::filesystem::path& path, const Project& project);
    [[nodiscard]] static Project load(const std::filesystem::path& path);
    static void save_autosave(const std::filesystem::path& project_path, const Project& project);
    [[nodiscard]] static std::optional<Project> load_autosave(const std::filesystem::path& project_path);
    static void clear_autosave(const std::filesystem::path& project_path);
    [[nodiscard]] static std::filesystem::path autosave_path_for(const std::filesystem::path& project_path);
};

} // namespace editor
