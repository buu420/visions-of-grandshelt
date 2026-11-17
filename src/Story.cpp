#include "resonance/Story.hpp"
#include "resonance/Localization.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
namespace resonance {
namespace {
bool captured(const StorySnapshot& s) {
    return s.ready&&!s.world.empty()&&!s.player.empty()&&(s.objective.empty()||!s.objective_id.empty());
}
bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
std::string context_identity(const StorySnapshot& s,int map) { return s.world+"\x1f"+s.player+"\x1f"+std::to_string(map); }
std::string objective_identity(const StorySnapshot& s) { return s.objective_id+"\x1e"+s.objective; }
std::string announcement_identity(const StorySnapshot& s,int map) {
    return context_identity(s,map)+"\x1d"+objective_identity(s)+"\x1c"+s.next_step;
}
bool same_point(const std::optional<Vec3>& a,const std::optional<Vec3>& b) {
    return a&&b&&a->x==b->x&&a->y==b->y&&a->z==b->z;
}
std::set<std::string> route_ids(const StorySnapshot& s) {
    std::set<std::string> result;
    for(const auto& d:story_destinations(s))if(d.disclosed&&d.local&&d.position)result.insert(d.id);
    return result;
}
std::string point_distance(double metres) {
    const double rounded=metres<10?std::round(metres*10)/10:std::round(metres);
    std::ostringstream text;text<<std::fixed<<std::setprecision(rounded==std::round(rounded)?0:1)<<rounded;
    return trf(rounded==1?"{0} metre":"{0} metres",{text.str()});
}
void append_detail(std::string& text,const std::string& detail) {
    if(detail.empty())return;
    if(!text.empty()&&text.back()!='.'&&text.back()!='!'&&text.back()!='?'&&
       !text.ends_with("。")&&!text.ends_with("！")&&!text.ends_with("？"))
        text=trf("{0}.",{text});
    text+=' ';text+=detail;
}
}
std::string describe_field_position(Vec3 point,const StorySpatialContext& context) {
    if(!finite(point)||!finite(context.origin)||!finite(context.right)||!finite(context.up)||
       !std::isfinite(context.world_to_metres)||context.world_to_metres<=0)return {};
    const double r=std::hypot(context.right.x,context.right.y),u=std::hypot(context.up.x,context.up.y);
    if(!std::isfinite(r)||!std::isfinite(u)||r<.01||u<.01)return {};
    const Vec3 right{context.right.x/r,context.right.y/r,0},up{context.up.x/u,context.up.y/u,0};
    if(std::abs(right.x*up.x+right.y*up.y)>.1)return {};
    const Vec3 delta{point.x-context.origin.x,point.y-context.origin.y,point.z-context.origin.z};
    if(!finite(delta))return {};
    const double horizontal=std::hypot(delta.x,delta.y);
    const double metres=std::hypot(horizontal,delta.z)/context.world_to_metres;
    const double height=std::abs(delta.z)/context.world_to_metres;
    if(!std::isfinite(horizontal)||!std::isfinite(metres)||!std::isfinite(height))return {};
    auto result=trf("{0} in a straight line",{point_distance(metres)});
    if(horizontal/context.world_to_metres>=.1) {
        const double x=delta.x*right.x+delta.y*right.y,y=delta.x*up.x+delta.y*up.y;
        if(!std::isfinite(x)||!std::isfinite(y))return {};
        const int octant=(static_cast<int>(std::llround(std::atan2(y,x)/(3.14159265358979323846/4)))+8)%8;
        static constexpr const char* directions[]={"right","up-right","up","up-left","left","down-left","down","down-right"};
        result=trf("Control-relative {0}, {1}",{tr(directions[octant]),result});
    }
    if(height>=.5)result=trf(delta.z>0?"{0}, target point {1} higher":"{0}, target point {1} lower",
        {result,point_distance(height)});
    return result;
}
std::vector<StoryDestination> story_destinations(const StorySnapshot& s) {
    if(!captured(s)||s.objective.empty())return {};
    std::vector<StoryDestination> result;
    std::map<std::string,std::size_t> seen;
    for(auto d:s.destinations) {
        if(!d.disclosed||d.id.empty())continue;
        if(auto found=seen.find(d.id);found!=seen.end()) {
            auto& earlier=result[found->second];
            earlier.position.reset();earlier.interactable=false;
            earlier.unavailable_reason=tr("The story destination is ambiguous. A walking route is unavailable.");
            continue;
        }
        if(result.size()>=256)break;
        if(d.label.empty())d.label=tr(d.exit?"Story exit":"Story destination");
        if(s.map_id<0||s.destination_epoch.empty()) {
            d.position.reset();
            if(d.unavailable_reason.empty())d.unavailable_reason=tr("The current story area or filtered destination is unavailable. A walking route cannot be verified.");
        } else if(!d.local) {
            d.position.reset();
            if(d.unavailable_reason.empty())d.unavailable_reason=d.area.empty()?
                tr("The destination is in another area. A current-area walking route is unavailable."):
                trf("The destination is in {0}. A current-area walking route is unavailable.",{d.area});
        } else if(!d.actor_available) {
            d.position.reset();
            if(d.unavailable_reason.empty())d.unavailable_reason=tr("This story destination is no longer active in the area.");
        } else if(d.position&&!finite(*d.position))d.position.reset();
        if(!d.position&&d.unavailable_reason.empty())d.unavailable_reason=tr("A route to this story destination is unavailable.");
        if(!d.actor_available||!d.position)d.interactable=false;
        seen.emplace(d.id,result.size());result.push_back(std::move(d));
    }
    if(result.empty()) {
        StoryDestination d;d.id="objective:"+s.objective_id;d.label=s.objective;d.disclosed=true;d.local=true;
        d.unavailable_reason=s.unavailable_reason.empty()?tr("No disclosed local story destination has been captured."):s.unavailable_reason;
        result.push_back(std::move(d));
    }
    return result;
}
std::optional<StoryDestination> continue_story_target(const StorySnapshot& snapshot,const std::string& selected) {
    std::optional<StoryDestination> only;
    std::size_t count=0;
    for(auto& destination:story_destinations(snapshot)) {
        if(!destination.position)continue;
        if(destination.id==selected)return std::move(destination);
        if(++count==1)only=std::move(destination);
    }
    return count==1?only:std::nullopt;
}

TargetSnapshot story_target_snapshot(std::string id,const StoryDestination& d) {
    const bool interactable=d.disclosed&&d.actor_available&&d.interactable&&d.position&&finite(*d.position);
    return {std::move(id),d.label,interactable?tr("Interaction available"):"",d.disclosed,interactable};
}
StoryDestinationDescription describe_story_destination(std::string id,const StorySnapshot& s,const StoryDestination& d,
        std::optional<StorySpatialContext> context) {
    StoryDestinationDescription result;result.target.id=std::move(id);
    const auto current=story_destinations(s);
    const auto found=std::find_if(current.begin(),current.end(),[&](const auto& item){return item.id==d.id;});
    if(found!=current.end()) {
        result.target=story_target_snapshot(std::move(result.target.id),*found);
        if(context&&story_route_available(s,*found))result.detail=describe_field_position(*found->position,*context);
    }
    return result;
}
bool story_route_available(const StorySnapshot& s,const StoryDestination& d) {
    if(!captured(s)||s.objective.empty()||s.map_id<0||s.destination_epoch.empty()||
       !d.disclosed||!d.local||!d.actor_available||!d.position||!finite(*d.position))return false;
    const auto current=story_destinations(s);
    auto found=std::find_if(current.begin(),current.end(),[&](const auto& item){return item.id==d.id;});
    return found!=current.end()&&found->disclosed&&found->local&&found->actor_available&&same_point(found->position,d.position);
}
std::string story_status(const StorySnapshot& s) {
    if(!captured(s))return s.unavailable_reason.empty()?tr("Current story objective unavailable."):
        trf("Current story objective unavailable. {0}",{s.unavailable_reason});
    if(s.objective.empty())return tr("No current story objective is displayed.");
    auto text=trf("Current objective. {0}",{s.objective});
    append_detail(text,s.next_step);
    return text;
}
std::string story_details_status(const StorySnapshot& s) {
    auto result=story_status(s);
    if(!captured(s)||s.objective.empty()||!s.next_step.empty())return result;
    if(s.description!=s.objective)append_detail(result,s.description);
    if(!s.destination_title.empty()&&s.destination_title!=s.objective&&s.destination_title!=s.description)
        append_detail(result,trf("Destination. {0}",{s.destination_title}));
    return result;
}
bool story_announcement_allowed(const Frame& ui,bool foreground,bool output_ready,bool field_control,bool description_busy,bool native_voice_clear) {
    return foreground&&output_ready&&field_control&&!description_busy&&native_voice_clear&&
        !(ui.menu&&ui.menu->visible)&&!(ui.dialogue&&ui.dialogue->visible);
}
std::string StoryTracker::announcement_key(const StorySnapshot& s) const {
    const int map=s.map_id<0&&s.world==last_world_&&s.player==last_player_?last_map_id_:s.map_id;
    return announcement_identity(s,map);
}
bool StoryTracker::needs_objective_announcement(const StorySnapshot& s,std::uint64_t epoch) const {
    if(!captured(s))return false;
    auto key=announcement_key(s);
    // Learning the first map identifies an already announced area; it does not
    // establish that a different area was entered.
    if(s.map_id>=0&&last_map_id_<0&&s.world==last_world_&&s.player==last_player_&&
       announced_==announcement_identity(s,-1))key=announced_;
    return key!=announced_||epoch!=announced_epoch_;
}
bool StoryTracker::needs_announcement(const StorySnapshot& s,std::uint64_t epoch) const {
    return needs_objective_announcement(s,epoch)||(captured(s)&&!s.nearby_clue.empty()&&
        std::find(announced_clues_.begin(),announced_clues_.end(),s.nearby_clue)==announced_clues_.end());
}
std::string story_help_status(const StorySnapshot& s) {
    return captured(s)&&!s.field_control?tr("Story objective is unavailable in the current scene."):story_status(s);
}
StoryUpdate StoryTracker::observe(const StorySnapshot& s,bool can_announce,std::uint64_t output_epoch) {
    StoryUpdate result;
    if(!captured(s)) {
        result.navigation_invalidated=current_.ready&&!current_.objective.empty();
        current_=s;current_.ready=false;current_.objective.clear();current_.objective_id.clear();current_.destinations.clear();
        current_.description.clear();current_.destination_title.clear();current_.next_step.clear();
        current_.nearby_clue.clear();
        current_.flight_destination.reset();
        pending_.reset();return result;
    }
    const bool known_context=!last_world_.empty()&&!last_player_.empty();
    const bool same_owner=s.world==last_world_&&s.player==last_player_;
    const bool known_map_changed=same_owner&&s.map_id>=0&&last_map_id_>=0&&s.map_id!=last_map_id_;
    const auto objective=objective_identity(s);
    result.context_changed=known_context&&(!same_owner||known_map_changed);
    result.objective_changed=!last_objective_.empty()&&last_objective_!=objective;
    result.destination_changed=!last_destination_.empty()&&!s.destination_epoch.empty()&&last_destination_!=s.destination_epoch;
    if(result.context_changed||result.objective_changed)announced_clues_.clear();
    result.navigation_invalidated=result.context_changed||result.objective_changed||result.destination_changed;
    const auto before=route_ids(current_),after=route_ids(s);
    for(const auto& id:before)if(!after.contains(id))result.navigation_invalidated=true;
    current_=s;current_.destinations=story_destinations(s);
    if(same_owner&&last_map_id_<0&&s.map_id>=0&&announced_==announcement_identity(s,-1))
        announced_=announcement_identity(s,s.map_id);
    if(!same_owner)last_map_id_=-1;
    last_world_=s.world;last_player_=s.player;last_objective_=objective;
    if(s.map_id>=0)last_map_id_=s.map_id;
    if(!s.destination_epoch.empty())last_destination_=s.destination_epoch;
    const auto key=announcement_key(s);
    if(needs_objective_announcement(s,output_epoch))pending_=SpeechEvent{story_status(current_),false};
    else pending_.reset();
    if(can_announce&&pending_) {
        result.speech.push_back(std::move(*pending_));pending_.reset();
        announced_=key;announced_epoch_=output_epoch;
    }
    if(can_announce&&!s.nearby_clue.empty()&&
       std::find(announced_clues_.begin(),announced_clues_.end(),s.nearby_clue)==announced_clues_.end()) {
        announced_clues_.push_back(s.nearby_clue);
        result.speech.push_back({s.nearby_clue,false});
    }
    return result;
}
void StoryTracker::reset() { current_={};last_world_.clear();last_player_.clear();last_map_id_=-1;last_objective_.clear();last_destination_.clear();announced_.clear();pending_.reset();announced_clues_.clear();announced_epoch_=0; }
std::optional<NavigationUpdate> stop_changed_story_navigation(const StoryUpdate& update,bool story_selected,NavigationFollower& follower) {
    if(!story_selected||!update.navigation_invalidated||follower.mode()==NavigationMode::Off)return {};
    return follower.stop(tr("Story navigation stopped. The objective or destination changed."));
}
}
