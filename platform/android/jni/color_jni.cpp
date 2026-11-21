#include <jni.h>
#include "editor/color.hpp"
#include "editor/color_gpu.hpp"
#include <new>
#include <limits>

namespace {
void fail(JNIEnv* env, const char* type_name, const char* message) {
    if (env->ExceptionCheck()) return;
    const auto type=env->FindClass(type_name);
    if (type) { env->ThrowNew(type,message); env->DeleteLocalRef(type); } // engine messages are ASCII
}
struct Buffer { std::uint8_t* data; std::size_t size; };
Buffer buffer(JNIEnv* env, jobject value, bool writable) {
    if (!value) throw std::invalid_argument("RGBA buffer is null");
    const auto capacity=env->GetDirectBufferCapacity(value);
    auto* data=static_cast<std::uint8_t*>(env->GetDirectBufferAddress(value));
    if (capacity < 0 || !data || static_cast<std::uint64_t>(capacity) > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("RGBA requires a direct byte buffer");
    const auto type=env->FindClass("java/nio/ByteBuffer");
    if (!type) throw std::runtime_error("ByteBuffer class unavailable");
    const auto readonly=env->GetMethodID(type,"isReadOnly","()Z");
    const auto position=env->GetMethodID(type,"position","()I");
    const auto limit=env->GetMethodID(type,"limit","()I");
    env->DeleteLocalRef(type);
    if (env->ExceptionCheck() || !readonly || !position || !limit)
        throw std::runtime_error("ByteBuffer contract unavailable");
    const auto read_only=env->CallBooleanMethod(value,readonly);
    if (env->ExceptionCheck()) throw std::runtime_error("could not inspect RGBA buffer");
    const auto pos=env->CallIntMethod(value,position);
    if (env->ExceptionCheck()) throw std::runtime_error("could not inspect RGBA buffer");
    const auto end=env->CallIntMethod(value,limit);
    if (env->ExceptionCheck()) throw std::runtime_error("could not inspect RGBA buffer");
    if ((writable && read_only) || pos != 0 || end != capacity)
        throw std::invalid_argument("RGBA buffers must expose full capacity at position zero; output must be writable");
    return {data,static_cast<std::size_t>(capacity)};
}
} // namespace
extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_ColorEngine_nativeRenderV1(JNIEnv* env, jobject,
    jobject source, jobject output, jint width, jint height, jint source_stride,
    jint output_stride, jdoubleArray values, jboolean enabled) {
    try {
        if (width <= 0 || height <= 0 || source_stride <= 0 || output_stride <= 0)
            throw std::invalid_argument("invalid RGBA dimensions/stride");
        if (!values || env->GetArrayLength(values) != static_cast<jsize>(editor::color::parameter_count))
            throw std::invalid_argument("grade v1 requires 17 parameters");
        editor::color::ParameterBlock parameters;
        env->GetDoubleArrayRegion(values,0,static_cast<jsize>(parameters.size()),parameters.data());
        if (env->ExceptionCheck()) return;
        const auto grade=editor::color::from_parameters(parameters,enabled != JNI_FALSE);
        const auto in=buffer(env,source,false), out=buffer(env,output,true);
        const auto w=static_cast<std::size_t>(width), h=static_cast<std::size_t>(height);
        (void)editor::color::apply_rgba8({{in.data,in.size},w,h,static_cast<std::size_t>(source_stride)},
                                      {{out.data,out.size},w,h,static_cast<std::size_t>(output_stride)},grade);
    } catch (const std::bad_alloc&) { fail(env,"java/lang/OutOfMemoryError","color processing allocation failed"); }
      catch (const std::exception& error) { fail(env,"java/lang/IllegalArgumentException",error.what()); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_owncut_app_ColorEngine_nativeShaderV1(JNIEnv* env, jobject, jdoubleArray values,
                                              jboolean enabled, jint encoding, jboolean vertex) {
    try {
        if (!values || env->GetArrayLength(values) != static_cast<jsize>(editor::color::parameter_count))
            throw std::invalid_argument("grade v1 requires 17 parameters");
        editor::color::ParameterBlock parameters;
        env->GetDoubleArrayRegion(values,0,static_cast<jsize>(parameters.size()),parameters.data());
        if (env->ExceptionCheck()) return nullptr;
        const auto grade=editor::color::from_parameters(parameters,enabled != JNI_FALSE);
        const auto format=static_cast<editor::color::TextureEncoding>(encoding);
        editor::color::validate(format);
        const auto fragment=editor::color::grade_fragment_shader(grade,format); // validates GPU budget for either request
        const auto source=vertex ? editor::color::grade_vertex_shader() : fragment;
        return env->NewStringUTF(source.c_str()); // generated ASCII only
    } catch (const std::bad_alloc&) { fail(env,"java/lang/OutOfMemoryError","color shader allocation failed"); }
      catch (const std::exception& error) { fail(env,"java/lang/IllegalArgumentException",error.what()); }
    return nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_ColorEngine_nativeValidateVideoSourceV1(JNIEnv* env, jobject,
    jint primaries, jint transfer, jint range, jboolean hdr_static) {
    try {
        editor::color::validate_sdr_video_source({
            static_cast<editor::color::VideoPrimaries>(primaries),
            static_cast<editor::color::VideoTransfer>(transfer),
            static_cast<editor::color::VideoRange>(range),hdr_static != JNI_FALSE});
    } catch (const std::bad_alloc&) { fail(env,"java/lang/OutOfMemoryError","color metadata validation allocation failed"); }
      catch (const std::exception& error) { fail(env,"java/lang/IllegalArgumentException",error.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_owncut_app_ColorEngine_nativeValidateDecodedFormatV1(JNIEnv* env, jobject, jintArray values) {
    try {
        if (!values || env->GetArrayLength(values) != 12)
            throw std::invalid_argument("decoded format comparison requires 12 integers");
        std::array<jint,12> block{};
        env->GetIntArrayRegion(values,0,12,block.data());
        if (env->ExceptionCheck()) return;
        const auto decode=[&](std::size_t offset) {
            const auto flag=block[offset+5];
            if (flag != 0 && flag != 1) throw std::invalid_argument("invalid decoded HDR flag");
            return editor::color::DecodedVideoFormat{block[offset],block[offset+1],{
                static_cast<editor::color::VideoPrimaries>(block[offset+2]),
                static_cast<editor::color::VideoTransfer>(block[offset+3]),
                static_cast<editor::color::VideoRange>(block[offset+4]),flag != 0}};
        };
        editor::color::validate_decoded_video_format(decode(0),decode(6));
    } catch (const std::bad_alloc&) { fail(env,"java/lang/OutOfMemoryError","decoded format validation allocation failed"); }
      catch (const std::exception& error) { fail(env,"java/lang/IllegalArgumentException",error.what()); }
}

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_owncut_app_ColorEngine_nativeCompareRgba8V1(JNIEnv* env, jobject,
    jobject expected, jobject actual, jint width, jint height, jint expected_stride, jint actual_stride) {
    try {
        if(width<=0 || height<=0 || expected_stride<=0 || actual_stride<=0)
            throw std::invalid_argument("invalid comparison dimensions/stride");
        const auto e=buffer(env,expected,false),a=buffer(env,actual,false);
        const auto result=editor::color::compare_rgba8({{e.data,e.size},static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),static_cast<std::size_t>(expected_stride)},
            {{a.data,a.size},static_cast<std::size_t>(width),static_cast<std::size_t>(height),static_cast<std::size_t>(actual_stride)});
        const std::array<jdouble,7> fields{static_cast<double>(result.pixels),
            static_cast<double>(result.maximum_rgb_error[0]),static_cast<double>(result.maximum_rgb_error[1]),
            static_cast<double>(result.maximum_rgb_error[2]),result.mean_absolute_rgb_error,
            result.mean_signed_rgb_error,static_cast<double>(result.alpha_mismatches)};
        auto out=env->NewDoubleArray(static_cast<jsize>(fields.size()));
        if(out) env->SetDoubleArrayRegion(out,0,static_cast<jsize>(fields.size()),fields.data());
        return out;
    } catch(const std::bad_alloc&) { fail(env,"java/lang/OutOfMemoryError","comparison allocation failed"); }
      catch(const std::exception& error) { fail(env,"java/lang/IllegalArgumentException",error.what()); }
    return nullptr;
}

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_owncut_app_ColorEngine_nativeWheelV1(JNIEnv* env,jobject,jdoubleArray values,jboolean enabled,
    jint group,jdouble x,jdouble y,jboolean position_only) {
    try {
        if(!values || env->GetArrayLength(values)!=17) throw std::invalid_argument("wheel requires 17 parameters");
        editor::color::ParameterBlock block;
        env->GetDoubleArrayRegion(values,0,17,block.data());
        if(env->ExceptionCheck()) return nullptr;
        const auto grade=editor::color::from_parameters(block,enabled!=JNI_FALSE);
        if(position_only) {
            const auto point=editor::color::wheel_position(grade,group);
            auto out=env->NewDoubleArray(2);if(out) env->SetDoubleArrayRegion(out,0,2,point.data());return out;
        }
        const auto adjusted=editor::color::parameters(editor::color::wheel_adjust(grade,group,x,y));
        auto out=env->NewDoubleArray(17);if(out) env->SetDoubleArrayRegion(out,0,17,adjusted.data());return out;
    } catch(const std::bad_alloc&) { fail(env,"java/lang/OutOfMemoryError","wheel allocation failed"); }
      catch(const std::exception& error) { fail(env,"java/lang/IllegalArgumentException",error.what()); }
    return nullptr;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_owncut_app_ColorEngine_nativeHdrStaticPresentV1(JNIEnv* env,jobject,jbyteArray value) {
    if(!value) return JNI_FALSE;
    const auto size=env->GetArrayLength(value);
    if(size==0) return JNI_FALSE;
    if(size!=25) return JNI_TRUE; // unknown shape remains blocked, no unbounded allocation
    std::array<jbyte,25> bytes{};env->GetByteArrayRegion(value,0,25,bytes.data());
    if(env->ExceptionCheck()) return JNI_TRUE;
    return editor::color::hdr_static_info_present({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()}) ? JNI_TRUE:JNI_FALSE;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_owncut_app_ColorEngine_nativeVisibleGeometryV1(JNIEnv* env,jobject,jintArray values) {
    try {
        if(!values || env->GetArrayLength(values)!=7)
            throw std::invalid_argument("video geometry requires 7 integers");
        std::array<jint,7> fields{};
        env->GetIntArrayRegion(values,0,7,fields.data());
        if(env->ExceptionCheck()) return nullptr;
        const auto size=editor::color::visible_video_geometry(fields[0],fields[1],fields[2],
            {fields[3],fields[4],fields[5],fields[6]});
        const std::array<jint,2> output{size[0],size[1]};
        auto result=env->NewIntArray(2);
        if(result) env->SetIntArrayRegion(result,0,2,output.data());
        return result;
    } catch(const std::bad_alloc&) { fail(env,"java/lang/OutOfMemoryError","video geometry allocation failed"); }
      catch(const std::exception& error) { fail(env,"java/lang/IllegalArgumentException",error.what()); }
    return nullptr;
}
