#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "resonance/RouteBeaconOutput.hpp"
#include "resonance/Speech.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <limits>
#include <span>
#include <stop_token>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace resonance {
namespace {
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr unsigned sample_rate=48000;
constexpr unsigned stereo_frame_bytes=4;

class BeaconBank {
    const char* samples_{};
    std::size_t pulse_bytes_{};
    int step_;
public:
    BeaconBank(const wchar_t* name,int step,unsigned max_ms,const char* format="RAB1"):step_(step) {
        const auto module=reinterpret_cast<HMODULE>(&__ImageBase);
        const auto resource=FindResourceW(module,name,RT_RCDATA);
        const auto size=resource?SizeofResource(module,resource):0;
        const auto loaded=resource?LoadResource(module,resource):nullptr;
        const auto* bytes=loaded?static_cast<const char*>(LockResource(loaded)):nullptr;
        constexpr std::size_t header_bytes=16;
        if(!bytes||size<header_bytes||std::memcmp(bytes,format,4)!=0)
            throw std::runtime_error("HRTF bank resource missing or unreadable");
        std::uint32_t rate{},count{},frames{};
        std::memcpy(&rate,bytes+4,4);
        std::memcpy(&count,bytes+8,4);
        std::memcpy(&frames,bytes+12,4);
        if(rate!=sample_rate||count!=static_cast<std::uint32_t>(directions())||frames==0||frames>=max_ms*sample_rate/1000||
           size!=header_bytes+static_cast<std::size_t>(frames)*stereo_frame_bytes*count)
            throw std::runtime_error("Invalid HRTF bank layout");
        samples_=bytes+header_bytes;
        pulse_bytes_=static_cast<std::size_t>(frames)*stereo_frame_bytes;
    }
    int directions() const {return 360/step_;}
    int angle(double degrees) const {
        const int bin=static_cast<int>(std::lround(std::remainder(degrees,360.)/step_));
        return ((bin+directions())%directions())*step_;
    }
    std::size_t pulse_bytes() const {return pulse_bytes_;}
    std::span<const char> pulse(int angle) const {
        return {samples_+(angle/step_)*pulse_bytes_,pulse_bytes_};
    }
};

void check_wave(MMRESULT result,const char* operation) {
    if(result!=MMSYSERR_NOERROR)
        throw std::runtime_error(std::string(operation)+" failed with WinMM status "+std::to_string(result));
}

class BeaconPlayer {
    struct Playback {
        HWAVEOUT device{};
        WAVEHDR header{};
        std::vector<char> buffer;
        bool prepared{};
    };
    std::unique_ptr<Playback> playback_=std::make_unique<Playback>();
public:
    ~BeaconPlayer() {
        auto& p=*playback_;
        if(!p.device)return;
        auto result=waveOutReset(p.device);
        if(result==MMSYSERR_NOERROR&&p.prepared)
            result=waveOutUnprepareHeader(p.device,&p.header,sizeof(WAVEHDR));
        if(result==MMSYSERR_NOERROR)result=waveOutClose(p.device);
        // An unresponsive driver may still own the header and buffer.
        if(result!=MMSYSERR_NOERROR)playback_.release();
    }
    void open(std::size_t pulse_bytes) {
        auto& p=*playback_;
        p.buffer.resize(pulse_bytes);
        WAVEFORMATEX format{};
        format.wFormatTag=WAVE_FORMAT_PCM;
        format.nChannels=2;
        format.nSamplesPerSec=sample_rate;
        format.wBitsPerSample=16;
        format.nBlockAlign=stereo_frame_bytes;
        format.nAvgBytesPerSec=sample_rate*stereo_frame_bytes;
        check_wave(waveOutOpen(&p.device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL),"waveOutOpen");
        p.header.lpData=p.buffer.data();
        p.header.dwBufferLength=static_cast<DWORD>(p.buffer.size());
        check_wave(waveOutPrepareHeader(p.device,&p.header,sizeof(WAVEHDR)),"waveOutPrepareHeader");
        p.prepared=true;
    }
    bool play(std::span<const char> pulse,int volume) {
        auto& p=*playback_;
        if(busy()||pulse.size()>p.buffer.size())return false;
        if(volume==100)std::copy(pulse.begin(),pulse.end(),p.buffer.begin());
        else for(std::size_t offset=0;offset<pulse.size();offset+=sizeof(std::int16_t)) {
            std::int16_t sample{};
            std::memcpy(&sample,pulse.data()+offset,sizeof sample);
            sample=scale_audio_sample(sample,volume);
            std::memcpy(p.buffer.data()+offset,&sample,sizeof sample);
        }
        p.header.dwBufferLength=static_cast<DWORD>(pulse.size());
        check_wave(waveOutWrite(p.device,&p.header,sizeof(WAVEHDR)),"waveOutWrite");
        return true;
    }
    bool busy() const {return (playback_->header.dwFlags&WHDR_INQUEUE)!=0;}
    void cancel() {
        auto& p=*playback_;
        if(p.header.dwFlags&WHDR_INQUEUE)check_wave(waveOutReset(p.device),"waveOutReset");
    }
};

bool game_focused() {
    DWORD pid{};
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}

constexpr std::size_t block_frames=960,block_samples=block_frames*2;
using StereoBlock=std::array<std::int16_t,block_samples>;

class FieldPcmOutput {
public:
    static constexpr std::size_t packet_count=3;
private:
    struct Packet {
        StereoBlock pcm{};
        WAVEHDR header{};
        bool prepared{};
    };
    struct Playback {
        HWAVEOUT device{};
        std::array<Packet,packet_count> packets{};
    };
    std::unique_ptr<Playback> playback_=std::make_unique<Playback>();
public:
    ~FieldPcmOutput() {
        auto& p=*playback_;
        if(!p.device)return;
        auto result=waveOutReset(p.device);
        for(auto& packet:p.packets) {
            if(result==MMSYSERR_NOERROR&&packet.prepared)
                result=waveOutUnprepareHeader(p.device,&packet.header,sizeof(WAVEHDR));
        }
        if(result==MMSYSERR_NOERROR)result=waveOutClose(p.device);
        if(result!=MMSYSERR_NOERROR)playback_.release();
    }
    void open() {
        auto& p=*playback_;
        WAVEFORMATEX format{};
        format.wFormatTag=WAVE_FORMAT_PCM;
        format.nChannels=2;
        format.nSamplesPerSec=sample_rate;
        format.wBitsPerSample=16;
        format.nBlockAlign=stereo_frame_bytes;
        format.nAvgBytesPerSec=sample_rate*stereo_frame_bytes;
        check_wave(waveOutOpen(&p.device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL),"waveOutOpen field cues");
        for(auto& packet:p.packets) {
            packet.header.lpData=reinterpret_cast<char*>(packet.pcm.data());
            packet.header.dwBufferLength=static_cast<DWORD>(sizeof packet.pcm);
            check_wave(waveOutPrepareHeader(p.device,&packet.header,sizeof(WAVEHDR)),"waveOutPrepareHeader field cues");
            packet.prepared=true;
        }
    }
    void cancel() {check_wave(waveOutReset(playback_->device),"waveOutReset field cues");}
    Packet* next() {
        for(auto& packet:playback_->packets)
            if(!(packet.header.dwFlags&WHDR_INQUEUE))return &packet;
        return nullptr;
    }
    bool busy() const {
        return std::any_of(playback_->packets.begin(),playback_->packets.end(),
            [](const auto& packet){return (packet.header.dwFlags&WHDR_INQUEUE)!=0;});
    }
    void submit(Packet& packet) {
        check_wave(waveOutWrite(playback_->device,&packet.header,sizeof(WAVEHDR)),"waveOutWrite field cues");
    }
};

struct FieldVolumes {
    std::array<double,2> entities{1.,1.};
    double wall{1.};
    bool operator==(const FieldVolumes&) const=default;
    bool muted_since(const FieldVolumes& previous) const {
        return (wall==0.&&previous.wall>0.)||
            (entities[0]==0.&&previous.entities[0]>0.)||
            (entities[1]==0.&&previous.entities[1]>0.);
    }
};

std::size_t entity_group(FieldCueKind kind) {return kind==FieldCueKind::Npc?0:1;}

class FieldMixer {
    struct Voice {
        FieldProximityCue cue;
        std::uint64_t first_frame{};
        std::optional<std::uint64_t> last_pulse;
        std::size_t playhead{std::numeric_limits<std::size_t>::max()};
        int angle{},reported_angle{};
        double gain{};
        unsigned pulses{};
    };
    struct WallVoice {
        std::size_t playhead{};
        int angle{};
        double gain{};
        unsigned held_blocks{};
    };
    BeaconBank npc_{L"FIELD_NPC",10,1200},interactable_{L"FIELD_INTERACTABLE",10,1200};
    std::array<BeaconBank,4> wall_banks_{{
        {L"FIELD_WALL_UP",10,1200,"RLB1"},
        {L"FIELD_WALL_RIGHT",10,1200,"RLB1"},
        {L"FIELD_WALL_DOWN",10,1200,"RLB1"},
        {L"FIELD_WALL_LEFT",10,1200,"RLB1"}
    }};
    std::vector<Voice> entities_;
    FieldWallCues walls_{};
    std::array<WallVoice,4> wall_voices_{};
    FieldVolumes volumes_;
    std::uint64_t frame_{};

