#include "resonance/PacedNarration.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>
namespace resonance {
namespace {
bool valid_text(std::string_view text,bool key) {
    bool nonblank=false;
    for(std::size_t i=0;i<text.size();) {
        auto c=static_cast<unsigned char>(text[i++]);
        if(c<0x80) {
            if(c<32||c==127){if(key||(c!='\t'&&c!='\n'&&c!='\r'))return false;}
            if(c>32)nonblank=true;
            continue;
        }
        nonblank=true;unsigned extra{},value{},minimum{};
        if(c>=0xc2&&c<=0xdf){extra=1;value=c&31;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){extra=2;value=c&15;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){extra=3;value=c&7;minimum=0x10000;}
        else return false;
        if(text.size()-i<extra)return false;
        while(extra--){c=static_cast<unsigned char>(text[i++]);if((c&0xc0)!=0x80)return false;value=(value<<6)|(c&63);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return nonblank;
}
bool valid_caption_sha256(std::string_view fingerprint) {
    return fingerprint.size()==64&&std::all_of(fingerprint.begin(),fingerprint.end(),[](char ch){
        return (ch>='0'&&ch<='9')||(ch>='a'&&ch<='f');
    });
}
}
PacedNarrationScheduler::PacedNarrationScheduler(std::vector<PacedNarrationCue> cues){set_cues(std::move(cues));}
std::vector<NarrationAction> PacedNarrationScheduler::set_cues(std::vector<PacedNarrationCue> cues){
    if(cues.size()>5000)throw std::invalid_argument("Paced narration cue count exceeds limit");
    std::set<std::pair<std::string,std::string>> events,identities,captions;
    for(const auto& cue:cues) {
        if(!valid_text(cue.scene,true)||!valid_text(cue.event,true)||!valid_text(cue.id,true)||
           !valid_text(cue.audio,true)||!valid_text(cue.text,false))
            throw std::invalid_argument("Invalid paced narration fields");
        // Reuse the ordinary scheduler's exact relative audio-path policy. Its
        // synthetic interval is validation only and never drives paced output.
        NarrationScheduler validate({{cue.scene,cue.id,cue.text,cue.audio,0,1}});
        if(!events.emplace(cue.scene,cue.event).second||!identities.emplace(cue.scene,cue.id).second)
            throw std::invalid_argument("Repeated paced scene/event or scene/cue identity");
        if(!cue.caption_sha256.empty()&&(!cue.event.starts_with("table:")||
           !valid_caption_sha256(cue.caption_sha256)||!captions.emplace(cue.scene,cue.caption_sha256).second))
            throw std::invalid_argument("Invalid or repeated paced scene/caption fingerprint");
    }
    std::sort(cues.begin(),cues.end(),[](const auto& a,const auto& b){
        return a.scene==b.scene?a.event<b.event:a.scene<b.scene;
    });
    auto out=reset();cues_=std::move(cues);return out;
}
NarrationAction PacedNarrationScheduler::action(NarrationActionKind kind,std::size_t cue)const{
    const auto& c=cues_[cue];return {kind,c.scene,c.id,c.text,c.audio,0};
}
void PacedNarrationScheduler::expire(std::vector<NarrationAction>& out){
    if(playing_)out.push_back(action(NarrationActionKind::Stop,playing_->cue));
    playing_.reset();pending_.reset();
}
std::vector<NarrationAction> PacedNarrationScheduler::reset(){
    std::vector<NarrationAction> out;expire(out);
    map_.clear();session_.clear();scene_.clear();occurrence_=0;audio_generation_=0;terminated_=false;
    return out;
}
bool PacedNarrationScheduler::has_scene(std::string_view scene)const{
    return std::any_of(cues_.begin(),cues_.end(),[&](const auto& cue){return cue.scene==scene;});
}
std::vector<NarrationAction> PacedNarrationScheduler::observe(const PacedNarrationFrame& f){
    std::vector<NarrationAction> out;
    const bool stale_audio=f.audio.generation<audio_generation_;
    if(!stale_audio)audio_generation_=f.audio.generation;
    // Reconcile completion before changing scene: never Stop a newer recording
    // owned by another mode, and never accept completion from an older generation.
    if(playing_&&!stale_audio) {
        auto& current=*playing_;const auto& cue=cues_[current.cue];
        if(!current.generation&&f.audio.generation>current.after_generation) {
            if(f.audio.scene==cue.scene&&f.audio.cue_id==cue.id)current.generation=f.audio.generation;
            else {playing_.reset();pending_.reset();}
        }
        if(playing_&&playing_->generation) {
            const auto generation=*playing_->generation;
            if(f.audio.generation>generation||
               (f.audio.generation==generation&&(f.audio.scene!=cue.scene||f.audio.cue_id!=cue.id))) {
                playing_.reset();pending_.reset();
            } else if(f.audio.generation==generation&&!f.audio.active) {
                const bool cancelled=!f.audio.completed&&!f.audio.error;
                playing_.reset();if(cancelled)pending_.reset();
            }
        }
    }
    bool stopped_own=false;
    if(f.map!=map_||f.session!=session_||f.scene!=scene_) {
        stopped_own=playing_.has_value();expire(out);
        map_=f.map;session_=f.session;scene_=f.scene;occurrence_=0;terminated_=false;
    }
    const bool new_event=f.event&&f.event->occurrence>0&&f.event->occurrence>occurrence_;
    if(new_event)occurrence_=f.event->occurrence;
    if(!f.active||f.map.empty()||f.session.empty()||f.scene.empty()||f.ended||f.skipped||f.modal||terminated_) {
        expire(out);if(f.ended||f.skipped)terminated_=true;
        return out;
    }
    if(!f.foreground)pending_.reset();
    if(new_event) {
        pending_.reset();
        if(playing_&&!cues_[playing_->cue].finish_on_advance) {
            stopped_own=true;expire(out);
        }
        if(f.foreground&&valid_text(f.event->key,true)) {
            auto cue=std::find_if(cues_.begin(),cues_.end(),[&](const auto& c){return c.scene==f.scene&&c.event==f.event->key;});
            if(cue!=cues_.end())pending_=Pending{static_cast<std::size_t>(cue-cues_.begin()),f.event->occurrence};
        }
    }
    if(playing_) {
        const bool pause=f.paused||!f.foreground;
        if(pause!=playing_->paused) {
            out.push_back(action(pause?NarrationActionKind::Pause:NarrationActionKind::Resume,playing_->cue));
            playing_->paused=pause;
        }
    }
    if(pending_&&!playing_&&f.foreground&&!f.paused&&f.may_start&&!stale_audio&&(!f.audio.active||stopped_own)) {
        const auto cue=pending_->cue;
        out.push_back(action(NarrationActionKind::Play,cue));
        playing_=Playing{cue,f.audio.generation,{},false};pending_.reset();
    }
    if(f.discard_unstarted)pending_.reset();
    return out;
}
}
