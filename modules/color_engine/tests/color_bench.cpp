// Host CPU timing harness. It prints measurements only and has no pass/fail threshold.
// These numbers do not represent phone, GPU, thermal, or sustained-playback performance.
#include "color/color_pipeline_api.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace color;
int main(int argc,char** argv) {
    try {
        const std::size_t width=argc>1?std::stoul(argv[1]):1920,height=argc>2?std::stoul(argv[2]):1080;
        const int runs=argc>3?std::stoi(argv[3]):5;
        if(width==0||height==0||width>8192||height>8192||runs<1||runs>100) { std::cerr<<"usage: color_bench [width<=8192] [height<=8192] [runs 1-100]\n";return 2; }
        Image32F input(width,height),output(width,height);
        for(std::size_t y=0;y<height;++y) for(std::size_t x=0;x<width;++x) input.pixels()[y*width+x]={static_cast<float>(x)/static_cast<float>(width),static_cast<float>(y)/static_cast<float>(height),.25F,1};
        auto engine=create_engine(EngineOptions{512U*1024U*1024U,64});
        GradeProgram program;
        Node primaries;primaries.id=1;Primaries p;p.exposure=.2F;p.saturation=1.1F;primaries.operation=p;program.clip.add(primaries);
        Node window;window.id=2;window.inputs={1};PowerWindow w;w.shape=PowerWindow::Shape::RoundedRectangle;w.sx=w.sy=.3F;w.corner_radius=.1F;window.operation=w;program.clip.add(window);
        std::vector<double> ms;
        for(int r=0;r<runs;++r) {
            const auto start=std::chrono::steady_clock::now();
            engine->RenderGradeFrame(program,input.view(),output.view(),{});
            const auto end=std::chrono::steady_clock::now();
            ms.push_back(std::chrono::duration<double,std::milli>(end-start).count());
        }
        std::sort(ms.begin(),ms.end());
        std::cout<<"host_cpu_threads="<<std::thread::hardware_concurrency()<<" frame="<<width<<"x"<<height<<" runs="<<runs<<"\n";
        std::cout<<"CPU reference render ms: min="<<ms.front()<<" median="<<ms[ms.size()/2]<<" max="<<ms.back()<<"\n";
        std::cout<<"NOTE: host CPU reference only. Not device, GPU, thermal, or 4K playback measurement.\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
    return 0;
}
