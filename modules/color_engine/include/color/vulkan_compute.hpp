#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
namespace color {
class GpuUnavailable:public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class BufferAccess { Input,Output,InputOutput };
struct ComputeBuffer { std::span<std::byte> bytes;BufferAccess access{BufferAccess::Input}; };
struct ComputeDispatch {
    std::span<const std::uint32_t> spirv;
    std::span<ComputeBuffer> buffers; // contiguous descriptor bindings0..N-1
    std::span<const std::byte> push_constants;
    std::uint32_t work_items{}; // bundled kernels use local_size_x64
};
class IVulkanCompute {
public:
    virtual ~IVulkanCompute()=default;
    virtual std::string device_name() const=0;
    // Synchronous queue submission, explicit host-visible upload/readback, no CPU fallback.
    virtual void dispatch(const ComputeDispatch&)=0;
};
// Optional COLOR_BUILD_VULKAN target, POSIX/Android loader; throws GpuUnavailable if absent.
std::unique_ptr<IVulkanCompute> create_vulkan_compute(std::size_t memory_budget_bytes=256U*1024U*1024U);
} // namespace color
