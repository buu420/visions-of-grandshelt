#include "resonance/Descriptions.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace resonance;
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool has(const DescriptionDecision& d,NarrationActionKind kind){for(const auto& a:d.actions)if(a.kind==kind)return true;return false;}
DescriptionFrame frame(){
    DescriptionFrame f;
    f.ui.context="map|talk";
    f.ui.dialogue=DialogueSnapshot{"line-1","Person","Look!",true,true};
    f.clocks={{"run","scene",1,1,1,true,false,true,true,false}};
    f.native_scene="scene";f.native_session="run";f.pause_known=true;f.voice_known=true;
    f.native_event_known=true;f.native_event_active=true;
    return f;
}
void set(DescriptionCoordinator& c){
    c.set_cues({},{{"scene","dialogue:Look!","pose","A figure turns.","voice.wav",true}});
}
constexpr auto stable_event="table:/game/strings/scene.scene|Opening_01";
const std::string caption_fingerprint(64,'a');
void stable_event_waits_for_completed_caption_and_native_voice(){
    DescriptionCoordinator c;
    c.set_cues({},{{"scene",stable_event,"pose","Une silhouette se retourne.","voice.wav",true}});
    auto f=frame();
    f.ui.dialogue->text="Regardez !";
    f.ui.dialogue->source_event=stable_event;
    f.ui.dialogue->ready=false;
    need(!has(c.observe(f),NarrationActionKind::Play),"stable ID requires a completed caption");
    f.ui.dialogue->ready=true;
    f.voice_busy=true;
    auto d=c.observe(f);
    need(d.defer_dialogue&&!has(d,NarrationActionKind::Play),"localized cue waits for native voice");
    f.voice_busy=false;
    d=c.observe(f);
    need(d.defer_dialogue&&has(d,NarrationActionKind::Play),"localized rendered caption triggers its stable cue");
}
void stable_event_requires_exact_identity_at_a_fresh_occurrence(){
    DescriptionCoordinator c;
    c.set_cues({},{{"scene",stable_event,"pose","A figure turns.","voice.wav",true}});
    auto f=frame();
    f.ui.dialogue->source_event="table:/game/strings/scene.scene|Opening_02";
    auto d=c.observe(f);
    need(!d.defer_dialogue&&!has(d,NarrationActionKind::Play),"another source cannot trigger a keyed description");
    f.ui.dialogue->source_event=stable_event;
    d=c.observe(f);
    need(!d.defer_dialogue&&!has(d,NarrationActionKind::Play),"late source discovery cannot replay an already released caption");
    f.ui.dialogue->id="line-2";
    need(has(c.observe(f),NarrationActionKind::Play),"a fresh native occurrence works with the same display text");
}
void stable_event_precedes_legacy_text_match(){
    DescriptionCoordinator c;
    c.set_cues({},{{"scene",stable_event,"keyed","A figure turns.","voice.wav",true},
                   {"scene","dialogue:Look!","legacy","A figure waits.","legacy.wav",true}});
    auto f=frame();
    f.ui.dialogue->source_event=stable_event;
    const auto d=c.observe(f);
    need(d.actions.size()==1&&d.actions[0].kind==NarrationActionKind::Play&&d.actions[0].cue_id=="keyed",
         "the exact source event takes precedence over coincident display text");
    DescriptionCoordinator legacy;set(legacy);
    need(has(legacy.observe(f),NarrationActionKind::Play),"an English legacy pack still matches text when source identity is present");
}
void unique_caption_fallback_keeps_scene_readiness_and_voice_gates(){
    DescriptionCoordinator c;
    c.set_cues({},{{"scene",stable_event,"pose","Une silhouette se retourne.","voice.wav",true,caption_fingerprint}});
    need(c.has_caption_fallback("scene")&&!c.has_caption_fallback("other scene"),"fallback availability uses exact scene");
    auto f=frame();
    f.ui.dialogue->text="Regardez !";
    f.caption_sha256=caption_fingerprint;
    f.native_scene="other scene";
    need(!has(c.observe(f),NarrationActionKind::Play),"fingerprint cannot trigger outside the verified scene");
    f.native_scene="scene";
    f.ui.dialogue->id="line-in-matched-scene";
    f.ui.dialogue->ready=false;
    need(!has(c.observe(f),NarrationActionKind::Play),"fingerprint still requires a ready visible caption");
    f.ui.dialogue->ready=true;
    f.voice_busy=true;
    auto d=c.observe(f);
    need(d.defer_dialogue&&!has(d,NarrationActionKind::Play),"fingerprint waits while native dialogue voice is busy");
    f.voice_busy=false;
    need(has(c.observe(f),NarrationActionKind::Play),"unique current caption can trigger when native source is unavailable");
}
void fingerprint_never_overrides_a_different_native_source_or_replays(){
    DescriptionCoordinator c;
    c.set_cues({},{{"scene",stable_event,"pose","A figure turns.","voice.wav",true,caption_fingerprint}});
    auto f=frame();
    f.caption_sha256=caption_fingerprint;
    f.ui.dialogue->source_event="table:/game/strings/scene.scene|Other";
    need(!has(c.observe(f),NarrationActionKind::Play),"a matching digest cannot override different native identity");
    f.ui.dialogue->source_event.clear();
    need(!has(c.observe(f),NarrationActionKind::Play),"late fallback cannot rearm a released occurrence");
    f.ui.dialogue->id="line-2";
    f.ui.dialogue->source_event=stable_event;
    f.caption_sha256=std::string(64,'b');
    need(has(c.observe(f),NarrationActionKind::Play),"native key does not rely on exact caption wording");
}
void reserved_table_cue_starts_when_caption_and_voice_clear_together(){
    DescriptionCoordinator c;
    c.set_cues({},{{"scene",stable_event,"pose","A figure turns.","voice.wav",true}});
    auto f=frame();
    f.ui.dialogue->source_event=stable_event;
    f.voice_busy=true;
    auto d=c.observe(f);
    need(d.defer_dialogue,"matched voiced caption reserves its description");
    need(d.trace.find("installed=pose matched=pose")!=d.trace.npos&&d.trace.find("Look!")==d.trace.npos,
         "trace identifies the cue and omits native caption text");
    f.ui.dialogue.reset();
    f.voice_busy=false;
    d=c.observe(f);
    need(d.caption_end_cue=="pose"&&d.actions.size()==1&&d.actions[0].kind==NarrationActionKind::Play&&
         d.actions[0].dialogue_id=="line-1","first quiet capture may start the reserved cue after the caption clears");
    need(d.trace.find("caption or gate lost cue=pose")!=d.trace.npos,"trace records the ready caption clearing");
    f.audio={1,"scene","pose",true,false,false,false,false,{}};
    f.ui.dialogue=DialogueSnapshot{"line-2","","Next line.",true,true};
    f.voice_busy=true;
    need(has(c.observe(f),NarrationActionKind::Pause),"native voice still pauses an active description");
    f.audio.paused=true;
    f.voice_busy=false;
    need(has(c.observe(f),NarrationActionKind::Resume),"active description can finish when the following voice clears");
}
void trace_reports_installed_keys_before_confirming_the_scene(){
    DescriptionCoordinator c;
    c.set_cues({},{{"scene",stable_event,"pose","A figure turns.","voice.wav",true}});
    auto f=frame();
    f.native_scene="other scene";
    f.ui.dialogue->source_event=stable_event;
    const auto d=c.observe(f);
    need(!has(d,NarrationActionKind::Play)&&d.trace.find("installed=pose matched=<none>")!=d.trace.npos&&
         d.trace.find("clock_scene=0")!=d.trace.npos&&d.trace.find("usable=0")!=d.trace.npos,
         "trace identifies an installed cue rejected by native clock mismatch");
    need(c.observe(f).trace.empty(),"unchanged caption is not traced every frame");
}
void caption_end_start_cannot_catch_up_after_disruption(){
    for(unsigned condition=0;condition<10;++condition){
        DescriptionCoordinator c;
        c.set_cues({},{{"scene",condition==8?"dialogue:Look!":stable_event,"pose","A figure turns.","voice.wav",condition!=7}});
        auto f=frame();
        f.ui.dialogue->source_event=stable_event;
        f.voice_busy=true;
        f.audio.generation=5;
        need(c.observe(f).defer_dialogue,"current cue is reserved before disruption");
        if(condition==3||condition==4||condition==5){
            if(condition==3)f.user_paused=true;
            if(condition==4)f.foreground=false;
            if(condition==5)f.ui.menu=MenuSnapshot{"choice","Yes","",0,2,true,true,true};
            c.observe(f);
            f.user_paused=false;
            f.foreground=true;
            f.ui.menu.reset();
        }
        f.voice_busy=condition==0;
        if(condition==1)f.ui.dialogue->ready=false;
        else if(condition==2)f.ui.dialogue=DialogueSnapshot{"line-2","","Next line.",true,true};
        else f.ui.dialogue.reset();
        if(condition==6)f.native_session="different run";
        if(condition==9)f.audio.generation=4;
        auto d=c.observe(f);
        need(!has(d,NarrationActionKind::Play),"unsafe or expired caption-end capture remains silent");
        if(condition==9)need(d.caption_end_cue=="pose","one-shot fallback exposes an attempted reservation");
        f.native_session="run";
        f.audio.generation=6;
        f.ui.dialogue.reset();
        f.voice_busy=false;
        d=c.observe(f);
        need(!has(d,NarrationActionKind::Play)&&d.caption_end_cue.empty(),"quiet later capture cannot revive the old caption");
    }
}
void native_gate_and_completion(){
    DescriptionCoordinator c;set(c);auto f=frame();f.voice_busy=true;
    auto d=c.observe(f);
    need(d.defer_dialogue&&!has(d,NarrationActionKind::Play),"current matched dialogue waits while native voice speaks");
    f.voice_busy=false;d=c.observe(f);
    need(d.defer_dialogue&&d.stop_speech&&has(d,NarrationActionKind::Play),"TalkWait permits narration once the actual voice is quiet");
    f.audio={1,"scene","pose",true,false,false,false,false,{}};
    f.ui.dialogue=DialogueSnapshot{"line-2","","Next line.",true,true};d=c.observe(f);
    need(d.defer_dialogue&&!has(d,NarrationActionKind::Play),"actual busy recording defers only latest current dialogue");
    f.audio.active=false;f.audio.completed=true;d=c.observe(f);
    need(!d.defer_dialogue,"driver completion releases the current dialogue");
}
void menus_and_unknowns(){
    DescriptionCoordinator c;set(c);auto f=frame();c.observe(f);
    f.audio={1,"scene","pose",true,false,false,false,false,{}};
    f.ui.menu=MenuSnapshot{"choice","Yes","",0,2,true,true,true};
    auto d=c.observe(f);need(!d.defer_dialogue&&has(d,NarrationActionKind::Stop),"choice cancels recording and reads immediately");
    DescriptionCoordinator unknown;set(unknown);f=frame();f.voice_known=false;d=unknown.observe(f);
    need(!d.defer_dialogue&&!has(d,NarrationActionKind::Play)&&!d.notice.empty(),"unknown gate reports unavailable without hiding dialogue");
    f.voice_known=true;d=unknown.observe(f);need(!d.defer_dialogue&&!has(d,NarrationActionKind::Play),"late gate discovery cannot catch up an already released line");
}
void occurrence_and_context(){
    DescriptionCoordinator c;set(c);auto f=frame();c.observe(f);
    f.audio={1,"scene","pose",false,false,true,false,false,{}};c.observe(f);
    f.ui.dialogue.reset();c.observe(f);f.ui.dialogue=DialogueSnapshot{"line-1","","Look!",true,true};
    need(!has(c.observe(f),NarrationActionKind::Play),"a visibility gap does not repeat an old native occurrence");
    f.ui.dialogue->id="line-3";need(has(c.observe(f),NarrationActionKind::Play),"a new native occurrence can repeat a visible line");
    f.audio={2,"scene","pose",true,false,false,false,false,{}};f.clocks.push_back(f.clocks[0]);
    auto d=c.observe(f);need(!d.defer_dialogue&&has(d,NarrationActionKind::Stop),"ambiguous active sources stop recorded output");
}
void pause_and_focus(){
    DescriptionCoordinator c;set(c);auto f=frame();c.observe(f);
    f.audio={1,"scene","pose",true,false,false,false,false,{}};
    f.user_paused=true;f.ui.menu=MenuSnapshot{"pause","Continue","",0,2,true,true,true};
    auto d=c.observe(f);need(!d.defer_dialogue&&has(d,NarrationActionKind::Pause)&&!has(d,NarrationActionKind::Stop),"real native pause pauses narration and preserves menu speech");
    f.user_paused=false;f.ui.menu.reset();f.audio.paused=true;d=c.observe(f);
    need(has(d,NarrationActionKind::Resume),"resume returns to the same native pose");
    f.foreground=false;f.audio.paused=false;d=c.observe(f);
    need(has(d,NarrationActionKind::Pause)&&!d.defer_dialogue&&d.notice.empty(),"background pauses audio without speech");
    f.foreground=true;f.audio.paused=true;d=c.observe(f);
    need(has(d,NarrationActionKind::Resume)&&!has(d,NarrationActionKind::Play),"focus restoration resumes rather than replaying");
}
NarrationAction failure(){
    NarrationAction a{NarrationActionKind::Play,"scene","pose","A figure turns.","voice.wav",0};
    a.session="run";a.dialogue_id="line-1";a.generation=1;return a;
}
void old_completion_cannot_release_new_pending_occurrence(){
    for(bool error:{false,true}){
        DescriptionCoordinator c;set(c);auto f=frame();c.observe(f);
        f.audio={1,"scene","pose",false,false,!error,error,false,{}};c.observe(f);
        f.ui.dialogue->id="line-2";f.voice_busy=true;
        auto d=c.observe(f);need(d.defer_dialogue&&!has(d,NarrationActionKind::Play),"new repeated cue waits behind native voice");
        d=c.observe(f);need(d.defer_dialogue&&!has(d,NarrationActionKind::Play),"old completed/error generation cannot release a pending repeated cue");
        f.voice_busy=false;d=c.observe(f);need(d.defer_dialogue&&has(d,NarrationActionKind::Play),"new occurrence begins after voice ends");
        need(c.observe(f).defer_dialogue,"pre-Play snapshot cannot acknowledge the new Play");
        f.audio={2,"scene","pose",true,false,false,false,true,{}};
        need(c.observe(f).defer_dialogue,"queued new generation is already busy");
        f.audio.active=false;f.audio.queued=false;f.audio.completed=true;
        need(!c.observe(f).defer_dialogue,"only this occurrence's actual completion releases it");
    }
}
void pending_gate_loss_expires_and_falls_back_once(){
    for(bool lose_voice:{false,true}){
        DescriptionCoordinator c;set(c);auto f=frame();f.voice_busy=true;c.observe(f);
        if(lose_voice)f.voice_known=false;else f.pause_known=false;
        auto d=c.observe(f);
        need(!d.defer_dialogue&&!has(d,NarrationActionKind::Play),"unknown pending gate releases the native caption");
        need(d.notice.find("A figure turns.")!=std::string::npos,"pending gate loss supplies current visual fallback");
        need(c.observe(f).notice.empty(),"pending gate loss fallback occurs once");
        f.voice_known=true;f.pause_known=true;f.voice_busy=false;d=c.observe(f);
        need(!d.defer_dialogue&&!has(d,NarrationActionKind::Play)&&!d.stop_speech,"gate recovery never steals the already released caption");
    }
}
void active_gate_loss_stops_without_late_resume(){
    DescriptionCoordinator c;set(c);auto f=frame();c.observe(f);
    f.audio={1,"scene","pose",true,false,false,false,false,{}};c.observe(f);
    f.voice_known=false;auto d=c.observe(f);
    need(has(d,NarrationActionKind::Stop)&&!d.defer_dialogue,"lost active native gate stops audio and releases caption");
    need(d.notice.find("A figure turns.")!=std::string::npos,"partial recording has current visual fallback");
    f.audio={2,"scene","pose",false,false,false,false,false,{}};f.voice_known=true;d=c.observe(f);
    need(!has(d,NarrationActionKind::Play)&&!has(d,NarrationActionKind::Resume)&&d.notice.empty(),"known gate cannot revive expired pose");
}
void native_identity_mismatch_never_describes_the_old_scene(){
    DescriptionCoordinator c;set(c);auto f=frame();f.native_scene="other-scene";f.native_session="other-run";
    auto d=c.observe(f);
    need(!has(d,NarrationActionKind::Play)&&!d.defer_dialogue,"explicit different native scene releases caption");
    need(d.notice.find("A figure turns.")==std::string::npos,"explicit native mismatch cannot describe the stale clock scene");
    f.native_scene.clear();f.native_session.clear();f.native_event_known=false;d=c.observe(f);
    need(d.notice.find("A figure turns.")==std::string::npos,"unknown native identity cannot invent a mapped scene");
}
void transient_capture_gaps_retain_same_run_deduplication(){
    for(int gap=0;gap<3;++gap){
        DescriptionCoordinator c;set(c);auto f=frame();c.observe(f);
        f.audio={1,"scene","pose",false,false,true,false,false,{}};c.observe(f);
        const auto clocks=f.clocks;
        if(gap==0)f.clocks.clear();else if(gap==1)f.clocks.push_back(f.clocks[0]);else f.output_ready=false;
        need(!c.observe(f).defer_dialogue,"uncertain source never hides current caption");
        need(c.has_context(),"transient capture loss retains the known native run");
        f.clocks=clocks;f.output_ready=true;auto d=c.observe(f);
        need(!has(d,NarrationActionKind::Play)&&!d.defer_dialogue,"same native occurrence cannot replay after recovery");
        f.ui.dialogue->id="line-2";need(has(c.observe(f),NarrationActionKind::Play),"new occurrence remains eligible after recovery");
    }
}
void capture_gap_stops_and_discards_current_pending_pose(){
    for(bool pending:{false,true}){
        DescriptionCoordinator c;set(c);auto f=frame();f.voice_busy=pending;c.observe(f);
        if(!pending)f.audio={1,"scene","pose",true,false,false,false,false,{}};
        const auto clocks=f.clocks;f.clocks.clear();auto d=c.observe(f);
        need(!d.defer_dialogue,"source gap releases current caption");
        if(!pending)need(has(d,NarrationActionKind::Stop),"source gap stops current audio");
        f.clocks=clocks;f.audio={2,"scene","pose",false,false,false,false,false,{}};f.voice_busy=false;d=c.observe(f);
        need(!has(d,NarrationActionKind::Play)&&!has(d,NarrationActionKind::Resume),"source gap expires pending and playing pose permanently");
    }
}
void captions_seen_while_unavailable_cannot_catch_up(){
    for(bool established:{false,true}){
        DescriptionCoordinator c;set(c);auto f=frame();
        if(established){c.observe(f);f.audio={1,"scene","pose",false,false,true,false,false,{}};c.observe(f);f.ui.dialogue->id="line-2";}
        const auto clocks=f.clocks;f.clocks.clear();c.observe(f);f.clocks=clocks;
        auto d=c.observe(f);need(!has(d,NarrationActionKind::Play)&&!d.defer_dialogue,"restored clocks cannot catch up a caption released during gap");
    }
    DescriptionCoordinator c;set(c);auto f=frame();f.output_ready=false;c.observe(f);f.output_ready=true;
    need(!has(c.observe(f),NarrationActionKind::Play),"output reconnection cannot replay an already released pose");
}
void clocked_consumption_survives_transient_source_loss(){
    for(int gap=0;gap<3;++gap){
        DescriptionCoordinator c;c.set_cues({{"scene","first","First view.","first.wav",1,1.5},{"scene","next","Next view.","next.wav",2,3}},{});
        auto f=frame();f.ui.dialogue.reset();f.clocks[0].playing=true;f.clocks[0].paused=false;
        need(has(c.observe(f),NarrationActionKind::Play),"clocked first cue starts at native time");
        const auto clocks=f.clocks;
        if(gap==0)f.clocks.clear();else if(gap==1)f.clocks.push_back(f.clocks[0]);else f.output_ready=false;
        need(has(c.observe(f),NarrationActionKind::Stop),"clocked source suspension stops current output");
        f.clocks=clocks;f.output_ready=true;f.clocks[0].seconds=1.05;f.clocks[0].sample_seconds=1.05;
        need(!has(c.observe(f),NarrationActionKind::Play),"clocked consumed cue cannot replay after recovery");
        f.clocks[0].seconds=1.9;f.clocks[0].sample_seconds=1.9;c.observe(f);
        f.clocks[0].seconds=2.02;f.clocks[0].sample_seconds=2.02;
        need(has(c.observe(f),NarrationActionKind::Play),"later natural clock crossing still starts its own cue");
    }
}
void confirmed_context_changes_reset_the_contract(){
    DescriptionCoordinator c;set(c);auto f=frame();c.observe(f);need(c.has_context(),"selected native run establishes context");
    f.audio={1,"scene","pose",false,false,true,false,false,{}};c.observe(f);
    f.native_session="run-2";f.clocks[0].session="run-2";
    need(has(c.observe(f),NarrationActionKind::Play),"new confirmed native run may describe its own repeated caption");
    f.audio={2,"scene","pose",false,false,true,false,false,{}};c.observe(f);f.ui.context="new-map|talk";
    need(has(c.observe(f),NarrationActionKind::Play),"positive map transition creates a new narration context");
    f.native_event_active=false;auto d=c.observe(f);
    need(!c.has_context()&&!d.defer_dialogue&&has(d,NarrationActionKind::Stop),"confirmed native event end fully stops and clears context");
}
void background_pending_occurrence_never_starts_late(){
    DescriptionCoordinator c;set(c);auto f=frame();f.voice_busy=true;c.observe(f);
    f.foreground=false;auto d=c.observe(f);need(!d.defer_dialogue&&d.notice.empty(),"background pending cue stays silent");
    f.foreground=true;f.voice_busy=false;d=c.observe(f);
    need(!has(d,NarrationActionKind::Play)&&!d.defer_dialogue,"discarded background pending cue cannot steal caption on return");
}
void a_pause_menu_cannot_rearm_a_released_pending_caption(){
    for(bool was_pending:{false,true}){
        DescriptionCoordinator c;set(c);auto f=frame();
        if(was_pending){f.voice_busy=true;c.observe(f);}
        f.user_paused=true;f.ui.menu=MenuSnapshot{"pause","Continue","",0,2,true,true,true};
        need(!c.observe(f).defer_dialogue,"pause menu keeps its current native caption immediate");
        f.user_paused=false;f.voice_busy=false;f.ui.menu.reset();const auto d=c.observe(f);
        need(!d.defer_dialogue&&!has(d,NarrationActionKind::Play),"a caption released through a pause menu cannot start a late pending cue");
    }
}
void failure_waits_for_the_captured_error_and_caption_release(){
    DescriptionCoordinator c;set(c);auto f=frame();ReaderCore core;auto d=c.observe(f);const auto a=failure();
    auto spoken=f.ui;if(d.defer_dialogue)spoken.dialogue.reset();
    need(core.observe(spoken).empty(),"recorded Play initially defers automatic native caption");
    need(classify_description_failure(a,f,d.defer_dialogue)==DescriptionFailureDisposition::Wait,"fast callback cannot flush against pre-error observer capture");
    f.audio={1,"scene","pose",false,false,false,true,false,"device failed"};d=c.observe(f);
    spoken=f.ui;if(d.defer_dialogue)spoken.dialogue.reset();const auto caption=core.observe(spoken);
    need(!d.defer_dialogue&&caption.size()==1&&caption[0].text=="Person: Look!","captured error releases native caption before fallback");
    need(classify_description_failure(a,f,d.defer_dialogue)==DescriptionFailureDisposition::Speak,"matched error may speak after capture queued native caption");
    need(classify_description_failure(a,f,true)==DescriptionFailureDisposition::Wait,"caption deferral postpones error fallback");
}
void failure_generation_and_error_identity_are_required(){
    auto f=frame();auto a=failure();f.audio={1,"scene","pose",false,false,false,true,false,"failed"};
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Speak,"current captured matching error is speakable");
    f.audio.generation=0;need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Wait,"older capture waits for worker generation");
    f.audio.generation=2;need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"newer Stop or Play discards staged old failure");
    f.audio.generation=1;f.audio.cue_id="other";need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"same generation different cue is stale");
    f.audio.cue_id="pose";f.audio.scene="other";need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"same generation different audio scene is stale");
    f.audio.scene="scene";f.audio.error=false;f.audio.active=true;f.audio.queued=true;
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Wait,"queued same generation must wait for captured error");
}
void failure_never_crosses_a_native_run(){
    auto f=frame();const auto a=failure();f.audio={1,"scene","pose",false,false,false,true,false,"failed"};
    f.native_session="run-2";f.clocks[0].session="run-2";
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"same scene asset new native run rejects old failure");
    f.clocks[0].session="run";
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"authoritative different run rejects surviving old clock");
    f.native_session="run";f.clocks[0].playing=false;f.clocks[0].paused=false;
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"confirmed stopped clock discards failure");
    f.clocks.clear();f.native_event_known=false;
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Wait,"missing clock without confirmed end is uncertainty");
}
void failure_requires_the_current_native_caption(){
    auto f=frame();const auto a=failure();f.audio={1,"scene","pose",false,false,false,true,false,"failed"};
    f.ui.dialogue->id="line-2";
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"new caption occurrence rejects old fallback");
    f.ui.dialogue->id="line-1";f.ui.dialogue->ready=false;
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Wait,"same caption still revealing postpones fallback");
    f.ui.dialogue.reset();need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Wait,"absent caption cannot authorize fallback");
    auto generic=a;generic.dialogue_id.clear();
    need(classify_description_failure(generic,f,false)==DescriptionFailureDisposition::Speak,"clocked failure without dialogue context may use current run");
}
void failure_foreground_menu_and_end_guards(){
    auto f=frame();const auto a=failure();f.audio={1,"scene","pose",false,false,false,true,false,"failed"};
    f.foreground=false;need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"background failure never becomes speech later");
    f.foreground=true;f.output_ready=false;need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"unavailable output discards stale fallback");
    f.output_ready=true;f.ui.menu=MenuSnapshot{"choice","Yes","",0,2,true,true,true};
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Wait,"menu stays immediate while fallback waits");
    f.ui.menu.reset();f.native_event_active=false;
    need(classify_description_failure(a,f,false)==DescriptionFailureDisposition::Discard,"confirmed event end discards pending fallback");
}
void mixed_timing_tables_are_rejected(){
    DescriptionCoordinator c;bool rejected=false;
    try{c.set_cues({{"scene","clock","View.","clock.wav",0,1}},{{"scene","dialogue:Look!","pose","View.","pose.wav",true}});}catch(const std::invalid_argument&){rejected=true;}
    need(rejected,"same scene cannot have both elapsed and paced timing ownership");
}
}
int main(int argc,char** argv){
    struct Test{const char* name;void(*run)();};
    const Test tests[]={
        {"stable_event_waits_for_completed_caption_and_native_voice",stable_event_waits_for_completed_caption_and_native_voice},
        {"stable_event_requires_exact_identity_at_a_fresh_occurrence",stable_event_requires_exact_identity_at_a_fresh_occurrence},
        {"stable_event_precedes_legacy_text_match",stable_event_precedes_legacy_text_match},
        {"unique_caption_fallback_keeps_scene_readiness_and_voice_gates",unique_caption_fallback_keeps_scene_readiness_and_voice_gates},
        {"fingerprint_never_overrides_a_different_native_source_or_replays",fingerprint_never_overrides_a_different_native_source_or_replays},
        {"reserved_table_cue_starts_when_caption_and_voice_clear_together",reserved_table_cue_starts_when_caption_and_voice_clear_together},
        {"trace_reports_installed_keys_before_confirming_the_scene",trace_reports_installed_keys_before_confirming_the_scene},
        {"caption_end_start_cannot_catch_up_after_disruption",caption_end_start_cannot_catch_up_after_disruption},
        {"native_gate_and_completion",native_gate_and_completion},
        {"menus_and_unknowns",menus_and_unknowns},
        {"occurrence_and_context",occurrence_and_context},
        {"pause_and_focus",pause_and_focus},
        {"old_completion_cannot_release_new_pending_occurrence",old_completion_cannot_release_new_pending_occurrence},
        {"pending_gate_loss_expires_and_falls_back_once",pending_gate_loss_expires_and_falls_back_once},
        {"active_gate_loss_stops_without_late_resume",active_gate_loss_stops_without_late_resume},
        {"native_identity_mismatch_never_describes_the_old_scene",native_identity_mismatch_never_describes_the_old_scene},
        {"transient_capture_gaps_retain_same_run_deduplication",transient_capture_gaps_retain_same_run_deduplication},
        {"capture_gap_stops_and_discards_current_pending_pose",capture_gap_stops_and_discards_current_pending_pose},
        {"captions_seen_while_unavailable_cannot_catch_up",captions_seen_while_unavailable_cannot_catch_up},
        {"clocked_consumption_survives_transient_source_loss",clocked_consumption_survives_transient_source_loss},
        {"confirmed_context_changes_reset_the_contract",confirmed_context_changes_reset_the_contract},
        {"background_pending_occurrence_never_starts_late",background_pending_occurrence_never_starts_late},
        {"a_pause_menu_cannot_rearm_a_released_pending_caption",a_pause_menu_cannot_rearm_a_released_pending_caption},
        {"failure_waits_for_the_captured_error_and_caption_release",failure_waits_for_the_captured_error_and_caption_release},
        {"failure_generation_and_error_identity_are_required",failure_generation_and_error_identity_are_required},
        {"failure_never_crosses_a_native_run",failure_never_crosses_a_native_run},
        {"failure_requires_the_current_native_caption",failure_requires_the_current_native_caption},
        {"failure_foreground_menu_and_end_guards",failure_foreground_menu_and_end_guards},
        {"mixed_timing_tables_are_rejected",mixed_timing_tables_are_rejected}
    };
    unsigned failed=0,ran=0;
    for(const auto& test:tests){
        if(argc>1&&std::string_view(argv[1])!=test.name)continue;
        ++ran;try{test.run();std::cout<<"PASS "<<test.name<<'\n';}
        catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<test.name<<": "<<e.what()<<'\n';}
    }
    std::cout<<ran<<" description tests, "<<failed<<" failures\n";
    return !ran||failed?1:0;
}
