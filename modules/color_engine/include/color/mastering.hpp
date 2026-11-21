#pragma once
#include "color_pipeline_api.hpp"
namespace color {
struct MasterSpec {std::string name;Primaries trim;CST output_transform;};
struct MasterFrame {std::string name;Image32F image;};
// Pixel transforms/trims only, not metadata authoring, codec output or display certification.
// Input and timing unchanged; independent owned outputs, all returned only after success.
std::vector<MasterFrame> render_masters(const IColorEngine&,const GradeProgram&,ColorFrameBuffer32F,
    const GradeContext&,std::span<const MasterSpec>,std::size_t memory_budget_bytes=256U*1024U*1024U);
} // namespace color
