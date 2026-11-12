#include "editor/color.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <istream>
#include <limits>
#include <ostream>
#include <string_view>
#include <utility>

namespace editor::color {
namespace {
void range(double value, double low, double high) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::invalid_argument("grading parameter outside supported finite range");
}
void finite(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("non-finite color value");
}
std::size_t checked_extent(std::size_t size, std::size_t width, std::size_t height, std::size_t stride) {
    if (width == 0 || height == 0 || width > max_pixels / height)
        throw std::invalid_argument("invalid image dimensions or pixel budget exceeded");
    const auto row = width * 4; // bounded by max_pixels even on 32-bit size_t
    if (stride < row || size < row || height - 1 > (size - row) / stride)
        throw std::invalid_argument("RGBA buffer/stride too small");
    return (height - 1) * stride + row;
}
std::uint8_t pack(double value) {
    finite(value);
    return static_cast<std::uint8_t>(std::floor(std::clamp(value,0.0,1.0) * 255.0 + 0.5));
}
bool neutral(const Grade& g) {
    return !g.enabled || g.strength == 0 ||
        (g.exposure_stops == 0 && g.contrast == 1 && g.saturation == 1 &&
         g.balance_stops == std::array<double,3>{0,0,0} && g.lift == std::array<double,3>{0,0,0} &&
         g.gamma == std::array<double,3>{1,1,1} && g.gain == std::array<double,3>{1,1,1});
}
void byte(std::ostream& out, std::uint8_t value) {
    out.put(static_cast<char>(value));
    if (!out) throw std::runtime_error("could not write grade preset");
}
std::uint8_t byte(std::istream& in) {
    const auto value = in.get();
    if (value == std::char_traits<char>::eof()) throw std::runtime_error("truncated grade preset");
    return static_cast<std::uint8_t>(value);
}
void u64(std::ostream& out, std::uint64_t value, unsigned bytes) {
    for (unsigned i=0;i<bytes;++i) byte(out,static_cast<std::uint8_t>(value >> (i*8)));
}
std::uint64_t u64(std::istream& in, unsigned bytes) {
    std::uint64_t value = 0;
    for (unsigned i=0;i<bytes;++i) value |= static_cast<std::uint64_t>(byte(in)) << (i*8);
    return value;
}
// One explicit field order shared by read/write; never serialize struct padding.
template<class G, class F> void fields(G& g, F visit) {
    visit(g.exposure_stops); visit(g.contrast); visit(g.saturation); visit(g.pivot); visit(g.strength);
    for (auto& x : g.balance_stops) visit(x);
    for (auto& x : g.lift) visit(x);
    for (auto& x : g.gamma) visit(x);
    for (auto& x : g.gain) visit(x);
}
} // namespace
void validate(const Grade& g) {
    range(g.exposure_stops,-8,8); range(g.contrast,0,4); range(g.saturation,0,4);
    range(g.pivot,0.001,1); range(g.strength,0,1);
    for (auto x : g.balance_stops) range(x,-2,2);
    for (auto x : g.lift) range(x,-1,1);
    for (auto x : g.gamma) range(x,0.1,4);
    for (auto x : g.gain) range(x,0,4);
}
namespace {
struct WheelSpec { double low, high, radius; };
WheelSpec wheel_spec(int group) {
    switch(group) {
        case 0:return {-2,2,1.5};
        case 1:return {-1,1,0.25};
        case 2:return {0.1,4,0.75};
        case 3:return {0,4,0.75};
        default:throw std::invalid_argument("unknown wheel group");
    }
}
std::array<double,3>& wheel_values(Grade& g,int group) {
    switch(group) {
        case 0:return g.balance_stops;
        case 1:return g.lift;
        case 2:return g.gamma;
        case 3:return g.gain;
        default:throw std::invalid_argument("unknown wheel group");
    }
}
}
Grade wheel_adjust(const Grade& base,int group,double x,double y) {
    validate(base);const auto spec=wheel_spec(group);
    if(!std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1 || std::abs(y)>1)
        throw std::invalid_argument("wheel coordinates must be finite within [-1,1]");
    const auto length=std::hypot(x,y);
    if(length>1) { x/=length;y/=length; }
    auto output=base;auto& channels=wheel_values(output,group);
    const auto master=(channels[0]+channels[1]+channels[2])/3;
    const std::array<double,3> delta{x,-0.5*x-std::sqrt(3.0)*0.5*y,-0.5*x+std::sqrt(3.0)*0.5*y};
    for(std::size_t i=0;i<3;++i) channels[i]=std::clamp(master+spec.radius*delta[i],spec.low,spec.high);
    validate(output);return output;
}
std::array<double,2> wheel_position(const Grade& grade,int group) {
    validate(grade);const auto spec=wheel_spec(group);auto copy=grade;
    const auto& c=wheel_values(copy,group);
    double x=(2*c[0]-c[1]-c[2])/(3*spec.radius);
    double y=(c[2]-c[1])/(std::sqrt(3.0)*spec.radius);
    const auto length=std::hypot(x,y);
    if(length>1) { x/=length;y/=length; }
    return {x,y};
}
bool hdr_static_info_present(std::span<const std::uint8_t> bytes) noexcept {
    if(bytes.empty()) return false;
    return bytes.size()!=25 || std::any_of(bytes.begin(),bytes.end(),[](auto byte){return byte!=0;});
}
ParameterBlock parameters(const Grade& grade) {
    validate(grade);
    ParameterBlock values{};
    std::size_t i = 0;
    fields(grade,[&](double x){ values.at(i++)=x; });
    return values;
}
Grade from_parameters(std::span<const double> values, bool enabled) {
    if (values.size() != parameter_count) throw std::invalid_argument("grade v1 requires 17 parameters");
    Grade grade; grade.enabled=enabled;
    std::size_t i = 0;
    fields(grade,[&](double& x){ x=values[i++]; });
    validate(grade);
    return grade;
}
double decode_srgb(double encoded) {
    finite(encoded);
    const auto x = std::abs(encoded);
    const auto value = x <= 0.04045 ? x/12.92 : std::pow((x+0.055)/1.055,2.4);
    if (!std::isfinite(value)) throw std::overflow_error("sRGB decode overflow");
    return std::copysign(value,encoded);
}
double encode_srgb(double linear) {
    finite(linear);
    const auto x = std::abs(linear);
    const auto value = x <= 0.0031308 ? 12.92*x : 1.055*std::pow(x,1.0/2.4)-0.055;
    if (!std::isfinite(value)) throw std::overflow_error("sRGB encode overflow");
    return std::copysign(value,linear);
}
Pipeline::Pipeline(Grade grade) : grade_(std::move(grade)), multipliers_{}, identity_(false) {
    validate(grade_);
    identity_ = neutral(grade_);
    for (std::size_t c=0;c<3;++c) multipliers_[c] = std::exp2(grade_.exposure_stops+grade_.balance_stops[c]);
}
Rgb Pipeline::linear(Rgb input) const {
    const std::array original{input.r,input.g,input.b};
    for (auto x : original) range(x,-65504,65504);
    if (identity_) return input;
    auto values = original;
    for (std::size_t c=0;c<3;++c) {
        auto x = values[c] * multipliers_[c];
        x = (x + grade_.lift[c]*(1-x)) * grade_.gain[c];
        x = std::copysign(std::pow(std::abs(x),1.0/grade_.gamma[c]),x);
        values[c] = grade_.pivot + (x-grade_.pivot)*grade_.contrast;
    }
    const auto y = 0.2126*values[0]+0.7152*values[1]+0.0722*values[2];
    for (std::size_t c=0;c<3;++c) {
        const auto saturated = y + grade_.saturation*(values[c]-y);
        values[c] = original[c] + grade_.strength*(saturated-original[c]);
        finite(values[c]);
    }
    return {values[0],values[1],values[2]};
}
Rgb Pipeline::srgb(Rgb input) const {
    // Identity still validates caller values; byte identity uses the bulk copy path.
    const auto graded = linear({decode_srgb(input.r),decode_srgb(input.g),decode_srgb(input.b)});
    return {encode_srgb(graded.r),encode_srgb(graded.g),encode_srgb(graded.b)};
}
bool apply_rgba8(Rgba8View source, Rgba8Buffer output, const Grade& grade, const std::atomic_bool* cancelled) {
    const Pipeline pipeline(grade);
    const auto src_size = checked_extent(source.bytes.size(),source.width,source.height,source.stride);
    const auto dst_size = checked_extent(output.bytes.size(),output.width,output.height,output.stride);
    if (source.width != output.width || source.height != output.height)
        throw std::invalid_argument("RGBA source/output dimensions differ");
    const std::less<const std::uint8_t*> before;
    if (before(source.bytes.data(),output.bytes.data()+dst_size) &&
        before(output.bytes.data(),source.bytes.data()+src_size))
        throw std::invalid_argument("overlapping RGBA buffers are not supported");
    for (std::size_t y=0;y<source.height;++y) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return false;
        const auto* in = source.bytes.data()+y*source.stride;
        auto* out = output.bytes.data()+y*output.stride;
        if (pipeline.identity()) { std::copy_n(in,source.width*4,out); continue; }
        for (std::size_t x=0;x<source.width;++x) {
            if ((x & 255U) == 0 && cancelled && cancelled->load(std::memory_order_relaxed)) return false;
            const auto offset = x*4;
            const auto rgb = pipeline.srgb({in[offset]/255.0,in[offset+1]/255.0,in[offset+2]/255.0});
            out[offset] = pack(rgb.r); out[offset+1] = pack(rgb.g); out[offset+2] = pack(rgb.b);
            out[offset+3] = in[offset+3];
        }
    }
    return true;
}
FrameComparison compare_rgba8(Rgba8View expected, Rgba8View actual) {
    (void)checked_extent(expected.bytes.size(),expected.width,expected.height,expected.stride);
    (void)checked_extent(actual.bytes.size(),actual.width,actual.height,actual.stride);
    if (expected.width != actual.width || expected.height != actual.height)
        throw std::invalid_argument("frame comparison dimensions differ");
    FrameComparison result;
    result.pixels=static_cast<std::uint64_t>(expected.width*expected.height);
    std::uint64_t absolute_sum=0;
    std::int64_t signed_sum=0;
    for (std::size_t y=0;y<expected.height;++y) for (std::size_t x=0;x<expected.width;++x) {
        const auto e=y*expected.stride+x*4, a=y*actual.stride+x*4;
        for (std::size_t c=0;c<3;++c) {
            const auto delta=static_cast<int>(actual.bytes[a+c])-static_cast<int>(expected.bytes[e+c]);
            const auto magnitude=static_cast<std::uint8_t>(std::abs(delta));
            result.maximum_rgb_error[c]=std::max(result.maximum_rgb_error[c],magnitude);
            absolute_sum+=magnitude;signed_sum+=delta;
        }
        if(expected.bytes[e+3] != actual.bytes[a+3]) ++result.alpha_mismatches;
    }
    const auto channels=static_cast<double>(result.pixels)*3;
    result.mean_absolute_rgb_error=static_cast<double>(absolute_sum)/channels;
    result.mean_signed_rgb_error=static_cast<double>(signed_sum)/channels;
    return result;
}

