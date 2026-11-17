#include "resonance/NarrationOutput.hpp"
#include "resonance/PacedNarration.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
namespace resonance {
namespace {
std::vector<std::byte> read_file(const std::filesystem::path& path,std::uintmax_t limit) {
    const auto length=std::filesystem::file_size(path);
    if(length>limit) throw std::runtime_error("Narration file exceeds size limit");
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot open narration file");
    std::vector<std::byte> data(static_cast<std::size_t>(length));
    if(length&&!input.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(length)))
        throw std::runtime_error("Cannot read complete narration file");
    if(input.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Narration file changed during read");
    return data;
}
std::uint16_t u16(const std::vector<std::byte>& data,std::size_t p) {
    return std::to_integer<std::uint16_t>(data[p])|(std::to_integer<std::uint16_t>(data[p+1])<<8);
}
std::uint32_t u32(const std::vector<std::byte>& data,std::size_t p) {
    return u16(data,p)|(static_cast<std::uint32_t>(u16(data,p+2))<<16);
}
bool tag(const std::vector<std::byte>& data,std::size_t p,const char* expected) {
    return std::memcmp(data.data()+p,expected,4)==0;
}
bool utf8(const std::string& s) {
    for(std::size_t i=0;i<s.size();) {
        auto c=static_cast<unsigned char>(s[i++]);
        if(c<0x80){if(c<32&&c!='\t'&&c!='\n'&&c!='\r')return false;continue;}
        unsigned extra{},value{},minimum{};
        if(c>=0xc2&&c<=0xdf){extra=1;value=c&31;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){extra=2;value=c&15;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){extra=3;value=c&7;minimum=0x10000;}
        else return false;
        if(s.size()-i<extra)return false;
        while(extra--){c=static_cast<unsigned char>(s[i++]);if((c&0xc0)!=0x80)return false;value=(value<<6)|(c&63);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return true;
}
std::filesystem::path checked_root(const std::filesystem::path& root) {
    if(!std::filesystem::is_directory(root))throw std::runtime_error("Narration asset root is missing");
    return std::filesystem::canonical(root);
}
std::filesystem::path asset_path(const std::filesystem::path& root,const std::string& audio) {
    // Reuse exactly the scheduler's cross-platform relative-path policy.
    NarrationScheduler validate({{"scene","id","text",audio,0,1}});
    auto file=std::filesystem::weakly_canonical(root/std::filesystem::path(std::u8string(audio.begin(),audio.end())));
    auto relative=file.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||*relative.begin()==L".."||!std::filesystem::is_regular_file(file))
        throw std::runtime_error("Narration audio is missing or resolves outside its asset root");
    auto extension=file.extension().wstring();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    if(extension!=L".wav")throw std::runtime_error("Narration audio must be PCM16 WAV");
    return file;
}
double number(const std::string& text) {
    double value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{}||end!=text.data()+text.size()||!std::isfinite(value))
        throw std::runtime_error("Invalid narration time");
    return value;
}
std::string wave_error(const char* operation,MMRESULT code) {
    return std::string(operation)+" failed (WinMM "+std::to_string(code)+")";
}
class WaveDevice final:public NarrationAudioDevice {
    struct Playback {HWAVEOUT device{};WAVEHDR header{};NarrationWave wave;bool prepared{};};
    std::unique_ptr<Playback> current_;
    bool unusable_{};
    void check(MMRESULT result,const char* operation){if(result!=MMSYSERR_NOERROR)throw std::runtime_error(wave_error(operation,result));}
public:
    ~WaveDevice() override {try{stop();}catch(...){}}
    void play(NarrationWave wave,double offset) override {
        stop();
        if(unusable_)throw std::runtime_error("Narration device disabled after teardown failure");
        if(!std::isfinite(offset)||offset<0||offset>=wave.duration())throw std::runtime_error("Invalid narration playback offset");
        current_=std::make_unique<Playback>();current_->wave=std::move(wave);
        auto& playback=*current_;
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=playback.wave.channels;
        format.nSamplesPerSec=playback.wave.sample_rate;format.wBitsPerSample=16;
        format.nBlockAlign=static_cast<WORD>(format.nChannels*2);format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
        check(waveOutOpen(&playback.device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL),"waveOutOpen");
        const auto start=static_cast<std::size_t>(offset*format.nSamplesPerSec)*format.nBlockAlign;
        if(start>=playback.wave.samples.size())throw std::runtime_error("Narration offset is past the final PCM frame");
        playback.header.lpData=reinterpret_cast<char*>(playback.wave.samples.data()+start);
        playback.header.dwBufferLength=static_cast<DWORD>(playback.wave.samples.size()-start);
        check(waveOutPrepareHeader(playback.device,&playback.header,sizeof(WAVEHDR)),"waveOutPrepareHeader");
        playback.prepared=true;
        check(waveOutWrite(playback.device,&playback.header,sizeof(WAVEHDR)),"waveOutWrite");
    }
    void pause() override {if(current_&&current_->device)check(waveOutPause(current_->device),"waveOutPause");}
    void resume() override {if(current_&&current_->device)check(waveOutRestart(current_->device),"waveOutRestart");}
    bool completed() override {
        // WinMM sets WHDR_DONE only after the driver has finished this buffer:
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-wavehdr
        return current_&&current_->prepared&&(current_->header.dwFlags&WHDR_DONE)!=0;
    }
    void stop() override {
        if(!current_)return;
        if(current_->device) {
            auto error=waveOutReset(current_->device);
            const char* operation="waveOutReset";
            if(error==MMSYSERR_NOERROR&&current_->prepared){operation="waveOutUnprepareHeader";error=waveOutUnprepareHeader(current_->device,&current_->header,sizeof(WAVEHDR));}
            if(error==MMSYSERR_NOERROR){operation="waveOutClose";error=waveOutClose(current_->device);}
            if(error!=MMSYSERR_NOERROR) {
                // A broken driver may still own the header/buffer. Preserve its
                // allocation until process exit and latch this backend off, so
                // no later Play frees live driver memory or grows this leak.
                current_.release();unusable_=true;
                throw std::runtime_error(wave_error(operation,error));
            }
        }
        current_.reset();
    }
};
}
double NarrationWave::duration() const {
    return channels&&sample_rate?static_cast<double>(samples.size())/(channels*2.0*sample_rate):0;
}
NarrationWave read_narration_wave(const std::filesystem::path& path) {
    const auto data=read_file(path,64u*1024u*1024u);
    if(data.size()<12||!tag(data,0,"RIFF")||!tag(data,8,"WAVE")||u32(data,4)!=data.size()-8)
        throw std::runtime_error("Invalid RIFF WAVE header or length");
    NarrationWave result;bool format=false,pcm=false;
    std::size_t position=12;
    while(position<data.size()) {
        if(data.size()-position<8)throw std::runtime_error("Truncated WAVE chunk header");
        const auto size=static_cast<std::size_t>(u32(data,position+4));
        const auto start=position+8;
        if(size>data.size()-start)throw std::runtime_error("WAVE chunk exceeds file bounds");
        if(tag(data,position,"fmt ")) {
            if(format||(size!=16&&size!=18)||u16(data,start)!=1||(size==18&&u16(data,start+16)!=0))
                throw std::runtime_error("Narration requires one PCM WAVE format");
            result.channels=u16(data,start+2);result.sample_rate=u32(data,start+4);
            const auto alignment=u16(data,start+12);
            if((result.channels!=1&&result.channels!=2)||result.sample_rate<8000||result.sample_rate>192000||
               u16(data,start+14)!=16||alignment!=result.channels*2||u32(data,start+8)!=result.sample_rate*alignment)
                throw std::runtime_error("Unsupported or inconsistent PCM16 WAVE format");
            format=true;
        } else if(tag(data,position,"data")) {
            if(pcm||!size)throw std::runtime_error("Narration requires one nonempty WAVE data chunk");
            result.samples.assign(data.begin()+start,data.begin()+start+size);pcm=true;
        }
        if(size%2&&size==data.size()-start)throw std::runtime_error("Missing WAVE chunk padding");
        position=start+size+(size%2);
    }
    if(!format||!pcm||result.samples.size()%(result.channels*2)!=0)throw std::runtime_error("Missing or incomplete PCM16 WAVE frames");
    return result;
}
std::vector<NarrationCue> load_narration_manifest(const std::filesystem::path& path,
        const std::filesystem::path& asset_root,DescriptionSource source_kind) {
    const auto root=checked_root(asset_root);const auto data=read_file(path,4u*1024u*1024u);
    if(data.empty())throw std::runtime_error("Narration manifest has no cues");
    std::string source(reinterpret_cast<const char*>(data.data()),data.size());
    if(!utf8(source))throw std::runtime_error("Narration manifest is not valid UTF-8");
    if(source.starts_with("\xef\xbb\xbf"))source.erase(0,3);
    std::vector<NarrationCue> cues;
    std::size_t position=0,line=0;
    while(position<source.size()) {
        const auto end=source.find('\n',position);auto row=source.substr(position,end==std::string::npos?source.size()-position:end-position);
        position=end==std::string::npos?source.size():end+1;++line;
        if(!row.empty()&&row.back()=='\r')row.pop_back();
        if(line==1){if(row!="scene\tid\tstart\tend\taudio\ttext")throw std::runtime_error("Invalid narration TSV header");continue;}
        if(row.empty())throw std::runtime_error("Blank narration TSV row");
        std::vector<std::string> fields;std::size_t field=0;
        for(;;){const auto tab=row.find('\t',field);fields.push_back(row.substr(field,tab==std::string::npos?row.size()-field:tab-field));if(tab==std::string::npos)break;field=tab+1;}
        if(fields.size()!=6)throw std::runtime_error("Narration TSV row must contain six fields at line "+std::to_string(line));
        for(const auto& field_value:fields)if(field_value.find('\r')!=std::string::npos)throw std::runtime_error("Unexpected carriage return in narration TSV field");
        cues.push_back({fields[0],fields[1],fields[5],
            source_kind==DescriptionSource::Recording?fields[4]:std::string{},number(fields[2]),number(fields[3]),0,source_kind});
        if(cues.size()>5000)throw std::runtime_error("Narration cue count exceeds limit");
    }
    if(!line||cues.empty())throw std::runtime_error("Narration manifest has no cues");
    NarrationScheduler validated(std::move(cues));
    auto measured=validated.cues();
    if(source_kind==DescriptionSource::ScreenReader)return measured;
    for(auto& cue:measured) {
        const auto wave=read_narration_wave(asset_path(root,cue.audio));
        cue.duration=wave.duration();
        if(cue.duration+.2>cue.end-cue.start+1e-9)throw std::runtime_error("Narration recording lacks cue margin: "+cue.id);
    }
    return measured;
}
std::vector<PacedNarrationCue> load_paced_narration_manifest(const std::filesystem::path& path,
        const std::filesystem::path& asset_root,DescriptionSource source_kind) {
    const auto root=checked_root(asset_root);const auto data=read_file(path,4u*1024u*1024u);
    if(data.empty())throw std::runtime_error("Paced narration manifest has no cues");
    std::string source(reinterpret_cast<const char*>(data.data()),data.size());
    if(!utf8(source))throw std::runtime_error("Paced narration manifest is not valid UTF-8");
    if(source.starts_with("\xef\xbb\xbf"))source.erase(0,3);
    std::vector<PacedNarrationCue> cues;
    std::size_t position=0,line=0;
    bool caption_column=false;
    while(position<source.size()) {
        const auto end=source.find('\n',position);auto row=source.substr(position,end==std::string::npos?source.size()-position:end-position);
        position=end==std::string::npos?source.size():end+1;++line;
        if(!row.empty()&&row.back()=='\r')row.pop_back();
        if(line==1) {
            caption_column=row=="scene\tevent\tid\taudio\ttext\tfinish_on_advance\tcaption_sha256";
            if(!caption_column&&row!="scene\tevent\tid\taudio\ttext\tfinish_on_advance")
                throw std::runtime_error("Invalid paced narration TSV header");
            continue;
        }
        if(row.empty())throw std::runtime_error("Blank paced narration TSV row");
        std::vector<std::string> fields;std::size_t field=0;
        for(;;){const auto tab=row.find('\t',field);fields.push_back(row.substr(field,tab==std::string::npos?row.size()-field:tab-field));if(tab==std::string::npos)break;field=tab+1;}
        if(fields.size()!=(caption_column?7u:6u))
            throw std::runtime_error("Wrong paced narration TSV field count at line "+std::to_string(line));
        for(const auto& value:fields)if(value.find('\r')!=std::string::npos)throw std::runtime_error("Unexpected carriage return in paced narration TSV field");
        if(fields[5]!="0"&&fields[5]!="1")throw std::runtime_error("Invalid paced narration advance policy");
        cues.push_back({fields[0],fields[1],fields[2],fields[4],
                        source_kind==DescriptionSource::Recording?fields[3]:std::string{},fields[5]=="1",
                        caption_column?std::move(fields[6]):std::string{},source_kind});
        if(cues.size()>5000)throw std::runtime_error("Paced narration cue count exceeds limit");
    }
    if(!line||cues.empty())throw std::runtime_error("Paced narration manifest has no cues");
    PacedNarrationScheduler validated(std::move(cues));
    if(source_kind==DescriptionSource::ScreenReader)return validated.cues();
    for(const auto& cue:validated.cues()) {
        const auto wave=read_narration_wave(asset_path(root,cue.audio));
        if(wave.duration()>15.0)throw std::runtime_error("Paced narration recording exceeds fifteen seconds: "+cue.id);
    }
    return validated.cues();
}
struct NarrationOutput::Impl {
    struct Work {NarrationAction action;std::uint64_t generation;};
    std::filesystem::path root;
    ErrorCallback error;
    std::unique_ptr<NarrationAudioDevice> device;
    mutable std::mutex mutex;std::condition_variable cv;std::deque<Work> queue;
    NarrationPlaybackStatus playback;
    bool quit{};std::uint64_t generation{};std::thread thread;
    Impl(std::filesystem::path folder,ErrorCallback callback,std::unique_ptr<NarrationAudioDevice> output):
        root(checked_root(folder)),error(std::move(callback)),device(std::move(output)) {
        if(!error||!device)throw std::invalid_argument("Narration output requires an error callback and device");
        thread=std::thread([this]{run();});
    }
    ~Impl(){ {std::lock_guard lock(mutex);quit=true;playback={};playback.generation=++generation;queue.clear();}cv.notify_one();if(thread.joinable())thread.join();}
    bool current(std::uint64_t value){std::lock_guard lock(mutex);return !quit&&value==generation;}
    NarrationPlaybackStatus status() const {std::lock_guard lock(mutex);return playback;}
    void publish(const Work& work,bool active,bool paused,bool completed) {
        std::lock_guard lock(mutex);
        if(!quit&&work.generation==generation)
            playback={work.generation,work.action.scene,work.action.cue_id,active,paused,completed,false,false,{}};
    }
    void report(const Work& work,const std::string& message) {
        std::lock_guard lock(mutex);
        if(!quit&&work.generation==generation){
            playback={work.generation,work.action.scene,work.action.cue_id,false,false,false,true,false,message};
            auto context=work.action;context.generation=work.generation;
            try{error(context,message);}catch(...){}
        }
    }
    void submit(const std::vector<NarrationAction>& actions) {
        std::lock_guard lock(mutex);if(quit)return;
        for(const auto& action:actions) {
            if(action.kind==NarrationActionKind::Play||action.kind==NarrationActionKind::Stop){
                ++generation;queue.clear();const bool play=action.kind==NarrationActionKind::Play;
                const bool busy=playback.active,was_paused=playback.paused;
                playback={generation,action.scene,action.cue_id,play||busy,!play&&busy&&was_paused,false,false,true,{}};
            } else {
                if(!playback.active||playback.scene!=action.scene||playback.cue_id!=action.cue_id)continue;
                if(!queue.empty()&&(queue.back().action.kind==NarrationActionKind::Pause||queue.back().action.kind==NarrationActionKind::Resume))queue.pop_back();
            }
            queue.push_back({action,generation}); // At most Play/Stop plus the latest pause state.
        }
        cv.notify_one();
    }
    void run() {
        std::optional<Work> playing;
        bool paused=false;
        for(;;) {
            Work work;bool poll=false;
            {
                std::unique_lock lock(mutex);
                if(playing&&playing->generation==generation)
                    cv.wait_for(lock,std::chrono::milliseconds(20),[&]{return quit||!queue.empty();});
                else cv.wait(lock,[&]{return quit||!queue.empty();});
                if(quit)break;
                if(queue.empty()){if(!playing)continue;work=*playing;poll=true;}
                else {work=std::move(queue.front());queue.pop_front();}
            }
            if(!current(work.generation))continue;
            try {
                if(poll) {
                    if(device->completed()) {
                        device->stop();playing.reset();paused=false;
                        publish(work,false,false,true);
                    }
                    continue;
                }
                switch(work.action.kind) {
                case NarrationActionKind::Play: {
                    device->stop();playing.reset();paused=false;
                    auto wave=read_narration_wave(asset_path(root,work.action.audio));
                    if(!current(work.generation))continue;
                    device->play(std::move(wave),work.action.offset);
                    if(!current(work.generation)){device->stop();continue;}
                    playing=work;publish(work,true,false,false);break;
                }
                case NarrationActionKind::Pause:
                    if(playing&&playing->action.scene==work.action.scene&&playing->action.cue_id==work.action.cue_id) {
                        device->pause();paused=true;publish(*playing,true,paused,false);
                    }break;
                case NarrationActionKind::Resume:
                    if(playing&&playing->action.scene==work.action.scene&&playing->action.cue_id==work.action.cue_id) {
                        device->resume();paused=false;publish(*playing,true,paused,false);
                    }break;
                case NarrationActionKind::Stop:
                    device->stop();playing.reset();paused=false;publish(work,false,false,false);break;
                }
            }catch(const std::exception& e){
                playing.reset();paused=false;std::string message=e.what();
                try{device->stop();}catch(const std::exception& cleanup){message+="; cleanup: ";message+=cleanup.what();}catch(...){message+="; cleanup failed";}
                report(work,message);
            }
            catch(...){playing.reset();paused=false;try{device->stop();}catch(...){}report(work,"Unknown narration output error");}
        }
        try{device->stop();}catch(...){} // Destruction never calls the user's callback.
    }
};
NarrationOutput::NarrationOutput(std::filesystem::path root,ErrorCallback error):
    NarrationOutput(std::move(root),std::move(error),std::make_unique<WaveDevice>()){}
NarrationOutput::NarrationOutput(std::filesystem::path root,ErrorCallback error,std::unique_ptr<NarrationAudioDevice> device):
    impl_(std::make_unique<Impl>(std::move(root),std::move(error),std::move(device))){}
NarrationOutput::~NarrationOutput()=default;
void NarrationOutput::submit(const std::vector<NarrationAction>& actions){impl_->submit(actions);}
NarrationPlaybackStatus NarrationOutput::status() const {return impl_->status();}
}
