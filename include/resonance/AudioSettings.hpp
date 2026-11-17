#pragma once
#include <cstdint>
#include <filesystem>

namespace resonance {
struct AudioSettings {
    int beacon_volume=100;
    int wall_volume=100;
    int npc_volume=100;
    int object_volume=100;
    bool wall_enabled=true;
    bool npc_enabled=true;
    bool object_enabled=true;
    bool operator==(const AudioSettings&) const=default;
};

AudioSettings load_audio_settings(const std::filesystem::path& path);
bool save_audio_settings(const std::filesystem::path& path,const AudioSettings& settings);
double audio_gain(int percent,bool enabled=true) noexcept;
std::int16_t scale_audio_sample(std::int16_t sample,int percent) noexcept;
}
