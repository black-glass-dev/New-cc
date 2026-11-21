bool klt_inside(float2 p,uint width,uint height,uint radius) {
 float r=float(radius+1u);return p.x>=r&&p.y>=r&&p.x<float(width)-r-1.0f&&p.y<float(height)-r-1.0f;
}
float klt_value(float x,float y,bool first,uint width,uint height KLT_ARG) {
 uint ix=min(uint(x),width-1u),iy=min(uint(y),height-1u),jx=min(ix+1u,width-1u),jy=min(iy+1u,height-1u);float tx=x-float(ix),ty=y-float(iy);
 float3 a=KLT_READ(ix,iy,first).rgb,b=KLT_READ(jx,iy,first).rgb,c=KLT_READ(ix,jy,first).rgb,d=KLT_READ(jx,jy,first).rgb;
 return y709(mix(mix(a,b,tx),mix(c,d,tx),ty));
}
float4 klt_track(float4 feature,uint width,uint height,uint radius,uint iterations,float max_error,float convergence KLT_ARG) {
 float2 origin=feature.xy,q=feature.zw;
 if(!klt_inside(origin,width,height,radius)) return float4(q,0.0f,1.0f);
 for(uint step=0u;step<iterations;++step) {
  if(!klt_inside(q,width,height,radius)) return float4(q,0.0f,1.0f);
  float a=0.0f,b=0.0f,c=0.0f,u=0.0f,v=0.0f;
  for(int dy=-int(radius);dy<=int(radius);++dy) for(int dx=-int(radius);dx<=int(radius);++dx) {
   float x=q.x+float(dx),y=q.y+float(dy),gx=(klt_value(x+1.0f,y,false,width,height KLT_PASS)-klt_value(x-1.0f,y,false,width,height KLT_PASS))*.5f,gy=(klt_value(x,y+1.0f,false,width,height KLT_PASS)-klt_value(x,y-1.0f,false,width,height KLT_PASS))*.5f;
   float difference=klt_value(x,y,false,width,height KLT_PASS)-klt_value(origin.x+float(dx),origin.y+float(dy),true,width,height KLT_PASS);a+=gx*gx;b+=gx*gy;c+=gy*gy;u+=gx*difference;v+=gy*difference;
  }
  float determinant=a*c-b*b;if(!(determinant>1e-6f*(a+c)*(a+c)&&determinant>1e-12f)) return float4(q,0.0f,2.0f);
  float2 delta=float2(c*u-b*v,a*v-b*u)/determinant;float distance=length(delta);if(distance>4.0f) delta*=4.0f/distance;q-=delta;if(distance<convergence) break;
 }
 if(!klt_inside(q,width,height,radius)) return float4(q,0.0f,1.0f);
 float error=0.0f;uint count=0u;
 for(int dy=-int(radius);dy<=int(radius);++dy) for(int dx=-int(radius);dx<=int(radius);++dx) {float e=klt_value(q.x+float(dx),q.y+float(dy),false,width,height KLT_PASS)-klt_value(origin.x+float(dx),origin.y+float(dy),true,width,height KLT_PASS);error+=e*e;++count;}
 error=sqrt(error/float(count));return float4(q,error,error<=max_error?0.0f:3.0f);
}