    const BeaconBank& bank(FieldCueKind kind) const {
        return kind==FieldCueKind::Npc?npc_:interactable_;
    }
    static bool playing(const Voice& voice,const BeaconBank& source) {
        return voice.playhead<source.pulse_bytes()/stereo_frame_bytes;
    }
    bool due(const Voice& voice,const FieldProximityCue& cue) const {
        const auto spacing=static_cast<std::uint64_t>(field_pulse_interval(cue.kind,cue.distance_metres).count())*sample_rate/1000;
        return frame_>=voice.first_frame&&(!voice.last_pulse||frame_-*voice.last_pulse>=spacing);
    }
    void start(Voice& voice,std::vector<std::string>& reports) {
        voice.playhead=0;
        voice.last_pulse=frame_;
        ++voice.pulses;
        const auto& cue=voice.cue;
        const int angle=bank(cue.kind).angle(cue.azimuth_degrees);
        if(voice.pulses<=3||voice.pulses%12==0||std::abs(std::remainder(angle-voice.reported_angle,360.))>=30.) {
            const auto type=cue.kind==FieldCueKind::Npc?"npc":"interactable";
            reports.push_back(std::string("PROXIMITY pulse kind=")+type+" azimuth="+
                std::to_string(angle)+" distance_m="+std::to_string(cue.distance_metres)+" target="+cue.id+
                " count="+std::to_string(voice.pulses));
            voice.reported_angle=angle;
        }
    }
    static void mix_frame(std::span<const char> previous,std::span<const char> next,std::size_t source_frame,
            double previous_gain,double next_gain,std::size_t output_frame,std::array<double,block_samples>& output) {
        const double alpha=static_cast<double>(output_frame+1)/block_frames;
        for(std::size_t ear=0;ear<2;++ear) {
            const auto offset=source_frame*stereo_frame_bytes+ear*sizeof(std::int16_t);
            std::int16_t sample{},previous_sample{};
            std::memcpy(&sample,next.data()+offset,sizeof sample);
            std::memcpy(&previous_sample,previous.data()+offset,sizeof previous_sample);
            output[output_frame*2+ear]+=std::lerp(previous_sample*previous_gain,sample*next_gain,alpha);
        }
    }
    static void mix(Voice& voice,const BeaconBank& bank,double mix_gain,std::array<double,block_samples>& output) {
        const auto& cue=voice.cue;
        const int next_angle=bank.angle(cue.azimuth_degrees);
        const double next_gain=mix_gain*field_pulse_gain(cue.kind,cue.distance_metres);
        if(!voice.playhead) {
            voice.angle=next_angle;
            voice.gain=next_gain;
        }
        const auto now=bank.pulse(next_angle),previous=bank.pulse(voice.angle);
        const std::size_t count=std::min(block_frames,now.size()/stereo_frame_bytes-voice.playhead);
        for(std::size_t i=0;i<count;++i)
            mix_frame(previous,now,voice.playhead+i,voice.gain,next_gain,i,output);
        voice.playhead+=count;
        voice.angle=next_angle;
        voice.gain=next_gain;
    }
    void mix_wall(std::size_t side,std::array<double,block_samples>& output,std::vector<std::string>& reports) {
        const auto& contact=walls_[side];
        auto& voice=wall_voices_[side];
        const double next_gain=contact?volumes_.wall*wall_tone_gain(contact->distance_metres):0.;
        if(next_gain==0.&&voice.gain==0.)return;
        const auto& source=wall_banks_[side];
        const int next_angle=next_gain>0.?source.angle(contact->azimuth_degrees):voice.angle;
        const auto now=source.pulse(next_angle),previous=source.pulse(voice.angle);
        const auto frames=now.size()/stereo_frame_bytes;
        for(std::size_t i=0;i<block_frames;++i) {
            mix_frame(previous,now,voice.playhead,voice.gain,next_gain,i,output);
            if(++voice.playhead==frames)voice.playhead=0;
        }
        if(next_gain==0.) {
            constexpr std::array sides{"up","right","down","left"};
            reports.push_back(std::string("WALL tone cleared side=")+sides[side]);
            voice={};
            return;
        }
        constexpr unsigned sustained_blocks=2000*sample_rate/(1000*block_frames);
        if(voice.held_blocks<=sustained_blocks) {
            if(voice.held_blocks==0||voice.held_blocks==sustained_blocks)
                reports.push_back("WALL tone side="+contact->id+" azimuth="+std::to_string(next_angle)+
                    " distance_m="+std::to_string(contact->distance_metres)+
                    " held_ms="+std::to_string(voice.held_blocks*block_frames*1000/sample_rate));
            ++voice.held_blocks;
        }
        voice.angle=next_angle;
        voice.gain=next_gain;
    }
public:
    int directions() const {return npc_.directions();}
    void set_volumes(const FieldVolumes& volumes) {
        volumes_=volumes;
        if(volumes_.wall==0.) {
            walls_={};
            wall_voices_={};
        }
    }
    bool has_audio() const {
        if(!entities_.empty())return true;
        for(std::size_t i=0;i<walls_.size();++i)
            if(wall_voices_[i].gain>0.||(walls_[i]&&wall_tone_gain(walls_[i]->distance_metres)>0.))return true;
        return false;
    }
    void clear() {
        entities_.clear();
        walls_={};
        wall_voices_={};
        frame_=0;
    }
    void update(const FieldAudioFrame& audio) {
        const auto same=[](const FieldProximityCue& a,const FieldProximityCue& b){return a.kind==b.kind&&a.id==b.id;};
        std::erase_if(entities_,[&](const auto& voice) {
            const auto channel=entity_group(voice.cue.kind);
            if(volumes_.entities[channel]==0.)return true;
            const auto& group=audio.entities[channel];
            return std::none_of(group.begin(),group.end(),[&](const auto& cue){return same(voice.cue,cue);});
        });
        for(std::size_t channel=0;channel<audio.entities.size();++channel) {
            if(volumes_.entities[channel]==0.)continue;
            const auto& group=audio.entities[channel];
            for(std::size_t i=0;i<group.size();++i) {
                const auto& cue=group[i];
                auto voice=std::find_if(entities_.begin(),entities_.end(),[&](const auto& v){return same(v.cue,cue);});
                // Stagger new emitters of the same type so their locations are easier to distinguish.
                if(voice==entities_.end())entities_.push_back(Voice{cue,frame_+(i%6)*sample_rate*70/1000});
                else voice->cue=cue;
            }
        }
        walls_=volumes_.wall>0.?audio.walls:FieldWallCues{};
    }
    void render(StereoBlock& output,std::vector<std::string>& reports) {
        std::array<double,block_samples> mixed{};
        const double crowd_gain=1./std::sqrt(std::max(1.,static_cast<double>(entities_.size())*.55));
        for(auto& voice:entities_) {
            const auto& source=bank(voice.cue.kind);
            if(!playing(voice,source)&&due(voice,voice.cue))start(voice,reports);
            if(playing(voice,source))mix(voice,source,crowd_gain*volumes_.entities[entity_group(voice.cue.kind)],mixed);
        }
        for(std::size_t side=0;side<walls_.size();++side)mix_wall(side,mixed,reports);
        std::transform(mixed.begin(),mixed.end(),output.begin(),[](double sample) {
            return static_cast<std::int16_t>(std::lround(std::clamp(sample,-32768.,32767.)));
        });
        frame_+=block_frames;
    }
};

// The field stream mixes nearby sources independently of the route beacon.
class ProximityOutput {
    struct Sample {FieldAudioFrame frame;Clock::time_point submitted;bool has_sources;};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::shared_ptr<const Sample> latest_;
    FieldVolumes volumes_;
    std::uint64_t reset_epoch_{},revision_{};
    bool quit_{};
    std::atomic<bool> ready_{};
    std::mutex report_mutex_;
    std::condition_variable_any report_wake_;
    std::vector<std::string> pending_reports_;
    std::jthread reporter_{[this](std::stop_token stop){write_reports(stop);}};
    std::thread worker_{[this]{run();}};
public:
    ~ProximityOutput() {
        {std::lock_guard lock(mutex_);quit_=true;}
        wake_.notify_one();
        worker_.join();
    }
    bool ready() const {return ready_;}
    void set_volumes(const FieldVolumes& volumes) {
        {
            std::lock_guard lock(mutex_);
            if(volumes_==volumes)return;
            volumes_=volumes;
            ++revision_;
        }
        wake_.notify_one();
    }
    void publish(FieldAudioFrame frame) {
        if(!ready_)return;
        const bool has_sources=!frame.entities[0].empty()||!frame.entities[1].empty()||
            std::any_of(frame.walls.begin(),frame.walls.end(),[](const auto& wall){return wall.has_value();});
        {
            std::lock_guard lock(mutex_);
            // Let the mixer fade a cleared wall. Gate loss still cancels immediately.
            if(!has_sources&&(!latest_||!latest_->has_sources))return;
            latest_=std::make_shared<const Sample>(Sample{std::move(frame),Clock::now(),has_sources});
            ++revision_;
        }
        wake_.notify_one();
    }
    void cancel() {
        if(!ready_)return;
        {
            std::lock_guard lock(mutex_);
            if(!latest_)return;
            latest_.reset();
            ++reset_epoch_;
            ++revision_;
        }
        wake_.notify_one();
    }
private:
    void post_reports(std::vector<std::string>& reports) {
        if(reports.empty())return;
        {
            std::lock_guard lock(report_mutex_);
            for(auto& report:reports) {
                if(pending_reports_.size()>=64)break;
                pending_reports_.push_back(std::move(report));
            }
        }
        report_wake_.notify_one();
    }
    void write_reports(std::stop_token stop) {
        while(!stop.stop_requested()) {
            std::vector<std::string> reports;
            {
                std::unique_lock lock(report_mutex_);
                if(!report_wake_.wait(lock,stop,[&]{return !pending_reports_.empty();}))return;
                reports.swap(pending_reports_);
            }
            for(const auto& report:reports)log(report);
        }
    }
    void run() {
        try {
            FieldMixer mixer;
            FieldPcmOutput player;
            player.open();
            ready_=true;
            log("PROXIMITY ready HRTF positions="+std::to_string(mixer.directions())+
                " stereo_hz="+std::to_string(sample_rate)+" wall=continuous");
            std::shared_ptr<const Sample> mixed;
            auto last_submit=Clock::now();
            std::uint64_t seen_epoch{};
            FieldVolumes seen_volumes;
            std::unique_lock lock(mutex_);
            while(!quit_) {
                const auto sample=latest_;
                const auto revision=revision_,epoch=reset_epoch_;
                const auto volumes=volumes_;
                lock.unlock();
                const auto now=Clock::now();
                // Menus and world changes reset explicitly. Allow a slow field tick to finish.
                const bool active=sample&&now-sample->submitted<1200ms&&game_focused();
                if(mixed&&(!active||seen_epoch!=epoch)) {
                    player.cancel();
                    mixer.clear();
                    mixed.reset();
                }
                seen_epoch=epoch;
                const bool volume_changed=seen_volumes!=volumes;
                if(volume_changed) {
                    // Drop buffers containing a just-muted voice, then mix the other voices as usual.
                    if(volumes.muted_since(seen_volumes)&&player.busy())player.cancel();
                    mixer.set_volumes(volumes);
                    seen_volumes=volumes;
                }
                if(active) {
                    if(mixed!=sample||volume_changed) {
                        if(!mixer.has_audio())last_submit=now;
                        mixer.update(sample->frame);
                    }
                    mixed=sample;
                    std::vector<std::string> reports;
                    for(std::size_t i=0;i<FieldPcmOutput::packet_count&&mixer.has_audio();++i) {
                        if(!game_focused())break;
                        auto* packet=player.next();
                        if(!packet) {
                            if(now-last_submit>=1500ms) {
                                player.cancel();
                                last_submit=now;
                                reports.push_back("PROXIMITY recovered a stalled WinMM queue");
                            }
                            break;
                        }
                        mixer.render(packet->pcm,reports);
                        player.submit(*packet);
                        last_submit=Clock::now();
                    }
                    // Do not hold up buffer refills for log traffic on another game thread.
                    post_reports(reports);
                }
                lock.lock();
                const auto changed=[&]{return quit_||revision_!=revision;};
                if(active&&(mixer.has_audio()||player.busy()))wake_.wait_for(lock,10ms,changed);
                else wake_.wait(lock,changed);
            }
        } catch(const std::exception& e) {
            log(std::string("PROXIMITY unavailable: ")+e.what());
        }
        ready_=false;
    }
};
}

