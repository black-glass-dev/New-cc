#include "editor/color_vulkan.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

#include "../shaders/generated/exposure_fp32_spv.inc"

namespace editor::color::vulkan {
namespace {

constexpr std::uint32_t kWorkgroupSize = 8; // must match local_size_x/y in exposure_fp32.comp
constexpr std::uint32_t kPushConstantBytes = 16;
constexpr VkFormat kFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

[[noreturn]] void fail(const char* what, VkResult result) {
    std::ostringstream message;
    message << what << " failed with VkResult " << static_cast<int>(result);
    throw std::runtime_error(message.str());
}

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) fail(what, result);
}

std::uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties& props, std::uint32_t type_bits,
                               VkMemoryPropertyFlags required) {
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const bool allowed = (type_bits & (1u << i)) != 0;
        const bool matches = (props.memoryTypes[i].propertyFlags & required) == required;
        if (allowed && matches) return i;
    }
    throw std::runtime_error("no host-visible coherent memory type for Vulkan buffer");
}

// Every handle created during one render() call. Destroyed in reverse order, also on error paths.
struct Scratch {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer input = VK_NULL_HANDLE, readback = VK_NULL_HANDLE;
    VkDeviceMemory input_memory = VK_NULL_HANDLE, readback_memory = VK_NULL_HANDLE, image_memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;

    Scratch() = default;
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    ~Scratch() {
        if (!device) return;
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (command && pool) vkFreeCommandBuffers(device, pool, 1, &command);
        if (view) vkDestroyImageView(device, view, nullptr);
        if (image) vkDestroyImage(device, image, nullptr);
        if (input) vkDestroyBuffer(device, input, nullptr);
        if (readback) vkDestroyBuffer(device, readback, nullptr);
        if (input_memory) vkFreeMemory(device, input_memory, nullptr);
        if (readback_memory) vkFreeMemory(device, readback_memory, nullptr);
        if (image_memory) vkFreeMemory(device, image_memory, nullptr);
    }
};

void create_buffer(VkDevice device, const VkPhysicalDeviceMemoryProperties& mem, VkDeviceSize size,
                   VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &info, nullptr, &buffer), "vkCreateBuffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = find_memory_type(mem, req.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device, &alloc, nullptr, &memory), "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
}

} // namespace

struct ExposureCompute::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceLimits limits{};
    std::string name;

    Impl() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "owncut-color";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        const VkResult created = vkCreateInstance(&info, nullptr, &instance);
        if (created != VK_SUCCESS) {
            throw NoVulkanDevice("Vulkan instance unavailable (VkResult " + std::to_string(static_cast<int>(created)) + ")");
        }
        try {
            setup();
        } catch (...) {
            destroy();
            throw;
        }
    }

    ~Impl() { destroy(); }

    void destroy() noexcept {
        if (device) {
            vkDeviceWaitIdle(device);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (shader) vkDestroyShaderModule(device, shader, nullptr);
            if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
        device = VK_NULL_HANDLE;
        instance = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        shader = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        set_layout = VK_NULL_HANDLE;
        pool = VK_NULL_HANDLE;
    }

    void setup() {
        std::uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> devices(count);
        if (count) check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");

        std::uint32_t family = 0;
        bool found = false;
        for (VkPhysicalDevice candidate : devices) {
            std::uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> props(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());
            VkFormatProperties format{};
            vkGetPhysicalDeviceFormatProperties(candidate, kFormat, &format);
            const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
            if ((format.optimalTilingFeatures & needed) != needed) continue;
            for (std::uint32_t i = 0; i < families && !found; ++i) {
                if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    physical = candidate;
                    family = i;
                    found = true;
                }
            }
            if (found) break;
        }
        if (!found) throw NoVulkanDevice("no Vulkan compute device with RGBA32F storage image support");

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physical, &props);
        name = props.deviceName;
        limits = props.limits;
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        check(vkCreateDevice(physical, &device_info, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, family, 0, &queue);

        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "vkCreateCommandPool");

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = 2;
        layout_info.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set_layout), "vkCreateDescriptorSetLayout");

        VkPushConstantRange range{};
        range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        range.size = kPushConstantBytes;
        VkPipelineLayoutCreateInfo pipeline_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_info.setLayoutCount = 1;
        pipeline_info.pSetLayouts = &set_layout;
        pipeline_info.pushConstantRangeCount = 1;
        pipeline_info.pPushConstantRanges = &range;
        check(vkCreatePipelineLayout(device, &pipeline_info, nullptr, &pipeline_layout), "vkCreatePipelineLayout");

        VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = 1;
        dp.poolSizeCount = 2;
        dp.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device, &dp, nullptr, &descriptor_pool), "vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo ds{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ds.descriptorPool = descriptor_pool;
        ds.descriptorSetCount = 1;
        ds.pSetLayouts = &set_layout;
        check(vkAllocateDescriptorSets(device, &ds, &descriptor_set), "vkAllocateDescriptorSets");

        VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader_info.codeSize = sizeof(generated::kExposureFp32Spirv);
        shader_info.pCode = generated::kExposureFp32Spirv;
        check(vkCreateShaderModule(device, &shader_info, nullptr, &shader), "vkCreateShaderModule");

        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = shader;
        cp.stage.pName = "main";
        cp.layout = pipeline_layout;
        check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &pipeline), "vkCreateComputePipelines");
    }
};

ExposureCompute::ExposureCompute() : impl_(std::make_unique<Impl>()) {}
ExposureCompute::~ExposureCompute() = default;

