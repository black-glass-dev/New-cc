#include "color_c_api.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
int main(void) {
 ColorEngineHandle* e=NULL;ColorProgramHandle* p=NULL;
 if(ColorEngineAPIVersion()!=1 || ColorEngineCreate(1048576,8,&e)!=COLOR_OK || ColorProgramCreate(&p)!=COLOR_OK) return 1;
 if(ColorProgramAddExposure(p,1,1,0,1)!=COLOR_OK) return 2;
 float pixel[4]={.25f,.125f,.5f,1};ColorFrameBuffer32F_C f={pixel,1,1,4,4};GradeContext_C c={7,9,1,0,0};
 if(RenderGradeFrame(e,p,f,f,c)!=COLOR_OK || fabsf(pixel[0]-.5f)>1e-6f || pixel[3]!=1) return 3;
 uint64_t bins[64]={0};if(ComputeVideoScopes(e,f,16,bins,64)!=COLOR_OK) return 4;
 uint64_t sum=0;for(unsigned i=0;i<64;++i) sum+=bins[i];if(sum!=4) return 5;
 if(RenderGradeFrame(NULL,p,f,f,c)==COLOR_OK || ColorEngineLastError()[0]=='\0') return 6;
 ColorCSTParams_C cst={0,0,0,0,0,0,0};
 if(ColorProgramAddCST(p,1,2,1,cst)!=COLOR_OK) return 7;
 cst.input_gamut=999;if(ColorProgramAddCST(p,1,3,2,cst)==COLOR_OK) return 8;
 size_t required=0;if(ColorProgramSerialize(p,NULL,0,&required)!=COLOR_OK||required==0) return 9;
 void* archive=malloc(required);if(!archive) return 10;
 if(ColorProgramSerialize(p,archive,required,&required)!=COLOR_OK) return 11;
 ColorProgramHandle* loaded=NULL;if(ColorProgramDeserialize(archive,required,&loaded)!=COLOR_OK) return 12;free(archive);
 ColorSpatialParams_C spatial={7,1,1.5f,.1f,.1f,.5f,1,1,1,.1f};
 if(ColorProgramAddSpatial(loaded,1,3,2,spatial)!=COLOR_OK) return 13;
 if(RenderGradeFrame(e,loaded,f,f,c)!=COLOR_OK) return 14;
 ColorProgramDestroy(loaded);ColorProgramDestroy(p);ColorEngineDestroy(e);puts("C ABI:version/ownership/in-place render/scopes/errors PASS");return 0;
}
