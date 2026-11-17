#include "resonance/Descriptions.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: description_assets_check <Descriptions directory>\n";return 2;}
    try {
        const auto root=std::filesystem::absolute(argv[1]);
        auto clocked=resonance::load_narration_manifest(root/"cues.tsv",root);
        auto paced=resonance::load_paced_narration_manifest(root/"paced.tsv",root);
        const auto timed_count=clocked.size(),paced_count=paced.size();
        resonance::DescriptionCoordinator policy;
        policy.set_cues(std::move(clocked),std::move(paced));
        std::cout<<"Runtime asset validation passed: "<<timed_count<<" clocked, "<<paced_count<<" paced cues.\n";
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
