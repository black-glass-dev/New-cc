float4 resize_pixel(uint x,uint y,uint sw,uint sh RESIZE_ARG) {
 float px=RM(0)*float(x)+RM(1)*float(y)+RM(2),py=RM(3)*float(x)+RM(4)*float(y)+RM(5);
 if(px<0.0f||py<0.0f||px>float(sw-1u)||py>float(sh-1u)) return float4(0.0f);
 uint ix=min(uint(px),sw-1u),iy=min(uint(py),sh-1u),jx=min(ix+1u,sw-1u),jy=min(iy+1u,sh-1u);float tx=px-float(ix),ty=py-float(iy);
 return mix(mix(RS(ix,iy),RS(jx,iy),tx),mix(RS(ix,jy),RS(jx,jy),tx),ty);
}
