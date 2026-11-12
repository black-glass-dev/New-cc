// Android CPU-reference JNI bridge. This file does NOT dispatch Vulkan shaders.
#include <jni.h>
#include "color_c_api.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace {
struct State {
    ColorEngineHandle* engine{};ColorProgramHandle* program{};std::mutex mutex;
    ~State() { ColorProgramDestroy(program);ColorEngineDestroy(engine); }
};
std::mutex registry_mutex;
std::unordered_map<jlong,std::shared_ptr<State>> registry;
jlong next_id=1;
void failure(JNIEnv* env,const char* text) { jclass c=env->FindClass("java/lang/IllegalStateException");if(c) env->ThrowNew(c,text); }
std::shared_ptr<State> lookup(jlong id) { std::lock_guard<std::mutex> lock(registry_mutex);auto it=registry.find(id);if(it==registry.end()) throw std::invalid_argument("Closed/invalid color handle");return it->second; }
ColorFrameBuffer32F_C buffer(JNIEnv* env,jobject object,jint w,jint h,jlong stride) {
    jclass cls=env->FindClass("java/nio/ByteBuffer");
    if(!cls||!object||!env->IsInstanceOf(object,cls)||w<=0||h<=0||stride<=0) throw std::invalid_argument("Expected direct ByteBuffer and positive extents");
    const auto capacity=env->GetDirectBufferCapacity(object);void* data=env->GetDirectBufferAddress(object);
    if(!data||capacity<0||static_cast<std::uint64_t>(stride)>std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("Invalid native buffer capacity");
    return {static_cast<float*>(data),static_cast<std::size_t>(w),static_cast<std::size_t>(h),static_cast<std::size_t>(stride),static_cast<std::size_t>(capacity)/sizeof(float)};
}
}
extern "C" JNIEXPORT jlong JNICALL Java_org_owncolor_ColorEngine_nativeCreateCPU(JNIEnv* env,jclass,jlong budget,jint tile) {
    try {
        if(budget<=0||static_cast<std::uint64_t>(budget)>std::numeric_limits<std::size_t>::max()||tile<=0) throw std::invalid_argument("Invalid CPU engine budget");
        auto state=std::make_shared<State>();
        if(ColorEngineCreate(static_cast<std::size_t>(budget),static_cast<std::size_t>(tile),&state->engine)!=COLOR_OK||ColorProgramCreate(&state->program)!=COLOR_OK) throw std::runtime_error(ColorEngineLastError());
        std::lock_guard<std::mutex> lock(registry_mutex);if(registry.size()>=64||next_id==std::numeric_limits<jlong>::max()) throw std::runtime_error("JNI handle budget");
        const auto id=next_id++;registry.emplace(id,std::move(state));return id;
    } catch(const std::exception& e) { failure(env,e.what());return 0; }catch(...) { failure(env,"Native color create failed");return 0; }
}
extern "C" JNIEXPORT void JNICALL Java_org_owncolor_ColorEngine_nativeClose(JNIEnv* env,jclass,jlong id) { try { std::lock_guard<std::mutex> lock(registry_mutex);registry.erase(id); }catch(const std::exception& e) { failure(env,e.what()); }catch(...) { failure(env,"Native color close failed"); } }
extern "C" JNIEXPORT void JNICALL Java_org_owncolor_ColorEngine_nativeExposure(JNIEnv* env,jclass,jlong id,jlong node,jlong input,jfloat stops) {
    try { auto state=lookup(id);std::lock_guard<std::mutex> lock(state->mutex);if(node<=0||input<0||ColorProgramAddExposure(state->program,1,static_cast<std::uint64_t>(node),static_cast<std::uint64_t>(input),stops)!=COLOR_OK) throw std::invalid_argument("Invalid exposure node"); }
    catch(const std::exception& e) { failure(env,e.what()); }catch(...) { failure(env,"Native color mutation failed"); }
}
extern "C" JNIEXPORT void JNICALL Java_org_owncolor_ColorEngine_nativeRenderCPU(JNIEnv* env,jclass,jlong id,jobject src,jobject dst,jint w,jint h,jlong src_stride,jlong dst_stride,jdouble time) {
    try { auto state=lookup(id);auto in=buffer(env,src,w,h,src_stride),out=buffer(env,dst,w,h,dst_stride);std::lock_guard<std::mutex> lock(state->mutex);if(RenderGradeFrame(state->engine,state->program,in,out,{0,0,0,time,0})!=COLOR_OK) throw std::runtime_error(ColorEngineLastError()); }
    catch(const std::exception& e) { failure(env,e.what()); }catch(...) { failure(env,"Native CPU render failed"); }
}
