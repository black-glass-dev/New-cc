#include "editor/session_coordinator.hpp"
#include "editor/unicode.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>
#include <sstream>

using namespace editor;
namespace {
void check(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("checkpoint assertion at line " + std::to_string(at.line()));
}
template<class F> void fails(F call) {
    bool failed=false; try { call(); } catch(const std::exception&) { failed=true; } check(failed);
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        for(unsigned i=0;i<100;++i) {
            path=std::filesystem::temp_directory_path()/("owncut-checkpoint-"+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));
            if(std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("temporary directory unavailable");
    }
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(path,ignored); }
};
std::string bytes(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}};
}
void put(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc); out.write(data.data(),static_cast<std::streamsize>(data.size()));
}
Project fixture(const std::string& name) {
    Project project("project-id",name,{1,30});
    project.timeline.add_track({"v1",TrackKind::video,false,false,{}});
    project.timeline.add_clip("v1",{"clip","content://source",{0,{1,30}},{0,{1,1000}},{4000,{1,1000}}});
    return project;
}
void unicode_roundtrips_and_invalid_input() {
    for(const auto& text : {std::string("ASCII"),std::string("বাংলা / \xf0\x9f\x8e\xac"),std::string("a\0b",3),std::string("\xf4\x8f\xbf\xbf")}) {
        const auto utf16=utf16_from_utf8(text);
        check(utf8_from_utf16(utf16) == text);
    }
    for(const auto& bad : {std::string("\xc0\x80"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xf0\x9f"),std::string("\x80"),std::string("\xe0\x80\xaf"),std::string("\xe0\x61\x80")})
        fails([&]{ (void)utf16_from_utf8(bad); });
    for(const auto& bad : {std::vector<std::uint16_t>{0xd800},std::vector<std::uint16_t>{0xdc00},std::vector<std::uint16_t>{0xd800,0x61}})
        fails([&]{ (void)utf8_from_utf16(bad); });
    Directory dir;
    auto project=fixture("বাংলা \xf0\x9f\x8e\xac");
    project.timeline.relink_media_references({"clip"},"content://source","content://বাংলা/\xf0\x9f\x8e\xac");
    ProjectStore::save_atomic(dir.path/"unicode.owncut",project);
    const auto loaded=ProjectStore::load(dir.path/"unicode.owncut");
    check(loaded.name == project.name && loaded.timeline.locate_clip("clip").clip.media_id == project.timeline.locate_clip("clip").clip.media_id);
}
void every_checkpoint_boundary() {
    for (const auto stage : {CheckpointStep::recovery_written,CheckpointStep::prepared,
                            CheckpointStep::primary_written,CheckpointStep::committed,
                            CheckpointStep::recovery_cleared,CheckpointStep::clean}) {
        Directory dir; const auto path=dir.path/"project.owncut";
        const auto old=fixture("Old"), draft=fixture("Latest draft");
        CheckpointJournal::commit(path,old,10);
        check(!ProjectStore::load_autosave(path));
        check(CheckpointJournal::inspect(path)->generation == 1);
        fails([&]{ CheckpointJournal::commit(path,draft,11,[&](CheckpointStep step) {
            if(step == stage) throw std::runtime_error("simulated interruption after durable boundary");
        }); });
        const bool primary_published=stage >= CheckpointStep::primary_written;
        check(ProjectStore::load(path).name == (primary_published ? "Latest draft" : "Old"));
        if(stage < CheckpointStep::recovery_cleared) {
            check(ProjectStore::load_autosave(path)->name == "Latest draft");
        } else check(!ProjectStore::load_autosave(path));
        const auto receipt=CheckpointJournal::inspect(path);
        check(receipt.has_value());
        check(receipt->generation == (stage == CheckpointStep::recovery_written ? 1u : 2u));
        if(stage == CheckpointStep::prepared || stage == CheckpointStep::primary_written)
            check(receipt->phase == CheckpointPhase::prepared);
        if(stage == CheckpointStep::committed || stage == CheckpointStep::recovery_cleared)
            check(receipt->phase == CheckpointPhase::committed);
        const auto generation=receipt->generation;
        CheckpointJournal::commit(path,draft,12);
        check(CheckpointJournal::inspect(path)->generation == generation+1);
        check(CheckpointJournal::inspect(path)->session_revision == 12);
        check(CheckpointJournal::inspect(path)->phase == CheckpointPhase::clean);
        check(!ProjectStore::load_autosave(path));
        for(const auto& file : std::filesystem::directory_iterator(dir.path))
            check(file.path().filename().string().find(".tmp.") == std::string::npos);
    }
}
void corrupt_or_future_receipt_preserved() {
    Directory dir; const auto path=dir.path/"project.owncut";
    const auto initial=fixture("Primary"), draft=fixture("Draft");
    CheckpointJournal::commit(path,initial,1);
    const auto clean=bytes(CheckpointJournal::path_for(path));
    auto future=clean; future[8]=2;
    auto badphase=clean; badphase[28]=99;
    auto overflow=clean;
    for(std::size_t i=12;i<20;++i) overflow[i]=static_cast<char>(255);
    for(const auto& damaged : {std::string("broken"),future,badphase,clean+"trailing",std::string(5000,'x'),overflow}) {
        put(CheckpointJournal::path_for(path),damaged);
        fails([&]{ CheckpointJournal::commit(path,draft,2); });
        check(bytes(CheckpointJournal::path_for(path)) == damaged);
        check(ProjectStore::load(path).name == "Primary");
        check(ProjectStore::load_autosave(path)->name == "Draft");
    }
    // Unknown future versions are refused even when larger than the v1 receipt budget.
    put(CheckpointJournal::path_for(path),future+std::string(5000,'x'));
    fails([&]{ (void)CheckpointJournal::archive_corrupt(path); });
    check(bytes(CheckpointJournal::path_for(path)) == future+std::string(5000,'x'));
    put(CheckpointJournal::path_for(path),"broken archive candidate");
    const auto primary_before=bytes(path), recovery_before=bytes(ProjectStore::autosave_path_for(path));
    const auto archived=CheckpointJournal::archive_corrupt(path);
    check(archived && bytes(*archived) == "broken archive candidate");
    check(bytes(path) == primary_before && bytes(ProjectStore::autosave_path_for(path)) == recovery_before);
    check(!CheckpointJournal::archive_corrupt(path));
    CheckpointJournal::commit(path,draft,3);
    check(!CheckpointJournal::archive_corrupt(path));
    std::filesystem::remove(CheckpointJournal::path_for(path));
    std::filesystem::create_directory(CheckpointJournal::path_for(path));
    const auto child=CheckpointJournal::path_for(path)/"keep.txt"; put(child,"keep directory contents");
    fails([&]{ (void)CheckpointJournal::archive_corrupt(path); });
    check(bytes(child) == "keep directory contents");
    const auto target=dir.path/"atomic.txt"; put(target,"original");
    fails([&]{ AtomicFileStore::write(target,[](std::ostream& out) { out << "partial"; throw std::runtime_error("writer failed"); }); });
    check(bytes(target) == "original");
    for(const auto& file : std::filesystem::directory_iterator(dir.path))
        check(file.path().filename().string().find(".tmp.") == std::string::npos);
}
void future_drafts_and_receipts_are_not_overwritten() {
    Directory dir; const auto path=dir.path/"project.owncut";
    const auto project=fixture("Current"), old_draft=fixture("Previous draft");
    CheckpointJournal::commit(path,project,1);
    ProjectStore::save_autosave(path,old_draft);
    const auto recovery=ProjectStore::autosave_path_for(path);
    const auto old_bytes=bytes(recovery);
    auto receipt=bytes(CheckpointJournal::path_for(path)); receipt[8]=2;
    put(CheckpointJournal::path_for(path),receipt);
    fails([&]{ CheckpointJournal::commit(path,project,2); });
    check(bytes(recovery) == old_bytes);
    fails([&]{ ProjectStore::clear_autosave(path); });
    check(bytes(recovery) == old_bytes);
    SessionCoordinator session("new"); session.load(path);
    fails([&]{ session.autosave(path); });
    check(bytes(recovery) == old_bytes);
    std::filesystem::remove(CheckpointJournal::path_for(path));
    auto future=old_bytes; future[8]=static_cast<char>(ProjectCodec::current_version+1); put(recovery,future);
    fails([&]{ ProjectStore::save_autosave(path,project); });
    fails([&]{ ProjectStore::clear_autosave(path); });
    fails([&]{ CheckpointJournal::commit(path,project,2); });
    check(bytes(recovery) == future);
    check(!CheckpointJournal::inspect(path));
}
void coordinator_and_media_catalog() {
    Directory dir; const auto path=dir.path/"session.owncut";
    SessionCoordinator session("media");
    const auto first=session.add_media("content://source/a",4000);
    session.execute(std::make_unique<SplitClipCommand>(first,Timestamp{60,{1,30}},"right"));
    const auto second=session.add_media("content://source/b",2000);
    auto catalog=session.media_assets();
    check(catalog.size() == 2 && catalog[0].clip_ids.size() == 2 && catalog[0].required_source_end_ms == 4000);
    check(catalog[1].clip_ids[0] == second && catalog[1].required_source_end_ms == 2000);
    session.execute(std::make_unique<RelinkClipMediaCommand>(first,"content://source/b"));
    check(session.media_assets().size() == 1 && session.media_assets()[0].clip_ids.size() == 3);
    check(session.undo());
    session.execute(std::make_unique<DeleteClipCommand>("right"));
    check(session.media_assets()[0].required_source_end_ms == 2000);
    session.checkpoint(path);
    check(!session.status().dirty && session.status().can_undo);
    const auto receipt=CheckpointJournal::inspect(path);
    check(receipt->session_revision == session.status().revision && receipt->phase == CheckpointPhase::clean);
    check(!ProjectStore::load_autosave(path));
    session.execute(std::make_unique<DeleteClipCommand>(first));
    check(session.media_assets().size() == 1 && session.media_assets()[0].media_id == "content://source/b");
    put(CheckpointJournal::path_for(path),"corrupt receipt");
    fails([&]{ session.checkpoint(path); });
    check(session.status().dirty && ProjectStore::load_autosave(path).has_value());
    // The catalog spans tracks; this is not an independent editable media registry.
    auto snapshot=session.snapshot().project;
    snapshot.timeline.add_clip("a1",{"audio","content://source/b",{0,{1,30}},{0,{1,1000}},{6000,{1,1000}}});
    ProjectStore::save_atomic(dir.path/"multi.owncut",snapshot);
    session.load(dir.path/"multi.owncut");
    check(session.media_assets()[0].clip_ids.size() == 2 && session.media_assets()[0].required_source_end_ms == 6000);
}
}
int main() {
    unicode_roundtrips_and_invalid_input(); every_checkpoint_boundary(); corrupt_or_future_receipt_preserved(); future_drafts_and_receipts_are_not_overwritten(); coordinator_and_media_catalog();
    std::cout << "Checkpoint boundaries, receipts, atomic cleanup and media catalog tests passed\n";
}
