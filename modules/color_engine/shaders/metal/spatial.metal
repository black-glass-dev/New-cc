#include <metal_stdlib>
using namespace metal;
#define SPATIAL_ARG ,device const float4* source,device const float4* history,device const float* parameters,uint stride
#define SPATIAL_PASS ,source,history,parameters,stride
#define SP(i) parameters[i]
float4 source_pixel(uint x,uint y SPATIAL_ARG) { return source[y*stride+x]; }
float4 history_pixel(uint x,uint y SPATIAL_ARG) { return history[y*stride+x]; }
#include "../common/math.inc"
#include "../common/spatial.inc"
struct SpatialMeta { uint width,height,kind,radius; };
kernel void color_spatial(device const float4* source [[buffer(0)]],device const float4* history [[buffer(1)]],device float4* output_pixels [[buffer(2)]],device const float* parameters [[buffer(3)]],constant SpatialMeta& m [[buffer(4)]],uint i [[thread_position_in_grid]]) {
 if(i>=m.width*m.height) return;output_pixels[i]=spatial_grade(i%m.width,i/m.width,m.width,m.height,m.kind,m.radius,source,history,parameters,m.width);
}
