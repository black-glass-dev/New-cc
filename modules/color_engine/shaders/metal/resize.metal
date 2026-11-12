#include <metal_stdlib>
using namespace metal;
#define RESIZE_ARG ,device const float4* source,device const float* matrix
#define RM(i) matrix[i]
#define RS(x,y) source[(y)*sw+(x)]
#include "../common/resize.inc"
struct ResizeMeta { uint sw,sh,dw,dh; };
kernel void color_resize(device const float4* source [[buffer(0)]],device float4* output_pixels [[buffer(1)]],device const float* matrix [[buffer(2)]],constant ResizeMeta& m [[buffer(3)]],uint i [[thread_position_in_grid]]) {
 if(i>=m.dw*m.dh) return;output_pixels[i]=resize_pixel(i%m.dw,i/m.dw,m.sw,m.sh,source,matrix);
}
