#include <metal_stdlib>
using namespace metal;
#define RAW_ARG ,device const float* mosaic,device const float* parameters,uint stride
#define RAW(x,y) mosaic[(y)*stride+(x)]
#define RP(i) parameters[i]
#include "../common/demosaic.inc"
struct DemosaicMeta { uint width,height,stride,pattern; };
kernel void color_demosaic(device const float* mosaic [[buffer(0)]],device float4* output_pixels [[buffer(1)]],device const float* parameters [[buffer(2)]],constant DemosaicMeta& m [[buffer(3)]],uint i [[thread_position_in_grid]]) {
 if(i>=m.width*m.height) return;output_pixels[i]=float4(demosaic(i%m.width,i/m.width,m.width,m.height,m.pattern,mosaic,parameters,m.stride),1);
}
