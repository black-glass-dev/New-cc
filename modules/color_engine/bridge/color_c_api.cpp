#include "color_c_api.h"
#include "color/color_pipeline_api.hpp"
#include "color/extended_tools.hpp"
#include "color/grade_archive.hpp"
#include <cstring>
#include <array>
#include <cstdio>
#include <exception>
#include <new>
#include <stdexcept>
#include <algorithm>

struct ColorEngineHandle { std::unique_ptr<color::IColorEngine> engine; };
struct ColorProgramHandle { color::GradeProgram program; };
namespace {
thread_local std::array<char,512> error{};
template<class F> ColorStatus protect(F fn) noexcept {
    error[0]='\0';
    try { fn();return COLOR_OK; }
    catch(const std::bad_alloc&) { std::snprintf(error.data(),error.size(),"Color allocation failed");return COLOR_OUT_OF_MEMORY; }
    catch(const std::exception& e) { std::snprintf(error.data(),error.size(),"%s",e.what());return COLOR_INVALID_ARGUMENT; }
    catch(...) { std::snprintf(error.data(),error.size(),"Unknown color engine failure");return COLOR_INTERNAL_ERROR; }
}
void require(bool b) { if(!b) throw std::invalid_argument("Invalid color API argument"); }
color::ColorFrameBuffer32F frame(ColorFrameBuffer32F_C f) { return {f.data,f.width,f.height,f.row_stride_floats,f.capacity_floats}; }
}
extern "C" {
uint32_t ColorEngineAPIVersion(void) { return COLOR_API_VERSION; }
const char* ColorEngineLastError(void) { return error.data(); }
ColorStatus ColorEngineCreate(size_t budget,size_t tile,ColorEngineHandle** out) {
    if(out) *out=nullptr;
    return protect([&] { require(out!=nullptr);auto e=color::create_engine({budget,tile});*out=new ColorEngineHandle{std::move(e)}; });
}
void ColorEngineDestroy(ColorEngineHandle* e) { delete e; }
ColorStatus ColorProgramCreate(ColorProgramHandle** out) { if(out) *out=nullptr;return protect([&]{require(out!=nullptr);*out=new ColorProgramHandle{};}); }
void ColorProgramDestroy(ColorProgramHandle* p) { delete p; }
ColorStatus ColorProgramAddExposure(ColorProgramHandle* p,unsigned stage,uint64_t id,uint64_t input,float stops) {
    return protect([&]{require(p&&stage<4);color::Primaries grade;grade.exposure=stops;(void)color::apply(color::Vec3{.18F,.18F,.18F},grade);color::Node n;n.id=id;n.operation=grade;if(input) n.inputs.push_back(input);
        std::array<color::NodeGraph*,4> graphs{&p->program.group_pre,&p->program.clip,&p->program.group_post,&p->program.timeline};graphs[stage]->add(std::move(n));});
}
ColorStatus ColorProgramAddCST(ColorProgramHandle* p,unsigned stage,uint64_t id,uint64_t input,ColorCSTParams_C params) {
    return protect([&]{require(p&&stage<4&&params.input_gamut<=10&&params.output_gamut<=10&&params.input_transfer<=11&&params.output_transfer<=11);color::CST c;c.input_gamut=static_cast<color::Gamut>(params.input_gamut);c.output_gamut=static_cast<color::Gamut>(params.output_gamut);c.input_transfer=static_cast<color::Transfer>(params.input_transfer);c.output_transfer=static_cast<color::Transfer>(params.output_transfer);c.exposure_stops=params.exposure_stops;c.tone_knee=params.tone_knee;c.gamut_compression=params.gamut_compression;(void)color::PreparedCST(c);color::Node n;n.id=id;n.operation=c;if(input) n.inputs.push_back(input);std::array<color::NodeGraph*,4> graphs{&p->program.group_pre,&p->program.clip,&p->program.group_post,&p->program.timeline};graphs[stage]->add(std::move(n));});
}
ColorStatus ColorProgramAddSpatial(ColorProgramHandle* p,unsigned stage,uint64_t id,uint64_t input,ColorSpatialParams_C params) {
    return protect([&]{require(p&&stage<4&&params.kind<=7&&params.kind!=5);color::SpatialFilter filter;filter.kind=static_cast<color::SpatialKind>(params.kind);filter.radius=params.radius;filter.spatial_sigma=params.spatial_sigma;filter.range_sigma=params.range_sigma;filter.amount=params.amount;filter.threshold=params.threshold;filter.airlight={params.airlight_r,params.airlight_g,params.airlight_b};filter.transmission_floor=params.transmission_floor;color::validate_spatial(filter);color::Node n;n.id=id;n.operation=filter;if(input) n.inputs.push_back(input);std::array<color::NodeGraph*,4> graphs{&p->program.group_pre,&p->program.clip,&p->program.group_post,&p->program.timeline};graphs[stage]->add(std::move(n));});
}
ColorStatus ColorProgramSerialize(const ColorProgramHandle* p,void* out,size_t capacity,size_t* required) {
    return protect([&]{require(p&&required);const auto bytes=color::serialize_grade(p->program);*required=bytes.size();if(!out) {require(capacity==0);return;}require(capacity>=bytes.size());std::memcpy(out,bytes.data(),bytes.size());});
}
ColorStatus ColorProgramDeserialize(const void* data,size_t bytes,ColorProgramHandle** out) {
    if(out) *out=nullptr;
    return protect([&]{require(data&&out&&bytes<=32U*1024U*1024U);auto program=color::deserialize_grade({static_cast<const std::byte*>(data),bytes});*out=new ColorProgramHandle{std::move(program)};});
}
ColorStatus RenderGradeFrame(ColorEngineHandle* e,const ColorProgramHandle* p,ColorFrameBuffer32F_C in,ColorFrameBuffer32F_C out,GradeContext_C context) {
    return protect([&]{require(e&&p);e->engine->RenderGradeFrame(p->program,frame(in),frame(out),{context.clip_id,context.group_id,context.source_revision,context.timeline_time_sec,context.frame_index});});
}
ColorStatus ComputeVideoScopes(ColorEngineHandle* e,ColorFrameBuffer32F_C in,size_t bins,uint64_t* histogram,size_t count) {
    return protect([&]{require(e&&histogram&&bins>=2&&bins<=4096&&count>=4*bins);const auto s=e->engine->ComputeVideoScopes(frame(in),{bins,1,0,1,color::Gamut::Rec709});for(size_t c=0;c<4;++c) std::copy(s.histogram[c].begin(),s.histogram[c].end(),histogram+c*bins);});
}
}