struct RouteBeaconOutput::Impl {
    struct Sample {
        NavigationMode mode;
        RouteBeaconCue cue;
        Clock::time_point updated;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Sample> current;
    NavigationMode active_mode{NavigationMode::Off};
    std::uint64_t generation{};
    int volume{100};
    bool quit{};
    std::atomic<bool> ready{};
    ProximityOutput proximity;
    std::thread worker{[this]{run();}};

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            quit=true;
        }
        wake.notify_one();
        worker.join();
    }
    void set_settings(const AudioSettings& settings) {
        proximity.set_volumes({{audio_gain(settings.npc_volume,settings.npc_enabled),
            audio_gain(settings.object_volume,settings.object_enabled)},
            audio_gain(settings.wall_volume,settings.wall_enabled)});
        const int percent=std::clamp(settings.beacon_volume,0,100);
        {
            std::lock_guard lock(mutex);
            if(volume==percent)return;
            volume=percent;
            if(volume==0)++generation;
        }
        wake.notify_one();
    }
    void publish(NavigationMode mode,const std::optional<RouteBeaconCue>& cue) {
        if(!ready)return;
        const bool valid=mode!=NavigationMode::Off&&cue&&
            std::isfinite(cue->azimuth_degrees)&&std::isfinite(cue->remaining_metres)&&cue->remaining_metres>=0;
        bool changed;
        {
            std::lock_guard lock(mutex);
            changed=valid!=current.has_value()||active_mode!=mode;
            // A temporarily missing bearing holds the cadence. Stop or
            // switch modes to cancel an already playing pulse.
            if(active_mode!=mode)++generation;
            active_mode=mode;
            if(valid)current=Sample{mode,*cue,Clock::now()};
            else current.reset();
        }
        if(changed)wake.notify_one();
    }
    void run() {
        std::optional<BeaconPlayer> player;
        try {
            BeaconBank bank(L"ROUTE_BEACON",5,220);
            player.emplace();
            player->open(bank.pulse_bytes());
            ready=true;
            log("BEACON ready HRTF positions="+std::to_string(bank.directions())+" stereo_hz="+std::to_string(sample_rate));
            play_routes(bank,*player);
        } catch(const std::exception& e) {
            ready=false;
            log(std::string("BEACON unavailable: ")+e.what());
        }
        ready=false;
    }
    void play_routes(const BeaconBank& bank,BeaconPlayer& player) {
        constexpr auto freshness=450ms;
        constexpr auto refresh=40ms;
        constexpr auto buffer_timeout=650ms;
        std::uint64_t seen_generation{};
        std::optional<Clock::time_point> last_pulse;
        int last_reported_angle{},last_reported_spacing{};
        std::unique_lock lock(mutex);
        while(!quit) {
            if(seen_generation!=generation) {
                seen_generation=generation;
                const bool was_playing=last_pulse.has_value();
                last_pulse.reset();
                lock.unlock();
                player.cancel();
                if(was_playing)log("BEACON idle");
                lock.lock();
                continue;
            }
            if(!current||volume==0) {
                wake.wait(lock);
                continue;
            }
            const auto now=Clock::now();
            if(now-current->updated>=freshness||!game_focused()) {
                current.reset();
                active_mode=NavigationMode::Off;
                ++generation;
                continue;
            }
            const auto sample=*current;
            const int spacing=static_cast<int>(route_beacon_interval_ms(sample.cue.remaining_metres));
            if(last_pulse) {
                if(now-*last_pulse>=buffer_timeout&&player.busy())
                    throw std::runtime_error("WinMM beacon buffer did not finish");
                const auto due=*last_pulse+std::chrono::milliseconds(spacing);
                if(now<due) {
                    wake.wait_until(lock,std::min(due,now+refresh));
                    continue;
                }
            }
            const double azimuth=std::remainder(sample.cue.azimuth_degrees,360.);
            const int nearest=bank.angle(azimuth);
            // A cue just behind the side already needs the rear pitch.
            const int angle=azimuth>90.&&nearest==90?95:
                azimuth<-90.&&nearest==270?265:nearest;
            const int pulse_volume=volume;
            lock.unlock();
            const bool played=game_focused()&&player.play(bank.pulse(angle),pulse_volume);
            if(played) {
                const bool report=!last_pulse||std::abs(std::remainder(angle-last_reported_angle,360.))>=30||
                    std::abs(spacing-last_reported_spacing)>=100;
                last_pulse=Clock::now();
                if(report) {
                    const auto mode=sample.mode==NavigationMode::Automatic?"automatic":"manual";
                    log(std::string("BEACON pulse mode=")+mode+" azimuth="+std::to_string(angle)+
                        " interval_ms="+std::to_string(spacing)+" remaining_m="+std::to_string(sample.cue.remaining_metres));
                    last_reported_angle=angle;
                    last_reported_spacing=spacing;
                }
            }
            lock.lock();
            if(!played&&seen_generation==generation&&!quit)wake.wait_for(lock,10ms);
        }
    }
};

RouteBeaconOutput::RouteBeaconOutput() {
    try {impl_=std::make_unique<Impl>();}
    catch(const std::exception& e) {log(std::string("BEACON unavailable: ")+e.what());}
}
RouteBeaconOutput::~RouteBeaconOutput()=default;
bool RouteBeaconOutput::ready() const {return impl_&&impl_->ready;}
void RouteBeaconOutput::set_settings(const AudioSettings& settings) {
    if(impl_)impl_->set_settings(settings);
}
void RouteBeaconOutput::publish(NavigationMode mode,const std::optional<RouteBeaconCue>& cue) {
    if(impl_)impl_->publish(mode,cue);
}
bool RouteBeaconOutput::proximity_ready() const {return impl_&&impl_->proximity.ready();}
void RouteBeaconOutput::publish_proximity(FieldAudioFrame frame) {
    if(impl_)impl_->proximity.publish(std::move(frame));
}
void RouteBeaconOutput::cancel_proximity() {
    if(impl_)impl_->proximity.cancel();
}
}
