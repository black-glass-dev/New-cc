#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#include "timeline_c_api.h"

@interface OCUTimelineBridge : NSObject
- (instancetype)initWithFpsNum:(int32_t)fpsNum fpsDen:(int32_t)fpsDen width:(int32_t)width height:(int32_t)height;
- (uint64_t)addSyntheticMedia:(NSString*)name width:(int32_t)width height:(int32_t)height durationFrames:(int64_t)frames;
- (BOOL)performEditMode:(int32_t)mode mediaId:(uint64_t)mediaId sourceIn:(int64_t)inFrames sourceOut:(int64_t)outFrames;
- (BOOL)bladeSplitAtFrame:(int64_t)frameIndex;
- (BOOL)undo;
- (BOOL)redo;
- (void)setPlayheadFrames:(int64_t)frameIndex;
- (int64_t)durationFrames;
@end

@implementation OCUTimelineBridge {
    OwnCutTimelineEngine* _engine;
}

- (instancetype)initWithFpsNum:(int32_t)fpsNum fpsDen:(int32_t)fpsDen width:(int32_t)width height:(int32_t)height {
    self = [super init];
    if (self) {
        _engine = owncut_tl_create(fpsNum, fpsDen, width, height);
    }
    return self;
}

- (void)dealloc {
    if (_engine) {
        owncut_tl_destroy(_engine);
        _engine = nullptr;
    }
}

- (uint64_t)addSyntheticMedia:(NSString*)name width:(int32_t)width height:(int32_t)height durationFrames:(int64_t)frames {
    return owncut_tl_add_synthetic_media(_engine, name.UTF8String, width, height, frames);
}

- (BOOL)performEditMode:(int32_t)mode mediaId:(uint64_t)mediaId sourceIn:(int64_t)inFrames sourceOut:(int64_t)outFrames {
    return owncut_tl_perform_edit(_engine, mode, mediaId, inFrames, outFrames) != 0;
}

- (BOOL)bladeSplitAtFrame:(int64_t)frameIndex {
    return owncut_tl_blade_split(_engine, frameIndex) != 0;
}

- (BOOL)undo {
    return owncut_tl_undo(_engine) != 0;
}

- (BOOL)redo {
    return owncut_tl_redo(_engine) != 0;
}

- (void)setPlayheadFrames:(int64_t)frameIndex {
    owncut_tl_set_playhead_frames(_engine, frameIndex);
}

- (int64_t)durationFrames {
    return owncut_tl_get_duration_frames(_engine);
}

@end
#endif
