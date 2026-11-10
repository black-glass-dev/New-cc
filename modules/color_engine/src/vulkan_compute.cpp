#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "color/vulkan_compute.hpp"
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

namespace color {
namespace {
void require(bool b,const char* s) {if(!b) throw std::invalid_argument(s);}
void success(VkResult result,const char* what) {if(result!=VK_SUCCESS) throw std::runtime_error(std::string(what)+": VkResult="+std::to_string(result));}
#define INSTANCE_FUNCTIONS(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) X(CmdPipelineBarrier) X(QueueSubmit) X(QueueWaitIdle) X(DeviceWaitIdle)
struct Functions {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr{};PFN_vkCreateInstance CreateInstance{};
#define FIELD(name) PFN_vk##name name{};
    INSTANCE_FUNCTIONS(FIELD) DEVICE_FUNCTIONS(FIELD)
#undef FIELD
};
class VulkanCompute final:public IVulkanCompute {
    Functions f_;void* loader_{};VkInstance instance_{};VkDevice device_{};VkPhysicalDevice physical_{};VkQueue queue_{};std::uint32_t family_{};
    VkPhysicalDeviceMemoryProperties memory_{};VkPhysicalDeviceProperties properties_{};std::size_t budget_{};std::mutex mutex_;
    void close() noexcept {if(device_&&f_.DestroyDevice) {if(f_.DeviceWaitIdle) (void)f_.DeviceWaitIdle(device_);f_.DestroyDevice(device_,nullptr);}if(instance_&&f_.DestroyInstance) f_.DestroyInstance(instance_,nullptr);if(loader_) dlclose(loader_);device_=VK_NULL_HANDLE;instance_=VK_NULL_HANDLE;loader_=nullptr;}
    std::uint32_t memory_type(std::uint32_t bits) const {
        for(std::uint32_t i=0;i<memory_.memoryTypeCount;++i) if((bits&(1U<<i))&&(memory_.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) return i;
        throw GpuUnavailable("No host-visible coherent Vulkan storage memory type");
    }
    struct Resources {
        VulkanCompute& c;VkShaderModule shader{};VkDescriptorSetLayout descriptor_layout{};VkPipelineLayout pipeline_layout{};VkPipeline pipeline{};VkDescriptorPool descriptor_pool{};VkCommandPool command_pool{};
        std::vector<VkBuffer> buffers;std::vector<VkDeviceMemory> memories;
        explicit Resources(VulkanCompute& context):c(context) {}
        ~Resources() {auto& f=c.f_;const auto d=c.device_;if(command_pool) f.DestroyCommandPool(d,command_pool,nullptr);if(descriptor_pool) f.DestroyDescriptorPool(d,descriptor_pool,nullptr);if(pipeline) f.DestroyPipeline(d,pipeline,nullptr);if(pipeline_layout) f.DestroyPipelineLayout(d,pipeline_layout,nullptr);if(descriptor_layout) f.DestroyDescriptorSetLayout(d,descriptor_layout,nullptr);if(shader) f.DestroyShaderModule(d,shader,nullptr);for(auto b:buffers) if(b) f.DestroyBuffer(d,b,nullptr);for(auto m:memories) if(m) f.FreeMemory(d,m,nullptr);}
    };
public:
    explicit VulkanCompute(std::size_t budget):budget_(budget) {
        require(budget>=4096,"Vulkan memory budget too small");
        try {
            loader_=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);if(!loader_) loader_=dlopen("libvulkan.so",RTLD_NOW|RTLD_LOCAL);if(!loader_) throw GpuUnavailable("Vulkan loader is not installed");
            f_.GetInstanceProcAddr=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader_,"vkGetInstanceProcAddr"));if(!f_.GetInstanceProcAddr) throw GpuUnavailable("Invalid Vulkan loader");
            f_.CreateInstance=reinterpret_cast<PFN_vkCreateInstance>(f_.GetInstanceProcAddr(VK_NULL_HANDLE,"vkCreateInstance"));if(!f_.CreateInstance) throw GpuUnavailable("Vulkan create-instance unavailable");
            VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.pApplicationName="OwnColorEngine";app.apiVersion=VK_API_VERSION_1_1;
            VkInstanceCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ci.pApplicationInfo=&app;success(f_.CreateInstance(&ci,nullptr,&instance_),"vkCreateInstance");
#define LOAD_INSTANCE(name) f_.name=reinterpret_cast<PFN_vk##name>(f_.GetInstanceProcAddr(instance_,"vk" #name));if(!f_.name) throw std::runtime_error("Missing Vulkan instance entry: " #name);
            INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
            std::uint32_t count=0;success(f_.EnumeratePhysicalDevices(instance_,&count,nullptr),"vkEnumeratePhysicalDevices");if(!count) throw GpuUnavailable("No Vulkan physical device");require(count<=64,"Vulkan device count budget");std::vector<VkPhysicalDevice> physical(count);success(f_.EnumeratePhysicalDevices(instance_,&count,physical.data()),"vkEnumeratePhysicalDevices");
            for(auto candidate:physical) {VkPhysicalDeviceProperties candidate_properties{};f_.GetPhysicalDeviceProperties(candidate,&candidate_properties);if(candidate_properties.apiVersion<VK_API_VERSION_1_1) continue;std::uint32_t n=0;f_.GetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);require(n<=256,"Vulkan queue count budget");std::vector<VkQueueFamilyProperties> queues(n);f_.GetPhysicalDeviceQueueFamilyProperties(candidate,&n,queues.data());for(std::uint32_t q=0;q<n;++q) if(queues[q].queueCount&&(queues[q].queueFlags&VK_QUEUE_COMPUTE_BIT)) {physical_=candidate;family_=q;break;}if(physical_) break;}
            if(!physical_) throw GpuUnavailable("No Vulkan compute queue");
            f_.GetPhysicalDeviceMemoryProperties(physical_,&memory_);f_.GetPhysicalDeviceProperties(physical_,&properties_);
            float priority=1;VkDeviceQueueCreateInfo qi{};qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;qi.queueFamilyIndex=family_;qi.queueCount=1;qi.pQueuePriorities=&priority;
            VkDeviceCreateInfo di{};di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;success(f_.CreateDevice(physical_,&di,nullptr,&device_),"vkCreateDevice");
#define LOAD_DEVICE(name) f_.name=reinterpret_cast<PFN_vk##name>(f_.GetDeviceProcAddr(device_,"vk" #name));if(!f_.name) throw std::runtime_error("Missing Vulkan device entry: " #name);
            DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
            f_.GetDeviceQueue(device_,family_,0,&queue_);
        }catch(...) {close();throw;}
    }
    ~VulkanCompute() override {close();}
    std::string device_name() const override {return properties_.deviceName;}
    void dispatch(const ComputeDispatch& job) override {
        std::lock_guard<std::mutex> lock(mutex_);const auto& limits=properties_.limits;
        require(job.spirv.size()>=5&&job.spirv.size()<=2U*1024U*1024U&&job.spirv[0]==0x07230203U,"Invalid bundled SPIR-V module");
        require(job.buffers.size()>=1&&job.buffers.size()<=8&&job.buffers.size()<=limits.maxPerStageDescriptorStorageBuffers&&job.push_constants.size()%4==0&&job.push_constants.size()<=std::min(std::size_t{128},static_cast<std::size_t>(limits.maxPushConstantsSize))&&job.work_items>0&&limits.maxComputeWorkGroupInvocations>=64,"Invalid compute dispatch dimensions/layout");
        const auto groups=static_cast<std::uint32_t>((static_cast<std::uint64_t>(job.work_items)+63)/64);require(groups<=limits.maxComputeWorkGroupCount[0],"Compute grid exceeds device limit");
        std::size_t requested=0;for(const auto& b:job.buffers) {require(static_cast<unsigned>(b.access)<=2&&!b.bytes.empty()&&b.bytes.size()<=limits.maxStorageBufferRange&&b.bytes.size()<=budget_-requested,"Compute buffer exceeds budget/device range");requested+=b.bytes.size();}
        Resources r{*this};const auto n=static_cast<std::uint32_t>(job.buffers.size());r.buffers.resize(n);r.memories.resize(n);std::vector<VkDescriptorBufferInfo> buffer_info(n);std::vector<VkDescriptorSetLayoutBinding> bindings(n);std::size_t allocated=0;
        for(std::uint32_t i=0;i<n;++i) {
            VkBufferCreateInfo bi{};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=job.buffers[i].bytes.size();bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;success(f_.CreateBuffer(device_,&bi,nullptr,&r.buffers[i]),"vkCreateBuffer");
            VkMemoryRequirements requirements{};f_.GetBufferMemoryRequirements(device_,r.buffers[i],&requirements);require(requirements.size<=budget_-allocated,"Aligned Vulkan allocations exceed budget");allocated+=static_cast<std::size_t>(requirements.size);
            VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=requirements.size;ai.memoryTypeIndex=memory_type(requirements.memoryTypeBits);success(f_.AllocateMemory(device_,&ai,nullptr,&r.memories[i]),"vkAllocateMemory");success(f_.BindBufferMemory(device_,r.buffers[i],r.memories[i],0),"vkBindBufferMemory");
            void* mapped=nullptr;success(f_.MapMemory(device_,r.memories[i],0,bi.size,0,&mapped),"vkMapMemory(upload)");if(job.buffers[i].access==BufferAccess::Output) std::memset(mapped,0,job.buffers[i].bytes.size());else std::memcpy(mapped,job.buffers[i].bytes.data(),job.buffers[i].bytes.size());f_.UnmapMemory(device_,r.memories[i]);
            buffer_info[i]={r.buffers[i],0,bi.size};bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        }
        VkShaderModuleCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;si.codeSize=job.spirv.size_bytes();si.pCode=job.spirv.data();success(f_.CreateShaderModule(device_,&si,nullptr,&r.shader),"vkCreateShaderModule");
        VkDescriptorSetLayoutCreateInfo li{};li.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;li.bindingCount=n;li.pBindings=bindings.data();success(f_.CreateDescriptorSetLayout(device_,&li,nullptr,&r.descriptor_layout),"vkCreateDescriptorSetLayout");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,static_cast<std::uint32_t>(job.push_constants.size())};VkPipelineLayoutCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pi.setLayoutCount=1;pi.pSetLayouts=&r.descriptor_layout;if(!job.push_constants.empty()) {pi.pushConstantRangeCount=1;pi.pPushConstantRanges=&push;}success(f_.CreatePipelineLayout(device_,&pi,nullptr,&r.pipeline_layout),"vkCreatePipelineLayout");
        VkComputePipelineCreateInfo pci{};pci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pci.layout=r.pipeline_layout;pci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pci.stage.module=r.shader;pci.stage.pName="main";success(f_.CreateComputePipelines(device_,VK_NULL_HANDLE,1,&pci,nullptr,&r.pipeline),"vkCreateComputePipelines");
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,n};VkDescriptorPoolCreateInfo dpi{};dpi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;dpi.maxSets=1;dpi.poolSizeCount=1;dpi.pPoolSizes=&pool_size;success(f_.CreateDescriptorPool(device_,&dpi,nullptr,&r.descriptor_pool),"vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo dai{};dai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;dai.descriptorPool=r.descriptor_pool;dai.descriptorSetCount=1;dai.pSetLayouts=&r.descriptor_layout;VkDescriptorSet descriptor{};success(f_.AllocateDescriptorSets(device_,&dai,&descriptor),"vkAllocateDescriptorSets");std::vector<VkWriteDescriptorSet> writes(n);
        for(std::uint32_t i=0;i<n;++i) {writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=descriptor;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffer_info[i];}f_.UpdateDescriptorSets(device_,n,writes.data(),0,nullptr);
        VkCommandPoolCreateInfo cpi{};cpi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cpi.queueFamilyIndex=family_;success(f_.CreateCommandPool(device_,&cpi,nullptr,&r.command_pool),"vkCreateCommandPool");VkCommandBufferAllocateInfo cai{};cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;cai.commandPool=r.command_pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;VkCommandBuffer command{};success(f_.AllocateCommandBuffers(device_,&cai,&command),"vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;success(f_.BeginCommandBuffer(command,&begin),"vkBeginCommandBuffer");f_.CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipeline);f_.CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,r.pipeline_layout,0,1,&descriptor,0,nullptr);if(!job.push_constants.empty()) f_.CmdPushConstants(command,r.pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,push.size,job.push_constants.data());f_.CmdDispatch(command,groups,1,1);
        VkMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;f_.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);success(f_.EndCommandBuffer(command),"vkEndCommandBuffer");VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;success(f_.QueueSubmit(queue_,1,&submit,VK_NULL_HANDLE),"vkQueueSubmit");success(f_.QueueWaitIdle(queue_),"vkQueueWaitIdle");
        std::vector<std::vector<std::byte>> outputs(n);for(std::uint32_t i=0;i<n;++i) if(job.buffers[i].access!=BufferAccess::Input) {outputs[i].resize(job.buffers[i].bytes.size());void* mapped=nullptr;success(f_.MapMemory(device_,r.memories[i],0,outputs[i].size(),0,&mapped),"vkMapMemory(readback)");std::memcpy(outputs[i].data(),mapped,outputs[i].size());f_.UnmapMemory(device_,r.memories[i]);}
        for(std::uint32_t i=0;i<n;++i) if(!outputs[i].empty()) std::copy(outputs[i].begin(),outputs[i].end(),job.buffers[i].bytes.begin());
    }
};
}
std::unique_ptr<IVulkanCompute> create_vulkan_compute(std::size_t budget) {return std::make_unique<VulkanCompute>(budget);}
} // namespace color
