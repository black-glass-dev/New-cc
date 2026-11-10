#pragma once
#include "color_pipeline_api.hpp"
#include "vulkan_compute.hpp"
#include <cstdint>
#include <vector>

namespace color {
// Bundled SPIR-V modules compiled from shaders/glsl/{grade,spatial}.comp by the caller's toolchain.
struct GpuKernels {
    std::vector<std::uint32_t> grade_spirv;
    std::vector<std::uint32_t> spatial_spirv;
};
// Executes the same four-stage GradeProgram as IColorEngine::RenderGradeFrame by dispatching
// the bundled kernels through IVulkanCompute. Host-staged: each node reads/writes host planes
// synchronously. Unsupported node/operator combinations throw std::invalid_argument; there is
// no silent CPU fallback. Equivalence is tested to a stated tolerance, not bit exactness.
class GpuGradeEngine {
public:
    GpuGradeEngine(IVulkanCompute& gpu,GpuKernels kernels,std::size_t memory_budget_bytes=256U*1024U*1024U);
    void RenderGradeFrame(const GradeProgram&,ColorFrameBuffer32F input,ColorFrameBuffer32F output,const GradeContext&) const;
private:
    IVulkanCompute& gpu_;GpuKernels kernels_;std::size_t budget_;
};
} // namespace color
