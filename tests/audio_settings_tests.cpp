#include "resonance/AudioSettings.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace resonance;
namespace {
void require(bool condition,const char* complaint) {
    if(!condition)throw std::runtime_error(complaint);
}

struct TestFiles {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("resonance-audio-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TestFiles() {
        std::error_code error;
        std::filesystem::remove(root/"settings.ini",error);
        std::filesystem::remove(root, error);
    }
};

void missing_or_damaged_settings_use_individual_defaults(const std::filesystem::path& path) {
    require(load_audio_settings(path)==AudioSettings{},"a missing file must keep the current audio defaults");
    std::filesystem::create_directory(path.parent_path());
    {
        std::ofstream file(path);
        file<<"wall_enabled = 0\nbeacon_volume=65\nwall_volume=200\nnpc_volume=-35\n"
            <<"object_enabled=false\nobject_volume=15%\nnpc_enabled=4\nunknown=1\n";
    }
    const auto loaded=load_audio_settings(path);
    require(loaded.beacon_volume==65&&loaded.wall_volume==100&&loaded.npc_volume==0,
        "valid slider values are loaded and bounded to percentages");
    require(!loaded.wall_enabled&&loaded.npc_enabled&&!loaded.object_enabled&&loaded.object_volume==100,
        "one malformed field must not discard other saved values or unmute an explicitly disabled cue");
}

void loading_and_saving_preserves_each_choice(const std::filesystem::path& path) {
    const AudioSettings chosen{25,40,55,70,false,true,false};
    require(save_audio_settings(path,chosen),"settings should replace the old file");
    require(load_audio_settings(path)==chosen,"all four gains and three toggles must survive a restart");
    const AudioSettings muted{0,15,0,75,true,false,true};
    require(save_audio_settings(path,muted),"another save should atomically replace the settings");
    require(load_audio_settings(path)==muted,"the new settings file should contain only the latest choices");
    require(!save_audio_settings(path.parent_path(),chosen),"the settings path must reject a directory");
    require(load_audio_settings(path)==muted,"a failed save should preserve existing settings");
}

void gains_are_independent_and_preserve_pcm_at_the_default() {
    const AudioSettings chosen{90,40,55,70,false,false,true};
    require(audio_gain(chosen.wall_volume,chosen.wall_enabled)==0.&&
        audio_gain(chosen.npc_volume,chosen.npc_enabled)==0.&&
        audio_gain(chosen.object_volume,chosen.object_enabled)==.70,
        "each cue gain must honor its own settings");
    require(scale_audio_sample(-32768,100)==-32768&&scale_audio_sample(32767,100)==32767,
        "100 percent must preserve the original stereo PCM samples");
    require(scale_audio_sample(2560,25)==640&&scale_audio_sample(-2560,25)==-640&&
        scale_audio_sample(32767,0)==0&&scale_audio_sample(-32768,-5)==0,
        "route gain must scale both polarities and mute at zero");
}
}

int main() {
    try {
        const TestFiles files;
        const auto path=files.root/"settings.ini";
        missing_or_damaged_settings_use_individual_defaults(path);
        loading_and_saving_preserves_each_choice(path);
        gains_are_independent_and_preserve_pcm_at_the_default();
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
