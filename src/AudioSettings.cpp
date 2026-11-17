#include "resonance/AudioSettings.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace resonance {
namespace {
constexpr std::array volume_fields{
    std::pair{"beacon_volume",&AudioSettings::beacon_volume},
    std::pair{"wall_volume",&AudioSettings::wall_volume},
    std::pair{"npc_volume",&AudioSettings::npc_volume},
    std::pair{"object_volume",&AudioSettings::object_volume},
};
constexpr std::array toggle_fields{
    std::pair{"wall_enabled",&AudioSettings::wall_enabled},
    std::pair{"npc_enabled",&AudioSettings::npc_enabled},
    std::pair{"object_enabled",&AudioSettings::object_enabled},
};

constexpr int bounded_volume(int percent) noexcept {return std::clamp(percent,0,100);}

std::string_view trim(std::string_view text) {
    constexpr auto whitespace=" \t\r";
    const auto start=text.find_first_not_of(whitespace);
    if(start==std::string_view::npos)return {};
    return text.substr(start,text.find_last_not_of(whitespace)-start+1);
}

void apply_setting(AudioSettings& settings,std::string_view key,std::string_view value) {
    if(value.empty())return;
    for(const auto& [name,member]:volume_fields) {
        if(name!=key)continue;
        int percent{};
        const auto end=value.data()+value.size();
        const auto result=std::from_chars(value.data(),end,percent);
        if(result.ec==std::errc{}&&result.ptr==end)settings.*member=bounded_volume(percent);
        return;
    }
    for(const auto& [name,member]:toggle_fields) {
        if(name!=key)continue;
        if(value=="1"||value=="true")settings.*member=true;
        else if(value=="0"||value=="false")settings.*member=false;
        return;
    }
}

std::filesystem::path temporary_name(std::filesystem::path path) {
    static std::atomic<unsigned long long> sequence{};
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    path += ".tmp."+std::to_string(pid)+"."+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));
    return path;
}

bool replace_file(const std::filesystem::path& from,const std::filesystem::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code error;
    std::filesystem::rename(from,to,error);
    return !error;
#endif
}
}

AudioSettings load_audio_settings(const std::filesystem::path& path) {
    AudioSettings settings;
    std::ifstream file(path,std::ios::binary);
    for(std::string line;std::getline(file,line);) {
        const auto separator=line.find('=');
        if(separator==std::string::npos)continue;
        const std::string_view text=line;
        apply_setting(settings,trim(text.substr(0,separator)),trim(text.substr(separator+1)));
    }
    return settings;
}

bool save_audio_settings(const std::filesystem::path& path,const AudioSettings& settings) {
    if(path.filename().empty())return false;
    std::error_code error;
    const auto parent=path.parent_path();
    if(!parent.empty())std::filesystem::create_directories(parent,error);
    if(error)return false;

    const auto temporary=temporary_name(path);
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
    if(!file)return false;
    for(const auto& [name,member]:volume_fields)
        file<<name<<'='<<bounded_volume(settings.*member)<<'\n';
    for(const auto& [name,member]:toggle_fields)
        file<<name<<'='<<(settings.*member?'1':'0')<<'\n';
    file.close();
    if(file&&replace_file(temporary,path))return true;
    std::filesystem::remove(temporary,error);
    return false;
}

double audio_gain(int percent,bool enabled) noexcept {
    return enabled?bounded_volume(percent)/100.:0.;
}

std::int16_t scale_audio_sample(std::int16_t sample,int percent) noexcept {
    const int scaled=sample*bounded_volume(percent);
    return static_cast<std::int16_t>((scaled+(scaled<0?-50:50))/100);
}
}
