#include "resonance/Narration.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
using namespace resonance;
namespace {
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
using Kind=NarrationActionKind;
NarrationClock clock_at(double time,double sample=-1) {
    return {"player-1","/Game/Opening.Opening",time,sample<0?time:sample,1,true,true,false,true,false};
}
NarrationCue cue(std::string id,double start,double end) {
    return {"/Game/Opening.Opening",std::move(id),"Visible action","opening/001.wav",start,end};
}
void expect(const std::vector<NarrationAction>& actions,Kind kind,const char* why) {
    require(actions.size()==1&&actions.front().kind==kind,why);
}
template<class F> void rejects(F&& f,const char* why) {
    bool rejected=false;
    try { f(); } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,why);
}
void natural_playback() {
    auto measured=cue("measured",1,5);measured.duration=2;
    NarrationScheduler loading({measured});
    loading.observe(clock_at(.9));
    expect(loading.observe(clock_at(1.6)),Kind::Play,"a natural loading gap may start a complete measured recording that still fits");
    auto tight=cue("tight",1,3.21);tight.duration=2;
    NarrationScheduler sampled({tight});sampled.observe(clock_at(.95));
    expect(sampled.observe(clock_at(1.08)),Kind::Play,"normal capture latency uses the asset's authored margin without dropping its recording");
    NarrationScheduler too_late({measured});
    too_late.observe(clock_at(.9));
    require(too_late.observe(clock_at(3.1)).empty(),"measured recording must remain silent when it cannot finish with margin");
    NarrationScheduler jumped({measured});jumped.observe(clock_at(.9));
    require(jumped.observe(clock_at(1.6,.95)).empty(),"known duration must not permit a seek catchup");
    measured.duration=4.1;
    rejects([&]{NarrationScheduler invalid({measured});},"duration larger than cue interval must be rejected");
    NarrationScheduler s({cue("a",0,2),cue("b",3,5)});
    auto out=s.observe(clock_at(.08));
    expect(out,Kind::Play,"first capture near the cue start must play");
    require(out[0].cue_id=="a"&&out[0].audio=="opening/001.wav"&&out[0].text=="Visible action"&&out[0].offset==0,
            "play action must carry reviewed text and relative audio path at offset zero");
    require(s.observe(clock_at(.08,.1)).empty(),"same native time must deduplicate");
    require(s.observe(clock_at(1.08,1.1)).empty(),"one-second normal sampling gap must not be treated as a seek");
    expect(s.observe(clock_at(2.08,2.1)),Kind::Stop,"native cue end must cut off active narration");
    require(s.observe(clock_at(2.9,2.92)).empty(),"future cue must stay silent");
    out=s.observe(clock_at(3.04,3.06));
    expect(out,Kind::Play,"naturally crossed cue start must play");
    require(out[0].cue_id=="b","crossing must choose the current cue");
    require(s.observe(clock_at(3.02,3.08)).empty(),"small clock jitter must not replay a consumed cue");
    require(s.observe(clock_at(3.06,3.12)).empty(),"jitter crossing must still deduplicate");
}
void pauses_and_focus() {
    NarrationScheduler s({cue("a",0,8)});
    s.observe(clock_at(0));
    auto c=clock_at(1);
    c.paused=true;c.playing=false;c.rate=0;
    expect(s.observe(c),Kind::Pause,"native pause must pause active audio, including zero paused rate");
    c.sample_seconds=11;
    require(s.observe(c).empty(),"a prolonged stationary pause must stay paused");
    c.paused=false;c.playing=true;c.rate=1;c.sample_seconds=11.01;
    expect(s.observe(c),Kind::Resume,"native resume at the same time must resume audio");
    c.foreground=false;c.sample_seconds=11.02;
    expect(s.observe(c),Kind::Pause,"losing foreground must pause audio");
    c.foreground=true;c.sample_seconds=11.03;
    expect(s.observe(c),Kind::Resume,"regaining foreground at the same native time must resume");
    c.foreground=false;c.sample_seconds=11.04;
    s.observe(c);
    c.seconds=2;c.sample_seconds=12.04;
    expect(s.observe(c),Kind::Stop,"clock advancing while output is paused must discard unsynchronized audio");
    c.foreground=true;c.seconds=3;c.sample_seconds=13.04;
    require(s.observe(c).empty(),"returning to foreground must not catch up the discarded cue");

    NarrationScheduler hidden({cue("future",1,4)});
    c=clock_at(0);c.foreground=false;hidden.observe(c);
    c=clock_at(1.05);c.foreground=false;
    require(hidden.observe(c).empty(),"background cue starts must not play");
    c=clock_at(1.08);c.foreground=true;
    require(hidden.observe(c).empty(),"focus return must not catch up a background start");
}
void custom_clock_holds() {
    NarrationScheduler held({cue("a",0,4)});
    expect(held.observe(clock_at(0)),Kind::Play,"opening cue must start before the custom clock hold");
    require(held.observe(clock_at(0,.08)).empty(),"one flat sample must allow clock quantization");
    require(held.observe(clock_at(0,.16)).empty(),"short clock quantization must not pause early");
    expect(held.observe(clock_at(0,.24)),Kind::Pause,"repeated 80ms flat samples must pause active audio within 240ms");
    require(held.observe(clock_at(0,.32)).empty(),"held native clock must not resume audio on another flat sample");
    require(held.observe(clock_at(0,.40)).empty(),"a prolonged custom hold must stay paused without repeated actions");
    require(held.observe(clock_at(1./30,.48)).empty(),"one native frame of advance must not resume audio that is still ahead");
    require(held.observe(clock_at(.16,.56)).empty(),"held audio must wait until the picture catches its estimated position");
    expect(held.observe(clock_at(.23,.64)),Kind::Resume,"genuine native recovery within one frame of held audio must resume safely");
    require(held.observe(clock_at(.31,.72)).empty(),"normal native advance after recovery must remain synchronized");

    NarrationScheduler overshot({cue("a",0,.2),cue("b",.3,2)});
    overshot.observe(clock_at(0));overshot.observe(clock_at(0,.08));overshot.observe(clock_at(0,.16));
    expect(overshot.observe(clock_at(0,.24)),Kind::Pause,"second hold must pause before recovery");
    auto out=overshot.observe(clock_at(.36,.32));
    expect(out,Kind::Stop,"native recovery beyond held audio by 100ms must stop without playing a crossed future cue");
    require(overshot.observe(clock_at(.44,.40)).empty(),"overshot recovery must not catch up the skipped cue");

    NarrationScheduler before({cue("a",1,2),cue("b",3,4)});
    before.observe(clock_at(.8,.8));
    for(double sample:{.88,.96,1.04}) require(before.observe(clock_at(.8,sample)).empty(),"hold before the first cue must stay silent");
    require(before.observe(clock_at(1.04,1.12)).empty(),"hold recovery must not infer a cue start that it crossed");
    require(before.observe(clock_at(1.12,1.20)).empty(),"the skipped recovery cue must not start late on the next frame");
    before.observe(clock_at(2.95,3.03));
    out=before.observe(clock_at(3.04,3.12));
    expect(out,Kind::Play,"a later naturally crossed cue must remain available after a hold");
    require(out[0].cue_id=="b","recovery must preserve future cue identity");

    NarrationScheduler background({cue("a",0,4)});
    background.observe(clock_at(0));
    auto c=clock_at(.08,.08);c.foreground=false;
    expect(background.observe(c),Kind::Pause,"background must pause audio before a clock hold");
    for(double sample:{.16,.24,.32}) {c.sample_seconds=sample;require(background.observe(c).empty(),"background hold must stay paused");}
    c.foreground=true;c.sample_seconds=.40;
    require(background.observe(c).empty(),"focus return into a held clock must not resume audio");
    c.seconds=.08+1./30;c.sample_seconds=.48;
    expect(background.observe(c),Kind::Resume,"already aligned background audio may resume on genuine native advance");

    NarrationScheduler hidden({cue("a",1,2),cue("b",3,4)});
    c=clock_at(.8,.8);c.foreground=false;hidden.observe(c);
    for(double sample:{.88,.96,1.04}) {c.sample_seconds=sample;require(hidden.observe(c).empty(),"background hold before a cue must stay silent");}
    c=clock_at(1.04,1.12);
    require(hidden.observe(c).empty(),"background recovery must not invent a visible cue start");
    require(hidden.observe(clock_at(1.12,1.20)).empty(),"focus recovery must not catch up a crossed cue");

    NarrationScheduler quantized({cue("a",0,8)});
    quantized.observe(clock_at(0));
    for(int i=1;i<=60;++i) {
        const double sample=i/60.;const double native=std::floor(sample*30+1e-9)/30.;
        require(quantized.observe(clock_at(native,sample)).empty(),"ordinary 30fps clock quantization must not trigger a custom hold");
    }
    require(quantized.observe(clock_at(2,2)).empty(),"slow sampling with matching native progress must keep working");

    NarrationScheduler quiet({cue("a",0,4)});
    quiet.observe(clock_at(0));quiet.observe(clock_at(0,.08));quiet.observe(clock_at(0,.16));
    expect(quiet.observe(clock_at(0,.24)),Kind::Pause,"quiet hold must pause before a slow sampling gap");
    require(quiet.observe(clock_at(0,1.24)).empty(),"an already paused custom hold must survive a long flat sampling gap");
    require(quiet.observe(clock_at(1./30,1.32)).empty(),"slow sampling must not lose the held audio alignment estimate");
    expect(quiet.observe(clock_at(.23,1.40)),Kind::Resume,"held audio may resume once the native picture catches up after slow sampling");

    NarrationScheduler jitter({cue("a",1,4)});
    jitter.observe(clock_at(1,0));
    require(jitter.observe(clock_at(1.002,.08)).empty(),"sub-frame native jitter must allow the hold grace period");
    require(jitter.observe(clock_at(1.001,.16)).empty(),"small backward jitter must not reset the cumulative hold timer");
    expect(jitter.observe(clock_at(1.003,.24)),Kind::Pause,"sub-5ms native jitter must not hide a cumulative custom hold");

    NarrationScheduler new_run({cue("a",0,4)});
    new_run.observe(clock_at(0));new_run.observe(clock_at(0,.08));new_run.observe(clock_at(0,.16));new_run.observe(clock_at(0,.24));
    c=clock_at(0,.32);c.session="player-2";
    out=new_run.observe(c);
    require(out.size()==2&&out[0].kind==Kind::Stop&&out[1].kind==Kind::Play,"a new run must discard the prior clock-hold state");
}
void seek_and_rewind() {
    NarrationScheduler s({cue("a",0,2),cue("b",4,6)});
    s.observe(clock_at(0));
    expect(s.observe(clock_at(4.05,.08)),Kind::Stop,"forward seek must stop audio without starting the skipped-to cue");
    require(s.observe(clock_at(4.1,.13)).empty(),"post-seek baseline must not catch up the skipped cue");
    require(s.observe(clock_at(3,.2)).empty(),"rewind must establish a new baseline");
    expect(s.observe(clock_at(4.05,1.25)),Kind::Play,"rewinding before a cue must allow its later natural start");
    expect(s.observe(clock_at(0,1.3)),Kind::Stop,"rewinding active playback must stop its audio first");
    auto out=s.observe(clock_at(.05,1.35));
    expect(out,Kind::Play,"rewind to scene origin must rearm the opening cue");
    require(out[0].cue_id=="a","rewind must not reveal a later cue");
    auto c=clock_at(.06,1.36);c.seeking=true;
    expect(s.observe(c),Kind::Stop,"explicit native seek state must stop audio");
    c.seeking=false;c.seconds=4.04;c.sample_seconds=1.4;
    require(s.observe(c).empty(),"first frame after explicit seek must establish a baseline without catchup");
}
void identities_and_invalid_clocks() {
    NarrationScheduler s({cue("a",0,4)});
    s.observe(clock_at(0));
    auto c=clock_at(.02,.02);c.session="player-2";
    auto out=s.observe(c);
    require(out.size()==2&&out[0].kind==Kind::Stop&&out[1].kind==Kind::Play,
            "a new instance of the same scene must stop the old run and start its own opening");
    c.scene="/Game/Unknown.Unknown";c.seconds=.03;c.sample_seconds=.03;
    expect(s.observe(c),Kind::Stop,"unknown scene must stop the previous scene's audio");
    c.seconds=2;c.sample_seconds=2;
    require(s.observe(c).empty(),"unknown scene must remain silent");
    require(s.has_scene("/Game/Opening.Opening")&&!s.has_scene(c.scene),"scene query must use exact mapped identity");

    for(int mode=0;mode<8;++mode) {
        NarrationScheduler invalid({cue("a",0,4)});invalid.observe(clock_at(0));
        c=clock_at(.05,.05);
        if(mode==0)c.valid=false;
        if(mode==1)c.playing=false;
        if(mode==2)c.seconds=std::numeric_limits<double>::quiet_NaN();
        if(mode==3)c.sample_seconds=std::numeric_limits<double>::infinity();
        if(mode==4)c.rate=2;
        if(mode==5)c.seconds=-1;
        if(mode==6)c.session.clear();
        if(mode==7)c.scene.clear();
        expect(invalid.observe(c),Kind::Stop,"invalid, ended or unsupported-rate playback must stop narration");
        require(invalid.observe(c).empty(),"invalid state must not repeatedly issue stop");
    }
    NarrationScheduler late({cue("a",0,4),cue("b",5,8)});
    require(late.observe(clock_at(.3)).empty(),"late initial capture must not catch up the opening cue");
    require(late.observe(clock_at(5.5,5.5)).empty(),"long natural capture gap must skip a missed start");

    NarrationScheduler prepared({cue("a",0,4)});
    c=clock_at(0);c.playing=false;
    require(prepared.observe(c).empty(),"prepared but stopped player must stay silent");
    expect(prepared.observe(clock_at(.05,.1)),Kind::Play,"starting a previously stopped player at the origin must read its opening");
    c=clock_at(4,4.1);c.playing=false;
    expect(prepared.observe(c),Kind::Stop,"ending a run must stop its output");
    expect(prepared.observe(clock_at(.05,4.2)),Kind::Play,"a stopped player restarted at the origin must rearm its opening");

    NarrationScheduler stall({cue("a",0,4)});stall.observe(clock_at(0));
    expect(stall.observe(clock_at(.05,1)),Kind::Stop,"unobserved native clock stall must discard audio that ran ahead");
    NarrationScheduler reverse_sample({cue("a",0,4)});reverse_sample.observe(clock_at(0,10));
    expect(reverse_sample.observe(clock_at(.05,9)),Kind::Stop,"backward monotonic sample must not preserve playback assumptions");
}
void tables_and_reset() {
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    for(auto bad:{cue("a",-1,1),cue("a",1,1),cue("a",2,1),cue("a",nan,3),cue("a",1,nan)})
        rejects([&]{NarrationScheduler s({bad});},"malformed and nonfinite cue intervals must be rejected");
    for(const auto* path:{"/a.wav","C:\\a.wav","../a.wav","a/../b.wav","a\\..\\b.wav","a/./b.wav","","\\a.wav"}) {
        auto bad=cue("a",0,1);bad.audio=path;
        rejects([&]{NarrationScheduler s({bad});},"audio path must remain a safe relative child path");
    }
    auto bad=cue("",0,1);
    rejects([&]{NarrationScheduler s({bad});},"missing cue id must be rejected");
    bad=cue("a",0,1);bad.scene.clear();
    rejects([&]{NarrationScheduler s({bad});},"missing scene must be rejected");
    bad=cue("a",0,1);bad.text.clear();
    rejects([&]{NarrationScheduler s({bad});},"missing description text must be rejected");
    rejects([&]{NarrationScheduler s({cue("same",0,1),cue("same",2,3)});},"duplicate scene/id pairs must be rejected");
    rejects([&]{NarrationScheduler s({cue("a",0,2),cue("b",1,3)});},"overlapping cues in one scene must be rejected");
    NarrationScheduler s({cue("b",2,4),cue("a",0,2)});
    require(s.cues()[0].id=="a","input cues must be sorted by scene and start");
    s.observe(clock_at(0));
    rejects([&]{s.set_cues({cue("bad",3,2)});},"invalid replacement table must be rejected");
    require(s.observe(clock_at(.1)).empty(),"rejected replacement must preserve the active cue and table");
    expect(s.reset(),Kind::Stop,"explicit reset must stop active output");
    require(s.reset().empty(),"repeated reset must stay silent");
    expect(s.observe(clock_at(.05,.2)),Kind::Play,"reset must keep validated cues and permit a new run");
    expect(s.set_cues({cue("new",0,1)}),Kind::Stop,"successful table replacement must return a stop for old output");
    require(s.cues().size()==1&&s.cues()[0].id=="new","replacement table must become active atomically");
    auto other=cue("new",0,1);other.scene="/Game/Other.Other";
    NarrationScheduler parallel({cue("new",0,1),other});
    require(parallel.cues().size()==2,"different scenes may reuse cue ids and timing");
}
}
int main() {
    try {
        natural_playback();pauses_and_focus();custom_clock_holds();seek_and_rewind();identities_and_invalid_clocks();tables_and_reset();
        std::cout<<"Narration scheduler checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
