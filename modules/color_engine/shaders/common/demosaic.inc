uint cfa_color(uint x,uint y,uint pattern) {
 uint k=(y%2u)*2u+x%2u;
 if(pattern==0u) return k==0u?0u:(k==3u?2u:1u);
 if(pattern==1u) return k==0u?2u:(k==3u?0u:1u);
 if(pattern==2u) return k==1u?0u:(k==2u?2u:1u);
 return k==1u?2u:(k==2u?0u:1u);
}
float3 demosaic(uint x,uint y,uint width,uint height,uint pattern RAW_ARG) {
 float3 value=float3(0.0f);uint own=cfa_color(x,y,pattern);
 for(uint c=0u;c<3u;++c) {
  if(c==own) { value[c]=RAW(x,y);continue; }
  float total=0.0f;uint count=0u;
  for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
   int xx=int(x)+dx,yy=int(y)+dy;if(xx<0||yy<0||xx>=int(width)||yy>=int(height)) continue;
   if(cfa_color(uint(xx),uint(yy),pattern)==c) { total+=RAW(uint(xx),uint(yy));++count; }
  }
  value[c]=total/float(count);
 }
 value=(value-float3(RP(0),RP(1),RP(2)))/(float3(RP(3),RP(4),RP(5))-float3(RP(0),RP(1),RP(2)))*float3(RP(6),RP(7),RP(8));
 return float3(dot(value,float3(RP(10),RP(11),RP(12))),dot(value,float3(RP(13),RP(14),RP(15))),dot(value,float3(RP(16),RP(17),RP(18))))*exp2(RP(9));
}
