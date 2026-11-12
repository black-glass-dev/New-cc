#include "editor/project.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <array>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(__linux__) || defined(__ANDROID__)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace editor {
namespace {
constexpr char kMagic[] = {'O', 'W', 'N', 'C', 'U', 'T', 'P', 'J'};
constexpr std::uint32_t kMaxStringBytes = 4096;
constexpr std::uint32_t kMaxTracks = 4096;
constexpr std::uint32_t kMaxClips = 8192;

void write_u8(std::ostream& out, std::uint8_t value) {
    out.put(static_cast<char>(value));
    if (!out) throw std::runtime_error("failed writing project data");
}
void write_u32(std::ostream& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) write_u8(out, static_cast<std::uint8_t>(value >> shift));
}
void write_u64(std::ostream& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) write_u8(out, static_cast<std::uint8_t>(value >> shift));
}
void write_i64(std::ostream& out, std::int64_t value) { write_u64(out, std::bit_cast<std::uint64_t>(value)); }
bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first == 0) return false;
        if (first <= 0x7f) { ++index; continue; }
        std::uint32_t codepoint = 0;
        std::size_t continuation_count = 0;
        if (first >= 0xc2 && first <= 0xdf) { codepoint = first & 0x1f; continuation_count = 1; }
        else if (first >= 0xe0 && first <= 0xef) { codepoint = first & 0x0f; continuation_count = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { codepoint = first & 0x07; continuation_count = 3; }
        else return false;
        if (index + continuation_count >= text.size()) return false;
        for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const auto byte = static_cast<unsigned char>(text[index + offset]);
            if ((byte & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((continuation_count == 2 && codepoint < 0x800) ||
            (continuation_count == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        index += continuation_count + 1;
    }
    return true;
}

void write_string(std::ostream& out, const std::string& value) {
    if (value.size() > kMaxStringBytes) throw std::invalid_argument("project string exceeds size limit");
    if (!is_valid_utf8(value)) throw std::invalid_argument("project strings must be valid UTF-8 without NUL bytes");
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!out) throw std::runtime_error("failed writing project string");
}

std::uint8_t read_u8(std::istream& in) {
    const int value = in.get();
    if (value == std::char_traits<char>::eof()) throw std::runtime_error("truncated project data");
    return static_cast<std::uint8_t>(value);
}
std::uint32_t read_u32(std::istream& in) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(read_u8(in)) << shift;
    return value;
}
std::uint64_t read_u64(std::istream& in) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(read_u8(in)) << shift;
    return value;
}
std::int64_t read_i64(std::istream& in) { return std::bit_cast<std::int64_t>(read_u64(in)); }
std::string read_string(std::istream& in) {
    const auto length = read_u32(in);
    if (length > kMaxStringBytes) throw std::runtime_error("project string exceeds size limit");
    std::string value(length, '\0');
    in.read(value.data(), static_cast<std::streamsize>(length));
    if (in.gcount() != static_cast<std::streamsize>(length)) throw std::runtime_error("truncated project string");
    if (!is_valid_utf8(value)) throw std::runtime_error("project string is not valid UTF-8");
    return value;
}
void write_timebase(std::ostream& out, TimeBase value) {
    write_i64(out, value.numerator);
    write_i64(out, value.denominator);
}
TimeBase read_timebase(std::istream& in) { return {read_i64(in), read_i64(in)}; }
void write_timestamp(std::ostream& out, Timestamp value) {
    write_i64(out, value.ticks);
    write_timebase(out, value.time_base);
}
Timestamp read_timestamp(std::istream& in) {
    const auto ticks = read_i64(in);
    return {ticks, read_timebase(in)};
}
void require_bool(std::uint8_t value) {
    if (value > 1) throw std::runtime_error("invalid boolean field in project");
}
void require_no_trailing_data(std::istream& in) {
    if (in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("unexpected trailing project data");
    if (in.bad()) throw std::runtime_error("failed reading project data");
}

void reject_future_document(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        if(!std::filesystem::is_regular_file(path)) throw std::filesystem::filesystem_error("project target is not a regular file",path,std::make_error_code(std::errc::invalid_argument));
        std::ifstream input(path,std::ios::binary);
        if(!input) throw std::runtime_error("could not inspect existing project document");
        char magic[8]{}; input.read(magic,8);
        if(input.bad()) throw std::runtime_error("I/O error inspecting project document");
        if(input && std::string_view(magic,8) == std::string_view(kMagic,8)) {
            // Do not destroy a future client's only draft. Truncated/ordinary corrupt
            // supported-version recovery retains existing replacement behavior.
            char version_bytes[4]{}; input.read(version_bytes,4);
            if(input.bad()) throw std::runtime_error("I/O error inspecting project document version");
            if(input) {
                std::uint32_t version=0;
                for(unsigned i=0;i<4;++i) version |= static_cast<std::uint32_t>(static_cast<unsigned char>(version_bytes[i])) << (8*i);
                if(version > ProjectCodec::current_version) throw UnsupportedProjectVersion("existing document belongs to a newer editor; original was kept");
            }
        }
    }
}

