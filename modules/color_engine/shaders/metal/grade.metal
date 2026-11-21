#include <metal_stdlib>
using namespace metal;
#define PARAM_ARG , device const float* parameters
#define PARAM_PASS , parameters
#define TABLE_ARG , device const float4* samples
#define TABLE_PASS , samples
#define P(i) parameters[i]
#define SAMPLE(i) samples[i]
#define AS_UINT(x) as_type<uint>(x)
#include "../common/math.inc"
#include "../common/grade.inc"
struct Meta { uint count,width,op,frame,edge,curve_n,input_count,reserved;float key_input,key_output,node_mix;uint invert; };
kernel void color_grade(device const float4* pixels [[buffer(0)]],device float4* output_pixels [[buffer(1)]],device const float* parameters [[buffer(2)]],device const float4* samples [[buffer(3)]],constant Meta& meta [[buffer(4)]],uint i [[thread_position_in_grid]]) {
 if(i>=meta.count) return;uint x=i%meta.width,y=i/meta.width,height=(meta.count+meta.width-1u)/meta.width;
 float4 base=pixels[i],result=base;
 if((meta.op<=10u||meta.op==16u||meta.op==17u||meta.op==22u)) result=grade(base,meta.op,meta.frame,meta.edge,meta.curve_n,x,y,(float(x)+.5f)/float(meta.width),(float(y)+.5f)/float(height),samples,parameters);
 else if(meta.op==11u) { result=float4(0);for(uint j=0u;j<meta.input_count;++j) result+=pixels[i+j*meta.count];result/=float(meta.input_count); }
 else if(meta.op==12u) for(uint j=1u;j<meta.input_count;++j) result=source_over(result,pixels[i+j*meta.count]);
 else if(meta.op==13u) result=mix(base,pixels[i+2u*meta.count],1.0f-unit(pixels[i+meta.count].a));
 else if(meta.op==14u) result=float4(float3(base[uint(P(0))]),base.a);
 else if(meta.op==15u) result=float4(base.r,pixels[i+meta.count].g,pixels[i+2u*meta.count].b,base.a);
 else if(meta.op==21u) result=pixels[i+meta.count];
 else if(meta.op==18u) result.a=max(base.a,pixels[i+meta.count].a);
 else if(meta.op==19u) result.a=min(base.a,pixels[i+meta.count].a);
 else if(meta.op==20u) result.a=max(0.0f,base.a-pixels[i+meta.count].a);
 float key=meta.key_input*meta.key_output;if(meta.invert!=0u) key=1.0f-key;
 output_pixels[i]=mix(base,result,meta.node_mix*key);
}
