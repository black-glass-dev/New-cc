#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace color {
inline constexpr std::uint32_t api_version=1;
struct Vec3 { float r{},g{},b{}; };
struct Pixel { float r,g,b,a; };
static_assert(sizeof(Pixel)==4*sizeof(float));
struct Mat3 { std::array<double,9> m{}; static Mat3 identity(); };
Vec3 operator+(Vec3,Vec3); Vec3 operator-(Vec3,Vec3); Vec3 operator*(Vec3,float);
Vec3 multiply(const Mat3&,Vec3); Mat3 multiply(const Mat3&,const Mat3&); Mat3 inverse(const Mat3&);
float luminance(Vec3); bool finite(Vec3); bool finite(Pixel);

enum class Gamut { Rec709,Rec2020,P3D65,ACESAP0,ACESAP1,DaVinciWide,ARRIWide3,ARRIWide4,SonySGamut3,SonySGamut3Cine,PanasonicVGamut };
enum class Transfer { Linear,SRGB,Gamma24,Rec709,DaVinciIntermediate,ACEScct,PQ,HLG,LogC3EI800,LogC4,SLog3,VLog };
struct Chromaticities { double rx,ry,gx,gy,bx,by,wx,wy; };
Chromaticities chromaticities(Gamut);
Mat3 rgb_to_xyz(Gamut); Mat3 bradford(double sx,double sy,double dx,double dy);
float encode(float linear,Transfer); float decode(float encoded,Transfer);
struct CST {
    Gamut input_gamut{Gamut::Rec709},output_gamut{Gamut::Rec709};
    Transfer input_transfer{Transfer::Linear},output_transfer{Transfer::Linear};
    // Explicit analytic mapping, NOT a proprietary DRT or mastering/certification transform.
    float exposure_stops{},tone_knee{},gamut_compression{};
};
class PreparedCST {
public:
    explicit PreparedCST(const CST&);
    Vec3 apply(Vec3) const;
    const Mat3& matrix() const noexcept;
private: CST params_;Mat3 matrix_;float exposure_{};
};
Vec3 transform(Vec3,const CST&);
// Already demosaiced sensor RGB code values, not a camera-file/Bayer decoder.
struct RawDecodeParams {
    Vec3 black_level{},white_level{65535,65535,65535},white_balance{1,1,1};
    Mat3 camera_to_working{Mat3::identity()};float exposure_stops{};
};
Vec3 apply(Vec3 sensor_rgb16,const RawDecodeParams&);

struct ColorFrameBuffer32F {
    float* data{}; std::size_t width{},height{},row_stride_floats{},capacity_floats{};
};
class Image32F;
struct GradeContext {
    std::uint64_t clip_id{},group_id{},source_revision{};
    double timeline_time_sec{};
    std::uint64_t frame_index{};
    const Image32F* temporal_history{};
};
class Image32F {
public:
    Image32F(std::size_t width,std::size_t height,std::size_t budget_bytes=256U*1024U*1024U);
    ~Image32F(); Image32F(Image32F&&) noexcept; Image32F& operator=(Image32F&&) noexcept;
    Image32F(const Image32F&)=delete; Image32F& operator=(const Image32F&)=delete;
    std::size_t width() const noexcept; std::size_t height() const noexcept;
    std::span<Pixel> pixels(); std::span<const Pixel> pixels() const;
    ColorFrameBuffer32F view();
private:
    struct Deleter { void operator()(Pixel*) const noexcept; };
    std::unique_ptr<Pixel,Deleter> data_; std::size_t width_{},height_{};
};

struct Primaries {
    Vec3 lift{},gamma{1,1,1},gain{1,1,1},offset{};
    float exposure{},contrast{1},pivot{0.18F},saturation{1},hue_radians{},vibrance{};
    float temperature{},tint{},luminance_mix{};
};
Vec3 apply(Vec3,const Primaries&);
struct LogWheels { Vec3 shadows{},midtones{},highlights{}; float low{0.2F},high{0.7F},softness{0.1F}; };
Vec3 apply(Vec3,const LogWheels&);
struct HDRWheels {
    std::array<float,6> stops{};
    std::array<float,6> centers{-8,-5,-2,0,2,5};
    float falloff{2},soft_clip{};
};
Vec3 apply(Vec3,const HDRWheels&);
struct ColorSlice { std::array<Vec3,7> hue_sat_density{}; float width{0.6F}; Vec3 shadow_tint{},highlight_tint{}; };
Vec3 apply(Vec3,const ColorSlice&);
struct RGBMixer { Mat3 matrix{Mat3::identity()}; bool monochrome{}; };
Vec3 apply(Vec3,const RGBMixer&);
struct HSLQualifier { Vec3 center{0,0.5F,0.5F},width{0.2F,1,1},softness{0.05F,0.05F,0.05F}; bool invert{}; };
float qualify(Vec3,const HSLQualifier&);
struct PowerWindow {
    enum class Shape { Circle,Rectangle,LinearGradient,RoundedRectangle };
    Shape shape{Shape::Circle}; float cx{0.5F},cy{0.5F},sx{0.25F},sy{0.25F},rotation{},feather{0.05F}; bool invert{};
    // Signed-distance rounded box corner radius; must be finite and within min(sx,sy).
    float corner_radius{};
};
float window_mask(float x,float y,const PowerWindow&);
struct CurvePoint { float x{},y{}; };
class Curve {
public:
    explicit Curve(std::span<const CurvePoint> points);
    float evaluate(float x) const;
    std::vector<float> bake(std::size_t samples) const;
    std::span<const CurvePoint> points() const noexcept;
private: std::vector<CurvePoint> points_; std::vector<float> tangents_;
};
struct Curves { std::shared_ptr<const Curve> master,red,green,blue; };
Vec3 apply(Vec3,const Curves&);
class Lut3D {
public:
    Lut3D(std::size_t edge,std::vector<Vec3> values,Vec3 low={0,0,0},Vec3 high={1,1,1});
    static Lut3D parse_cube(std::string_view text,std::size_t max_edge=65);
    Vec3 tetrahedral(Vec3) const; Vec3 trilinear(Vec3) const;
    std::size_t edge() const noexcept; std::span<const Vec3> values() const;
    Vec3 domain_min() const noexcept;Vec3 domain_max() const noexcept;
private: std::size_t edge_{}; std::vector<Vec3> values_; Vec3 low_,high_;
};
// Per-channel shaper/1D LUT. Parsed from strict .cube LUT_1D_SIZE; DOMAIN_MIN/MAX optional.
class Lut1D {
public:
    Lut1D(std::vector<Vec3> values,Vec3 low={0,0,0},Vec3 high={1,1,1});
    static Lut1D parse_cube(std::string_view text,std::size_t max_size=65536);
    Vec3 apply(Vec3) const;
    std::size_t size() const noexcept; std::span<const Vec3> values() const;
    Vec3 domain_min() const noexcept; Vec3 domain_max() const noexcept;
private: std::vector<Vec3> values_; Vec3 low_,high_;
};
struct Grain { float amount{},colored{}; std::uint32_t seed{}; };
Vec3 grain(Vec3,std::uint32_t x,std::uint32_t y,std::uint32_t frame,const Grain&);
void halation(const Image32F&,Image32F&,float threshold,float amount,std::size_t radius);

