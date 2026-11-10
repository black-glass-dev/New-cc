#ifndef OWN_COLOR_C_API_H
#define OWN_COLOR_C_API_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define COLOR_API_VERSION 1u
typedef struct ColorEngineHandle ColorEngineHandle;
typedef struct ColorProgramHandle ColorProgramHandle;
typedef struct ColorFrameBuffer32F_C { float* data; size_t width,height,row_stride_floats,capacity_floats; } ColorFrameBuffer32F_C;
typedef struct GradeContext_C { uint64_t clip_id,group_id,source_revision; double timeline_time_sec; uint64_t frame_index; } GradeContext_C;
typedef enum ColorStatus { COLOR_OK=0,COLOR_INVALID_ARGUMENT=1,COLOR_OUT_OF_MEMORY=2,COLOR_INTERNAL_ERROR=3 } ColorStatus;
/* Opaque handles are caller-owned. Programs may be rendered concurrently only if not mutated.
 * Frame buffers are straight-alpha RGBA32F, explicit capacity, caller-owned valid memory.
 * Input/output aliasing allowed. No exceptions cross this ABI. All params finite. */
uint32_t ColorEngineAPIVersion(void);
const char* ColorEngineLastError(void);
ColorStatus ColorEngineCreate(size_t memory_budget_bytes,size_t tile_edge,ColorEngineHandle**);
void ColorEngineDestroy(ColorEngineHandle*);
ColorStatus ColorProgramCreate(ColorProgramHandle**);
void ColorProgramDestroy(ColorProgramHandle*);
/* Add a serial exposure node in stage0=group-pre,1=clip,2=group-post,3=timeline.
 * id/input_id are stage-local; input_id0 uses original input. */
ColorStatus ColorProgramAddExposure(ColorProgramHandle*,unsigned stage,uint64_t id,uint64_t input_id,float stops);
/* Enum numeric values match the ordered C++ Gamut/Transfer definitions, v1.
 * Gamut0..10:709,2020,P3D65,AP0,AP1,DWG,AWG3,AWG4,SGamut3,SGamut3Cine,VGamut.
 * Transfer0..11:linear,sRGB,gamma2.4,709,DI,ACEScct,PQ,HLG,LogC3EI800,LogC4,SLog3,VLog. */
typedef struct ColorCSTParams_C {
 uint32_t input_gamut,output_gamut,input_transfer,output_transfer;
 float exposure_stops,tone_knee,gamut_compression;
} ColorCSTParams_C;
ColorStatus ColorProgramAddCST(ColorProgramHandle*,unsigned stage,uint64_t id,uint64_t input_id,ColorCSTParams_C);
typedef struct ColorSpatialParams_C {
 uint32_t kind,radius;
 float spatial_sigma,range_sigma,amount,threshold,airlight_r,airlight_g,airlight_b,transmission_floor;
} ColorSpatialParams_C;
/* kinds0..7 match SpatialKind; temporal kind5 requires C++ history context and is rejected here. */
ColorStatus ColorProgramAddSpatial(ColorProgramHandle*,unsigned stage,uint64_t id,uint64_t input_id,ColorSpatialParams_C);
/* Two-call archive API: out=NULL,capacity=0 queries size. No partial writes on failure. */
ColorStatus ColorProgramSerialize(const ColorProgramHandle*,void* out,size_t capacity,size_t* required);
ColorStatus ColorProgramDeserialize(const void* data,size_t bytes,ColorProgramHandle** out);
ColorStatus RenderGradeFrame(ColorEngineHandle*,const ColorProgramHandle*,ColorFrameBuffer32F_C,ColorFrameBuffer32F_C,GradeContext_C);
/* Histogram is R,G,B,luma, four contiguous planes of bins uint64_t. No hidden allocation
 * ownership crosses the ABI; scopes only commit after successful full-frame validation. */
ColorStatus ComputeVideoScopes(ColorEngineHandle*,ColorFrameBuffer32F_C,size_t bins,uint64_t* histogram,size_t histogram_entries);
#ifdef __cplusplus
}
#endif
#endif
