#include <metal_stdlib>
using namespace metal;
struct HalationMeta { uint width,height,radius,phase;float threshold,amount; };
kernel void color_halation(device const float4* source [[buffer(0)]],device const float* horizontal [[buffer(1)]],device float4* output_pixels [[buffer(2)]],device float* output_horizontal [[buffer(3)]],constant HalationMeta& m [[buffer(4)]],uint i [[thread_position_in_grid]]) {
 if(i>=m.width*m.height) return;int x=int(i%m.width),y=int(i/m.width);float sigma=max(1.0f,float(m.radius)/2.0f),sum=0.0f,value=0.0f;
 for(int k=-int(m.radius);k<=int(m.radius);++k) {
  float w=exp(-float(k*k)/(2.0f*sigma*sigma));sum+=w;
  if(m.phase==0u) { int xx=clamp(x+k,0,int(m.width)-1);value+=max(dot(source[uint(y)*m.width+uint(xx)].rgb,float3(.2126f,.7152f,.0722f))-m.threshold,0.0f)*w; }
  else { int yy=clamp(y+k,0,int(m.height)-1);value+=horizontal[uint(yy)*m.width+uint(x)]*w; }
 }
 if(m.phase==0u) output_horizontal[i]=value/sum;
 else output_pixels[i]=source[i]+float4(value/sum*m.amount*float3(1,.25,0),0);
}