const std::string& ExposureCompute::device_name() const noexcept { return impl_->name; }

std::vector<fp32::RgbaF32> ExposureCompute::render(std::span<const std::uint8_t> rgba8, std::uint32_t width,
                                                   std::uint32_t height, float exposure_stops) {
    if (width == 0 || height == 0) throw std::invalid_argument("render: width and height must be positive");
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (rgba8.size() != pixels * 4) throw std::invalid_argument("render: input size must be width*height*4 bytes");
    if (!std::isfinite(exposure_stops) || exposure_stops < -8.0f || exposure_stops > 8.0f) {
        throw std::invalid_argument("render: exposure stops must be finite within [-8, 8]");
    }
    if (width > impl_->limits.maxImageDimension2D || height > impl_->limits.maxImageDimension2D) {
        throw std::invalid_argument("render: image exceeds device maxImageDimension2D");
    }
    const std::uint64_t input_bytes = pixels * 4;
    const std::uint64_t output_bytes = pixels * sizeof(fp32::RgbaF32);
    if (input_bytes > impl_->limits.maxStorageBufferRange || input_bytes > 0xFFFFFFFFull) {
        throw std::invalid_argument("render: input exceeds device maxStorageBufferRange");
    }

    Impl& d = *impl_;
    Scratch s;
    s.device = d.device;

    create_buffer(d.device, d.memory, input_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, s.input, s.input_memory);
    create_buffer(d.device, d.memory, output_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, s.readback, s.readback_memory);
    {
        void* mapped = nullptr;
        check(vkMapMemory(d.device, s.input_memory, 0, input_bytes, 0, &mapped), "vkMapMemory(input)");
        std::memcpy(mapped, rgba8.data(), static_cast<std::size_t>(input_bytes));
        vkUnmapMemory(d.device, s.input_memory);
    }

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = kFormat;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(d.device, &image_info, nullptr, &s.image), "vkCreateImage");
    VkMemoryRequirements image_req{};
    vkGetImageMemoryRequirements(d.device, s.image, &image_req);
    VkMemoryAllocateInfo image_alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    image_alloc.allocationSize = image_req.size;
    image_alloc.memoryTypeIndex = find_memory_type(d.memory, image_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(d.device, &image_alloc, nullptr, &s.image_memory), "vkAllocateMemory(image)");
    check(vkBindImageMemory(d.device, s.image, s.image_memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = s.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = kFormat;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(d.device, &view_info, nullptr, &s.view), "vkCreateImageView");

    VkDescriptorBufferInfo buffer_desc{s.input, 0, input_bytes};
    VkDescriptorImageInfo image_desc{VK_NULL_HANDLE, s.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = d.descriptor_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &buffer_desc;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = d.descriptor_set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &image_desc;
    vkUpdateDescriptorSets(d.device, 2, writes, 0, nullptr);

    VkCommandBufferAllocateInfo cb_alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cb_alloc.commandPool = d.pool;
    cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(d.device, &cb_alloc, &s.command), "vkAllocateCommandBuffers");
    s.pool = d.pool; // so ~Scratch returns the command buffer to the pool

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(s.command, &begin), "vkBeginCommandBuffer");

    VkImageSubresourceRange color{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier to_general{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_general.srcAccessMask = 0;
    to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_general.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.image = s.image;
    to_general.subresourceRange = color;
    vkCmdPipelineBarrier(s.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &to_general);

    vkCmdBindPipeline(s.command, VK_PIPELINE_BIND_POINT_COMPUTE, d.pipeline);
    vkCmdBindDescriptorSets(s.command, VK_PIPELINE_BIND_POINT_COMPUTE, d.pipeline_layout, 0, 1,
                            &d.descriptor_set, 0, nullptr);
    struct PushConstants {
        std::uint32_t width;
        std::uint32_t height;
        float exposure_scale;
        float reserved;
    } push{width, height, std::exp2(exposure_stops), 0.0f};
    static_assert(sizeof(PushConstants) == kPushConstantBytes);
    vkCmdPushConstants(s.command, d.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(s.command, (width + kWorkgroupSize - 1) / kWorkgroupSize,
                  (height + kWorkgroupSize - 1) / kWorkgroupSize, 1);

    VkImageMemoryBarrier to_copy = to_general;
    to_copy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_copy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_copy.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(s.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &to_copy);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(s.command, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.readback, 1, &region);

    VkBufferMemoryBarrier to_host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    to_host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_host.buffer = s.readback;
    to_host.offset = 0;
    to_host.size = output_bytes;
    vkCmdPipelineBarrier(s.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                         1, &to_host, 0, nullptr);
    check(vkEndCommandBuffer(s.command), "vkEndCommandBuffer");

    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(d.device, &fence_info, nullptr, &s.fence), "vkCreateFence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &s.command;
    check(vkQueueSubmit(d.queue, 1, &submit, s.fence), "vkQueueSubmit");
    check(vkWaitForFences(d.device, 1, &s.fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
          "vkWaitForFences");

    std::vector<fp32::RgbaF32> out(static_cast<std::size_t>(pixels));
    void* mapped = nullptr;
    check(vkMapMemory(d.device, s.readback_memory, 0, output_bytes, 0, &mapped), "vkMapMemory(readback)");
    std::memcpy(out.data(), mapped, static_cast<std::size_t>(output_bytes));
    vkUnmapMemory(d.device, s.readback_memory);
    return out;
}

} // namespace editor::color::vulkan
