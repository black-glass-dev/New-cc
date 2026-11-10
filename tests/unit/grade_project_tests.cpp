#include "editor/session_coordinator.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <source_location>

using namespace editor;
namespace {
void check(bool value, std::source_location at=std::source_location::current()) {
    if (!value) throw std::runtime_error("grade project assertion at line "+std::to_string(at.line()));
}
template<class F> void fails(F call) { bool caught=false; try { call(); } catch(const std::exception&) { caught=true; } check(caught); }
struct Directory {
    std::filesystem::path path;
    Directory() {
        for(unsigned i=0;i<100;++i) {
            path=std::filesystem::temp_directory_path()/("owncut-grade-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));
            if(std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("temporary directory unavailable");
    }
    ~Directory(){ std::error_code error; std::filesystem::remove_all(path,error); }
};
color::Grade look() {
    color::Grade g; g.exposure_stops=0.12345678901234567; g.contrast=1.4;
    g.balance_stops={0.1,-0.2,0.3}; g.lift={0.01,-0.02,0.03}; g.gamma={0.8,1.2,1.5};
    g.gain={1.1,0.9,1.3}; g.strength=0.7; return g;
}
color::Grade grade(const SessionCoordinator& s,const std::string& id) { return s.snapshot().project.timeline.locate_clip(id).clip.grade; }
void set(SessionCoordinator& s,std::vector<std::string> ids,color::Grade g) { s.execute(std::make_unique<SetClipGradesCommand>(std::move(ids),g)); }
std::string bytes(const std::filesystem::path& p) { std::ifstream in(p,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
void put(const std::filesystem::path& p,const std::string& data) { std::ofstream out(p,std::ios::binary); out.write(data.data(),static_cast<std::streamsize>(data.size())); }
void history_and_edit_inheritance() {
    {
        SessionCoordinator split("split"); const auto id=split.add_media("source",4000);
        set(split,{id},look());
        split.execute(std::make_unique<SplitClipCommand>(id,Timestamp{60,{1,30}},"tail"));
        check(split.undo() && split.status().clips==1 && grade(split,id)==look());
        check(split.redo() && grade(split,"tail")==look());
    }
    SessionCoordinator s("project");
    const auto first=s.add_media("shared-source",4000), other=s.add_media("shared-source",2000);
    const auto g=look(); set(s,{first},g);
    check(grade(s,first)==g && grade(s,other)==color::Grade{});
    const auto frozen=s.snapshot();
    s.execute(std::make_unique<SplitClipCommand>(first,Timestamp{60,{1,30}},"right"));
    check(grade(s,"right")==g && grade(s,first)==g);
    auto bypass=g; bypass.enabled=false; set(s,{"right"},bypass);
    check(s.undo() && grade(s,"right")==g); check(s.redo() && grade(s,"right")==bypass);
    set(s,{first,"right",other},color::Grade{}); // reset/paste is one undo entry
    check(s.undo() && grade(s,first)==g && grade(s,"right")==bypass && grade(s,other)==color::Grade{});
    check(s.redo() && grade(s,first)==color::Grade{}); check(s.undo());
    s.execute(std::make_unique<RelinkClipMediaCommand>(first,"new-source"));
    check(grade(s,first)==g && grade(s,"right")==bypass && grade(s,other)==color::Grade{});
    s.execute(std::make_unique<TrimClipCommand>(first,TrimEdge::right,Timestamp{1500,{1,1000}}));
    check(grade(s,first)==g); check(s.undo() && grade(s,first)==g);
    s.execute(std::make_unique<MoveClipCommand>(other,"v1",Timestamp{200,{1,30}}));
    check(grade(s,first)==g); check(s.undo());
    s.execute(std::make_unique<DeleteClipCommand>("right")); check(s.undo() && grade(s,"right")==bypass);
    s.execute(std::make_unique<RippleDeleteClipCommand>(first)); check(s.undo() && grade(s,first)==g);
    check(frozen.project.timeline.locate_clip(first).clip.grade==g); // owned worker snapshot
    const auto revision=s.status().revision;
    fails([&]{ set(s,{first,"missing"},{}); });
    fails([&]{ set(s,{first,first},{}); }); fails([&]{ set(s,{},{}); });
    auto invalid=g; invalid.gamma[0]=0; fails([&]{ set(s,{first},invalid); });
    check(s.status().revision==revision && grade(s,first)==g);
    check(s.redo()); check(s.undo() && grade(s,first)==g); // failed grade command retained redo
    // Standalone command must also be atomic when one target is locked/audio/missing.
    auto timeline=s.snapshot().project.timeline;
    timeline.add_track({"locked",TrackKind::video,false,false,{}});
    timeline.add_clip("locked",{"locked-clip","source",{0,{1,30}},{0,{1,1000}},{1000,{1,1000}}});
    timeline.set_track_state("locked",false,true);
    SetClipGradesCommand bad({first,"locked-clip"},{}); fails([&]{ bad.apply(timeline); });
    check(timeline.locate_clip(first).clip.grade==g);
    timeline.add_clip("a1",{"audio","source",{0,{1,30}},{0,{1,1000}},{1000,{1,1000}}});
    fails([&]{ SetClipGradesCommand command({first,"audio"},g); command.apply(timeline); });
    check(timeline.locate_clip(first).clip.grade==g);
    fails([&]{ timeline.move_clip(first,"a1",{0,{1,30}}); });
    check(timeline.locate_clip(first).track_id=="v1");
}
std::string legacy(unsigned version) {
    // Independently authored v0/v1 byte fixture, not the current writer relabelled.
    std::string result="OWNCUTPJ";
    auto number=[&](std::uint64_t x,unsigned n){ for(unsigned i=0;i<n;++i) result.push_back(static_cast<char>((x>>(8*i))&255)); };
    auto text=[&](const std::string& value){ number(value.size(),4); result+=value; };
    number(version,4); text("legacy"); text("Legacy"); number(1,8); number(30,8); number(1,4);
    text("v1"); number(0,1); if(version==1){number(1,1);number(0,1);} number(1,4);
    text("clip"); text("source");
    number(0,8);number(1,8);number(30,8);
    number(0,8);number(1,8);number(1000,8);
    number(4000,8);number(1,8);number(1000,8);
    return result;
}
void codec_migration_and_failures() {
    for(unsigned version : {0U,1U}) {
        std::istringstream in(legacy(version),std::ios::binary); const auto p=ProjectCodec::read(in);
        check(p.timeline.locate_clip("clip").clip.grade==color::Grade{});
        check(p.timeline.tracks()[0].muted==(version==1));
    }
    SessionCoordinator s("id"); const auto id=s.add_media("source",4000); set(s,{id},look());
    Directory dir; const auto path=dir.path/"project.owncut";
    s.checkpoint(path); auto data=bytes(path);
    check(static_cast<unsigned char>(data[8])==2 && !s.status().dirty);
    SessionCoordinator loaded("other"); loaded.load(path); check(grade(loaded,id)==look() && !loaded.status().can_undo);
    for(std::size_t missing=1;missing<=color::PresetCodec::encoded_size;++missing) {
        put(dir.path/"bad",data.substr(0,data.size()-missing));
        const auto rev=loaded.status().revision;
        fails([&]{ loaded.load(dir.path/"bad"); });
        check(loaded.status().revision==rev && grade(loaded,id)==look());
    }
    // Single-video-track payload keeps the grade at the tail, so locate by magic.
    const auto offset=data.find("OWNCUTCG"); check(offset!=std::string::npos);
    auto bad=data; bad[offset+8]=2; put(dir.path/"bad",bad); fails([&]{ loaded.load(dir.path/"bad"); });
    bad=data; bad[offset+12]=2; put(dir.path/"bad",bad); fails([&]{ loaded.load(dir.path/"bad"); });
    bad=data; for(std::size_t i=0;i<8;++i) bad[offset+13+i]=0; bad[offset+19]=static_cast<char>(0xF0);bad[offset+20]=0x7F;
    put(dir.path/"bad",bad); fails([&]{ loaded.load(dir.path/"bad"); });
    check(grade(loaded,id)==look());
    auto future=data; future[8]=3; put(path,future);
    const auto recovery=ProjectStore::autosave_path_for(path); put(recovery,"preserved-recovery");
    const auto receipt=bytes(CheckpointJournal::path_for(path));
    fails([&]{ s.checkpoint(path); }); fails([&]{ s.save(path); }); fails([&]{ s.autosave(path); }); fails([&]{ s.clear_recovery(path); });
    check(bytes(path)==future && bytes(recovery)=="preserved-recovery" && bytes(CheckpointJournal::path_for(path))==receipt);
}
void recovery_and_checkpoint_boundaries() {
    const auto g=look();
    for(auto step : {CheckpointStep::recovery_written,CheckpointStep::prepared,CheckpointStep::primary_written,
                     CheckpointStep::committed,CheckpointStep::recovery_cleared,CheckpointStep::clean}) {
        Directory dir; const auto path=dir.path/"project";
        SessionCoordinator s("id"); const auto id=s.add_media("source",4000); s.checkpoint(path); set(s,{id},g);
        const auto draft=s.snapshot();
        fails([&]{ CheckpointJournal::commit(path,draft.project,draft.revision,[&](auto actual){if(actual==step)throw std::runtime_error("interruption");}); });
        check(ProjectStore::load(path).timeline.locate_clip(id).clip.grade==(step>=CheckpointStep::primary_written ? g : color::Grade{}));
        if(step<CheckpointStep::recovery_cleared) check(ProjectStore::load_autosave(path)->timeline.locate_clip(id).clip.grade==g);
    }
    Directory dir; const auto path=dir.path/"p";
    SessionCoordinator original("id"); const auto id=original.add_media("source",4000); original.checkpoint(path);
    set(original,{id},g); original.autosave(path);
    SessionCoordinator other("independent"); other.load(path); check(grade(other,id)==color::Grade{});
    check(other.recover(path) && other.status().dirty && grade(other,id)==g);
    check(ProjectStore::load(path).timeline.locate_clip(id).clip.grade==color::Grade{});
    other.checkpoint(path); SessionCoordinator reopened("new"); reopened.load(path); check(grade(reopened,id)==g);
}
} // namespace
int main(){ history_and_edit_inheritance();codec_migration_and_failures();recovery_and_checkpoint_boundaries();
    std::cout<<"Grade/project v2: atomic history, edits, immutable snapshots, v0/v1 migration, corrupt/future preservation and recovery boundaries passed\n";
}
