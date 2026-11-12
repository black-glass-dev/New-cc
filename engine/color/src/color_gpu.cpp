#include "editor/color_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace editor::color {
void validate_sdr_video_source(const VideoColorMetadata& source) {
    if (source.has_hdr_static_metadata) throw std::invalid_argument("HDR static metadata is unsupported for SDR grading");
    if (source.primaries != VideoPrimaries::rec709)
        throw std::invalid_argument("grade source requires explicit Rec709 primaries");
    if (source.transfer != VideoTransfer::sdr_video)
        throw std::invalid_argument("grade source requires explicit SDR video transfer; unknown/HDR unsupported");
    if (source.range != VideoRange::full && source.range != VideoRange::limited)
        throw std::invalid_argument("grade source requires explicit full or limited video range");
}
std::array<int,2> visible_video_geometry(int width,int height,int crop_mask,const std::array<int,4>& crop) {
    const auto describe=[&] {
        return "coded="+std::to_string(width)+"x"+std::to_string(height)+" crop-mask="+std::to_string(crop_mask)+
            " crop=["+std::to_string(crop[0])+","+std::to_string(crop[1])+","+
            std::to_string(crop[2])+","+std::to_string(crop[3])+"]";
    };
    if(width<=0 || height<=0) throw std::invalid_argument("invalid video dimensions: "+describe());
    if(crop_mask==0) return {width,height};
    if(crop_mask!=15) throw std::invalid_argument("incomplete video crop rectangle: "+describe());
    const auto left=crop[0],top=crop[1],right=crop[2],bottom=crop[3];
    if(left<0 || top<0 || right<left || bottom<top || right>=width || bottom>=height)
        throw std::invalid_argument("invalid video crop bounds: "+describe());
    // Bounds above make inclusive +1 arithmetic safe, even at INT_MAX dimensions.
    return {right-left+1,bottom-top+1};
}
void validate_decoded_video_format(const DecodedVideoFormat& expected, const DecodedVideoFormat& actual) {
    validate_sdr_video_source(expected.color);
    validate_sdr_video_source(actual.color);
    if (expected.width <= 0 || expected.height <= 0 || actual.width != expected.width || actual.height != expected.height)
        throw std::invalid_argument("decoded video geometry differs from admitted input: expected-visible="+
            std::to_string(expected.width)+"x"+std::to_string(expected.height)+" actual-visible="+
            std::to_string(actual.width)+"x"+std::to_string(actual.height));
    if (actual.color.primaries != expected.color.primaries || actual.color.transfer != expected.color.transfer ||
        actual.color.range != expected.color.range)
        throw std::invalid_argument("decoded video color differs from admitted input");
}
void validate(TextureEncoding e) {
    if (e != TextureEncoding::srgb && e != TextureEncoding::linear709 && e != TextureEncoding::media3_sdr709)
        throw std::invalid_argument("unsupported grading texture encoding");
}
namespace {
double decode(double x, TextureEncoding e) {
    if (e == TextureEncoding::srgb) return decode_srgb(x);
    if (e == TextureEncoding::linear709) return x;
    const auto a = std::abs(x);
    // Matches the explicitly pinned Media3 1.8.1 DEFAULT SDR working contract.
    return std::copysign(a < 0.0812 ? a/4.5 : std::pow((a+0.099)/1.099,1.0/0.45),x);
}
double encode(double x, TextureEncoding e) {
    if (e == TextureEncoding::srgb) return encode_srgb(x);
    if (e == TextureEncoding::linear709) return x;
    const auto a = std::abs(x);
    return std::copysign(a < 0.018 ? a*4.5 : 1.099*std::pow(a,0.45)-0.099,x);
}
std::string number(double x) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::scientific << std::setprecision(17) << x;
    return out.str();
}
std::string vector(const std::array<double,3>& x) {
    return "vec3("+number(x[0])+","+number(x[1])+","+number(x[2])+")";
}
void gpu_budget(const Grade& g) {
    double bound=1;
    for (std::size_t c=0;c<3;++c) {
        const auto exposed=std::exp2(g.exposure_stops+g.balance_stops[c]);
        const auto lifted=std::max(std::abs(g.lift[c]),std::abs(exposed+g.lift[c]*(1-exposed)))*g.gain[c];
        const auto powered=std::pow(lifted,1/g.gamma[c]);
        // Reject unsafe intermediates even if contrast/strength could later cancel them.
        if (!std::isfinite(powered) || powered > 1e25) throw std::invalid_argument("grade exceeds safe GPU float budget");
        bound=std::max(bound,g.pivot+(powered+g.pivot)*g.contrast);
    }
    if (bound*(1+2*g.saturation) > 1e25) throw std::invalid_argument("grade exceeds safe GPU float budget");
}
} // namespace
Rgb texture_reference(Rgb rgb,const Grade& grade,TextureEncoding e) {
    validate(e);
    for (auto x : {rgb.r,rgb.g,rgb.b}) if (!std::isfinite(x) || x<0 || x>1)
        throw std::invalid_argument("grading texture input must be normalized RGB709");
    const Pipeline pipeline(grade);
    if (pipeline.identity()) return rgb;
    const auto result=pipeline.linear({decode(rgb.r,e),decode(rgb.g,e),decode(rgb.b,e)});
    return {encode(result.r,e),encode(result.g,e),encode(result.b,e)};
}
std::string grade_vertex_shader() {
    return R"glsl(#version 100
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying highp vec2 vTexCoord;
void main() { gl_Position=aPosition; vTexCoord=aTexCoord; }
)glsl";
}
std::string grade_fragment_shader(const Grade& g,TextureEncoding e) {
    validate(g); validate(e);
    std::string shader=R"glsl(#version 100
precision highp float;
uniform highp sampler2D uSource;
varying highp vec2 vTexCoord;
)glsl";
    if (Pipeline(g).identity()) return shader+"void main() { gl_FragColor=texture2D(uSource,vTexCoord); }\n";
    gpu_budget(g);
    // Standard transfer constants, authored here; never rely on a player's implicit
    // transfer assumption. Input/output encoding is identical for this single pass.
    if (e == TextureEncoding::srgb) shader+=R"glsl(
