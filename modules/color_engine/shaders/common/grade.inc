// PARAM_ARG/TABLE_ARG are explicit Metal pointers or GLSL SSBO globals.
float3 p3(uint i PARAM_ARG) { return float3(P(i),P(i+1u),P(i+2u)); }
float3 matrix_rgb(float3 c,uint start PARAM_ARG) { return float3(dot(p3(start PARAM_PASS),c),dot(p3(start+3u PARAM_PASS),c),dot(p3(start+6u PARAM_PASS),c)); }
float3 lut_at(uint r,uint g,uint b,uint edge TABLE_ARG) { return SAMPLE((b*edge+g)*edge+r).rgb; }
float3 tetra(float3 v,uint edge TABLE_ARG PARAM_ARG) {
 float3 q=clamp((v-p3(0u PARAM_PASS))/(p3(3u PARAM_PASS)-p3(0u PARAM_PASS)),float3(0.0f),float3(1.0f))*float(edge-1u);
 uint r=min(uint(q.r),edge-2u),g=min(uint(q.g),edge-2u),b=min(uint(q.b),edge-2u);float3 f=q-float3(float(r),float(g),float(b));
 float3 a=lut_at(r,g,b,edge TABLE_PASS),z=lut_at(r+1u,g+1u,b+1u,edge TABLE_PASS),u,vv;
 if(f.r>=f.g) {
  if(f.g>=f.b) { u=lut_at(r+1u,g,b,edge TABLE_PASS);vv=lut_at(r+1u,g+1u,b,edge TABLE_PASS);return a+(u-a)*f.r+(vv-u)*f.g+(z-vv)*f.b; }
  if(f.r>=f.b) { u=lut_at(r+1u,g,b,edge TABLE_PASS);vv=lut_at(r+1u,g,b+1u,edge TABLE_PASS);return a+(u-a)*f.r+(vv-u)*f.b+(z-vv)*f.g; }
  u=lut_at(r,g,b+1u,edge TABLE_PASS);vv=lut_at(r+1u,g,b+1u,edge TABLE_PASS);return a+(u-a)*f.b+(vv-u)*f.r+(z-vv)*f.g;
 }
 if(f.r>=f.b) { u=lut_at(r,g+1u,b,edge TABLE_PASS);vv=lut_at(r+1u,g+1u,b,edge TABLE_PASS);return a+(u-a)*f.g+(vv-u)*f.r+(z-vv)*f.b; }
 if(f.g>=f.b) { u=lut_at(r,g+1u,b,edge TABLE_PASS);vv=lut_at(r,g+1u,b+1u,edge TABLE_PASS);return a+(u-a)*f.g+(vv-u)*f.b+(z-vv)*f.r; }
 u=lut_at(r,g,b+1u,edge TABLE_PASS);vv=lut_at(r,g+1u,b+1u,edge TABLE_PASS);return a+(u-a)*f.b+(vv-u)*f.g+(z-vv)*f.r;
}
float baked_curve(float x,uint channel,uint n TABLE_ARG) { float t=unit(x)*float(n-1u);uint i=min(uint(t),n-2u);return mix(SAMPLE(i)[channel],SAMPLE(i+1u)[channel],t-float(i)); }
float4 grade(float4 src,uint op,uint frame,uint edge,uint curve_n,uint x,uint y,float nx,float ny TABLE_ARG PARAM_ARG) {
 float3 v=src.rgb;
 if(op==0u) {
  uint ti=uint(P(0)),to=uint(P(1));v=float3(transfer_decode(v.r,ti),transfer_decode(v.g,ti),transfer_decode(v.b,ti));v=matrix_rgb(v,5u PARAM_PASS)*exp2(P(2));
  float lum=y709(v);if(P(3)>0.0f&&lum>P(3)) v*=(P(3)+(lum-P(3))/(1.0f+(lum-P(3))/P(3)))/lum;
  if(P(4)>0.0f) { lum=y709(v);float excess=max(0.0f,max(-min(v.r,min(v.g,v.b)),max(v.r,max(v.g,v.b))-1.0f));v=float3(lum)+(v-float3(lum))/(1.0f+P(4)*excess); }
  v=float3(transfer_encode(v.r,to),transfer_encode(v.g,to),transfer_encode(v.b,to));
 } else if(op==1u) {
  v=primaries(v,p3(0u PARAM_PASS),p3(3u PARAM_PASS),p3(6u PARAM_PASS),p3(9u PARAM_PASS),P(12),P(13),P(14),P(15),P(16),P(17),P(18),P(19),P(20));
 } else if(op==2u) {
  float lum=transfer_encode(max(y709(v),0.0f),4u),a=1.0f-soft(P(9)-P(11),P(9)+P(11),lum),b=soft(P(10)-P(11),P(10)+P(11),lum);
  float3 delta=p3(0u PARAM_PASS)*a+p3(3u PARAM_PASS)*max(0.0f,1.0f-a-b)+p3(6u PARAM_PASS)*b;
  if(delta.r!=0.0f||delta.g!=0.0f||delta.b!=0.0f) { v=float3(transfer_encode(v.r,4u),transfer_encode(v.g,4u),transfer_encode(v.b,4u))+delta;v=float3(transfer_decode(v.r,4u),transfer_decode(v.g,4u),transfer_decode(v.b,4u)); }
 } else if(op==3u) {
  float z=log2(max(y709(v),1e-12f)/.18f),total=0.0f,stops=0.0f;
  for(uint i=0u;i<6u;++i) { float q=(z-P(6u+i))/P(12),w=exp(-.5f*q*q);total+=w;stops+=P(i)*w; }if(total>0.0f) v*=exp2(stops/total);
  if(P(13)>0.0f) for(uint c=0u;c<3u;++c) if(v[c]>P(13)) v[c]=P(13)+(v[c]-P(13))/(1.0f+(v[c]-P(13))/P(13));
 } else if(op==4u) {
  float3 h=to_hsl(v);float hue=0.0f,saturation=0.0f,density=0.0f;
  for(uint i=0u;i<7u;++i) { float center=i==6u?.08f:float(i)/6.0f,dist=circular(h.r-center),w=pow(max(0.0f,1.0f-dist/(P(21)*.5f)),2.0f);hue+=P(i*3u)*w;saturation+=P(i*3u+1u)*w;density+=P(i*3u+2u)*w; }
  v=primaries(v,float3(0.0f),float3(1.0f),float3(1.0f),float3(0.0f),0.0f,1.0f,.18f,max(0.0f,1.0f+saturation),hue,0.0f,0.0f,0.0f,0.0f)*exp2(-density);
  v+=p3(22u PARAM_PASS)*(1.0f-h.b)+p3(25u PARAM_PASS)*h.b;
 } else if(op==5u) { v=matrix_rgb(v,0u PARAM_PASS);if(P(9)!=0.0f) v=float3(y709(v));
 } else if(op==6u) {
  float3 h=to_hsl(v);float m=band_mask(circular(h.r-P(0)),P(3)*.5f,P(6))*band_mask(abs(h.g-P(1)),P(4)*.5f,P(7))*band_mask(abs(h.b-P(2)),P(5)*.5f,P(8));if(P(9)!=0.0f) m=1.0f-m;return float4(v,src.a*m);
 } else if(op==7u) {
  float u=nx-P(1),w=ny-P(2),qx=stable_cos(P(5))*u+stable_sin(P(5))*w,qy=-stable_sin(P(5))*u+stable_cos(P(5))*w,d=0.0f,m;
  if(P(0)==0.0f) d=(length(float2(qx/P(3),qy/P(4)))-1.0f)*min(P(3),P(4));
  else if(P(0)==1.0f) { float a=abs(qx)-P(3),b=abs(qy)-P(4);d=length(float2(max(a,0.0f),max(b,0.0f)))+min(max(a,b),0.0f); }
  else if(P(0)==3.0f) { float a=abs(qx)-P(3)+P(8),b=abs(qy)-P(4)+P(8);d=length(float2(max(a,0.0f),max(b,0.0f)))+min(max(a,b),0.0f)-P(8); }
  if(P(0)==2.0f) m=1.0f-soft(-P(3)-P(6),P(3)+P(6),qx);else m=P(6)==0.0f?(d<=0.0f?1.0f:0.0f):1.0f-soft(-P(6),P(6),d);
  if(P(7)!=0.0f) m=1.0f-m;return float4(v,src.a*m);
 } else if(op==8u) {
  if(P(0)!=0.0f) v=float3(baked_curve(v.r,0u,curve_n TABLE_PASS),baked_curve(v.g,0u,curve_n TABLE_PASS),baked_curve(v.b,0u,curve_n TABLE_PASS));
  for(uint c=0u;c<3u;++c) if(P(c+1u)!=0.0f) v[c]=baked_curve(v[c],c+1u,curve_n TABLE_PASS);
 } else if(op==9u) v=tetra(v,edge TABLE_PASS PARAM_PASS);
 else if(op==10u) {
  uint seed=mix_hash(x)^mix_hash(y+0x9e3779b9u)^mix_hash(frame)^AS_UINT(P(2));float n=random_noise(seed);
  v+=float3(n*(1.0f-P(1))+random_noise(seed+1u)*P(1),n*(1.0f-P(1))+random_noise(seed+2u)*P(1),n*(1.0f-P(1))+random_noise(seed+3u)*P(1))*P(0);
 } else if(op==16u) {
  v=(v-p3(0u PARAM_PASS))/(p3(3u PARAM_PASS)-p3(0u PARAM_PASS))*p3(6u PARAM_PASS);
  v=matrix_rgb(v,10u PARAM_PASS)*exp2(P(9));
 }
 if(op==17u) {
  uint count=uint(P(0));bool inside=false;float closest=3.4028234e38f;
  for(uint i=0u;i<count;++i) { uint j=(i+1u)%count;float2 a=float2(P(3u+2u*i),P(4u+2u*i)),b=float2(P(3u+2u*j),P(4u+2u*j)),d=b-a,q=float2(nx,ny)-a;
   float t=clamp(dot(q,d)/dot(d,d),0.0f,1.0f);closest=min(closest,length(q-d*t));
   if((a.y>ny)!=(b.y>ny)) { float hit=(b.x-a.x)*(ny-a.y)/(b.y-a.y)+a.x;if(nx<hit) inside=!inside; }
  }
  float dist=inside?-closest:closest,m=P(1)==0.0f?(dist<=0.0f?1.0f:0.0f):1.0f-soft(-P(1),P(1),dist);if(P(2)!=0.0f) m=1.0f-m;return float4(v,src.a*m);
 }
 if(op==22u) {
  float3 lo=p3(0u PARAM_PASS),hi=p3(3u PARAM_PASS);
  for(uint c=0u;c<3u;++c) { float t=unit((v[c]-lo[c])/(hi[c]-lo[c]))*float(edge-1u);uint i=min(uint(t),edge-2u);v[c]=mix(SAMPLE(i)[c],SAMPLE(i+1u)[c],t-float(i)); }
 }
 return float4(v,src.a);
}