std::filesystem::path make_temp_path(const std::filesystem::path& target) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temp = target;
    temp += ".tmp." + std::to_string(now) + "." + std::to_string(sequence.fetch_add(1));
    return temp;
}

#if defined(__linux__) || defined(__ANDROID__)
void sync_descriptor(int descriptor, const char* operation) {
    while (::fsync(descriptor) != 0) {
        if (errno == EINTR) continue;
        throw std::system_error(errno, std::generic_category(), operation);
    }
}

void sync_file(const std::filesystem::path& path) {
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "could not open project file for sync");
    try {
        sync_descriptor(descriptor, "could not sync project file");
    } catch (...) {
        (void)::close(descriptor);
        throw;
    }
    if (::close(descriptor) != 0)
        throw std::system_error(errno, std::generic_category(), "could not close synced project file");
}

void sync_parent_directory(const std::filesystem::path& path) {
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
    const int descriptor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "could not open project directory for sync");
    try {
        sync_descriptor(descriptor, "could not sync project directory");
    } catch (...) {
        (void)::close(descriptor);
        throw;
    }
    if (::close(descriptor) != 0)
        throw std::system_error(errno, std::generic_category(), "could not close synced project directory");
}
#endif
} // namespace

Project::Project(std::string project_id, std::string project_name, TimeBase frame_time_base)
    : id(std::move(project_id)), name(std::move(project_name)), timeline(frame_time_base) {}

void ProjectCodec::write(const Project& project, std::ostream& output) {
    if (project.id.empty() || project.name.empty()) throw std::invalid_argument("project id and name are required");
    const auto& tracks = project.timeline.tracks();
    if (tracks.size() > kMaxTracks) throw std::invalid_argument("project track limit exceeded");

    output.write(kMagic, sizeof(kMagic));
    if (!output) throw std::runtime_error("failed writing project header");
    write_u32(output, current_version);
    write_string(output, project.id);
    write_string(output, project.name);
    write_timebase(output, project.timeline.frame_time_base());
    write_u32(output, static_cast<std::uint32_t>(tracks.size()));

    std::uint64_t total_clips = 0;
    for (const auto& track : tracks) {
        total_clips += track.clips.size();
        if (total_clips > kMaxClips) throw std::invalid_argument("project clip limit exceeded");
        write_string(output, track.id);
        write_u8(output, static_cast<std::uint8_t>(track.kind));
        write_u8(output, track.muted ? 1 : 0);
        write_u8(output, track.locked ? 1 : 0);
        write_u32(output, static_cast<std::uint32_t>(track.clips.size()));
        for (const auto& clip : track.clips) {
            write_string(output, clip.id);
            write_string(output, clip.media_id);
            write_timestamp(output, clip.timeline_start);
            write_timestamp(output, clip.source_in);
            write_timestamp(output, clip.source_out);
            color::PresetCodec::write(clip.grade,output);
        }
    }
    if (!output) throw std::runtime_error("failed writing project data");
}

