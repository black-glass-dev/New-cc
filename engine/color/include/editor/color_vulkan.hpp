#pragma once

// Vulkan compute path for the FP32 colour pipeline (step 2). Host-tested only where a Vulkan device exists.
//
// Contract:
//  * Input is packed RGBA8 bytes (R,G,B,A per pixel, row-major, tightly packed).
//  * The shader writes an RGBA32F storage image (binary32 per channel) with Rec.709 exposure applied.
//    No clamp happens on the GPU; values above 1.0 survive. Display conversion is the caller's job.
//  * Matches editor::color::fp32::rec709_exposure up to float rounding of pow() on the device.
//  * If no Vulkan device with the required features exists, the constructor throws NoVulkanDevice.
//    Callers must treat that as "GPU path unavailable" and must not silently substitute another path.
//  * This is NOT wired into the Android export or preview path yet.

#include "editor/color_fp32.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace editor::color::vulkan {

class NoVulkanDevice : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ExposureCompute final {
public:
    // Throws NoVulkanDevice when no compute-capable device with RGBA32F storage images is present.
    ExposureCompute();
    ~ExposureCompute();
    ExposureCompute(const ExposureCompute&) = delete;
    ExposureCompute& operator=(const ExposureCompute&) = delete;

    // Requires rgba8.size() == width*height*4, width and height > 0, stops finite in [-8, 8].
    std::vector<fp32::RgbaF32> render(std::span<const std::uint8_t> rgba8, std::uint32_t width,
                                      std::uint32_t height, float exposure_stops);

    const std::string& device_name() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace editor::color::vulkan
