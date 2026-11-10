#include <metal_stdlib>
using namespace metal;
#include "../common/math.inc"
struct ScopeMeta { uint count,width,bins,columns;float low,high;uint transfer,reserved; };
uint bin_of(constant ScopeMeta& m,float x) { float span=m.high-m.low;float t=isinf(span)?(x*.5f-m.low*.5f)/(m.high*.5f-m.low*.5f):(x-m.low)/span;return min(uint(clamp(t,0.0f,1.0f)*float(m.bins)),m.bins-1u); }
kernel void color_scopes(device const float4* source [[buffer(0)]],device atomic_uint* counters [[buffer(1)]],device const float* xyz_matrix [[buffer(2)]],constant ScopeMeta& m [[buffer(3)]],uint index [[thread_position_in_grid]]) {
 uint i=index;if(i>=m.count) return;float3 v=source[i].rgb;float l=y709(v);uint x=i%m.width,col=min(x*m.columns/m.width,m.columns-1u),wave=m.bins*m.columns,grid=m.bins*m.bins;
 for(uint c=0u;c<4u;++c) atomic_fetch_add_explicit(&counters[c*m.bins+bin_of(m,c==3u?l:v[c])],1u,memory_order_relaxed);
 uint start=4u*m.bins;
 for(uint c=0u;c<3u;++c) atomic_fetch_add_explicit(&counters[start+c*wave+col*m.bins+bin_of(m,v[c])],1u,memory_order_relaxed);
 atomic_fetch_add_explicit(&counters[start+3u*wave+col*m.bins+bin_of(m,l)],1u,memory_order_relaxed);
 atomic_fetch_add_explicit(&counters[start+4u*wave+bin_of(m,(v.r/1.5748-l/1.5748)f+.5f)*m.bins+bin_of(m,(v.b/1.8556-l/1.8556)f+.5f)],1u,memory_order_relaxed);
 v=float3(transfer_decode(v.r,m.transfer),transfer_decode(v.g,m.transfer),transfer_decode(v.b,m.transfer));
 v/=max(max(abs(v.r),max(abs(v.g),abs(v.b))),1e-30f);
 float3 xyz=float3(dot(v,float3(xyz_matrix[0],xyz_matrix[1],xyz_matrix[2])),dot(v,float3(xyz_matrix[3],xyz_matrix[4],xyz_matrix[5])),dot(v,float3(xyz_matrix[6],xyz_matrix[7],xyz_matrix[8])));float total=xyz.x+xyz.y+xyz.z;
 if(total>0.0f&&min(xyz.x,min(xyz.y,xyz.z))>=0.0f) atomic_fetch_add_explicit(&counters[start+4u*wave+grid+bin_of(m,xyz.y/total)*m.bins+bin_of(m,xyz.x/total)],1u,memory_order_relaxed);
 else atomic_fetch_add_explicit(&counters[start+4u*wave+2u*grid],1u,memory_order_relaxed);
}