Project ProjectCodec::read(std::istream& input) {
    char magic[sizeof(kMagic)]{};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
        !std::equal(std::begin(magic), std::end(magic), std::begin(kMagic)))
        throw std::runtime_error("invalid project header");

    const auto version = read_u32(input);
    if (version > current_version) throw UnsupportedProjectVersion("project schema version is newer than this editor");
    // v0/v1 are supported migration inputs; current v2 adds per-clip grading.

    auto id = read_string(input);
    auto name = read_string(input);
    if (id.empty() || name.empty()) throw std::runtime_error("project id and name are required");
    const auto frame_time_base = read_timebase(input);
    const auto track_count = read_u32(input);
    if (track_count > kMaxTracks) throw std::runtime_error("project track limit exceeded");
    Project project{std::move(id), std::move(name), frame_time_base};

    std::uint64_t total_clips = 0;
    for (std::uint32_t track_index = 0; track_index < track_count; ++track_index) {
        Track track{read_string(input), TrackKind::video, false, false, {}};
        const auto kind = read_u8(input);
        if (kind > static_cast<std::uint8_t>(TrackKind::audio)) throw std::runtime_error("invalid track kind");
        track.kind = static_cast<TrackKind>(kind);
        std::uint8_t muted = 0;
        std::uint8_t locked = 0;
        if (version >= 1) {
            muted = read_u8(input);
            locked = read_u8(input);
            require_bool(muted);
            require_bool(locked);
        }
        const auto clip_count = read_u32(input);
        total_clips += clip_count;
        if (total_clips > kMaxClips) throw std::runtime_error("project clip limit exceeded");
        const auto track_id = track.id;
        project.timeline.add_track(std::move(track));

        for (std::uint32_t clip_index = 0; clip_index < clip_count; ++clip_index) {
            Clip clip{read_string(input), read_string(input), read_timestamp(input), read_timestamp(input), read_timestamp(input)};
            if (version >= 2) {
                std::array<char,color::PresetCodec::encoded_size> data{};
                input.read(data.data(),static_cast<std::streamsize>(data.size()));
                if (!input) throw std::runtime_error("truncated project clip grade");
                std::istringstream preset(std::string(data.data(),data.size()),std::ios::binary);
                clip.grade = color::PresetCodec::read(preset);
            }
            project.timeline.add_clip(track_id, std::move(clip));
        }
        project.timeline.set_track_state(track_id, muted != 0, locked != 0);
    }
    require_no_trailing_data(input);
    return project;
}

void AtomicFileStore::write(const std::filesystem::path& path,
                            const std::function<void(std::ostream&)>& writer) {
    if (path.empty() || path.filename().empty()) throw std::invalid_argument("project path must name a file");
    const auto temp_path = make_temp_path(path);
    try {
        {
            std::ofstream output(temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!output) throw std::runtime_error("could not create temporary project file");
            writer(output);
            output.flush();
            if (!output) throw std::runtime_error("could not flush temporary project file");
            output.close();
            if (!output) throw std::runtime_error("could not close temporary project file");
        }
#if defined(__linux__) || defined(__ANDROID__)
        sync_file(temp_path);
#endif
        std::error_code error;
        std::filesystem::rename(temp_path, path, error);
        if (error) throw std::filesystem::filesystem_error("could not atomically replace project", temp_path, path, error);
#if defined(__linux__) || defined(__ANDROID__)
        sync_parent_directory(path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temp_path, ignored);
        throw;
    }
}

void ProjectStore::save_atomic(const std::filesystem::path& path, const Project& project) {
    reject_future_document(path);
    AtomicFileStore::write(path, [&](std::ostream& output) { ProjectCodec::write(project, output); });
}

Project ProjectStore::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open project file");
    return ProjectCodec::read(input);
}

std::filesystem::path ProjectStore::autosave_path_for(const std::filesystem::path& project_path) {
    if (project_path.empty() || project_path.filename().empty())
        throw std::invalid_argument("project path must name a file");
    auto autosave = project_path;
    autosave += ".autosave";
    return autosave;
}

void ProjectStore::save_autosave(const std::filesystem::path& project_path, const Project& project) {
    reject_future_document(project_path);
    const auto path=autosave_path_for(project_path);
    reject_future_document(path);
    save_atomic(path, project);
}

std::optional<Project> ProjectStore::load_autosave(const std::filesystem::path& project_path) {
    const auto autosave = autosave_path_for(project_path);
    std::error_code error;
    const bool exists = std::filesystem::exists(autosave, error);
    if (error) throw std::filesystem::filesystem_error("could not inspect autosave", autosave, error);
    if (!exists) return std::nullopt;
    return load(autosave);
}

void ProjectStore::clear_autosave(const std::filesystem::path& project_path) {
    reject_future_document(project_path);
    (void)CheckpointJournal::inspect(project_path); // never discard an unknown future transaction's draft
    const auto autosave = autosave_path_for(project_path);
    reject_future_document(autosave);
    std::error_code error;
    const bool removed = std::filesystem::remove(autosave, error);
    if (error) throw std::filesystem::filesystem_error("could not remove autosave", autosave, error);
#if defined(__linux__) || defined(__ANDROID__)
    if (removed) sync_parent_directory(autosave);
#else
    (void)removed;
#endif
}

