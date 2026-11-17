#include "resonance/PacedNarration.hpp"
#include <iostream>
#include <stdexcept>
using namespace resonance;
namespace {
using Kind=NarrationActionKind;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
PacedNarrationCue cue(std::string event,std::string id,bool finish=true){return {"/Game/Scene.Scene",std::move(event),std::move(id),"A visible action.","voice/action.wav",finish};}
PacedNarrationFrame frame(){return {"map-1","run-1","/Game/Scene.Scene",{}, {},true,false,false,true,false,false,true};}
NarrationPlaybackStatus audio(std::string id,std::uint64_t generation,bool active=true,bool completed=false,bool error=false){
    return {generation,"/Game/Scene.Scene",std::move(id),active,false,completed,error,false,error?"Playback failed":""};
}
void event(PacedNarrationFrame& f,std::string key,std::uint64_t occurrence){f.event=PacedNarrationEvent{std::move(key),occurrence};}
void expect(const std::vector<NarrationAction>& actions,Kind kind,const std::string& id,const char* message){
    require(actions.size()==1&&actions[0].kind==kind&&actions[0].cue_id==id,message);
}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}require(rejected,message);}
void native_event_and_gate(){
    PacedNarrationScheduler s({cue("EntryA","a"),cue("EntryB","b")});auto f=frame();
    require(s.observe(f).empty(),"scene identity without an actual native event must stay silent");
    event(f,"EntryA",1);f.may_start=false;
    require(s.observe(f).empty(),"matching native event must wait behind the supplied voice gate");
    f.event.reset();f.may_start=true;
    auto out=s.observe(f);expect(out,Kind::Play,"a","current pending native event must start when the gate opens");
    require(out[0].scene==f.scene&&out[0].audio=="voice/action.wav"&&out[0].text=="A visible action."&&out[0].offset==0,
            "paced Play must carry reviewed scene, recording and text at offset zero");
    f.audio=audio("a",1);event(f,"EntryA",1);
    require(s.observe(f).empty(),"same run and event occurrence must deduplicate");
    f.event.reset();f.may_start=false;
    require(s.observe(f).empty(),"dialogue gate or a TalkWait hold must not pause a recording already playing");
    f.audio=audio("a",1,false,true);f.may_start=true;
    require(s.observe(f).empty(),"actual completion must release the current cue without a timed Stop");
    event(f,"EntryA",1);require(s.observe(f).empty(),"completion must not rearm an already consumed occurrence");
    event(f,"EntryA",2);expect(s.observe(f),Kind::Play,"a","a later actual occurrence may describe a repeated action");
}
void pending_replacement_and_completion(){
    PacedNarrationScheduler s({cue("EntryA","a"),cue("EntryB","b"),cue("EntryC","c")});auto f=frame();
    event(f,"EntryA",1);s.observe(f);f.audio=audio("a",1);
    event(f,"EntryB",2);require(s.observe(f).empty(),"a configured short current cue may finish after player advance");
    event(f,"EntryC",3);f.may_start=false;
    require(s.observe(f).empty(),"newer native event must replace the older pending pose");
    f.event.reset();f.audio=audio("a",1,false,true);
    require(s.observe(f).empty(),"completion must still respect a closed native voice gate");
    f.may_start=true;expect(s.observe(f),Kind::Play,"c","only the latest pending foreground event may follow completion");

    PacedNarrationScheduler interrupted({cue("EntryA","a",false),cue("EntryB","b")});f=frame();
    event(f,"EntryA",1);interrupted.observe(f);f.audio=audio("a",1);event(f,"EntryB",2);
    auto out=interrupted.observe(f);
    require(out.size()==2&&out[0].kind==Kind::Stop&&out[0].cue_id=="a"&&out[1].kind==Kind::Play&&out[1].cue_id=="b",
            "a cue configured not to finish on advance must stop before a new matching pose starts");

    PacedNarrationScheduler unseen({cue("EntryA","a"),cue("EntryB","b")});f=frame();f.may_start=false;
    event(f,"EntryA",1);unseen.observe(f);event(f,"EntryB",2);unseen.observe(f);
    event(f,"UnknownEntry",3);unseen.observe(f);f.event.reset();f.may_start=true;
    require(unseen.observe(f).empty(),"an unmapped newer event must discard stale pending narration");
    event(f,"EntryA",4);f.may_start=false;unseen.observe(f);f.foreground=false;f.event.reset();unseen.observe(f);
    f.foreground=true;f.may_start=true;event(f,"EntryA",4);
    require(unseen.observe(f).empty(),"losing foreground must discard pending cues without later catchup");
    f.foreground=false;event(f,"EntryB",5);unseen.observe(f);f.foreground=true;
    require(unseen.observe(f).empty(),"native events seen only in the background must remain consumed and silent");
}
void true_pause_focus_and_modal(){
    PacedNarrationScheduler s({cue("EntryA","a"),cue("EntryB","b")});auto f=frame();
    event(f,"EntryA",1);s.observe(f);f.event.reset();f.audio=audio("a",1);
    f.paused=true;expect(s.observe(f),Kind::Pause,"a","true user pause must pause actual recording");
    require(s.observe(f).empty(),"unchanged user pause must deduplicate");
    f.paused=false;expect(s.observe(f),Kind::Resume,"a","true user resume must resume the recording");
    f.foreground=false;event(f,"EntryB",2);expect(s.observe(f),Kind::Pause,"a","focus loss must pause current short cue while discarding background events");
    f.foreground=true;f.event.reset();expect(s.observe(f),Kind::Resume,"a","focus recovery may finish the configured current cue");
    f.audio=audio("a",1,false,true);require(s.observe(f).empty(),"discarded background event must not queue behind completion");
    event(f,"EntryB",3);expect(s.observe(f),Kind::Play,"b","new foreground event must remain usable after focus recovery");
    f.audio=audio("b",2);f.modal=true;f.event.reset();expect(s.observe(f),Kind::Stop,"b","modal must expire current narration rather than pausing stale poses");
    f.audio=audio("b",3,false);f.modal=false;event(f,"EntryB",3);
    require(s.observe(f).empty(),"closing modal must not replay an old event occurrence");
    event(f,"EntryB",4);expect(s.observe(f),Kind::Play,"b","a newly fired event after modal may start");
}
void expiry_and_terminal_runs(){
    for(int condition=0;condition<7;++condition){
        PacedNarrationScheduler s({cue("EntryA","a"),cue("EntryB","b")});auto f=frame();
        event(f,"EntryA",1);s.observe(f);f.audio=audio("a",1);f.event.reset();
        if(condition==0)f.map="map-2";
        if(condition==1)f.session="run-2";
        if(condition==2)f.scene="/Game/Other.Other";
        if(condition==3)f.ended=true;
        if(condition==4)f.skipped=true;
        if(condition==5)f.active=false;
        if(condition==6)f.modal=true;
        expect(s.observe(f),Kind::Stop,"a","scene, run, map, end, skip, inactivity and modal must expire current audio");
        require(s.observe(f).empty(),"expiry must not repeatedly stop the same recording");
        if(condition==3||condition==4){
            f.ended=false;f.skipped=false;event(f,"EntryB",2);f.audio=audio("a",2,false);
            require(s.observe(f).empty(),"ended or skipped run must stay terminal until a new run/context");
            f.session="run-new";event(f,"EntryA",1);
            expect(s.observe(f),Kind::Play,"a","new run must rearm actual native events");
        }
    }
}
void generation_and_failures(){
    PacedNarrationScheduler s({cue("EntryA","a"),cue("EntryB","b")});auto f=frame();
    f.audio.generation=10;event(f,"EntryA",1);s.observe(f);f.audio=audio("a",11);f.event.reset();s.observe(f);
    f.audio=audio("a",10,false,true);event(f,"EntryB",2);
    require(s.observe(f).empty(),"stale completion must not release an active newer recording");
    f.event.reset();f.audio=audio("a",11,false,true);
    expect(s.observe(f),Kind::Play,"b","matching actual completion may release the latest current event");
    f.audio=audio("b",12,false,false,true);f.event.reset();
    require(s.observe(f).empty(),"current playback error must clear the cue without a retry loop");
    event(f,"EntryB",2);require(s.observe(f).empty(),"failed occurrence must not automatically retry");

    PacedNarrationScheduler foreign({cue("EntryA","a"),cue("EntryB","b")});f=frame();event(f,"EntryA",1);foreign.observe(f);
    f.audio=audio("a",1);f.event.reset();foreign.observe(f);
    f.audio=audio("other",2);event(f,"EntryB",2);
    require(foreign.observe(f).empty(),"superseding foreign output must not be stopped or interrupted by a pending old pose");
    f.audio=audio("a",1,false,true);f.event.reset();
    require(foreign.observe(f).empty(),"stale foreign-era completion must not start a pending cue over newer output");
}
void validation_and_reset(){
    auto a=cue("EntryA","a");
    for(int bad=0;bad<8;++bad){auto invalid=a;
        if(bad==0)invalid.scene.clear();if(bad==1)invalid.event.clear();if(bad==2)invalid.id.clear();if(bad==3)invalid.text=" ";
        if(bad==4)invalid.audio="../voice.wav";if(bad==5)invalid.audio="C:/voice.wav";
        if(bad==6)invalid.event="Entry\nA";if(bad==7)invalid.text="\xc0\xaf";
        rejects([&]{PacedNarrationScheduler s({invalid});},"malformed paced cue fields and unsafe paths must be rejected");
    }
    rejects([&]{PacedNarrationScheduler s({a,a});},"duplicate native scene/event mapping must be rejected");
    auto repeated=a;repeated.event="Other";
    rejects([&]{PacedNarrationScheduler s({a,repeated});},"repeated scene/cue identity must be rejected");
    a.caption_sha256=std::string(64,'a');
    rejects([&]{PacedNarrationScheduler s({a});},"fingerprint requires a native table event");
    auto keyed=cue("table:/game/scene.scene|EntryA","a");
    keyed.caption_sha256="invalid";
    rejects([&]{PacedNarrationScheduler s({keyed});},"malformed fingerprint must be rejected");
    keyed.caption_sha256=std::string(64,'a');
    auto ambiguous=cue("table:/game/scene.scene|EntryB","b");
    ambiguous.caption_sha256=keyed.caption_sha256;
    rejects([&]{PacedNarrationScheduler s({keyed,ambiguous});},"repeated scene/fingerprint must be rejected");
    ambiguous.scene="/Game/Other.Other";
    PacedNarrationScheduler distinct({keyed,ambiguous});
    require(distinct.cues().size()==2,"different scenes may use the same visible caption");
    a.caption_sha256.clear();
    PacedNarrationScheduler s({a});auto f=frame();event(f,"EntryA",0);
    require(s.observe(f).empty(),"zero occurrence is not evidence of an actually fired event");
    event(f,"EntryA",1);s.observe(f);f.audio=audio("a",1);f.event.reset();s.observe(f);
    auto invalid=a;invalid.audio="../escape.wav";
    rejects([&]{s.set_cues({invalid});},"invalid replacement must preserve the validated current table");
    require(s.observe(f).empty(),"rejected replacement must not interrupt current recording");
    expect(s.reset(),Kind::Stop,"a","explicit policy reset must stop owned recording");
    require(s.reset().empty(),"repeated reset must remain silent");
    require(s.has_scene(f.scene)&&!s.has_scene("/Game/Other.Other"),"paced scene lookup must use exact asset identity");
}
}
int main(){try{native_event_and_gate();pending_replacement_and_completion();true_pause_focus_and_modal();expiry_and_terminal_runs();generation_and_failures();validation_and_reset();
    std::cout<<"Paced narration checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
