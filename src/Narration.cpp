#include "resonance/Narration.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>
namespace resonance {
namespace {
constexpr double start_window=.15;
constexpr double seek_tolerance=.35;
constexpr double rewind_tolerance=.05;
constexpr double paused_drift_tolerance=.1;
constexpr double hold_seconds=.24;
constexpr double native_progress=.005;
constexpr double resume_tolerance=.03;
bool nonblank(const std::string& value) {
    return std::any_of(value.begin(),value.end(),[](unsigned char c){return c>32;});
}
bool safe_audio_path(const std::string& path) {
    if(path.empty()||path.front()=='/'||path.front()=='\\') return false;
    std::size_t component=0;
    for(std::size_t i=0;i<=path.size();++i) {
        if(i==path.size()||path[i]=='/'||path[i]=='\\') {
            const auto part=std::string_view(path).substr(component,i-component);
            if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ') return false;
            component=i+1;
        } else {
            const auto c=static_cast<unsigned char>(path[i]);
            if(c<32||c==127||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|') return false;
        }
    }
    return true;
}
bool usable_clock(const NarrationClock& c) {
    return c.valid&&!c.session.empty()&&!c.scene.empty()&&std::isfinite(c.seconds)&&c.seconds>=0&&
        std::isfinite(c.sample_seconds)&&c.sample_seconds>=0&&std::isfinite(c.rate);
}
}
NarrationScheduler::NarrationScheduler(std::vector<NarrationCue> cues) {set_cues(std::move(cues));}
std::vector<NarrationAction> NarrationScheduler::set_cues(std::vector<NarrationCue> cues) {
    std::set<std::pair<std::string,std::string>> identities;
    for(const auto& cue:cues) {
        const bool valid_source=cue.source==DescriptionSource::ScreenReader?cue.audio.empty():safe_audio_path(cue.audio);
        if(!nonblank(cue.scene)||!nonblank(cue.id)||!nonblank(cue.text)||!valid_source||
           !std::isfinite(cue.start)||!std::isfinite(cue.end)||cue.start<0||cue.end<=cue.start||
           !std::isfinite(cue.duration)||cue.duration<0||cue.duration>cue.end-cue.start)
            throw std::invalid_argument("Invalid narration cue or relative audio path");
        if(!identities.emplace(cue.scene,cue.id).second)
            throw std::invalid_argument("Repeated narration scene and cue id");
    }
    std::sort(cues.begin(),cues.end(),[](const auto& a,const auto& b) {
        return a.scene==b.scene?a.start<b.start:a.scene<b.scene;
    });
    for(std::size_t i=1;i<cues.size();++i)
        if(cues[i].scene==cues[i-1].scene&&cues[i].start<cues[i-1].end)
            throw std::invalid_argument("Overlapping narration cues in one scene");
    // Allocate all replacement state before touching the running table.
    std::vector<bool> consumed(cues.size(),false);
    auto out=reset();
    cues_=std::move(cues);consumed_=std::move(consumed);
    return out;
}
NarrationAction NarrationScheduler::action(NarrationActionKind kind,std::size_t index) const {
    const auto& cue=cues_[index];
    return {kind,cue.scene,cue.id,cue.text,cue.audio,0};
}
void NarrationScheduler::stop(std::vector<NarrationAction>& out) {
    if(active_) out.push_back(action(NarrationActionKind::Stop,*active_));
    active_.reset();output_paused_=false;hold_paused_=false;audio_native_=0;
}
std::vector<NarrationAction> NarrationScheduler::reset() {
    std::vector<NarrationAction> out;
    stop(out);previous_.reset();session_.clear();scene_.clear();initial_window_=false;
    clock_tracking_=false;clock_held_=false;
    std::fill(consumed_.begin(),consumed_.end(),false);
    return out;
}
bool NarrationScheduler::has_scene(std::string_view scene) const {
    return std::any_of(cues_.begin(),cues_.end(),[&](const auto& cue){return cue.scene==scene;});
}
std::vector<NarrationAction> NarrationScheduler::observe(const NarrationClock& c) {
    std::vector<NarrationAction> out;
    if(!usable_clock(c)) {
        stop(out);previous_.reset();initial_window_=false;
        clock_tracking_=false;clock_held_=false;
        return out;
    }
    if(c.session!=session_||c.scene!=scene_) {
        stop(out);session_=c.session;scene_=c.scene;previous_.reset();initial_window_=true;
        clock_tracking_=false;clock_held_=false;
        std::fill(consumed_.begin(),consumed_.end(),false);
    }
    if(c.seeking||(!c.playing&&!c.paused)||(c.playing&&!c.paused&&std::abs(c.rate-1)>1e-6)) {
        stop(out);previous_=c;initial_window_=false;
        clock_tracking_=false;clock_held_=false;
        return out;
    }
    if(previous_&&!previous_->seeking&&!previous_->playing&&!previous_->paused&&
       c.playing&&!c.paused&&c.seconds<=start_window) {
        // A prepared player can be observed before its first Play, and some
        // players reuse their UObject for a replay after Stop.
        std::fill(consumed_.begin(),consumed_.end(),false);
        previous_.reset();initial_window_=true;clock_tracking_=false;clock_held_=false;
    }
    if(previous_) {
        const auto native_delta=c.seconds-previous_->seconds;
        const auto sample_delta=c.sample_seconds-previous_->sample_seconds;
        if(previous_->seeking||sample_delta<0) {
            stop(out);previous_=c;initial_window_=false;
            clock_tracking_=false;clock_held_=false;
            return out;
        }
        if(native_delta < -rewind_tolerance) {
            stop(out);
            for(std::size_t i=0;i<cues_.size();++i)
                if(cues_[i].scene==c.scene&&cues_[i].start>=c.seconds) consumed_[i]=false;
            previous_=c;initial_window_=c.seconds<=start_window;
            clock_tracking_=false;clock_held_=false;
            return out;
        }
        // Normal slow sampling follows wall time. A seek or an unobserved stall
        // does not: establish a fresh baseline without playing any crossed cues.
        const bool uninterrupted=c.playing&&!c.paused&&previous_->playing&&!previous_->paused;
        if(native_delta>sample_delta+seek_tolerance||
           (uninterrupted&&active_&&!output_paused_&&sample_delta-native_delta>seek_tolerance)) {
            stop(out);previous_=c;initial_window_=false;
            clock_tracking_=false;clock_held_=false;
            return out;
        }
    }
    // Infer a hold cumulatively: each normal 80ms sample is too short to trip the
    // single-gap guard above, even when the native clock stays flat indefinitely.
    const bool recovering_hold=clock_held_||hold_paused_;
    if(!clock_tracking_||c.paused||(previous_&&previous_->paused)) {
        progress_native_=c.seconds;progress_sample_=c.sample_seconds;
        clock_tracking_=true;clock_held_=false;
    } else if(c.seconds>progress_native_+native_progress) {
        progress_native_=c.seconds;progress_sample_=c.sample_seconds;clock_held_=false;
    } else clock_held_=c.sample_seconds-progress_sample_+1e-9>=hold_seconds;

    // Estimate file progress only while audio was actually running between
    // samples. A detected hold may leave it up to one hold threshold ahead of
    // the picture; wait for the native clock to catch it before Resume.
    if(active_&&!output_paused_&&previous_) audio_native_+=c.sample_seconds-previous_->sample_seconds;
    const bool paused_drift=hold_paused_?
        c.seconds>audio_native_+paused_drift_tolerance||c.seconds<paused_at_-rewind_tolerance:
        std::abs(c.seconds-paused_at_)>paused_drift_tolerance;
    if(active_&&output_paused_&&paused_drift) {
        stop(out);previous_=c;initial_window_=false;
        return out;
    }
    if(active_&&c.seconds>=cues_[*active_].end) stop(out);
    const bool hold_caught_up=!hold_paused_||
        (c.seconds>paused_at_+native_progress&&c.seconds>=audio_native_-resume_tolerance);
    const bool running=c.foreground&&c.playing&&!c.paused&&!clock_held_&&hold_caught_up;
    if(active_) {
        if(!running&&!output_paused_) {
            out.push_back(action(NarrationActionKind::Pause,*active_));output_paused_=true;paused_at_=c.seconds;
            hold_paused_=clock_held_;
        } else if(running&&output_paused_) {
            out.push_back(action(NarrationActionKind::Resume,*active_));output_paused_=false;hold_paused_=false;
        }
    }
    if(!active_&&running&&!recovering_hold) {
        const bool natural=previous_&&previous_->foreground&&previous_->playing&&!previous_->paused;
        const bool resumed=previous_&&previous_->foreground&&previous_->paused&&
            std::abs(c.seconds-previous_->seconds)<=rewind_tolerance;
        for(std::size_t i=0;i<cues_.size();++i) {
            const auto& cue=cues_[i];
            const bool fits=cue.duration>0?c.seconds+cue.duration+.05<=cue.end+1e-9:
                c.seconds-cue.start<=start_window;
            if(cue.scene!=c.scene||consumed_[i]||c.seconds<cue.start||c.seconds>=cue.end||!fits) continue;
            const bool crossed=natural&&previous_->seconds<cue.start;
            if(!initial_window_&&!resumed&&!crossed) continue;
            consumed_[i]=true;active_=i;output_paused_=false;
            audio_native_=c.seconds;
            out.push_back(action(NarrationActionKind::Play,i));
            break;
        }
    }
    if(running||!c.foreground) initial_window_=false;
    previous_=c;
    return out;
}
}
