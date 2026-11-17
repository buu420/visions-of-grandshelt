#include "resonance/Descriptions.hpp"
#include "resonance/Localization.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>
namespace resonance {
namespace {
void append(std::vector<NarrationAction>& to,std::vector<NarrationAction> from) {
    to.insert(to.end(),std::make_move_iterator(from.begin()),std::make_move_iterator(from.end()));
}
bool ready_dialogue(const DescriptionFrame& f) {
    return f.ui.dialogue&&f.ui.dialogue->visible&&f.ui.dialogue->ready&&!f.ui.dialogue->text.empty();
}
bool positive_native_context(const DescriptionFrame& f) {
    return f.native_event_known&&f.native_event_active&&!f.native_scene.empty()&&!f.native_session.empty();
}
bool visible_menu(const DescriptionFrame& f) { return f.ui.menu&&f.ui.menu->visible; }
}

DescriptionFailureDisposition classify_description_failure(const NarrationAction& action,
                                                           const DescriptionFrame& f,
                                                           bool dialogue_deferred) {
    using Disposition=DescriptionFailureDisposition;
    if(!f.foreground||!f.output_ready||(f.native_event_known&&!f.native_event_active)||
       action.scene.empty()||action.session.empty()||action.cue_id.empty()||!action.generation)
        return Disposition::Discard;
    if(positive_native_context(f)&&(f.native_scene!=action.scene||f.native_session!=action.session))
        return Disposition::Discard;
    if(f.audio.generation>action.generation)return Disposition::Discard;
    if(!action.dialogue_id.empty()&&f.ui.dialogue&&!f.ui.dialogue->id.empty()&&
       f.ui.dialogue->id!=action.dialogue_id)return Disposition::Discard;

    bool current=false,stopped=false,other_run=false;
    for(const auto& clock:f.clocks)if(clock.valid) {
        const bool same=clock.scene==action.scene&&clock.session==action.session;
        if(clock.playing||clock.paused) {current|=same;other_run|=!same;}
        else stopped|=same;
    }
    if(!current)return stopped||other_run?Disposition::Discard:Disposition::Wait;
    if(f.audio.generation<action.generation)return Disposition::Wait;
    if(f.audio.scene!=action.scene||f.audio.cue_id!=action.cue_id)return Disposition::Discard;
    // A callback can arrive after the frozen capture sampled a queued/running
    // generation. Only the next captured error acknowledges that failure.
    if(!f.audio.error)return f.audio.active||f.audio.queued?Disposition::Wait:Disposition::Discard;
    if(f.audio.active||f.audio.queued)return Disposition::Wait;
    if(!action.dialogue_id.empty()&&!ready_dialogue(f))return Disposition::Wait;
    if(dialogue_deferred||visible_menu(f))return Disposition::Wait;
    return Disposition::Speak;
}

bool DescriptionCoordinator::has_scene(std::string_view scene)const {
    return clocked_.has_scene(scene)||paced_.has_scene(scene);
}
bool DescriptionCoordinator::has_caption_fallback(std::string_view scene)const {
    return std::any_of(paced_.cues().begin(),paced_.cues().end(),[&](const auto& cue){
        return cue.scene==scene&&!cue.caption_sha256.empty();
    });
}
bool DescriptionCoordinator::has_context()const { return !session_.empty(); }
std::vector<NarrationAction> DescriptionCoordinator::reset(){
    auto out=clocked_.reset();append(out,paced_.reset());
    map_.clear();scene_.clear();session_.clear();last_dialogue_.clear();reserved_.reset();seen_.clear();
    traced_cue_.clear();
    occurrence_=0;unavailable_reported_=false;return out;
}
std::vector<NarrationAction> DescriptionCoordinator::set_cues(std::vector<NarrationCue> timed,std::vector<PacedNarrationCue> paced){
    NarrationScheduler new_clocked(std::move(timed));PacedNarrationScheduler new_paced(std::move(paced));
    // A scene has one timing authority. Mixing elapsed-time and TalkWait cues
    // would give both schedulers ownership of the same output.
    for(const auto& cue:new_paced.cues())if(new_clocked.has_scene(cue.scene))
        throw std::invalid_argument("Description scene mixes clocked and paced cues");
    auto out=reset();clocked_=std::move(new_clocked);paced_=std::move(new_paced);return out;
}
std::vector<NarrationAction> DescriptionCoordinator::suspend(const DescriptionFrame& f) {
    // Invalid clock stops output without resetting consumption. An inactive
    // paced snapshot expires pending/playing poses, retaining the event boundary.
    auto out=clocked_.observe({});
    PacedNarrationFrame paced;
    paced.map=map_;paced.session=session_;paced.scene=scene_;paced.audio=f.audio;
    paced.foreground=f.foreground;
    append(out,paced_.observe(paced));reserved_.reset();return out;
}
void DescriptionCoordinator::reconcile_reservation(const NarrationPlaybackStatus& audio) {
    if(!reserved_||!reserved_->started)return;
    auto& reservation=*reserved_;
    if(!reservation.generation&&audio.generation>reservation.after_generation) {
        if(audio.scene==scene_&&audio.cue_id==reservation.cue_id)reservation.generation=audio.generation;
        else {reserved_.reset();return;}
    }
    if(reservation.generation&&audio.generation>=*reservation.generation&&
       (audio.generation>*reservation.generation||audio.scene!=scene_||
        audio.cue_id!=reservation.cue_id||!audio.active))reserved_.reset();
}
DescriptionDecision DescriptionCoordinator::observe(const DescriptionFrame& f){
    DescriptionDecision result;
    const bool held_before=reserved_.has_value();
    const auto trace_loss=[&](std::string_view why){
        if(traced_cue_.empty())return;
        result.trace=std::string(why)+" cue="+traced_cue_;
        traced_cue_.clear();
    };
    if(f.native_event_known&&!f.native_event_active){
        trace_loss("event ended");
        if(!result.trace.empty())result.trace+=" held_before="+std::to_string(held_before);
        result.actions=reset();return result;
    }
    const auto bar=f.ui.context.find('|');
    const auto map=f.ui.context.substr(0,bar);
    const bool native_known=positive_native_context(f);
    if(has_context()&&((native_known&&(f.native_scene!=scene_||f.native_session!=session_))||
       (!map.empty()&&!map_.empty()&&map!=map_))){
        trace_loss("context changed");
        result.actions=reset();
    }

    const NarrationClock* selected=nullptr;unsigned candidates=0;
    for(const auto& clock:f.clocks)if(clock.valid&&(clock.playing||clock.paused)&&has_scene(clock.scene)) {
        selected=&clock;++candidates;
    }
    const bool usable=f.output_ready&&candidates==1&&
        (!native_known||(selected->scene==f.native_scene&&selected->session==f.native_session))&&
        (!has_context()||(selected->scene==scene_&&selected->session==session_));
    if(usable&&!has_context()) {
        scene_=selected->scene;session_=selected->session;map_=map.empty()?scene_:map;
    }
    const bool native_identity=native_known&&f.native_scene==scene_&&f.native_session==session_;
    const bool gates_known=native_identity&&f.pause_known&&f.voice_known;
    const bool menu=visible_menu(f);
    const bool modal=menu&&!(native_identity&&f.pause_known&&f.user_paused);
    const bool ready=ready_dialogue(f);
    const std::string key=ready?f.ui.dialogue->id+"\n"+f.ui.dialogue->text:std::string{};
    const bool changed=key!=last_dialogue_;
    last_dialogue_=key;
    // Captions released during uncertainty are also consumed. Recovery is not
    // a new native occurrence, including the first late clock capture.
    const bool fresh=changed&&ready&&seen_.size()<4096&&seen_.insert(key).second;
    reconcile_reservation(f.audio);
    const bool awaiting_current=reserved_&&reserved_->dialogue_key==key;
    const auto& cues=paced_.cues();
    auto cue=cues.end();
    if(ready) {
        const auto match=[&](std::string_view event){
            return std::find_if(cues.begin(),cues.end(),[&](const auto& c){return c.scene==scene_&&c.event==event;});
        };
        const auto& source=f.ui.dialogue->source_event;
        if(source.starts_with("table:"))cue=match(source);
        // A verified native identity has priority. Only an absent identity
        // can use the fingerprint of the current, already matched caption.
        if(source.empty()&&!f.caption_sha256.empty())
            cue=std::find_if(cues.begin(),cues.end(),[&](const auto& c){return c.scene==scene_&&c.caption_sha256==f.caption_sha256;});
        if(cue==cues.end())cue=match("dialogue:"+f.ui.dialogue->text);
    }
    if(changed||(held_before&&(!usable||!gates_known||!f.foreground||menu||f.user_paused)))
        trace_loss("caption or gate lost");
    if(changed&&ready&&(f.ui.dialogue->source_event.starts_with("table:")||!f.caption_sha256.empty())&&
       (paced_.has_scene(scene_)||paced_.has_scene(f.native_scene)||
        std::any_of(f.clocks.begin(),f.clocks.end(),[&](const auto& clock){return paced_.has_scene(clock.scene);}))) {
        const auto& source=f.ui.dialogue->source_event;
        const auto installed=std::find_if(cues.begin(),cues.end(),[&](const auto& item){
            if(!item.event.starts_with("table:"))return false;
            if(!source.empty())return item.event==source;
            return !f.caption_sha256.empty()&&item.scene==f.native_scene&&item.caption_sha256==f.caption_sha256;
        });
        if(installed!=cues.end()||(fresh&&source.starts_with("table:"))) {
            if(!result.trace.empty())result.trace+=" | ";
            result.trace+="ready supplied="+(source.empty()?"<fingerprint>":source)+
                " installed="+(installed==cues.end()?"<none>":installed->id)+
                " matched="+(cue==cues.end()?"<none>":cue->id);
            if(installed!=cues.end())traced_cue_=installed->id;
        }
    }
    const auto trace_state=[&]{
        if(result.trace.empty())return;
        result.trace+=" native_scene="+f.native_scene+" native_run="+f.native_session+
            " confirmed_scene="+scene_+" confirmed_run="+session_+" clocks="+std::to_string(candidates);
        const auto flag=[&](std::string_view name,bool value){
            result.trace+=' ';result.trace+=name;result.trace+=value?"=1":"=0";
        };
        flag("clock_scene",selected&&selected->scene==f.native_scene);
        flag("clock_run",selected&&selected->session==f.native_session);
        flag("usable",usable);flag("identity",native_identity);flag("known",gates_known);
        flag("output",f.output_ready);flag("foreground",f.foreground);flag("menu",menu);
        flag("ready",ready);flag("fresh",fresh);flag("held_before",held_before);
        flag("held",reserved_.has_value());flag("started",reserved_&&reserved_->started);
        flag("caption_end_allowed",reserved_&&reserved_->may_start_at_caption_end);
        flag("audio",f.audio.active);flag("voice_known",f.voice_known);flag("voice",f.voice_busy);
        flag("pause_known",f.pause_known);flag("paused",f.user_paused);
        for(const auto& clock:f.clocks) {
            result.trace+=" [clock="+clock.scene+" run="+clock.session+
                " seconds="+std::to_string(clock.seconds)+" sample="+std::to_string(clock.sample_seconds);
            flag("valid",clock.valid);flag("playing",clock.playing);flag("paused",clock.paused);
            result.trace+=']';
        }
    };
    const auto unavailable=[&]{
        if(native_identity&&f.foreground&&!menu&&ready&&(fresh||awaiting_current)&&cue!=paced_.cues().end()) {
            result.notice=trf(unavailable_reported_||f.screen_reader?"Description: {0}":"Recorded description unavailable. Description: {0}",
                {cue->text});
            unavailable_reported_=true;
        }
    };
    if(!usable) {
        unavailable();append(result.actions,suspend(f));trace_state();return result;
    }
    if(clocked_.has_scene(scene_)) {
        append(result.actions,paced_.reset());reserved_.reset();
        auto current=*selected;current.foreground=f.foreground;
        if(modal)current.valid=false;
        else if(native_identity&&((f.pause_known&&f.user_paused)||(f.voice_known&&f.voice_busy))) {
            current.paused=true;current.playing=false;
        }
        append(result.actions,clocked_.observe(current));
        result.stop_speech=std::any_of(result.actions.begin(),result.actions.end(),[](const auto& a){return a.kind==NarrationActionKind::Play;});
        trace_state();
        return result;
    }

    append(result.actions,clocked_.reset());
    PacedNarrationFrame paced;
    paced.map=map_;paced.session=session_;paced.scene=scene_;paced.active=gates_known;paced.audio=f.audio;
    paced.foreground=f.foreground;
    // A pause menu preserves audio that already started. An unstarted pose must
    // expire because menu capture releases its native caption immediately.
    paced.modal=modal||(menu&&!f.audio.active)||(changed&&!f.foreground&&f.audio.active);
    paced.paused=f.user_paused||f.voice_busy;
    if(reserved_&&(!gates_known||!f.foreground||menu||f.user_paused))
        reserved_->may_start_at_caption_end=false;
    // Some voiced sequences clear the finished caption in the same update that
    // the voice falls quiet. Give its verified pending cue only that capture.
    const bool caption_ended=changed&&!f.ui.dialogue&&reserved_&&!reserved_->started&&
        reserved_->may_start_at_caption_end&&!f.voice_busy&&!f.audio.active;
    if(caption_ended)result.caption_end_cue=reserved_->cue_id;
    paced.may_start=gates_known&&(ready||caption_ended)&&!f.voice_busy&&!f.user_paused;
    paced.discard_unstarted=caption_ended;
    if(changed&&!caption_ended) {
        reserved_.reset();
        const bool eligible=fresh&&f.foreground&&gates_known&&!menu&&cue!=paced_.cues().end();
        paced.event=PacedNarrationEvent{eligible?cue->event:"advance",++occurrence_};
        if(eligible){
            const bool may_finish=cue->event.starts_with("table:")&&cue->finish_on_advance&&
                !f.user_paused&&!f.ui.dialogue->id.empty();
            reserved_=Reservation{cue->id,key,f.ui.dialogue->id,f.audio.generation,{},false,may_finish};
            result.stop_speech=true;
        }
    }
    if(!gates_known){unavailable();reserved_.reset();}
    if(paced.modal||(!f.foreground&&!f.audio.active))reserved_.reset();
    append(result.actions,paced_.observe(paced));
    bool starting=false,stopping=false;
    for(auto& action:result.actions){
        starting|=action.kind==NarrationActionKind::Play;stopping|=action.kind==NarrationActionKind::Stop;
        if(action.kind==NarrationActionKind::Play&&reserved_&&action.cue_id==reserved_->cue_id) {
            reserved_->started=true;reserved_->after_generation=f.audio.generation;reserved_->generation.reset();
            if(caption_ended)action.dialogue_id=reserved_->dialogue_id;
        }
    }
    if(caption_ended&&reserved_&&!reserved_->started)reserved_.reset();
    result.stop_speech|=starting;
    const bool owned_audio=f.audio.active&&f.audio.scene==scene_&&
        std::any_of(paced_.cues().begin(),paced_.cues().end(),[&](const auto& c){return c.scene==scene_&&c.id==f.audio.cue_id;});
    result.defer_dialogue=f.foreground&&!menu&&gates_known&&(reserved_||starting||(owned_audio&&!stopping));
    trace_state();
    return result;
}
}