Histogram histogram(Rgba8View source, bool skip_transparent) {
    (void)checked_extent(source.bytes.size(),source.width,source.height,source.stride);
    Histogram result;
    for (std::size_t y=0;y<source.height;++y) for (std::size_t x=0;x<source.width;++x) {
        const auto* p = source.bytes.data()+y*source.stride+x*4;
        if (skip_transparent && p[3] == 0) continue;
        ++result.red[p[0]]; ++result.green[p[1]]; ++result.blue[p[2]];
        const auto luma = 0.2126*decode_srgb(p[0]/255.0)+0.7152*decode_srgb(p[1]/255.0)+0.0722*decode_srgb(p[2]/255.0);
        ++result.luminance[pack(encode_srgb(luma))]; ++result.pixels;
    }
    return result;
}
void PresetCodec::write(const Grade& grade, std::ostream& output) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    validate(grade); // invalid settings must not write even a partial header
    output.write("OWNCUTCG",8);
    if (!output) throw std::runtime_error("could not write grade preset header");
    u64(output,version,4); byte(output,grade.enabled ? 1 : 0);
    fields(grade,[&](double x){ u64(output,std::bit_cast<std::uint64_t>(x),8); });
}
Grade PresetCodec::read(std::istream& input) {
    char magic[8]{}; input.read(magic,8);
    if (!input || std::string_view(magic,8) != "OWNCUTCG") throw std::runtime_error("invalid grade preset header");
    if (u64(input,4) != version) throw UnsupportedGradeVersion("unsupported grade preset version");
    const auto enabled = byte(input);
    if (enabled > 1) throw std::runtime_error("invalid grade bypass flag");
    Grade result; result.enabled = enabled != 0;
    fields(result,[&](double& x){ x=std::bit_cast<double>(u64(input,8)); });
    if (input.get() != std::char_traits<char>::eof()) throw std::runtime_error("trailing grade preset data");
    if (input.bad() || !input.eof()) throw std::runtime_error("I/O error reading grade preset");
    validate(result);
    return result;
}
} // namespace editor::color
