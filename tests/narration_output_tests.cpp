#include "resonance/NarrationOutput.hpp"
#include "resonance/PacedNarration.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace resonance;
namespace {
int failed{};
void check(bool value,const char* label) {if(!value){++failed;std::cerr<<"FAIL "<<label<<'\n';}}
template<class F> void rejects(F action,const char* label) {
    try{action();check(false,label);}catch(const std::exception&){}
}
void bytes(const std::filesystem::path& path,const std::vector<unsigned char>& value) {
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(value.data()),value.size());
}
void text(const std::filesystem::path& path,const std::string& value) {std::ofstream(path,std::ios::binary)<<value;}
std::vector<unsigned char> wave(unsigned frames=800) {
    std::vector<unsigned char> out={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    out.resize(44+frames*2);
    for(unsigned i=0;i<4;++i){out[4+i]=static_cast<unsigned char>((out.size()-8)>>(8*i));out[40+i]=static_cast<unsigned char>((frames*2)>>(8*i));}
    return out;
}
const std::string header="scene\tid\tstart\tend\taudio\ttext\n";
const std::string row="/Game/Opening\tintro\t1\t2\tvoice.wav\tCrystals float.\n";
struct FakeState {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> events;
    bool hold{}, entered{}, fail{};
    void event(std::string name){std::lock_guard lock(mutex);events.push_back(std::move(name));cv.notify_all();}
    bool wait(std::size_t count){std::unique_lock lock(mutex);return cv.wait_for(lock,std::chrono::seconds(3),[&]{return events.size()>=count;});}
};
class FakeDevice final:public NarrationAudioDevice {
    std::shared_ptr<FakeState> s_;
public:
    explicit FakeDevice(std::shared_ptr<FakeState> s):s_(std::move(s)){}
    void play(NarrationWave w,double offset) override {
        check(w.sample_rate==8000&&offset==0,"worker loads validated PCM");
        std::unique_lock lock(s_->mutex);s_->events.push_back("play");s_->entered=true;s_->cv.notify_all();
        s_->cv.wait(lock,[&]{return !s_->hold;});
        if(s_->fail)throw std::runtime_error("fake play failure");
    }
    void pause() override{s_->event("pause");}
    void resume() override{s_->event("resume");}
    void stop() override{s_->event("stop");}
};
NarrationAction action(NarrationActionKind kind,std::string id="intro") {return {kind,"/Game/Opening",std::move(id),"Crystals float.","voice.wav",0,"test-session","test-line"};}
struct CompletionState {
    std::mutex mutex;std::condition_variable cv;
    unsigned plays{},stops{},pauses{},resumes{},polls{};
    bool hold_play{},hold_poll{},done{},fail_play{},fail_poll{},destroyed{};
    template<class F> bool wait(F predicate) {std::unique_lock lock(mutex);return cv.wait_for(lock,std::chrono::seconds(3),predicate);}
    template<class F> void change(F f) {std::lock_guard lock(mutex);f();cv.notify_all();}
};
class CompletingDevice final:public NarrationAudioDevice {
    std::shared_ptr<CompletionState> s_;
public:
    explicit CompletingDevice(std::shared_ptr<CompletionState> s):s_(std::move(s)){}
    ~CompletingDevice() override{s_->change([&]{s_->destroyed=true;});}
    void play(NarrationWave,double) override {
        std::unique_lock lock(s_->mutex);++s_->plays;s_->done=false;const bool fail=s_->fail_play;s_->cv.notify_all();
        s_->cv.wait(lock,[&]{return !s_->hold_play;});
        if(fail)throw std::runtime_error("fake asynchronous play failure");
    }
    void pause() override{s_->change([&]{++s_->pauses;});}
    void resume() override{s_->change([&]{++s_->resumes;});}
    void stop() override{s_->change([&]{++s_->stops;s_->done=false;});}
    bool completed() override {
        std::unique_lock lock(s_->mutex);++s_->polls;const bool fail=s_->fail_poll;s_->cv.notify_all();
        s_->cv.wait(lock,[&]{return !s_->hold_poll;});
        if(fail)throw std::runtime_error("fake completion poll failure");
        return s_->done;
    }
};
struct ReleaseDevice {
    std::shared_ptr<CompletionState> state;
    ~ReleaseDevice(){state->change([&]{state->hold_play=false;state->hold_poll=false;});}
};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool wait_status(NarrationOutput& output,F predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do {if(predicate(output.status()))return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    while(std::chrono::steady_clock::now()<deadline);
    return false;
}
void completion_checks(const std::filesystem::path& root) {
    std::atomic<unsigned> errors{};
    auto state=std::make_shared<CompletionState>();state->hold_play=true;
    {
        NarrationOutput output(root,[&](const auto&,const auto&){++errors;},std::make_unique<CompletingDevice>(state));
        ReleaseDevice release{state};
        output.submit({action(NarrationActionKind::Play)});
        auto status=output.status();const auto generation=status.generation;
        require(status.active&&status.queued&&generation>0&&status.cue_id=="intro","queued Play must publish busy current cue immediately");
        require(state->wait([&]{return state->plays==1;}),"completion fake must enter Play");
        state->change([&]{state->hold_play=false;});
        require(wait_status(output,[](const auto& s){return s.active&&!s.queued;}),"successful Play must clear queued state while remaining active");
        output.submit({action(NarrationActionKind::Pause)});
        require(wait_status(output,[](const auto& s){return s.active&&s.paused;}),"actual Pause must publish paused active state");
        output.submit({action(NarrationActionKind::Resume)});
        require(wait_status(output,[](const auto& s){return s.active&&!s.paused;}),"actual Resume must clear paused state");
        state->change([&]{state->done=true;});
        require(wait_status(output,[](const auto& s){return !s.active&&s.completed&&!s.error;}),"device completion must clear busy and publish completion");
        status=output.status();
        require(status.generation==generation&&status.cue_id=="intro"&&!status.queued&&!status.paused,"completion must keep its exact generation and cue identity");
        unsigned polls{};{std::lock_guard lock(state->mutex);polls=state->polls;}
        std::this_thread::sleep_for(std::chrono::milliseconds(65));
        {std::lock_guard lock(state->mutex);require(state->polls==polls,"idle completed output must stop polling");}
        output.submit({action(NarrationActionKind::Stop)});
        status=output.status();
        require(!status.active&&!status.completed&&!status.error&&status.generation>generation,"new Stop must immediately invalidate old completion");
    }
    require(state->destroyed&&errors==0,"completion device must tear down without callbacks or a busy latch");

    auto superseded=std::make_shared<CompletionState>();superseded->hold_poll=true;
    {
        NarrationOutput output(root,[&](const auto&,const auto&){++errors;},std::make_unique<CompletingDevice>(superseded));
        ReleaseDevice release{superseded};
        output.submit({action(NarrationActionKind::Play,"old")});
        require(superseded->wait([&]{return superseded->polls>=1;}),"old completion poll must enter");
        output.submit({action(NarrationActionKind::Play,"new")});
        const auto generation=output.status().generation;
        require(output.status().active&&output.status().queued&&output.status().cue_id=="new","new queued Play must own busy state during an old poll");
        superseded->change([&]{superseded->done=true;superseded->hold_poll=false;});
        require(wait_status(output,[](const auto& s){return s.active&&!s.queued&&s.cue_id=="new";}),"old completion must not clear the new Play's busy state");
        const auto status=output.status();
        require(status.generation==generation&&!status.completed&&!status.error,"superseded completion must not overwrite the current generation");
    }
    auto stale_error=std::make_shared<CompletionState>();stale_error->hold_play=true;stale_error->fail_play=true;
    {
        NarrationOutput output(root,[&](const auto&,const auto&){++errors;},std::make_unique<CompletingDevice>(stale_error));
        ReleaseDevice release{stale_error};
        output.submit({action(NarrationActionKind::Play,"old")});
        require(stale_error->wait([&]{return stale_error->plays==1;}),"old failing Play must enter");
        output.submit({action(NarrationActionKind::Play,"new")});
        stale_error->change([&]{stale_error->fail_play=false;stale_error->hold_play=false;});
        require(wait_status(output,[](const auto& s){return s.active&&!s.queued&&s.cue_id=="new"&&!s.error;}),"superseded Play failure must preserve newer busy state");
    }
    require(errors==0,"superseded completion and errors must not call the current error handler");

    auto stopping=std::make_shared<CompletionState>();stopping->hold_poll=true;
    {
        NarrationOutput output(root,[&](const auto&,const auto&){++errors;},std::make_unique<CompletingDevice>(stopping));
        ReleaseDevice release{stopping};output.submit({action(NarrationActionKind::Play)});
        require(stopping->wait([&]{return stopping->polls>=1;}),"active device poll must hold the Stop acknowledgement");
        output.submit({action(NarrationActionKind::Stop)});
        const auto stop_generation=output.status().generation;
        require(output.status().active&&output.status().queued&&!output.status().completed&&!output.status().error,
                "pending Stop must remain busy until actual device teardown");
        stopping->change([&]{stopping->done=true;stopping->hold_poll=false;});
        require(wait_status(output,[](const auto& s){return !s.active&&!s.queued&&!s.completed&&!s.error;}),
                "Stop acknowledgement must clear busy without stale completion");
        require(output.status().generation==stop_generation,"old completion cannot overwrite a pending Stop generation");
    }

    for(bool poll_failure:{false,true}) {
        auto failing=std::make_shared<CompletionState>();failing->fail_play=!poll_failure;failing->fail_poll=poll_failure;
        NarrationOutput output(root,[&](const auto&,const auto&){++errors;},std::make_unique<CompletingDevice>(failing));
        output.submit({action(NarrationActionKind::Play)});
        require(wait_status(output,[](const auto& s){return s.error&&!s.active&&!s.queued&&!s.message.empty();}),"current device failure must publish an error and clear busy");
        auto status=output.status();const auto failed_generation=status.generation;
        require(!status.completed&&!status.paused&&status.cue_id=="intro","failed status must keep only the failed cue identity");
        failing->change([&]{failing->fail_play=false;failing->fail_poll=false;});
        output.submit({action(NarrationActionKind::Play,"recovered")});
        require(wait_status(output,[](const auto& s){return s.active&&!s.queued&&!s.error&&s.cue_id=="recovered";}),"new Play must clear the error latch and become busy");
        require(output.status().generation>failed_generation,"recovery must use a new generation");
        output.submit({action(NarrationActionKind::Stop)});
        require(wait_status(output,[](const auto& s){return !s.active&&!s.queued&&!s.error&&!s.completed;}),
                "Stop after failure recovery must clear busy after device acknowledgement");
    }
    require(errors==2,"each current failure must report once without stale callbacks");
}
}
int main(int argc,char** argv) {
    if(argc>2) return 2;
    const auto parent=argc==2?std::filesystem::absolute(argv[1]):std::filesystem::temp_directory_path();
    std::filesystem::create_directories(parent);
    std::filesystem::path root;
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned i=0;i<64;++i) {
        auto candidate=parent/("ResonanceNarrationOutputTests-"+std::to_string(stamp)+"-"+std::to_string(i));
        if(std::filesystem::create_directory(candidate)){root=std::move(candidate);break;}
    }
    if(root.empty())throw std::runtime_error("Cannot allocate fresh narration test fixtures");
    std::cout<<"Fixtures: "<<root.string()<<'\n';
    bytes(root/"voice.wav",wave());text(root/"cues.tsv",header+row);
    const std::string paced_header="scene\tevent\tid\taudio\ttext\tfinish_on_advance\n";
    const std::string paced_row="/Game/Scene\tdialogue:Look!\tpose\tvoice.wav\tA figure turns.\t1\n";
    text(root/"paced.tsv",paced_header+paced_row);
    try {
        const auto paced=load_paced_narration_manifest(root/"paced.tsv",root);
        check(paced.size()==1&&paced[0].event=="dialogue:Look!"&&paced[0].finish_on_advance&&
            paced[0].caption_sha256.empty(),"legacy paced manifest preserves native event and advance policy");
    } catch(const std::exception& e){check(false,e.what());}
    const auto fingerprint_header=paced_header.substr(0,paced_header.size()-1)+"\tcaption_sha256\n";
    const std::string fingerprint(64,'a');
    const std::string fingerprint_row="/Game/Scene\ttable:/game/scene.scene|Look\tpose\tvoice.wav\tA figure turns.\t1\t";
    text(root/"keyed-paced.tsv",fingerprint_header+fingerprint_row+fingerprint+"\n");
    text(root/"scene-text.tsv",header+"/Game/Opening\tintro\t1\t2\tmissing.wav\tCristaux en suspension.\n");
    text(root/"paced-text.tsv",fingerprint_header+
        "/Game/Scene\ttable:/game/scene.scene|Look\tpose\t\tUne silhouette se retourne.\t1\t"+fingerprint+"\n");
    try {
        const auto timed=load_narration_manifest(root/"scene-text.tsv",root,DescriptionSource::ScreenReader);
        const auto paced=load_paced_narration_manifest(root/"paced-text.tsv",root,DescriptionSource::ScreenReader);
        check(timed.size()==1&&timed[0].audio.empty()&&timed[0].text=="Cristaux en suspension."&&timed[0].start==1,
            "localized timed description can load without its old recording");
        check(paced.size()==1&&paced[0].audio.empty()&&paced[0].text=="Une silhouette se retourne."&&
            paced[0].caption_sha256==fingerprint,"text-only paced description keeps native caption fingerprint");
    } catch(const std::exception& e){check(false,e.what());}
    rejects([&]{load_narration_manifest(root/"scene-text.tsv",root);},"recorded manifest still requires its WAV");
    try {
        const auto paced=load_paced_narration_manifest(root/"keyed-paced.tsv",root);
        check(paced.size()==1&&paced[0].event=="table:/game/scene.scene|Look"&&
            paced[0].caption_sha256==fingerprint,"paced manifest loads an optional caption fingerprint alongside native event");
    } catch(const std::exception& e){check(false,e.what());}
    for(const auto& bad:std::vector<std::string>{paced_header,paced_header+paced_row+paced_row,
        paced_header+"/Game/Scene\te\ti\t../voice.wav\tText\t1\n",
        paced_header+"/Game/Scene\te\ti\tmissing.wav\tText\t1\n",
        paced_header+"/Game/Scene\te\ti\tvoice.wav\tText\tyes\n",
        paced_header+"/Game/Scene\te\ti\tvoice.wav\tText\t1\textra\n",
        fingerprint_header+fingerprint_row+"garbage\n",
        fingerprint_header+paced_row}) {
        text(root/"invalid-paced.tsv",bad);
        rejects([&]{load_paced_narration_manifest(root/"invalid-paced.tsv",root);},"invalid paced table rejected atomically");
    }
    bytes(root/"long.wav",wave(120001));
    text(root/"long-paced.tsv",paced_header+"/Game/Scene\te\ti\tlong.wav\tText\t0\n");
    rejects([&]{load_paced_narration_manifest(root/"long-paced.tsv",root);},"paced recording longer than fifteen seconds rejected");
    try{
        const auto w=read_narration_wave(root/"voice.wav");
        check(w.channels==1&&w.sample_rate==8000&&w.samples.size()==1600,"valid PCM16 wave");
        check(w.duration()==.1,"PCM duration");
        const auto cues=load_narration_manifest(root/"cues.tsv",root);
        check(cues.size()==1&&cues[0].id=="intro"&&cues[0].text=="Crystals float.","valid cue manifest");
    }catch(const std::exception& e){check(false,e.what());}
    text(root/"bom.tsv","\xef\xbb\xbfscene\tid\tstart\tend\taudio\ttext\r\n/Game/Opening\tbom\t1\t2\tvoice.wav\tA glowing crystal.\r\n");
    try{check(load_narration_manifest(root/"bom.tsv",root).at(0).id=="bom","UTF-8 BOM and CRLF accepted");}catch(const std::exception& e){check(false,e.what());}
    rejects([&]{load_narration_manifest(root/"missing.tsv",root);},"missing manifest is error");
    for(const auto& bad:std::vector<std::string>{"",header,header+row+row,header+"/Game/Opening\ta\tNaN\t2\tvoice.wav\tText\n",
        header+"/Game/Opening\ta\t1junk\t2\tvoice.wav\tText\n",header+"/Game/Opening\ta\t1\t2\t../voice.wav\tText\n",
        header+"/Game/Opening\ta\t1\t2\tC:/voice.wav\tText\n",header+"/Game/Opening\ta\t1\t2\tmissing.wav\tText\n",
        header+"/Game/Opening\ta\t1\t1.05\tvoice.wav\tText\n",header+"/Game/Opening\ta\t1\t2\tvoice.wav\tText\textra\n",
        header+"/Game/Opening\ta\t1\t2\tvoice.wav\t\xc0\xaf\n",
        header+row+"/Game/Opening\tb\t1.5\t3\tvoice.wav\tOverlaps\n"}) {
        text(root/"invalid.tsv",bad);rejects([&]{load_narration_manifest(root/"invalid.tsv",root);},"malformed manifest rejected");
    }
    auto corrupt=wave();corrupt[20]=3;bytes(root/"float.wav",corrupt);
    rejects([&]{read_narration_wave(root/"float.wav");},"float WAV rejected");
    corrupt=wave();corrupt.resize(43);bytes(root/"truncated.wav",corrupt);
    rejects([&]{read_narration_wave(root/"truncated.wav");},"truncated WAV rejected");
    corrupt=wave();corrupt[32]=4;bytes(root/"alignment.wav",corrupt);
    rejects([&]{read_narration_wave(root/"alignment.wav");},"bad PCM frame alignment rejected");
    corrupt=wave();corrupt[40]=0xff;corrupt[41]=0xff;bytes(root/"bounds.wav",corrupt);
    rejects([&]{read_narration_wave(root/"bounds.wav");},"out-of-bounds WAV chunk rejected");
    corrupt=wave(0);bytes(root/"empty.wav",corrupt);
    rejects([&]{read_narration_wave(root/"empty.wav");},"empty PCM rejected");
    corrupt=wave();corrupt.insert(corrupt.begin()+36,{'J','U','N','K',1,0,0,0,0x42,0});
    for(unsigned i=0;i<4;++i)corrupt[4+i]=static_cast<unsigned char>((corrupt.size()-8)>>(8*i));
    bytes(root/"metadata.wav",corrupt);
    try{check(read_narration_wave(root/"metadata.wav").duration()==.1,"RIFF metadata and odd chunk padding accepted");}catch(const std::exception& e){check(false,e.what());}

    auto state=std::make_shared<FakeState>();int errors=0;
    {
        NarrationOutput output(root,[&](const auto&,const auto&){++errors;},std::make_unique<FakeDevice>(state));
        output.submit({action(NarrationActionKind::Play)});check(state->wait(2),"worker starts audio");
        output.submit({action(NarrationActionKind::Pause)});check(state->wait(3),"worker pauses audio");
        output.submit({action(NarrationActionKind::Resume)});check(state->wait(4),"worker resumes audio");
        output.submit({action(NarrationActionKind::Stop)});check(state->wait(5),"worker stops audio");
    }
    {std::lock_guard lock(state->mutex);check(state->events==std::vector<std::string>({"stop","play","pause","resume","stop","stop"}),"ordered lifecycle and shutdown");}
    check(errors==0,"successful worker has no errors");

    auto held=std::make_shared<FakeState>();held->hold=true;held->fail=true;
    {
        NarrationOutput output(root,[&](const auto&,const auto&){++errors;},std::make_unique<FakeDevice>(held));
        output.submit({action(NarrationActionKind::Play)});check(held->wait(2),"blocked play entered");
        output.submit({action(NarrationActionKind::Play,"old"),action(NarrationActionKind::Pause,"old"),action(NarrationActionKind::Stop)});
        {std::lock_guard lock(held->mutex);held->hold=false;held->cv.notify_all();}
        check(held->wait(4),"superseding Stop reaches worker");
    }
    check(errors==0,"superseded play error callback suppressed");
    {std::lock_guard lock(held->mutex);check(std::count(held->events.begin(),held->events.end(),"play")==1,"queued stale Play discarded");}

    auto failing=std::make_shared<FakeState>();failing->fail=true;
    auto callback=std::make_shared<FakeState>();
    {
        NarrationOutput output(root,[&](const auto& a,const auto& error){check(a.cue_id=="intro"&&!error.empty()&&a.session=="test-session"&&a.dialogue_id=="test-line"&&a.generation>0,"error preserves immutable native run, caption and actual generation");callback->event("error");},std::make_unique<FakeDevice>(failing));
        output.submit({action(NarrationActionKind::Play)});check(callback->wait(1),"current playback failure reported");
    }
    try{completion_checks(root);}catch(const std::exception& e){check(false,e.what());}
    if(failed){std::cerr<<failed<<" failures\n";return 1;}
    std::cout<<"Narration output checks passed\n";
}
