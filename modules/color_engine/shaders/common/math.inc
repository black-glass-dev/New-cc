// Original portable float32 math; included by GLSL and Metal. Host validates domains.
float unit(float x) { return clamp(x,0.0f,1.0f); }
float soft(float a,float b,float x) { float t=unit((x-a)/(b-a));return t*t*(3.0f-2.0f*t); }
float y709(float3 v) { return dot(v,float3(.2126f,.7152f,.0722f)); }
// Explicit polynomial avoids device-dependent low-accuracy native sin/cos approximations.
float stable_sin(float x) {
 const float pi=3.14159265358979323846f,tau=6.28318530717958647692f;
 x-=floor((x+pi)/tau)*tau;if(x>pi*.5f) x=pi-x;if(x<-pi*.5f) x=-pi-x;
 float z=x*x;return x+x*z*(-.16666666666666667f+z*(.008333333333333333f+z*(-.0001984126984126984f+z*(.000002755731922398589f+z*(-.00000002505210838544172f+z*.00000000016059043836821615f)))));
}
float stable_cos(float x) {return stable_sin(x+1.57079632679489661923f);}
float signedpow(float x,float p) { return x<0.0f?-pow(-x,p):pow(x,p); }
float expm1_small(float x) {
 return x*(1.0f+x*(.5f+x*(.16666666666666667f+x*(.041666666666666667f+x*(.008333333333333333f+x*(.001388888888888889f+x*(.0001984126984126984f+x*.0000248015873015873f)))))));
}
float transfer_encode(float x,uint t) {
 if(t==0u) return x;
 if(t==1u) return x<=.0031308f?12.92f*x:1.055f*pow(x,1.0f/2.4f)-.055f;
 if(t==2u) return signedpow(x,1.0f/2.4f);
 if(t==3u) return x<.018f?4.5f*x:1.099f*pow(x,.45f)-.099f;
 if(t==4u) return x<=.00262409f?x*10.44426855f:(log2(x+.0075f)+7.0f)*.07329248f;
 if(t==5u) return x<=.0078125f?10.5402377416545f*x+.0729055341958355f:(log2(x)+9.72f)/17.52f;
 if(t==6u) { float p=pow(x,2610.0f/16384.0f);return pow(1.0f-(.1640625f*(1.0f-p))/(1.0f+18.6875f*p),2523.0f/32.0f); }
 if(t==7u) return x<=1.0f/12.0f?sqrt(3.0f*x):.17883277f*log(12.0f*x-.28466892f)+.55991073f;
 if(t==8u) return x>.010591f?.247190f*log(5.555556f*x+.052272f)/log(10.0f)+.385537f:5.367655f*x+.092809f;
 if(t==9u) { float a=(262144.0f-16.0f)/117.45f,b=928.0f/1023.0f,c=95.0f/1023.0f,s=7.0f*log(2.0f)*exp2(7.0f-14.0f*c/b)/(a*b),cut=(exp2(6.0f-14.0f*c/b)-64.0f)/a;return x>=cut?(log2(a*x+64.0f)-6.0f)/14.0f*b+c:(x-cut)/s; }
 if(t==10u) return (x>=.01125f?420.0f+log((x+.01f)/.19f)/log(10.0f)*261.5f:x*(171.2102946929f-95.0f)/.01125f+95.0f)/1023.0f;
 return x<.01f?5.6f*x+.125f:.241514f*log(x+.00873f)/log(10.0f)+.598206f;
}
float transfer_decode(float x,uint t) {
 if(t==0u) return x;
 if(t==1u) return x<=.04045f?x/12.92f:pow((x+.055f)/1.055f,2.4f);
 if(t==2u) return signedpow(x,2.4f);
 if(t==3u) return x<.081f?x/4.5f:pow((x+.099f)/1.099f,1.0f/.45f);
 if(t==4u) return x<=.02740668f?x/10.44426855f:exp2(x/.07329248f-7.0f)-.0075f;
 if(t==5u) return x<=.155251141552511f?(x-.0729055341958355f)/10.5402377416545f:exp2(x*17.52f-9.72f);
 if(t==6u) { if(x<=.0000007309559f) return 0.0f;float delta=-expm1_small(log(x)*(32.0f/2523.0f));return pow(max(.1640625f-delta,0.0f)/(.1640625f+18.6875f*delta),16384.0f/2610.0f); }
 if(t==7u) return x<=.5f?x*x/3.0f:(exp((x-.55991073f)/.17883277f)+.28466892f)/12.0f;
 if(t==8u) return x>5.367655f*.010591f+.092809f?(pow(10.0f,(x-.385537f)/.247190f)-.052272f)/5.555556f:(x-.092809f)/5.367655f;
 if(t==9u) { float a=(262144.0f-16.0f)/117.45f,b=928.0f/1023.0f,c=95.0f/1023.0f,s=7.0f*log(2.0f)*exp2(7.0f-14.0f*c/b)/(a*b),cut=(exp2(6.0f-14.0f*c/b)-64.0f)/a;return x>=0.0f?(exp2(14.0f*(x-c)/b+6.0f)-64.0f)/a:x*s+cut; }
 if(t==10u) return x>=171.2102946929f/1023.0f?pow(10.0f,(x*1023.0f-420.0f)/261.5f)*.19f-.01f:(x*1023.0f-95.0f)*.01125f/(171.2102946929f-95.0f);
 return x<.181f?(x-.125f)/5.6f:pow(10.0f,(x-.598206f)/.241514f)-.00873f;
}
float3 to_hsl(float3 v) {
 v=clamp(v,float3(0.0f),float3(1.0f));float hi=max(v.r,max(v.g,v.b)),lo=min(v.r,min(v.g,v.b)),d=hi-lo,l=(hi+lo)*.5f;
 if(d==0.0f) return float3(0.0f,0.0f,l);
 float h=hi==v.r?(v.g-v.b)/d:(hi==v.g?2.0f+(v.b-v.r)/d:4.0f+(v.r-v.g)/d);h=h/6.0f-floor(h/6.0f);
 return float3(h,d/(l<=.5f?hi+lo:2.0f-hi-lo),l);
}
float circular(float h) { h=abs(h);return abs(h-floor(h+.5f)); }
float band_mask(float distance,float width,float softness) { return softness==0.0f?(distance<=width?1.0f:0.0f):1.0f-soft(width,width+softness,distance); }
uint mix_hash(uint x) { x^=x>>16u;x*=0x7feb352du;x^=x>>15u;x*=0x846ca68bu;x^=x>>16u;return x; }
float random_noise(uint x) { return float(mix_hash(x)>>8u)*(1.0f/16777216.0f)-.5f; }
float3 primaries(float3 v,float3 lift,float3 gamma,float3 gain,float3 offset,float exposure,float contrast,float pivot,float saturation,float hue,float vibrance,float temp,float tint,float lum_mix) {
 float old=y709(v);v*=exp2(exposure);v*=float3(exp2(temp),exp2(tint),exp2(-temp));
 v=v*gain+lift*(float3(1.0f)-v)+offset;
 v=float3(signedpow(v.r,1.0f/gamma.r),signedpow(v.g,1.0f/gamma.g),signedpow(v.b,1.0f/gamma.b));
 if(contrast!=1.0f) v=pivot*float3(signedpow(v.r/pivot,contrast),signedpow(v.g/pivot,contrast),signedpow(v.b/pivot,contrast));
 float y=y709(v),u=v.b-y,w=v.r-y,cs=stable_cos(hue),sn=stable_sin(hue),r=y+w*cs-u*sn,b=y+w*sn+u*cs;
 v=float3(r,(y-.2126f*r-.0722f*b)/.7152f,b);
 float chroma=max(v.r,max(v.g,v.b))-min(v.r,min(v.g,v.b));
 float scale=saturation*(1.0f+vibrance*(1.0f-chroma/(abs(y)+chroma+1e-6f)));
 v=float3(y)+(v-float3(y))*scale;return v+float3((old-y709(v))*lum_mix);
}
float4 source_over(float4 a,float4 b) {
 float ba=unit(b.a),aa=unit(a.a),out_a=ba+aa*(1.0f-ba);
 if(out_a==0.0f) return float4(0.0f);
 return float4((b.rgb*ba+a.rgb*aa*(1.0f-ba))/out_a,out_a);
}
