#pragma once
#include "color_c_api.h"
#ifdef __OBJC__
#import <Metal/Metal.h>
/* Real pipeline-state creation using an already compiled library, not a render dispatcher.
 * Compile shaders/metal/grade.metal into a metallib with the common headers present. */
id<MTLComputePipelineState> ColorCreateMetalGradePipeline(id<MTLDevice> device,id<MTLLibrary> library,NSError** error);
#endif
#ifdef __OBJC__
typedef struct ColorMetalBuffer { void* data;size_t bytes;unsigned access; } ColorMetalBuffer;
/* access0=input,1=zero-initialized output,2=input/output. Trusted bundled kernels only.
 * Synchronous shared-buffer dispatch with completion check and readback. No CPU fallback.
 * Metadata is bound at buffer_count (grade4, spatial4, demosaic3, resize3, tracking4). */
bool ColorDispatchMetal(id<MTLDevice> device,id<MTLComputePipelineState> pipeline,
                       ColorMetalBuffer* buffers,size_t buffer_count,
                       const void* metadata,size_t metadata_bytes,size_t work_items,
                       size_t memory_budget_bytes,NSError** error);
#endif
