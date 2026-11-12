#import "ios_metal_bridge.h"
id<MTLComputePipelineState> ColorCreateMetalGradePipeline(id<MTLDevice> device,id<MTLLibrary> library,NSError** error) {
    if(!device||!library) {
        if(error) *error=[NSError errorWithDomain:@"OwnColorEngine" code:1 userInfo:@{NSLocalizedDescriptionKey:@"A Metal device and compiled color library are required"}];
        return nil;
    }
    id<MTLFunction> function=[library newFunctionWithName:@"color_grade"];
    if(!function) {
        if(error) *error=[NSError errorWithDomain:@"OwnColorEngine" code:2 userInfo:@{NSLocalizedDescriptionKey:@"color_grade kernel was not found"}];
        return nil;
    }
    return [device newComputePipelineStateWithFunction:function error:error];
}
#include <cstring>
bool ColorDispatchMetal(id<MTLDevice> device,id<MTLComputePipelineState> pipeline,
                       ColorMetalBuffer* descriptors,size_t count,const void* metadata,
                       size_t metadata_bytes,size_t work_items,size_t budget,NSError** error) {
    auto fail=[&](NSString* message)->bool {if(error) *error=[NSError errorWithDomain:@"OwnColorEngine" code:3 userInfo:@{NSLocalizedDescriptionKey:message}];return false;};
    if(!device||!pipeline||pipeline.device!=device||!descriptors||count<1||count>8||!metadata||metadata_bytes<1||metadata_bytes>4096||!work_items) return fail(@"Invalid Metal dispatch layout");
    size_t used=metadata_bytes;
    if(used>budget) return fail(@"Metal metadata exceeds budget");
    for(size_t i=0;i<count;++i) {if(!descriptors[i].data||!descriptors[i].bytes||descriptors[i].access>2||descriptors[i].bytes>budget-used) return fail(@"Metal buffer extent/access/budget invalid");used+=descriptors[i].bytes;}
    id<MTLBuffer> buffers[8];
    for(size_t i=0;i<count;++i) {
        buffers[i]=[device newBufferWithLength:descriptors[i].bytes options:MTLResourceStorageModeShared];
        if(!buffers[i]||!buffers[i].contents) return fail(@"Metal shared-buffer allocation failed");
        if(descriptors[i].access==1) std::memset(buffers[i].contents,0,descriptors[i].bytes);
        else std::memcpy(buffers[i].contents,descriptors[i].data,descriptors[i].bytes);
    }
    id<MTLCommandQueue> queue=[device newCommandQueue];if(!queue) return fail(@"Metal command queue unavailable");
    id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];if(!command||!encoder) return fail(@"Metal command encoder unavailable");
    [encoder setComputePipelineState:pipeline];for(size_t i=0;i<count;++i) [encoder setBuffer:buffers[i] offset:0 atIndex:i];
    [encoder setBytes:metadata length:metadata_bytes atIndex:count];const size_t threads=pipeline.maxTotalThreadsPerThreadgroup<64?pipeline.maxTotalThreadsPerThreadgroup:64;
    if(!threads) { [encoder endEncoding];return fail(@"Invalid Metal pipeline threadgroup size"); }
    [encoder dispatchThreads:MTLSizeMake(work_items,1,1) threadsPerThreadgroup:MTLSizeMake(threads,1,1)];[encoder endEncoding];[command commit];[command waitUntilCompleted];
    if(command.status!=MTLCommandBufferStatusCompleted) {if(error) *error=command.error;return false;}
    for(size_t i=0;i<count;++i) if(descriptors[i].access!=0) std::memcpy(descriptors[i].data,buffers[i].contents,descriptors[i].bytes);
    return true;
}
