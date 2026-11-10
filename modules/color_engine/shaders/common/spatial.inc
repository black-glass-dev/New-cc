// source_pixel/history_pixel functions and SP(i) parameters supplied by platform wrapper.
float4 spatial_grade(uint x,uint y,uint width,uint height,uint kind,uint radius SPATIAL_ARG) {
 float4 center=source_pixel(x,y SPATIAL_PASS);float3 c=center.rgb;
 if(kind==5u) { float3 old=history_pixel(x,y SPATIAL_PASS).rgb,d=old-c;float weight=SP(2)*exp(-dot(d,d)/(2.0f*SP(1)*SP(1)));return float4(c+(old-c)*weight,center.a); }
 float3 total=float3(0.0f);float norm=0.0f,alpha_sum=0.0f,alpha_min=1.0f,alpha_max=0.0f,dark=3.4028234e38f,bloom=0.0f;
 float3 air=float3(SP(4),SP(5),SP(6));
 for(int dy=-int(radius);dy<=int(radius);++dy) for(int dx=-int(radius);dx<=int(radius);++dx) {
  uint xx=uint(clamp(int(x)+dx,0,int(width)-1)),yy=uint(clamp(int(y)+dy,0,int(height)-1));float4 q=source_pixel(xx,yy SPATIAL_PASS);float3 v=q.rgb,d=v-c;
  float weight=exp(-float(dx*dx+dy*dy)/(2.0f*SP(0)*SP(0)));
  if(kind==0u) weight*=exp(-dot(d,d)/(2.0f*SP(1)*SP(1)));
  total+=v*weight;alpha_sum+=q.a*weight;norm+=weight;alpha_min=min(alpha_min,q.a);alpha_max=max(alpha_max,q.a);
  dark=min(dark,min(v.r/air.r,min(v.g/air.g,v.b/air.b)));bloom+=max(y709(v)-SP(3),0.0f)*weight;
 }
 float3 blur=total/norm,result=c;float alpha=center.a;
 if(kind==0u) result=c+(blur-c)*SP(2);
 else if(kind==1u) result=c+(c-blur)*SP(2);
 else if(kind==2u) alpha=alpha_min;
 else if(kind==3u) alpha=alpha_max;
 else if(kind==4u) alpha=alpha_sum/norm;
 else if(kind==6u) { float t=clamp(1.0f-SP(2)*dark,SP(7),1.0f);result=(c-air)/t+air; }
 else if(kind==7u) result=c+float3(1.0f,.25f,0.0f)*(SP(2)*bloom/norm);
 return float4(result,alpha);
}
