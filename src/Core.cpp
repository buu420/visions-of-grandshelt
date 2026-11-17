#include "resonance/Core.hpp"
#include "resonance/Localization.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace resonance {
MenuSnapshot option_menu(const OptionObservation& option,std::string id,int row,int rows) {
    std::string value=option.presented_value;
    if(value.empty()&&option.selected>=0&&option.selected<static_cast<int>(option.alternatives.size())) value=option.alternatives[option.selected];
    const bool unavailable=(value.empty()&&option.selection_expected)||value=="Value unavailable";
    if(value.empty()&&unavailable) value=tr("Value unavailable");
    if(!value.empty()&&!unavailable&&option.alternatives.size()==1) value+=". "+tr("Only available option");
    if(option.popup_open) {
        if(!value.empty())value+=". ";
        value+=tr("Expanded");
    }
    return {std::move(id),option.label,std::move(value),row,rows,true,option.binding_matches,option.enabled};
}
std::optional<MenuSnapshot> screen_text_menu(const ScreenTextObservation& screen) {
    if(!screen.visible||!screen.ready||screen.field_hud||screen.dialogue_window||screen.text.empty())return {};
    return MenuSnapshot{screen.id,screen.text,"",-1,0,true,true,true};
}
DialogueVoice assigned_dialogue_voice(const VoiceAssignment& voice) {
    if(!voice.known||!voice.active||!voice.reflected)return DialogueVoice::Unknown;
    return voice.voice_valid&&voice.playback_id>=0?DialogueVoice::Voiced:DialogueVoice::Unvoiced;
}
std::vector<int> presented_children(int count,bool switcher,int active) {
    if(count<0||count>4096) return {};
    if(switcher) return active>=0&&active<count?std::vector<int>{active}:std::vector<int>{};
    std::vector<int> indices;for(int i=0;i<count;++i) indices.push_back(i);return indices;
}
void ReaderCore::output_epoch(unsigned long long epoch) {
    if(epoch_!=epoch) { reset();epoch_=epoch; }
}
std::optional<std::string> presented_text(const RenderedText& text) {
    if(!text.visible||text.text.empty()||(text.gradual&&!text.finished)) return {};
    return text.text;
}
void SpeechQueue::push(SpeechEvent event) {
    if(event.text.empty()) return;
    if(event.delivery==SpeechDelivery::LatestSelection) {
        std::erase_if(queue_,[&](const auto& pending) {
            return pending.delivery==SpeechDelivery::LatestSelection||
                (event.interrupt&&pending.delivery!=SpeechDelivery::OrderedBattle);
        });
        // Allow one current selection even when the ordered queue is at capacity.
        queue_.push_back(std::move(event));
        return;
    }
    if(event.interrupt)queue_.clear();
    if(queue_.size()>=16) {
        const auto disposable=std::find_if(queue_.begin(),queue_.end(),[](const auto& pending) {
            return pending.delivery!=SpeechDelivery::OrderedBattle;
        });
        if(disposable!=queue_.end())queue_.erase(disposable);
        else queue_.pop_front();
    }
    queue_.push_back(std::move(event));
}
std::optional<SpeechEvent> SpeechQueue::pop() {
    if(queue_.empty()) return {};
    auto event=std::move(queue_.front()); queue_.pop_front(); return event;
}
void SpeechQueue::discard(SpeechDelivery delivery) {
    std::erase_if(queue_,[delivery](const auto& event){return event.delivery==delivery;});
}
bool SpeechQueue::contains(SpeechDelivery delivery) const {
    return std::any_of(queue_.begin(),queue_.end(),[delivery](const auto& event){return event.delivery==delivery;});
}
std::vector<SpeechEvent> ReaderCore::observe(const Frame& frame) {
    if (context_!=frame.context) { reset(); context_=frame.context; }
    std::vector<SpeechEvent> result;
    if (frame.dialogue) {
        const auto& d=*frame.dialogue;
        if(d.visible && d.ready && !d.text.empty()) {
            const auto key=d.id+"\n"+d.speaker+"\n"+d.text;
            if(key!=dialogue_) {
                dialogue_=key;
                last_=d.speaker.empty()?d.text:d.speaker+": "+d.text;
                if(d.voice!=DialogueVoice::Voiced)result.push_back({last_,true});
            }
        }
    } else dialogue_.clear();
    if(frame.menu) {
        const auto& m=*frame.menu;
        if(m.visible && m.ready && !m.label.empty()) {
            const auto key=m.id+"\n"+std::to_string(m.index)+"\n"+m.label+"\n"+m.value+(m.enabled?"+":"-");
            if(key!=menu_) {
                menu_=key;
                last_=m.label;
                if(!m.value.empty()) last_+=". "+m.value;
                if(!m.enabled) last_+=". "+tr("Unavailable");
                if(m.index>=0 && m.count>m.index) last_+=". "+trf("{0} of {1}",{std::to_string(m.index+1),std::to_string(m.count)});
                const auto delivery=m.index>=0?SpeechDelivery::LatestSelection:SpeechDelivery::Normal;
                result.push_back({last_,frame.menu_interrupts&&result.empty(),delivery});
            }
        }
    } else menu_.clear();
    const bool ready=(frame.menu&&frame.menu->visible&&frame.menu->ready&&!frame.menu->label.empty()) ||
        (frame.dialogue&&frame.dialogue->visible&&frame.dialogue->ready&&
            frame.dialogue->voice!=DialogueVoice::Voiced&&!frame.dialogue->text.empty());
    if(!introduced_&&!frame.introduction.empty()&&ready) {
        introduced_=true;
        if(result.empty()) result.push_back({frame.introduction,frame.menu_interrupts});
        else {
            const char end=frame.introduction.back();
            const bool complete=end=='.'||end=='?'||end=='!'||
                frame.introduction.ends_with("。")||frame.introduction.ends_with("？")||frame.introduction.ends_with("！");
            result.front().text=frame.introduction+(complete?" ":". ")+result.front().text;
        }
        last_=result.back().text;
    }
    return result;
}
void ReaderCore::reset() { context_.clear(); menu_.clear(); dialogue_.clear(); last_.clear(); introduced_=false; }
std::string current_review(const Frame& frame,const std::string& previous) {
    auto review=frame;review.introduction.clear();
    ReaderCore current;current.observe(review);
    return current.repeat().empty()?previous:current.repeat();
}
Frame speech_frame(const Frame& frame,bool describing) {
    auto automatic=frame;
    if(describing&&automatic.dialogue&&automatic.dialogue->voice!=DialogueVoice::Voiced)
        automatic.dialogue->ready=false;
    return automatic;
}
namespace {
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
double length(Vec3 p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); }
std::string distance(double metres) {
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(metres<10 && std::abs(metres-std::round(metres))>.04?1:0)<<metres;
    return trf(std::abs(metres-1)<.04?"{0} metre":"{0} metres",{s.str()});
}
}
std::optional<std::string> describe_route(const PathSnapshot& p,std::size_t max_legs) {
    if(!max_legs||!p.valid||p.partial||p.points.size()<2||p.points.size()>4096||!std::isfinite(p.world_to_metres)||p.world_to_metres<=0) return {};
    if(!finite(p.right)||!finite(p.up)) return {};
    const double r=std::hypot(p.right.x,p.right.y), u=std::hypot(p.up.x,p.up.y);
    if(r<.01||u<.01||std::abs((p.right.x*p.up.x+p.right.y*p.up.y)/(r*u))>.1) return {};
    for(auto point:p.points) if(!finite(point)) return {};
    std::vector<Vec3> legs;
    for(size_t i=1;i<p.points.size();++i) {
        Vec3 d{p.points[i].x-p.points[i-1].x,p.points[i].y-p.points[i-1].y,p.points[i].z-p.points[i-1].z};
        if(length(d)<.0001) continue;
        if(!legs.empty()) {
            const auto a=legs.back();
            const double cosine=(a.x*d.x+a.y*d.y+a.z*d.z)/(length(a)*length(d));
            if(cosine>1.-1e-8) { legs.back()={a.x+d.x,a.y+d.y,a.z+d.z}; continue; }
        }
        legs.push_back(d);
    }
    if(legs.empty()) return tr("At destination");
    std::string result;
    const auto leg_count=std::min(legs.size(),max_legs);
    for(std::size_t leg=0;leg<leg_count;++leg) {
        const auto d=legs[leg];
        if(!result.empty()) result+=", "+tr("then")+" ";
        const auto horizontal=std::hypot(d.x,d.y);
        if(horizontal<.1*std::abs(d.z)) {
            result+=trf(d.z>0?"higher ground {0}":"lower ground {0}",{distance(std::abs(d.z)/p.world_to_metres)});
            continue;
        }
        const double x=(d.x*p.right.x+d.y*p.right.y)/r;
        const double y=(d.x*p.up.x+d.y*p.up.y)/u;
        const int octant=static_cast<int>(std::llround(std::atan2(y,x)/(3.14159265358979323846/4)) + 8)%8;
        static constexpr const char* names[]={"right","up-right","up","up-left","left","down-left","down","down-right"};
        result+=trf("{0} {1}",{tr(names[octant]),distance(horizontal/p.world_to_metres)});
        if(std::abs(d.z)/p.world_to_metres>=.5)
            result+=", "+trf(d.z>0?"higher ground {0}":"lower ground {0}",{distance(std::abs(d.z)/p.world_to_metres)});
    }
    return result;
}
std::optional<std::string> describe_target(const TargetSnapshot& t) {
    if(!t.visible||t.label.empty()) return {};
    return t.label+(t.interactable&&!t.prompt.empty()?". "+t.prompt:"");
}
}
