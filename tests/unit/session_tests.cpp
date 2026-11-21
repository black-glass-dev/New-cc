#include "editor/session_coordinator.hpp"
#include "editor/session_registry.hpp"
#include <atomic>
#include <array>
#include <limits>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <source_location>
#include <thread>

using namespace editor;
namespace {
void check(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("session assertion at line " + std::to_string(at.line()));
}
template<class F> void fails(F f) { bool caught = false; try { f(); } catch(const std::exception&) { caught = true; } check(caught); }
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        for (unsigned i=0; i<100; ++i) {
            path = base / ("owncut-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("no temporary directory");
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
struct BrokenCommand : EditCommand {
    void apply(Timeline& timeline) override {
        timeline.add_clip("v1", {"partial", "source", {900,{1,30}}, {0,{1,1000}}, {1000,{1,1000}}});
        throw std::runtime_error("injected failure after mutation");
    }
    void undo(Timeline&) override {}
};
void history_snapshots_and_failure() {
    SessionCoordinator session("one");
    check(!session.status().dirty && !session.undo() && session.status().revision == 0);
    const auto clip = session.add_media("source",4000);
    session.execute(std::make_unique<SplitClipCommand>(clip,Timestamp{60,{1,30}},"right"));
    check(session.status().revision == 2 && session.status().clips == 2);
    auto copy = session.snapshot();
    copy.project.name = "not live";
    (void)copy.project.timeline.remove_clip("right");
    check(session.status().name == "Untitled" && session.status().clips == 2);
    check(session.undo());
    const auto before = session.status();
    fails([&]{ session.execute(std::make_unique<BrokenCommand>()); });
    check(session.status().revision == before.revision && session.status().clips == 1 && session.status().can_redo);
    fails([&]{ session.execute(nullptr); });
    check(session.status().revision == before.revision);
    check(session.redo() && session.status().clips == 2);
    const auto resolved = session.resolve_playhead("v1",{75,{1,30}});
    check(resolved && resolved->clip_id == "right");
    fails([&]{ (void)session.add_media("bad",0); });
    check(session.status().revision == before.revision+1);
}
void save_recovery_and_isolation() {
    TemporaryDirectory dir;
    const auto primary = dir.path/"primary.owncut";
    SessionCoordinator session("original"), other("independent");
    const auto id = session.add_media("source",4000);
    session.save(primary);
    check(!session.status().dirty && session.status().can_undo);
    (void)session.add_media("second",2000);
    session.save_copy(dir.path/"external.owncut");
    check(session.status().dirty);
    session.restore_draft(dir.path/"external.owncut");
    check(session.status().dirty && !session.status().can_undo);
    session.execute(std::make_unique<MoveClipCommand>(id,"v1",Timestamp{0,{1,30}}));
    session.autosave(primary);
    check(session.status().dirty && ProjectStore::load(primary).timeline.tracks()[0].clips.size() == 1);
    const auto revision = session.status().revision;
    fails([&]{ session.save(dir.path/"missing"/"output.owncut"); });
    check(session.status().dirty && session.status().revision == revision);
    { std::ofstream file(dir.path/"bad.owncut"); file << "corrupt"; }
    fails([&]{ session.load(dir.path/"bad.owncut"); });
    check(session.status().revision == revision && session.status().clips == 2 && session.status().can_undo);
    session.load(primary);
    check(!session.status().dirty && !session.status().can_undo && session.status().clips == 1);
    const auto fresh = session.add_media("after-load",1000);
    check(fresh != id); // live IDs are probed after replacement
    session.load(primary);
    check(session.recover(primary) && session.status().dirty && session.status().clips == 2 && !session.status().can_undo);
    check(other.status().clips == 0 && other.snapshot().project.id == "independent");
    const auto recovered_revision = session.status().revision;
    { std::ofstream file(ProjectStore::autosave_path_for(primary)); file << "invalid recovery"; }
    fails([&]{ (void)session.recover(primary); });
    check(session.status().revision == recovered_revision && session.status().clips == 2);
    session.clear_recovery(primary);
    check(!session.recover(primary) && session.status().revision == recovered_revision);
    session.save(primary);
    check(!session.status().dirty);
    // Primary save does not silently clear recovery: the Android transaction owner does that.
    session.autosave(primary); session.save(primary);
    check(std::filesystem::exists(ProjectStore::autosave_path_for(primary)));
}
void legacy_and_track_boundaries() {
    TemporaryDirectory dir;
    std::string legacy = "OWNCUTPJ";
    auto number = [&](std::uint64_t value, unsigned width) {
        for(unsigned i=0;i<width;++i) legacy.push_back(static_cast<char>((value >> (8*i)) & 255));
    };
    auto text = [&](const std::string& value) { number(value.size(),4); legacy += value; };
    number(0,4); text("legacy"); text("Legacy project"); number(1,8); number(30,8);
    number(1,4); text("v1"); number(0,1); number(1,4); // v0 has no mute/lock bytes
    text("clip-1"); text("content://legacy/source");
    number(0,8); number(1,8); number(30,8);
    number(0,8); number(1,8); number(1000,8);
    number(2000,8); number(1,8); number(1000,8);
    const auto path = dir.path/"legacy.owncut";
    { std::ofstream out(path,std::ios::binary); out.write(legacy.data(), static_cast<std::streamsize>(legacy.size())); }
    SessionCoordinator session("new"); session.load(path);
    check(session.snapshot().project.id == "legacy" && !session.status().dirty);
    check(!session.snapshot().project.timeline.tracks()[0].locked);
    check(session.add_media("new source",1000) == "clip-2");
    Project multitrack("multi","Multiple tracks",{1,30});
    for(const auto& name : {"v1","a1"}) {
        multitrack.timeline.add_track({name, name == std::string("v1") ? TrackKind::video : TrackKind::audio,false,false,{}});
        for(int i=0;i<2;++i) multitrack.timeline.add_clip(name,
            {std::string(name)+std::to_string(i),"source",{i*30,{1,30}},{0,{1,1000}},{1000,{1,1000}}});
    }
    ProjectStore::save_atomic(path,multitrack); session.load(path);
    const auto page = session.clip_page(1,2);
    check(page.total == 4 && page.next_offset == 3 && page.clips[0].track_id == "v1" && page.clips[1].track_id == "a1");
}
void paging_and_concurrent_calls() {
    SessionCoordinator session("parallel");
    std::atomic<bool> done{false}, failed{false};
    std::thread reader([&] {
        try {
            while(!done.load()) {
                const auto snap = session.snapshot();
                check(snap.revision == snap.project.timeline.tracks()[0].clips.size());
            }
        } catch(...) { failed = true; }
    });
    auto writer = [&] {
        try { for(int i=0;i<140;++i) (void)session.add_media("source",1000); }
        catch(...) { failed = true; }
    };
    std::thread a(writer), b(writer);
    a.join(); b.join(); done = true; reader.join();
    check(!failed && session.status().revision == 280 && session.status().clips == 280);
    const auto first = session.clip_page(0,256);
    const auto second = session.clip_page(first.next_offset,256,first.revision);
    check(first.clips.size() == 256 && second.clips.size() == 24 && second.next_offset == 280);
    std::set<std::string> ids;
    for(const auto& c : first.clips) ids.insert(c.clip.id);
    for(const auto& c : second.clips) ids.insert(c.clip.id);
    check(ids.size() == 280);
    check(session.clip_page(280,1).clips.empty());
    fails([&]{ (void)session.clip_page(0,0); });
    fails([&]{ (void)session.clip_page(0,257); });
    fails([&]{ (void)session.clip_page(281,1); });
    (void)session.add_media("changed",1000);
    fails([&]{ (void)session.clip_page(256,256,first.revision); });
}
void registry_lifetime_and_nonreused_handles() {
    SessionRegistry registry;
    fails([&]{ (void)registry.acquire(0); });
    fails([&]{ (void)registry.acquire(-1); });
    fails([&]{ (void)registry.acquire(std::numeric_limits<std::int64_t>::max()); });
    const auto first = registry.create("retained");
    auto pin = registry.acquire(first);
    std::weak_ptr<SessionCoordinator> weak = pin;
    std::atomic<bool> closed{false}, worked{false};
    std::thread worker([&, owned = std::move(pin)] {
        while (!closed.load()) std::this_thread::yield();
        (void)owned->add_media("after-close",1000);
        worked = owned->status().clips == 1;
    });
    registry.close(first);
    check(!weak.expired());
    fails([&]{ (void)registry.acquire(first); });
    registry.close(first); registry.close(0); registry.close(-1);
    closed = true;
    worker.join();
    check(worked && weak.expired());
    const auto second = registry.create("independent");
    check(second > first && registry.acquire(second)->status().clips == 0);
    fails([&]{ (void)registry.acquire(first); });
    registry.close(second);

    std::array<std::vector<SessionRegistry::Handle>,4> tokens;
    std::array<std::thread,4> writers;
    for (std::size_t t=0;t<writers.size();++t) writers[t]=std::thread([&,t] {
        for (int i=0;i<80;++i) {
            const auto handle=registry.create("parallel");
            tokens[t].push_back(handle);
            (void)registry.acquire(handle)->add_media("source",1000);
            registry.close(handle);
        }
    });
    for (auto& thread : writers) thread.join();
    std::set<SessionRegistry::Handle> unique;
    for (const auto& list : tokens) for (const auto handle : list) {
        check(unique.insert(handle).second && handle > second);
        fails([&]{ (void)registry.acquire(handle); });
    }
    check(unique.size() == 320);
}
} // namespace
int main() {
    registry_lifetime_and_nonreused_handles();
    history_snapshots_and_failure(); save_recovery_and_isolation(); legacy_and_track_boundaries(); paging_and_concurrent_calls();
    std::cout << "Session ownership, rollback, revision, persistence, paging and concurrency tests passed\n";
}
