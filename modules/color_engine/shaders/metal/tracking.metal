#include <metal_stdlib>
using namespace metal;
#define KLT_ARG ,device const float4* previous,device const float4* current
#define KLT_PASS ,previous,current
#define KLT_READ(x,y,first) ((first)?previous[(y)*width+(x)]:current[(y)*width+(x)])
#include "../common/math.inc"
#include "../common/tracking.inc"
struct TrackingMeta { uint width,height,count,radius,iterations;float max_error,convergence;uint reserved; };
kernel void color_track(device const float4* previous [[buffer(0)]],device const float4* current [[buffer(1)]],device const float4* features [[buffer(2)]],device float4* tracks [[buffer(3)]],constant TrackingMeta& m [[buffer(4)]],uint i [[thread_position_in_grid]]) {
 if(i>=m.count) return;tracks[i]=klt_track(features[i],m.width,m.height,m.radius,m.iterations,m.max_error,m.convergence,previous,current);
}