std::filesystem::path CheckpointJournal::path_for(const std::filesystem::path& primary) {
    if (primary.empty() || primary.filename().empty()) throw std::invalid_argument("checkpoint path must name a file");
    auto result = primary; result += ".checkpoint"; return result;
}
std::optional<CheckpointReceipt> CheckpointJournal::inspect(const std::filesystem::path& primary) {
    const auto path = path_for(primary);
    if (!std::filesystem::exists(path)) return std::nullopt;
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("checkpoint record is not a regular file");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not open checkpoint receipt");
    try {
        char magic[8]{}; in.read(magic,8);
        if (!in || std::string_view(magic,8) != "OWNCUTCK") throw std::runtime_error("invalid checkpoint receipt");
        if (read_u32(in) != 1) throw UnsupportedCheckpointVersion("unsupported checkpoint receipt version");
        if (std::filesystem::file_size(path) > 4160) throw std::runtime_error("checkpoint receipt size limit exceeded");
        const auto generation = read_u64(in), revision = read_u64(in);
        const auto phase = read_u8(in);
        if (generation == 0 || phase > 2) throw std::runtime_error("invalid checkpoint receipt state");
        auto id = read_string(in);
        if (id.empty()) throw std::runtime_error("checkpoint project id is required");
        require_no_trailing_data(in);
        return CheckpointReceipt{generation,revision,static_cast<CheckpointPhase>(phase),std::move(id)};
    } catch(const UnsupportedCheckpointVersion&) { throw; }
      catch(const std::filesystem::filesystem_error&) { throw; }
      catch(const std::bad_alloc&) { throw; }
      catch(const std::exception& error) {
        if(in.bad()) throw std::runtime_error("I/O error reading checkpoint receipt");
        throw CorruptCheckpointReceipt(error.what());
    }
}
std::optional<std::filesystem::path> CheckpointJournal::archive_corrupt(const std::filesystem::path& primary) {
    try { (void)inspect(primary); return std::nullopt; }
    catch (const CorruptCheckpointReceipt&) {
        // At least the primary must be readable before changing checkpoint metadata.
        // Recovery bytes are never modified by repair.
        (void)ProjectStore::load(primary);
        const auto source=path_for(primary);
        auto archive=source;
        static std::atomic<std::uint64_t> sequence{0};
        archive += ".preserved-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+
                   "-"+std::to_string(sequence.fetch_add(1));
        if (std::filesystem::exists(archive)) throw std::runtime_error("checkpoint archive already exists");
        std::filesystem::rename(source,archive);
#if defined(__linux__) || defined(__ANDROID__)
        sync_parent_directory(source);
#endif
        return archive;
    }
}
void CheckpointJournal::commit(const std::filesystem::path& primary, const Project& project,
                               std::uint64_t session_revision,
                               const std::function<void(CheckpointStep)>& after_step) {
    reject_future_document(primary);
    auto notify = [&](CheckpointStep step) { if (after_step) after_step(step); };
    // Unknown future metadata must be refused before touching its recovery payload.
    std::optional<CheckpointReceipt> previous;
    try { previous=inspect(primary); }
    catch(const CorruptCheckpointReceipt&) {
        ProjectStore::save_autosave(primary,project); // known-format draft, primary/receipt preserved
        throw;
    }
    ProjectStore::save_autosave(primary,project);
    notify(CheckpointStep::recovery_written);
    if (previous && previous->generation == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("checkpoint generation exhausted");
    const auto generation = previous ? previous->generation+1 : 1;
    auto receipt = [&](CheckpointPhase phase) {
        AtomicFileStore::write(path_for(primary),[&](std::ostream& out) {
            out.write("OWNCUTCK",8); write_u32(out,1); write_u64(out,generation);
            write_u64(out,session_revision); write_u8(out,static_cast<std::uint8_t>(phase));
            write_string(out,project.id);
        });
    };
    receipt(CheckpointPhase::prepared); notify(CheckpointStep::prepared);
    ProjectStore::save_atomic(primary,project); notify(CheckpointStep::primary_written);
    // Parse from disk before recording commit or removing the only recovery copy.
    (void)ProjectStore::load(primary);
    receipt(CheckpointPhase::committed); notify(CheckpointStep::committed);
    ProjectStore::clear_autosave(primary); notify(CheckpointStep::recovery_cleared);
    receipt(CheckpointPhase::clean); notify(CheckpointStep::clean);
}
} // namespace editor
