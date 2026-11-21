#pragma once

#include <cstdint>

namespace owncut::timeline {

#ifdef __cplusplus
extern "C" {
#endif

// POD 64-bit rational timestamp shared safely across C++20 and C-ABI color hooks.
typedef struct RationalTime64 {
    int64_t num;
    int64_t den;
} RationalTime64;

typedef enum ColorPassScopeC {
    OWNCUT_COLOR_PASS_CLIP = 0,
    OWNCUT_COLOR_PASS_GROUP_PRE = 1,
    OWNCUT_COLOR_PASS_GROUP_POST = 2,
    OWNCUT_COLOR_PASS_TIMELINE = 3
} ColorPassScopeC;

// Zero-dependency float32 RGBA frame view passed to external color engines.
typedef struct FrameBufferView32F {
    float* rgba32f;          // Linear or scene-referred RGBA float32 buffer (4 floats/pixel)
    int32_t width;           // Active frame width in pixels
    int32_t height;          // Active frame height in pixels
    int32_t stride;          // Row stride in float elements (>= width * 4)
    RationalTime64 time;     // Timeline or clip evaluation timestamp
    uint64_t clip_id;        // Stable clip identifier (0 for timeline-level pass)
    uint64_t track_id;       // Stable track identifier (0 for timeline-level pass)
    uint64_t group_id;       // Color group identifier (0 if ungrouped)
} FrameBufferView32F;

// Optional C-ABI callback interface for dynamically loaded external color engines.
typedef bool (*ColorHookProcessFn)(FrameBufferView32F* buffer,
                                   ColorPassScopeC scope,
                                   void* user_data);
typedef bool (*ColorHookBypassedFn)(uint64_t clip_id,
                                    uint64_t group_id,
                                    void* user_data);

typedef struct ColorPipelineHookC {
    ColorHookProcessFn process_frame;
    ColorHookBypassedFn is_bypassed;
    const char* hook_name;
    void* user_data;
} ColorPipelineHookC;

#ifdef __cplusplus
} // extern "C"
#endif

enum class ColorPassScope : uint8_t {
    Clip = OWNCUT_COLOR_PASS_CLIP,
    GroupPre = OWNCUT_COLOR_PASS_GROUP_PRE,
    GroupPost = OWNCUT_COLOR_PASS_GROUP_POST,
    Timeline = OWNCUT_COLOR_PASS_TIMELINE
};

// Pure C++20 interface implemented by external color engines (e.g., modules/color_engine/).
// Zero dependency on internal timeline headers to prevent circular coupling.
class IColorPipelineHook {
public:
    virtual ~IColorPipelineHook() = default;

    // Processes `buffer` in-place for the given pass scope. Returns true if pixels were processed
    // (or validly passed through), false on unrecoverable validation/processing failure.
    virtual bool process_frame(FrameBufferView32F& buffer, ColorPassScope scope) noexcept = 0;

    // Queries whether the external color grade is currently bypassed for a clip/group.
    virtual bool is_bypassed(uint64_t clip_id, uint64_t group_id) const noexcept = 0;

    // Identifies the external color engine implementation for diagnostics/logging.
    virtual const char* hook_name() const noexcept = 0;
};

// Adapter wrapping a C-ABI ColorPipelineHookC into IColorPipelineHook.
class CAbiColorPipelineHook final : public IColorPipelineHook {
public:
    explicit CAbiColorPipelineHook(ColorPipelineHookC c_hook) noexcept : c_hook_(c_hook) {}

    bool process_frame(FrameBufferView32F& buffer, ColorPassScope scope) noexcept override {
        if (!c_hook_.process_frame) return true;
        return c_hook_.process_frame(&buffer, static_cast<ColorPassScopeC>(scope), c_hook_.user_data);
    }

    bool is_bypassed(uint64_t clip_id, uint64_t group_id) const noexcept override {
        if (!c_hook_.is_bypassed) return false;
        return c_hook_.is_bypassed(clip_id, group_id, c_hook_.user_data);
    }

    const char* hook_name() const noexcept override {
        return c_hook_.hook_name ? c_hook_.hook_name : "CAbiColorPipelineHook";
    }

private:
    ColorPipelineHookC c_hook_;
};

} // namespace owncut::timeline