enum class SpatialKind { Bilateral,Unsharp,MatteErode,MatteDilate,MatteBlur,TemporalDenoise,Dehaze,Halation };
struct SpatialFilter {
    SpatialKind kind{SpatialKind::Bilateral};std::size_t radius{2};
    float spatial_sigma{1.5F},range_sigma{.1F},amount{1},threshold{.5F};
    Vec3 airlight{1,1,1};float transmission_floor{.1F};
};

struct Point2 { float x{},y{}; };
struct PolygonWindow {
    std::array<Point2,32> vertices{};std::size_t count{};float feather{};bool invert{};
};

// Caller-provided pixel plane (for example depth-derived matte). Dimensions must match the render.
struct ExternalPlane { std::shared_ptr<const Image32F> image; };
using Operator=std::variant<std::monostate,CST,RawDecodeParams,Primaries,LogWheels,HDRWheels,ColorSlice,RGBMixer,HSLQualifier,PowerWindow,Curves,std::shared_ptr<const Lut3D>,Grain,SpatialFilter,PolygonWindow,std::shared_ptr<const Lut1D>,ExternalPlane>;
enum class NodeKind { Serial,Parallel,LayerMixer,Outside,Channel,Combiner,SharedNode,GroupPreClip,GroupPostClip,TimelineNode,MatteUnion,MatteIntersect,MatteSubtract };
struct Node {
    std::uint64_t id{}; NodeKind kind{NodeKind::Serial}; std::vector<std::uint64_t> inputs;
    Operator operation{}; float mix{1},key_input_gain{1},key_output_gain{1};
    bool key_invert{},bypass{},locked{}; unsigned channel{}; std::string label;
};
struct LayerListView { std::uint64_t id{}; std::string label; std::size_t order{}; bool locked{},bypass{}; float mix{}; };
class NodeGraph {
public:
    NodeGraph()=default;NodeGraph(const NodeGraph&)=default;NodeGraph& operator=(const NodeGraph&);
    NodeGraph(NodeGraph&&) noexcept;NodeGraph& operator=(NodeGraph&&) noexcept;
    void add(Node); void replace(Node); void set_locked(std::uint64_t,bool);
    std::vector<Node> compile() const;
    std::vector<Node> snapshot_nodes() const;
    bool has_explicit_output() const noexcept;
    std::vector<LayerListView> layers() const;
    std::uint64_t revision() const noexcept;
    void set_output(std::uint64_t id);std::uint64_t output() const noexcept;
private: std::vector<Node> nodes_; std::uint64_t revision_{},output_id_{};
};
enum class TouchWheel { Lift,Gamma,Gain,Offset };
// Absolute wheel coordinates, not cumulative slider deltas; returns a node for explicit commit.
Node touch_wheel(const Node&,TouchWheel,float x,float y,float luminance_move);
struct GradeProgram {
    NodeGraph group_pre,clip,group_post,timeline;
};
struct ScopeConfig { std::size_t bins{256},waveform_columns{256}; float low{},high{1}; Gamut gamut{Gamut::Rec709}; Transfer transfer{Transfer::Linear}; };
struct VideoScopes {
    std::array<std::vector<std::uint64_t>,4> histogram;
    std::array<std::vector<std::uint64_t>,3> parade;
    std::vector<std::uint64_t> waveform,vectorscope,cie_xy;
    std::uint64_t pixels{},undefined_chromaticity{};
};
struct EngineOptions { std::size_t memory_budget_bytes{256U*1024U*1024U},tile_edge{64}; };
class IColorEngine {
public:
    virtual ~IColorEngine()=default;
    virtual std::string_view backend_name() const noexcept=0;
    virtual void RenderGradeFrame(const GradeProgram&,ColorFrameBuffer32F input,ColorFrameBuffer32F output,const GradeContext&) const=0;
    virtual VideoScopes ComputeVideoScopes(ColorFrameBuffer32F,const ScopeConfig&) const=0;
};
std::unique_ptr<IColorEngine> create_engine(EngineOptions={});
// Explicit capability inventory lives in docs/CAPABILITIES.md; no implicit GPU/AI certification.
} // namespace color
