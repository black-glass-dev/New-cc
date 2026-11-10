#include "color/vulkan_compute.hpp"
#include "color/color_pipeline_api.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
using namespace color;
int main() {
    try {
        auto gpu=create_vulkan_compute();std::cout<<"Vulkan device: "<<gpu->device_name()<<'\n';
        std::ifstream shader(COLOR_GRADE_SPIRV,std::ios::binary|std::ios::ate);if(!shader) throw std::runtime_error("Missing compiled grade SPIR-V");const auto size=shader.tellg();if(size<=0||size%4!=0) throw std::runtime_error("Malformed SPIR-V file");std::vector<std::uint32_t> spirv(static_cast<std::size_t>(size)/4);shader.seekg(0);shader.read(reinterpret_cast<char*>(spirv.data()),static_cast<std::streamsize>(size));
        std::array<Pixel,16> source{},target{},reference{};for(std::size_t i=0;i<source.size();++i) source[i]={static_cast<float>(i)/16,.18F,.3F,.75F};reference=source;
        std::array<float,21> parameters{};parameters[3]=parameters[4]=parameters[5]=parameters[6]=parameters[7]=parameters[8]=1;parameters[12]=.1F;parameters[13]=1;parameters[14]=.18F;parameters[15]=1;
        std::array<Pixel,1> dummy{};std::array<std::uint32_t,12> meta{16,4,1,0,2,2,1,0,std::bit_cast<std::uint32_t>(1.F),std::bit_cast<std::uint32_t>(1.F),std::bit_cast<std::uint32_t>(1.F),0};
        for(int node=0;node<8;++node) {
            std::array<ComputeBuffer,4> buffers{{{std::as_writable_bytes(std::span(source)),BufferAccess::Input},{std::as_writable_bytes(std::span(target)),BufferAccess::Output},{std::as_writable_bytes(std::span(parameters)),BufferAccess::Input},{std::as_writable_bytes(std::span(dummy)),BufferAccess::Input}}};
            gpu->dispatch({spirv,buffers,std::as_bytes(std::span(meta)),16});Primaries p;p.exposure=.1F;
            for(std::size_t i=0;i<source.size();++i) {auto c=apply(Vec3{reference[i].r,reference[i].g,reference[i].b},p);reference[i]={c.r,c.g,c.b,reference[i].a};if(std::abs(target[i].r-c.r)>1e-5F||std::abs(target[i].g-c.g)>1e-5F||std::abs(target[i].b-c.b)>1e-5F||target[i].a!=reference[i].a) throw std::runtime_error("Actual GPU/CPU parity mismatch");}source=target;
        }
        std::cout<<"PASS:8 actual Vulkan compute submissions + readbacks vs CPU <=1e-5\n";return 0;
    }catch(const GpuUnavailable& e) {std::cout<<"UNRUN: "<<e.what()<<'\n';return 77;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