float toLight(float x) { float a=abs(x); return sign(x)*(a<=0.04045 ? a/12.92 : pow((a+0.055)/1.055,2.4)); }
float toSignal(float x) { float a=abs(x); return sign(x)*(a<=0.0031308 ? 12.92*a : 1.055*pow(a,1.0/2.4)-0.055); }
)glsl";
    else if (e == TextureEncoding::media3_sdr709) shader+=R"glsl(
float toLight(float x) { float a=abs(x); return sign(x)*(a<0.0812 ? a/4.5 : pow((a+0.099)/1.099,1.0/0.45)); }
float toSignal(float x) { float a=abs(x); return sign(x)*(a<0.018 ? 4.5*a : 1.099*pow(a,0.45)-0.099); }
)glsl";
    else shader+="float toLight(float x) { return x; }\nfloat toSignal(float x) { return x; }\n";
    std::array<double,3> scale{}, inverse_gamma{};
    for (std::size_t c=0;c<3;++c) { scale[c]=std::exp2(g.exposure_stops+g.balance_stops[c]); inverse_gamma[c]=1/g.gamma[c]; }
    shader+="void main() {\n vec4 sampleColor=texture2D(uSource,vTexCoord);\n";
    shader+=" vec3 original=vec3(toLight(sampleColor.r),toLight(sampleColor.g),toLight(sampleColor.b));\n";
    shader+=" vec3 x=original*"+vector(scale)+";\n";
    shader+=" x=(x+"+vector(g.lift)+"*(vec3(1.0)-x))*"+vector(g.gain)+";\n";
    shader+=" x=sign(x)*pow(abs(x),"+vector(inverse_gamma)+");\n";
    shader+=" x=vec3("+number(g.pivot)+")+(x-vec3("+number(g.pivot)+"))*"+number(g.contrast)+";\n";
    shader+=" float y=dot(x,vec3(0.2126,0.7152,0.0722));\n";
    shader+=" x=vec3(y)+"+number(g.saturation)+"*(x-vec3(y));\n";
    shader+=" x=original+"+number(g.strength)+"*(x-original);\n";
    shader+=" gl_FragColor=vec4(toSignal(x.r),toSignal(x.g),toSignal(x.b),sampleColor.a);\n}\n";
    return shader;
}
} // namespace editor::color
