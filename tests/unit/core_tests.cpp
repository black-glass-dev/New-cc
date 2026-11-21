#include "editor/command_history.hpp"
#include "editor/project.hpp"
#include "editor/time.hpp"
#include "editor/graph.hpp"
#include "editor/timeline.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <source_location>
#include <stdexcept>
#include <string>

using namespace editor;

namespace {
void check(bool condition, const std::source_location location = std::source_location::current()) {
    if (!condition) throw std::runtime_error("unit test assertion failed at line " + std::to_string(location.line()));
}

template <typename Function>
void expect_failure(Function&& function) {
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed);
}

void append_u8(std::string& out, std::uint8_t value) { out.push_back(static_cast<char>(value)); }
void append_u32(std::string& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) append_u8(out, static_cast<std::uint8_t>(value >> shift));
}
void append_u64(std::string& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) append_u8(out, static_cast<std::uint8_t>(value >> shift));
}
void append_string(std::string& out, const std::string& value) {
    append_u32(out, static_cast<std::uint32_t>(value.size()));
    out += value;
}
void append_timebase(std::string& out, std::int64_t numerator, std::int64_t denominator) {
    append_u64(out, static_cast<std::uint64_t>(numerator));
    append_u64(out, static_cast<std::uint64_t>(denominator));
}
void append_timestamp(std::string& out, std::int64_t ticks, std::int64_t numerator, std::int64_t denominator) {
    append_u64(out, static_cast<std::uint64_t>(ticks));
    append_timebase(out, numerator, denominator);
}

std::string legacy_v0_fixture() {
    std::string out{"OWNCUTPJ", 8};
    append_u32(out, 0);
    append_string(out, "old-project");
    append_string(out, "Legacy import");
    append_timebase(out, 1, 30);
    append_u32(out, 1); // tracks
    append_string(out, "v1");
    append_u8(out, 0); // video
    append_u32(out, 1); // clips
    append_string(out, "legacy-clip");
    append_string(out, "content://media/item/1");
    append_timestamp(out, 30, 1, 30); // timeline start
    append_timestamp(out, 0, 1, 1000); // source in
    append_timestamp(out, 2000, 1, 1000); // source out
    return out;
}

std::filesystem::path make_temp_directory() {
    const auto base = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto path = base / ("owncut-project-test-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(path, error)) return path;
    }
    throw std::runtime_error("could not create isolated temporary test directory");
}
} // namespace

