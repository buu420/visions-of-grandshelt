#include "resonance/Descriptions.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char** argv) {
    const bool speech=argc==3&&std::string_view(argv[2])=="--screen-reader";
    if(argc!=2&&!speech){std::cerr<<"Usage: description_assets_check <Descriptions directory> [--screen-reader]\n";return 2;}
    try {
        const auto root=std::filesystem::absolute(argv[1]);
        const auto source=speech?resonance::DescriptionSource::ScreenReader:resonance::DescriptionSource::Recording;
        auto clocked=resonance::load_narration_manifest(root/"cues.tsv",root,source);
        auto paced=resonance::load_paced_narration_manifest(root/"paced.tsv",root,source);
        const auto timed_count=clocked.size(),paced_count=paced.size();
        resonance::DescriptionCoordinator policy;
        policy.set_cues(std::move(clocked),std::move(paced));
        std::cout<<"Runtime asset validation passed: "<<timed_count<<" clocked, "<<paced_count<<" paced cues.\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
