#include "color/mastering.hpp"
#include <limits>
#include <stdexcept>
#include <unordered_set>
namespace color {
std::vector<MasterFrame> render_masters(const IColorEngine& engine,const GradeProgram& program,ColorFrameBuffer32F input,const GradeContext& context,std::span<const MasterSpec> specs,std::size_t budget) {
    auto require=[](bool b,const char* s){if(!b) throw std::invalid_argument(s);};
    require(input.width&&input.height&&input.width<=std::numeric_limits<std::size_t>::max()/input.height&&input.width*input.height<=std::numeric_limits<std::size_t>::max()/sizeof(Pixel),"Master frame extent overflow");
    const auto bytes=input.width*input.height*sizeof(Pixel);require(!specs.empty()&&specs.size()<=8&&bytes<=budget/(specs.size()+2),"Multi-master output/scratch budget");
    const auto nodes=program.timeline.snapshot_nodes();require(nodes.size()<=254,"Timeline needs two free master-trim slots");std::uint64_t maximum=0;for(const auto& n:nodes) if(n.id>maximum) maximum=n.id;
    require(maximum<=std::numeric_limits<std::uint64_t>::max()-2,"Master node ID overflow");std::unordered_set<std::string> names;
    for(const auto& s:specs) {require(!s.name.empty()&&s.name.size()<=128&&names.insert(s.name).second,"Duplicate/invalid master name");(void)PreparedCST(s.output_transform);(void)apply(Vec3{.18F,.18F,.18F},s.trim);}
    std::vector<MasterFrame> output;output.reserve(specs.size());
    for(const auto& s:specs) {
        GradeProgram master=program;Node trim;trim.id=maximum+1;if(program.timeline.output()) trim.inputs={program.timeline.output()};trim.operation=s.trim;master.timeline.add(trim);
        Node cst;cst.id=maximum+2;cst.inputs={trim.id};cst.operation=s.output_transform;master.timeline.add(cst);master.timeline.set_output(cst.id);
        output.push_back({s.name,Image32F(input.width,input.height,bytes)});engine.RenderGradeFrame(master,input,output.back().image.view(),context);
    }
    return output;
}
} // namespace color