int main() {
    {
        ProcessingGraph graph{{{1, "source", {}, {PortKind::image}},
                               {2, "transform", {PortKind::image}, {PortKind::image}},
                               {3, "output", {PortKind::image}, {}}}, {{1, 0, 2, 0}, {2, 0, 3, 0}}, 3};
        const auto expected = std::vector<std::uint64_t>{1, 2, 3};
        check(validate_graph(graph) == expected);
        check(validate_graph(graph) == expected); // deterministic and read-only
        auto bad = graph; bad.nodes[1].id = 1;
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.edges[0].source = 99;
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.edges[0].output = 42;
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.nodes[1].inputs[0] = PortKind::audio;
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.edges.push_back(graph.edges[0]);
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.edges.erase(bad.edges.begin());
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.edges[0].source = 2; // self-cycle while required input is connected
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.output_node = 0;
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.nodes.push_back({4, "unused", {}, {PortKind::image}});
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.nodes[0].outputs[0] = static_cast<PortKind>(99);
        expect_failure([&] { (void)validate_graph(bad); });
        bad = graph; bad.nodes.resize(4097);
        expect_failure([&] { (void)validate_graph(bad); });
        ProcessingGraph fanout{{{1, "source", {}, {PortKind::image}},
                               {2, "merge", {PortKind::image, PortKind::image}, {PortKind::image}},
                               {3, "output", {PortKind::image}, {}}},
                              {{1, 0, 2, 1}, {1, 0, 2, 0}, {2, 0, 3, 0}}, 3};
        check(validate_graph(fanout) == expected);
    }

    const TimeBase ntsc_frame{1001, 30000};
    const TimeBase milliseconds{1, 1000};
    {
        Timeline imported{TimeBase{1, 30}};
        imported.add_track({"v1", TrackKind::video, false, false, {}});
        CommandHistory history;
        const Clip clip{"import-1", "original", {0, TimeBase{1, 30}}, {0, milliseconds}, {6000, milliseconds}};
        history.execute(imported, std::make_unique<AddClipCommand>("v1", clip));
        check(imported.locate_clip("import-1").clip.source_out == clip.source_out);
        history.execute(imported, std::make_unique<SplitClipCommand>("import-1", Timestamp{90, TimeBase{1, 30}}, "right"));
        check(history.undo(imported)); // undo split, then undo import
        check(history.undo(imported));
        check(imported.tracks()[0].clips.empty());
        expect_failure([&] {
            history.execute(imported, std::make_unique<AddClipCommand>("missing-track", clip));
        });
        check(history.redo(imported)); // failed import must not erase redo
        check(imported.locate_clip("import-1").clip.media_id == "original");
        check(history.redo(imported));
        check(imported.locate_clip("right").clip.source_in == Timestamp{3000, milliseconds});
    }
    {
        Timeline seeking{TimeBase{1, 30}};
        seeking.add_track({"v1", TrackKind::video, false, false, {}});
        seeking.add_clip("v1", {"a", "source-a", {0, TimeBase{1, 30}}, {1000, milliseconds}, {2000, milliseconds}});
        seeking.add_clip("v1", {"b", "source-b", {30, TimeBase{1, 30}}, {5000, milliseconds}, {6000, milliseconds}});
        check(seeking.resolve_playhead("v1", {0, TimeBase{1, 30}})->source_position == Timestamp{1000, milliseconds});
        const auto boundary = seeking.resolve_playhead("v1", {30, TimeBase{1, 30}});
        check(boundary && boundary->clip_id == "b" && boundary->source_position == Timestamp{5000, milliseconds});
        check(seeking.resolve_playhead("v1", {45, TimeBase{1, 30}})->source_position == Timestamp{5500, milliseconds});
        check(!seeking.resolve_playhead("v1", {60, TimeBase{1, 30}}));
        seeking.move_clip("b", "v1", {90, TimeBase{1, 30}});
        check(!seeking.resolve_playhead("v1", {45, TimeBase{1, 30}}));
        check(seeking.resolve_playhead("v1", {90, TimeBase{1, 30}})->source_position == Timestamp{5000, milliseconds});
        expect_failure([&] { (void)seeking.resolve_playhead("missing", {0, TimeBase{1, 30}}); });
        expect_failure([&] { (void)seeking.resolve_playhead("v1", {-1, TimeBase{1, 30}}); });
        seeking.move_clip("b", "v1", {15, TimeBase{1, 30}});
        expect_failure([&] { (void)seeking.resolve_playhead("v1", {15, TimeBase{1, 30}}); });
        check(seeking.tracks()[0].clips.size() == 2); // Seeking never edits media/timing.
    }
    check((TimeBase{2, 4} == TimeBase{1, 2}));
    check((Timestamp{30, ntsc_frame} == Timestamp{1001, TimeBase{1, 1000}}));
    check((Timestamp{1, TimeBase{1, 2}}.rescaled(milliseconds).ticks == 500));
    check((Timestamp{2, milliseconds} + Timestamp{1, TimeBase{1, 500}}).ticks == 4);
    check((Timestamp{-1, TimeBase{1, 3}}.rescaled(milliseconds).ticks == -333));
    expect_failure([] { (void)TimeBase{0, 1}; });

    Timeline timeline{ntsc_frame};
    timeline.add_track({"v1", TrackKind::video, false, false, {}});
    timeline.add_track({"a1", TrackKind::audio, false, false, {}});
    timeline.add_clip("v1", {"clip-v", "media-1", {0, ntsc_frame}, {0, milliseconds}, {5000, milliseconds}});
    timeline.add_clip("a1", {"clip-a", "media-1", {0, ntsc_frame}, {0, milliseconds}, {5000, milliseconds}});
    check(timeline.tracks().size() == 2);
    check(timeline.tracks()[0].clips[0].timeline_start.time_base == ntsc_frame);
    expect_failure([&] { timeline.add_clip("v1", {"bad", "media-1", {0, ntsc_frame}, {5, milliseconds}, {5, milliseconds}}); });
    expect_failure([&] { timeline.add_clip("missing", {"bad", "media-1", {0, ntsc_frame}, {0, milliseconds}, {5, milliseconds}}); });

    CommandHistory history;
    history.execute(timeline, std::make_unique<MoveClipCommand>("clip-v", "a1", Timestamp{30, ntsc_frame}));
    auto moved = timeline.locate_clip("clip-v");
    check(moved.track_id == "a1" && moved.clip.timeline_start.ticks == 30);
    check(history.can_undo() && !history.can_redo());
    check(history.undo(timeline));
    auto restored = timeline.locate_clip("clip-v");
    check(restored.track_id == "v1" && restored.clip.timeline_start.ticks == 0);
    check(history.can_redo());
    check(history.redo(timeline));
    check(timeline.locate_clip("clip-v").track_id == "a1");
    check(history.undo(timeline));
    history.execute(timeline, std::make_unique<MoveClipCommand>("clip-v", "v1", Timestamp{60, ntsc_frame}));
    check(!history.can_redo()); // New edit invalidates redo history.
    check(timeline.locate_clip("clip-v").clip.timeline_start.ticks == 60);
    history.execute(timeline, std::make_unique<RelinkClipMediaCommand>("clip-v", "media-2"));
    check(timeline.locate_clip("clip-v").clip.media_id == "media-2");
    check(timeline.locate_clip("clip-a").clip.media_id == "media-2"); // Shared source is relinked together.
    check(history.undo(timeline));
    check(timeline.locate_clip("clip-v").clip.media_id == "media-1");
    check(timeline.locate_clip("clip-a").clip.media_id == "media-1");
    check(history.redo(timeline));
    check(timeline.locate_clip("clip-v").clip.media_id == "media-2");
    check(timeline.locate_clip("clip-a").clip.media_id == "media-2");
    check(history.undo(timeline));
    expect_failure([&] { history.execute(timeline, std::make_unique<RelinkClipMediaCommand>("clip-v", "")); });
    timeline.set_track_state("v1", false, true);
    expect_failure([&] { history.execute(timeline, std::make_unique<RelinkClipMediaCommand>("clip-v", "media-locked")); });
    check(timeline.locate_clip("clip-v").clip.media_id == "media-1");
    check(timeline.locate_clip("clip-a").clip.media_id == "media-1"); // Failed group relink is all-or-nothing.
    timeline.set_track_state("v1", false, false);
    expect_failure([&] { history.execute(timeline, std::make_unique<MoveClipCommand>("clip-v", "absent", Timestamp{0, ntsc_frame})); });
    timeline.set_track_state("a1", false, true);
    expect_failure([&] { history.execute(timeline, std::make_unique<MoveClipCommand>("clip-v", "a1", Timestamp{0, ntsc_frame})); });
    timeline.set_track_state("a1", false, false);
    expect_failure([&] { history.execute(timeline, std::make_unique<MoveClipCommand>("clip-v", "v1", Timestamp{-1, ntsc_frame})); });
    check(timeline.locate_clip("clip-v").clip.timeline_start.ticks == 60);

    Timeline edit_ops{ntsc_frame};
    edit_ops.add_track({"v1", TrackKind::video, false, false, {}});
    edit_ops.add_clip("v1", {"source-clip", "media-edit", {30, ntsc_frame}, {0, milliseconds}, {5000, milliseconds}});
    CommandHistory edit_history;
    edit_history.execute(edit_ops, std::make_unique<SplitClipCommand>("source-clip", Timestamp{60, ntsc_frame}, "right-clip"));
    check(edit_ops.tracks()[0].clips.size() == 2);
    check(edit_ops.locate_clip("source-clip").clip.source_out.ticks == 1001);
    check(edit_ops.locate_clip("right-clip").clip.source_in.ticks == 1001);
    check(edit_ops.locate_clip("right-clip").clip.timeline_start.ticks == 60);
    check(edit_history.undo(edit_ops));
    check(edit_ops.tracks()[0].clips.size() == 1);
    check(edit_ops.locate_clip("source-clip").clip.source_out.ticks == 5000);
    check(edit_history.redo(edit_ops));
    check(edit_ops.tracks()[0].clips.size() == 2);
    expect_failure([&] { edit_history.execute(edit_ops, std::make_unique<SplitClipCommand>("source-clip", Timestamp{30, ntsc_frame}, "edge-clip")); });
    check(edit_ops.tracks()[0].clips.size() == 2);

    edit_history.execute(edit_ops, std::make_unique<TrimClipCommand>("source-clip", TrimEdge::left, Timestamp{500, milliseconds}));
    check(edit_ops.locate_clip("source-clip").clip.source_in.ticks == 500);
    check(edit_ops.locate_clip("source-clip").clip.timeline_start.ticks == 45);
    check(edit_history.undo(edit_ops));
    check(edit_ops.locate_clip("source-clip").clip.timeline_start.ticks == 30);
    edit_history.execute(edit_ops, std::make_unique<TrimClipCommand>("source-clip", TrimEdge::right, Timestamp{800, milliseconds}));
    check(edit_ops.locate_clip("source-clip").clip.source_out.ticks == 800);
    check(edit_history.undo(edit_ops));
    check(edit_ops.locate_clip("source-clip").clip.source_out.ticks == 1001);
    expect_failure([&] { edit_history.execute(edit_ops, std::make_unique<TrimClipCommand>("source-clip", TrimEdge::right, Timestamp{-1, milliseconds})); });

    edit_history.execute(edit_ops, std::make_unique<DeleteClipCommand>("right-clip"));
    check(edit_ops.tracks()[0].clips.size() == 1);
    check(edit_history.undo(edit_ops));
    check(edit_ops.tracks()[0].clips.size() == 2);
    check(edit_history.redo(edit_ops));
    check(edit_ops.tracks()[0].clips.size() == 1);
    check(edit_history.undo(edit_ops));

    Timeline ripple{TimeBase{1, 30}};
    ripple.add_track({"video", TrackKind::video, false, false, {}});
    ripple.add_track({"audio", TrackKind::audio, false, false, {}});
    ripple.add_clip("video", {"gap-clip", "m0", {0, TimeBase{1, 30}}, {0, milliseconds}, {2000, milliseconds}});
    ripple.add_clip("video", {"overlap", "m1", {30, TimeBase{1, 30}}, {0, milliseconds}, {1000, milliseconds}});
    ripple.add_clip("video", {"at-end", "m2", {60, TimeBase{1, 30}}, {0, milliseconds}, {1000, milliseconds}});
    ripple.add_clip("video", {"later", "m3", {120, TimeBase{1, 30}}, {0, milliseconds}, {1000, milliseconds}});
    ripple.add_clip("audio", {"other-track", "m4", {120, TimeBase{1, 30}}, {0, milliseconds}, {1000, milliseconds}});
    CommandHistory ripple_history;
    ripple_history.execute(ripple, std::make_unique<RippleDeleteClipCommand>("gap-clip"));
    check(ripple.tracks()[0].clips.size() == 3);
    check(ripple.locate_clip("overlap").clip.timeline_start.ticks == 30);
    check(ripple.locate_clip("at-end").clip.timeline_start.ticks == 0);
    check(ripple.locate_clip("later").clip.timeline_start.ticks == 60);
    check(ripple.locate_clip("other-track").clip.timeline_start.ticks == 120);
    check(ripple_history.undo(ripple));
    check(ripple.tracks()[0].clips.size() == 4);
    check(ripple.locate_clip("at-end").clip.timeline_start.ticks == 60);
    check(ripple.locate_clip("later").clip.timeline_start.ticks == 120);
    check(ripple_history.redo(ripple));
    ripple.set_track_state("video", false, true);
    expect_failure([&] { ripple_history.execute(ripple, std::make_unique<RippleDeleteClipCommand>("gap-clip")); });
    check(ripple.tracks()[0].clips.size() == 3);

    Timeline bounded{TimeBase{1, 30}};
    bounded.add_track({"v", TrackKind::video, false, false, {}});
    bounded.add_clip("v", {"c", "m", {0, TimeBase{1, 30}}, {0, milliseconds}, {1000, milliseconds}});
    CommandHistory bounded_history{2};
    for (std::int64_t position = 10; position <= 30; position += 10)
        bounded_history.execute(bounded, std::make_unique<MoveClipCommand>("c", "v", Timestamp{position, TimeBase{1, 30}}));
    check(bounded_history.undo(bounded) && bounded.locate_clip("c").clip.timeline_start.ticks == 20);
    check(bounded_history.undo(bounded) && bounded.locate_clip("c").clip.timeline_start.ticks == 10);
    check(!bounded_history.undo(bounded)); // Oldest command was pruned at the configured depth.
    check(bounded_history.redo(bounded) && bounded.locate_clip("c").clip.timeline_start.ticks == 20);
    expect_failure([] { (void)CommandHistory{0}; });

    Project project{"project-01", "First edit", ntsc_frame};
    project.timeline.add_track({"video-main", TrackKind::video, false, false, {}});
    project.timeline.add_track({"audio-main", TrackKind::audio, true, true, {}});
    project.timeline.add_clip("video-main", {"clip-01", "content://media/one", {12, ntsc_frame}, {0, milliseconds}, {2000, milliseconds}});
    project.timeline.set_track_state("video-main", true, false);
    std::ostringstream encoded(std::ios::out | std::ios::binary);
    ProjectCodec::write(project, encoded);
    const auto bytes = encoded.str();
    std::istringstream roundtrip_input(bytes, std::ios::in | std::ios::binary);
    auto roundtrip = ProjectCodec::read(roundtrip_input);
    check(roundtrip.id == project.id && roundtrip.name == project.name);
    check(roundtrip.timeline.frame_time_base() == ntsc_frame);
    check(roundtrip.timeline.tracks().size() == 2);
    check(roundtrip.timeline.tracks()[0].muted);
    check(roundtrip.timeline.tracks()[1].muted && roundtrip.timeline.tracks()[1].locked);
    check(roundtrip.timeline.tracks()[0].clips[0].id == "clip-01");
    check(roundtrip.timeline.tracks()[0].clips[0].media_id == "content://media/one");
    check(roundtrip.timeline.tracks()[0].clips[0].timeline_start.ticks == 12);

    std::istringstream legacy_input(legacy_v0_fixture(), std::ios::in | std::ios::binary);
    auto migrated = ProjectCodec::read(legacy_input);
    check(migrated.id == "old-project" && migrated.timeline.tracks().size() == 1);
    check(!migrated.timeline.tracks()[0].muted && !migrated.timeline.tracks()[0].locked);
    check(migrated.timeline.tracks()[0].clips[0].media_id == "content://media/item/1");
    std::ostringstream migrated_output(std::ios::out | std::ios::binary);
    ProjectCodec::write(migrated, migrated_output);
    check(migrated_output.str().substr(8, 4) == std::string("\x02\0\0\0", 4));

    auto truncated = bytes;
    truncated.pop_back();
    expect_failure([&] { std::istringstream input(truncated, std::ios::in | std::ios::binary); (void)ProjectCodec::read(input); });
    auto future = bytes;
    future[8] = static_cast<char>(ProjectCodec::current_version + 1);
    expect_failure([&] { std::istringstream input(future, std::ios::in | std::ios::binary); (void)ProjectCodec::read(input); });
    auto trailing = bytes + "x";
    expect_failure([&] { std::istringstream input(trailing, std::ios::in | std::ios::binary); (void)ProjectCodec::read(input); });
    auto oversized_string = bytes;
    oversized_string.replace(12, 4, std::string(4, static_cast<char>(0xff)));
    expect_failure([&] { std::istringstream input(oversized_string, std::ios::in | std::ios::binary); (void)ProjectCodec::read(input); });
    auto invalid_boolean = bytes;
    const auto first_track = 8 + 4 + 4 + project.id.size() + 4 + project.name.size() + 16 + 4;
    const auto muted_offset = first_track + 4 + project.timeline.tracks()[0].id.size() + 1;
    invalid_boolean[muted_offset] = 2;
    expect_failure([&] { std::istringstream input(invalid_boolean, std::ios::in | std::ios::binary); (void)ProjectCodec::read(input); });
    auto invalid_utf8 = bytes;
    invalid_utf8[16] = static_cast<char>(0xff); // First byte of the project ID field.
    expect_failure([&] { std::istringstream input(invalid_utf8, std::ios::in | std::ios::binary); (void)ProjectCodec::read(input); });

    const auto test_dir = make_temp_directory();
    try {
        const auto path = test_dir / "edit.owncut";
        { std::ofstream sentinel(path, std::ios::binary); sentinel << "prior contents"; }
        ProjectStore::save_atomic(path, project);
        auto saved = ProjectStore::load(path);
        check(saved.id == project.id && saved.timeline.tracks().size() == 2);
        Project invalid{"", "bad", ntsc_frame};
        expect_failure([&] { ProjectStore::save_atomic(path, invalid); });
        check(ProjectStore::load(path).id == project.id); // failed write preserved the previous project
        Project autosave_draft = project;
        autosave_draft.name = "Recovered draft";
        ProjectStore::save_autosave(path, autosave_draft);
        auto recovery = ProjectStore::load_autosave(path);
        check(recovery.has_value() && recovery->name == "Recovered draft");
        check(ProjectStore::load(path).name == project.name); // recovery never silently overwrites the main project
        { std::ofstream corrupt(ProjectStore::autosave_path_for(path), std::ios::binary | std::ios::trunc); corrupt << "broken"; }
        expect_failure([&] { (void)ProjectStore::load_autosave(path); });
        check(ProjectStore::load(path).id == project.id);
        ProjectStore::save_autosave(path, autosave_draft);
        ProjectStore::clear_autosave(path);
        check(!ProjectStore::load_autosave(path).has_value());
        unsigned files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(test_dir)) { (void)entry; ++files; }
        check(files == 1); // temporary file was cleaned up
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(test_dir, ignored);
        throw;
    }
    std::filesystem::remove_all(test_dir);

    std::cout << "All core, graph-contract, command-history, and project tests passed\n";
}
