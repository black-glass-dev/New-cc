#include "color/grade_archive.hpp"
#include "color/extended_tools.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace color {
namespace {
constexpr std::size_t limit=32U*1024U*1024U;
void require(bool b,const char* s) {if(!b) throw std::invalid_argument(s);}
struct Writer {
    std::vector<std::byte> bytes;
    void u8(std::uint8_t v) {require(bytes.size()<limit,"Grade archive budget");bytes.push_back(static_cast<std::byte>(v));}
    void u32(std::uint32_t v) {for(unsigned i=0;i<4;++i) u8(static_cast<std::uint8_t>(v>>(8*i)));}
    void u64(std::uint64_t v) {for(unsigned i=0;i<8;++i) u8(static_cast<std::uint8_t>(v>>(8*i)));}
    void f(float v) {require(std::isfinite(v),"Nonfinite archive value");u32(std::bit_cast<std::uint32_t>(v));}
    void b(bool v) {u8(v?1:0);}
    void v(Vec3 value) {f(value.r);f(value.g);f(value.b);}
    void m(const Mat3& matrix) {for(auto x:matrix.m) {require(std::isfinite(x),"Nonfinite archive matrix");u64(std::bit_cast<std::uint64_t>(x));}}
    void text(const std::string& s) {require(s.size()<=256,"Archive label budget");u32(static_cast<std::uint32_t>(s.size()));for(char c:s) u8(static_cast<std::uint8_t>(static_cast<unsigned char>(c)));}
};
struct Reader {
    std::span<const std::byte> bytes;std::size_t offset{};
    std::uint8_t u8() {require(offset<bytes.size(),"Truncated grade archive");return std::to_integer<std::uint8_t>(bytes[offset++]);}
    std::uint32_t u32() {std::uint32_t v=0;for(unsigned i=0;i<4;++i) v|=static_cast<std::uint32_t>(u8())<<(8*i);return v;}
    std::uint64_t u64() {std::uint64_t v=0;for(unsigned i=0;i<8;++i) v|=static_cast<std::uint64_t>(u8())<<(8*i);return v;}
    std::size_t count(std::size_t max) {const auto n=u32();require(n<=max,"Grade archive count budget");return n;}
    float f() {auto v=std::bit_cast<float>(u32());require(std::isfinite(v),"Nonfinite archive value");return v;}
    bool b() {auto v=u8();require(v<=1,"Invalid archive boolean");return v!=0;}
    Vec3 v() {const float r=f(),g=f(),b_=f();return {r,g,b_};}
    Mat3 m() {Mat3 matrix;for(auto& x:matrix.m) {x=std::bit_cast<double>(u64());require(std::isfinite(x),"Nonfinite archive matrix");}return matrix;}
    std::string text() {auto n=count(256);require(n<=bytes.size()-offset,"Truncated archive label");std::string s;s.reserve(n);for(std::size_t i=0;i<n;++i) s.push_back(static_cast<char>(u8()));return s;}
};
std::uint32_t checksum(std::span<const std::byte> data) {std::uint32_t h=2166136261U;for(auto b:data) {h^=std::to_integer<std::uint8_t>(b);h*=16777619U;}return h;}
void curve(Writer& w,const std::shared_ptr<const Curve>& c) {w.b(static_cast<bool>(c));if(c) {w.u32(static_cast<std::uint32_t>(c->points().size()));for(auto p:c->points()) {w.f(p.x);w.f(p.y);}}}
std::shared_ptr<const Curve> curve(Reader& r) {if(!r.b()) return {};std::vector<CurvePoint> points(r.count(64));for(auto& p:points) {p.x=r.f();p.y=r.f();}return std::make_shared<Curve>(points);}
void operation(Writer& w,const Operator& op) {
    w.u8(static_cast<std::uint8_t>(op.index()));
    std::visit([&](const auto& p) {using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,CST>) { (void)PreparedCST(p);w.u32(static_cast<std::uint32_t>(p.input_gamut));w.u32(static_cast<std::uint32_t>(p.output_gamut));w.u32(static_cast<std::uint32_t>(p.input_transfer));w.u32(static_cast<std::uint32_t>(p.output_transfer));w.f(p.exposure_stops);w.f(p.tone_knee);w.f(p.gamut_compression); }
        else if constexpr(std::is_same_v<T,RawDecodeParams>) { (void)apply(Vec3{},p);w.v(p.black_level);w.v(p.white_level);w.v(p.white_balance);w.m(p.camera_to_working);w.f(p.exposure_stops); }
        else if constexpr(std::is_same_v<T,Primaries>) { (void)apply(Vec3{.18F,.18F,.18F},p);w.v(p.lift);w.v(p.gamma);w.v(p.gain);w.v(p.offset);for(float x:{p.exposure,p.contrast,p.pivot,p.saturation,p.hue_radians,p.vibrance,p.temperature,p.tint,p.luminance_mix}) w.f(x); }
        else if constexpr(std::is_same_v<T,LogWheels>) { (void)apply(Vec3{},p);w.v(p.shadows);w.v(p.midtones);w.v(p.highlights);w.f(p.low);w.f(p.high);w.f(p.softness); }
        else if constexpr(std::is_same_v<T,HDRWheels>) { (void)apply(Vec3{},p);for(auto x:p.stops) w.f(x);for(auto x:p.centers) w.f(x);w.f(p.falloff);w.f(p.soft_clip); }
        else if constexpr(std::is_same_v<T,ColorSlice>) { (void)apply(Vec3{},p);for(auto x:p.hue_sat_density) w.v(x);w.f(p.width);w.v(p.shadow_tint);w.v(p.highlight_tint); }
        else if constexpr(std::is_same_v<T,RGBMixer>) { (void)apply(Vec3{},p);w.m(p.matrix);w.b(p.monochrome); }
        else if constexpr(std::is_same_v<T,HSLQualifier>) { (void)qualify(Vec3{},p);w.v(p.center);w.v(p.width);w.v(p.softness);w.b(p.invert); }
        else if constexpr(std::is_same_v<T,PowerWindow>) { (void)window_mask(0,0,p);w.u32(static_cast<std::uint32_t>(p.shape));for(float x:{p.cx,p.cy,p.sx,p.sy,p.rotation,p.feather}) w.f(x);w.b(p.invert);w.f(p.corner_radius); }
        else if constexpr(std::is_same_v<T,std::shared_ptr<const Lut1D>>) {require(static_cast<bool>(p),"Missing 1D LUT in archive");w.u32(static_cast<std::uint32_t>(p->size()));w.v(p->domain_min());w.v(p->domain_max());for(auto v:p->values()) w.v(v);}
        else if constexpr(std::is_same_v<T,ExternalPlane>) throw std::invalid_argument("External image planes are not archivable; store the source recipe instead");
        else if constexpr(std::is_same_v<T,Curves>) {curve(w,p.master);curve(w,p.red);curve(w,p.green);curve(w,p.blue);}
        else if constexpr(std::is_same_v<T,std::shared_ptr<const Lut3D>>) {require(static_cast<bool>(p),"Missing LUT in archive");w.u32(static_cast<std::uint32_t>(p->edge()));w.v(p->domain_min());w.v(p->domain_max());for(auto v:p->values()) w.v(v);}
        else if constexpr(std::is_same_v<T,Grain>) {(void)grain(Vec3{},0,0,0,p);w.f(p.amount);w.f(p.colored);w.u32(p.seed);}
        else if constexpr(std::is_same_v<T,SpatialFilter>) {validate_spatial(p);w.u32(static_cast<std::uint32_t>(p.kind));w.u32(static_cast<std::uint32_t>(p.radius));w.f(p.spatial_sigma);w.f(p.range_sigma);w.f(p.amount);w.f(p.threshold);w.v(p.airlight);w.f(p.transmission_floor);}
        else if constexpr(std::is_same_v<T,PolygonWindow>) {(void)polygon_mask(0,0,p);w.u32(static_cast<std::uint32_t>(p.count));w.f(p.feather);w.b(p.invert);for(std::size_t i=0;i<p.count;++i) {w.f(p.vertices[i].x);w.f(p.vertices[i].y);}}
    },op);
}
Operator operation(Reader& r) {
    const auto index=r.u8();
    switch(index) {
    case 0:return {};
    case 1:{CST p;p.input_gamut=static_cast<Gamut>(r.count(10));p.output_gamut=static_cast<Gamut>(r.count(10));p.input_transfer=static_cast<Transfer>(r.count(11));p.output_transfer=static_cast<Transfer>(r.count(11));p.exposure_stops=r.f();p.tone_knee=r.f();p.gamut_compression=r.f();(void)PreparedCST(p);return p;}
    case 2:{RawDecodeParams p;p.black_level=r.v();p.white_level=r.v();p.white_balance=r.v();p.camera_to_working=r.m();p.exposure_stops=r.f();(void)apply(Vec3{},p);return p;}
    case 3:{Primaries p;p.lift=r.v();p.gamma=r.v();p.gain=r.v();p.offset=r.v();p.exposure=r.f();p.contrast=r.f();p.pivot=r.f();p.saturation=r.f();p.hue_radians=r.f();p.vibrance=r.f();p.temperature=r.f();p.tint=r.f();p.luminance_mix=r.f();(void)apply(Vec3{.18F,.18F,.18F},p);return p;}
    case 4:{LogWheels p;p.shadows=r.v();p.midtones=r.v();p.highlights=r.v();p.low=r.f();p.high=r.f();p.softness=r.f();(void)apply(Vec3{},p);return p;}
    case 5:{HDRWheels p;for(auto& x:p.stops) x=r.f();for(auto& x:p.centers) x=r.f();p.falloff=r.f();p.soft_clip=r.f();(void)apply(Vec3{},p);return p;}
    case 6:{ColorSlice p;for(auto& x:p.hue_sat_density) x=r.v();p.width=r.f();p.shadow_tint=r.v();p.highlight_tint=r.v();(void)apply(Vec3{},p);return p;}
    case 7:{RGBMixer p;p.matrix=r.m();p.monochrome=r.b();(void)apply(Vec3{},p);return p;}
    case 8:{HSLQualifier p;p.center=r.v();p.width=r.v();p.softness=r.v();p.invert=r.b();(void)qualify(Vec3{},p);return p;}
    case 9:{PowerWindow p;p.shape=static_cast<PowerWindow::Shape>(r.count(3));p.cx=r.f();p.cy=r.f();p.sx=r.f();p.sy=r.f();p.rotation=r.f();p.feather=r.f();p.invert=r.b();p.corner_radius=r.f();(void)window_mask(0,0,p);return p;}
    case 10:{Curves p;p.master=curve(r);p.red=curve(r);p.green=curve(r);p.blue=curve(r);return p;}
    case 11:{const auto edge=r.count(65);require(edge>=2,"Invalid archive LUT edge");const auto lo=r.v(),hi=r.v();const auto n=edge*edge*edge;require(n<=(r.bytes.size()-r.offset)/12,"Truncated archive LUT");std::vector<Vec3> values(n);for(auto& v:values) v=r.v();return std::make_shared<Lut3D>(edge,std::move(values),lo,hi);}
    case 12:{Grain p;p.amount=r.f();p.colored=r.f();p.seed=r.u32();(void)grain(Vec3{},0,0,0,p);return p;}
    case 13:{SpatialFilter p;p.kind=static_cast<SpatialKind>(r.count(7));p.radius=r.count(8);p.spatial_sigma=r.f();p.range_sigma=r.f();p.amount=r.f();p.threshold=r.f();p.airlight=r.v();p.transmission_floor=r.f();validate_spatial(p);return p;}
    case 15:{const auto size=r.count(65536);require(size>=2,"Invalid archive 1D LUT size");const auto lo=r.v(),hi=r.v();require(size<=(r.bytes.size()-r.offset)/12,"Truncated archive 1D LUT");std::vector<Vec3> values(size);for(auto& v:values) v=r.v();return std::make_shared<Lut1D>(std::move(values),lo,hi);}
    case 14:{PolygonWindow p;p.count=r.count(32);p.feather=r.f();p.invert=r.b();for(std::size_t i=0;i<p.count;++i) {p.vertices[i].x=r.f();p.vertices[i].y=r.f();}(void)polygon_mask(0,0,p);return p;}
    default:throw std::invalid_argument("Unsupported archive operator version");
    }
}
void graph(Writer& w,const NodeGraph& g) {
    (void)g.compile();const auto nodes=g.snapshot_nodes();w.u32(static_cast<std::uint32_t>(nodes.size()));w.u64(g.has_explicit_output()?g.output():0);
    for(const auto& n:nodes) {w.u64(n.id);w.u32(static_cast<std::uint32_t>(n.kind));w.u32(static_cast<std::uint32_t>(n.inputs.size()));for(auto edge:n.inputs) w.u64(edge);w.f(n.mix);w.f(n.key_input_gain);w.f(n.key_output_gain);w.b(n.key_invert);w.b(n.bypass);w.b(n.locked);w.u32(n.channel);w.text(n.label);operation(w,n.operation);}
}
NodeGraph graph(Reader& r) {
    NodeGraph g;const auto count=r.count(256);const auto output=r.u64();
    for(std::size_t i=0;i<count;++i) {Node n;n.id=r.u64();n.kind=static_cast<NodeKind>(r.count(static_cast<std::uint32_t>(NodeKind::MatteSubtract)));n.inputs.resize(r.count(16));for(auto& edge:n.inputs) edge=r.u64();n.mix=r.f();n.key_input_gain=r.f();n.key_output_gain=r.f();n.key_invert=r.b();n.bypass=r.b();n.locked=r.b();n.channel=r.u32();n.label=r.text();n.operation=operation(r);g.add(std::move(n));}
    g.set_output(output);(void)g.compile();return g;
}
std::string key(std::string_view s) {require(!s.empty()&&s.size()<=64,"Invalid gallery key length");for(char c:s) require((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-',"Unsafe gallery key");return std::string(s);}
}
std::vector<std::byte> serialize_grade(const GradeProgram& program) {
    Writer payload;graph(payload,program.group_pre);graph(payload,program.clip);graph(payload,program.group_post);graph(payload,program.timeline);
    Writer out;for(char c:std::string_view("OCLRGRD1")) out.u8(static_cast<std::uint8_t>(c));out.u32(static_cast<std::uint32_t>(payload.bytes.size()));out.u32(checksum(payload.bytes));require(payload.bytes.size()<=limit-16,"Grade archive budget");out.bytes.insert(out.bytes.end(),payload.bytes.begin(),payload.bytes.end());return out.bytes;
}
GradeProgram deserialize_grade(std::span<const std::byte> bytes) {
    require(bytes.size()>=16&&bytes.size()<=limit,"Grade archive size budget");Reader r{bytes};for(char c:std::string_view("OCLRGRD1")) require(r.u8()==static_cast<std::uint8_t>(c),"Unknown grade archive magic/version");const auto size=r.u32(),hash=r.u32();require(size==bytes.size()-16&&hash==checksum(bytes.subspan(16)),"Grade archive integrity failure");GradeProgram p;p.group_pre=graph(r);p.clip=graph(r);p.group_post=graph(r);p.timeline=graph(r);require(r.offset==bytes.size(),"Trailing grade archive payload");return p;
}
void save_grade_atomic(const std::filesystem::path& path,const GradeProgram& p) {
    const auto bytes=serialize_grade(p);static std::atomic<std::uint64_t> serial{};
    auto temporary=path;temporary+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial.fetch_add(1));
    auto* file=std::fopen(temporary.string().c_str(),"wbx");if(!file) throw std::runtime_error("Cannot create exclusive grade temporary file");
    const auto written=std::fwrite(bytes.data(),1,bytes.size(),file);const auto closed=std::fclose(file);
    if(written!=bytes.size()||closed!=0) {std::error_code e;std::filesystem::remove(temporary,e);throw std::runtime_error("Grade write failed");}
    std::error_code error;std::filesystem::rename(temporary,path,error);if(error) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw std::runtime_error("Atomic grade replacement failed");}
}
GradeProgram load_grade(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);require(size>=16&&size<=limit,"Grade archive file size budget");std::vector<std::byte> bytes(static_cast<std::size_t>(size));std::ifstream file(path,std::ios::binary);file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));require(static_cast<std::size_t>(file.gcount())==bytes.size()&&file.peek()==std::char_traits<char>::eof(),"Grade read failed/changed concurrently");return deserialize_grade(bytes);
}
LookGallery::LookGallery(std::filesystem::path directory):directory_(std::move(directory)) {std::filesystem::create_directories(directory_);}
void LookGallery::store(std::string_view id,const GradeProgram& p) {const auto name=key(id);std::lock_guard<std::mutex> lock(mutex_);save_grade_atomic(directory_/(name+".ocg"),p);}
GradeProgram LookGallery::recall(std::string_view id) const {const auto name=key(id);std::lock_guard<std::mutex> lock(mutex_);return load_grade(directory_/(name+".ocg"));}
void LookGallery::erase(std::string_view id) {const auto name=key(id);std::lock_guard<std::mutex> lock(mutex_);std::filesystem::remove(directory_/(name+".ocg"));}
std::vector<std::string> LookGallery::list() const {std::lock_guard<std::mutex> lock(mutex_);std::vector<std::string> result;for(const auto& entry:std::filesystem::directory_iterator(directory_)) if(entry.is_regular_file()&&entry.path().extension()==".ocg") result.push_back(entry.path().stem().string());std::sort(result.begin(),result.end());return result;}
} // namespace color
