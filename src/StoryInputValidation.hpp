#pragma once
// Private adapter safety decisions; no engine objects or retained native pointers.
#include "resonance/Localization.hpp"
#include "resonance/Story.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
namespace resonance::story_input_detail {
enum class NativeLayoutResult { Valid, InvalidBounds, WrongAllocation, WrongReturnOffset, WrongParameterExtent };
inline NativeLayoutResult native_call_layout(int parms_size,int properties_size,int actual_return_offset,
        int expected_allocation,int return_offset,int return_size,int capacity) {
    if(expected_allocation<=0||expected_allocation>capacity||return_offset<0||return_size<=0||
       return_size>expected_allocation||return_offset>expected_allocation-return_size)return NativeLayoutResult::InvalidBounds;
    if(properties_size!=expected_allocation)return NativeLayoutResult::WrongAllocation;
    if(actual_return_offset!=return_offset)return NativeLayoutResult::WrongReturnOffset;
    // The JMAP proves PropertiesSize (allocation), not UFunction::ParmsSize.
    // Accept only the exact consumed end or the audited padded representation.
    return parms_size==return_offset+return_size||parms_size==expected_allocation?
        NativeLayoutResult::Valid:NativeLayoutResult::WrongParameterExtent;
}
inline const char* native_layout_name(NativeLayoutResult result) {
    switch(result) {
    case NativeLayoutResult::Valid:return "valid";
    case NativeLayoutResult::InvalidBounds:return "bounds";
    case NativeLayoutResult::WrongAllocation:return "allocation";
    case NativeLayoutResult::WrongReturnOffset:return "return-offset";
    case NativeLayoutResult::WrongParameterExtent:return "parameter-extent";
    }
    return "unknown";
}
class NativeProblemDeduplicator {
    std::array<std::string,16> seen_{};
    std::size_t count_{};
public:
    bool accept(std::string_view problem) {
        if(problem.empty()||problem.size()>512||count_==seen_.size()||
           std::find(seen_.begin(),seen_.begin()+count_,problem)!=seen_.begin()+count_)return false;
        seen_[count_++]=problem;return true;
    }
};
struct ObjectKey {
    std::uint8_t type{};
    std::int32_t id{};
    bool operator==(const ObjectKey&) const = default;
};
enum class CoordinateSpace { Unknown, World, Map2D };
enum class DestinationSource { Unknown, RawScenario, FilteredMainQuest };
struct FilteredPoint {
    std::uint8_t role{},map_kind{};
    int map_id{};
    bool object_target{};
    ObjectKey object;
    bool operator==(const FilteredPoint&)const=default;
};
struct PointEvidence {
    ObjectKey key;
    Vec3 position;
    bool current_objective{},current_main{},disclosed{},same_world{},local{};
    CoordinateSpace coordinates{CoordinateSpace::Unknown};
    DestinationSource source{DestinationSource::Unknown};
    int field_object_id{-1};
};
inline bool valid_filtered_point(const FilteredPoint& point) {
    return point.role<=1&&point.map_kind<=2&&point.map_id>=0&&
        (!point.object_target||(point.object.type>=1&&point.object.type<=6&&point.object.id>=0));
}
inline std::string filtered_destination_epoch(std::optional<std::span<const FilteredPoint>> points) {
    if(!points||points->size()>256)return {};
    std::vector<FilteredPoint> sorted;
    for(auto point:*points) {
        if(!valid_filtered_point(point))return {};
        if(!point.object_target)point.object={};
        sorted.push_back(point);
    }
    const auto fields=[](const auto& p){return std::tuple{p.role,p.map_kind,p.map_id,p.object_target,p.object.type,p.object.id};};
    std::sort(sorted.begin(),sorted.end(),[&](const auto& a,const auto& b){return fields(a)<fields(b);});
    sorted.erase(std::unique(sorted.begin(),sorted.end()),sorted.end());
    std::string result="filtered-main-quest:";
    for(const auto& p:sorted)result+=std::to_string(p.role)+":"+std::to_string(p.map_kind)+":"+
        std::to_string(p.map_id)+":"+std::to_string(p.object_target)+":"+
        std::to_string(p.object.type)+":"+std::to_string(p.object.id)+";";
    return result;
}
template<class Object> struct ScopeSelection {Object* object{};bool ambiguous{};};
template<class Object,class Access> Object* resolved_world(Object* current,const Access& access) {
    for(int depth=0;depth<16&&access.valid(current);++depth,current=access.outer(current)) {
        if(access.is_world(current))return current;
        if(access.is_level(current)) {
            auto* owner=access.level_world(current);
            return access.valid(owner)&&access.is_world(owner)?owner:nullptr;
        }
    }
    return nullptr;
}
template<class Object,class WorldOf> ScopeSelection<Object> unique_world_member(std::span<Object* const> candidates,Object* world,WorldOf world_of) {
    ScopeSelection<Object> result;if(!world)return result;
    for(auto* candidate:candidates)if(world_of(candidate)==world) {
        if(result.object)return {nullptr,true};
        result.object=candidate;
    }
    return result;
}
inline bool valid_native_array(const void* data,int count,int capacity,int limit) {
    return limit>=0&&count>=0&&count<=limit&&capacity>=count&&capacity<=65536&&
        ((capacity==0&&data==nullptr)||(capacity>0&&data!=nullptr));
}
inline bool valid_object_key(ObjectKey key) {return key.type>=1&&key.type<=6&&key.id>=0;}
inline bool unique_capture_ids(std::span<const int> ids,std::size_t limit) {
    if(ids.size()>limit)return false;
    std::vector<int> sorted(ids.begin(),ids.end());
    if(std::any_of(sorted.begin(),sorted.end(),[](int id){return id<0;}))return false;
    std::sort(sorted.begin(),sorted.end());
    return std::adjacent_find(sorted.begin(),sorted.end())==sorted.end();
}
struct RenderFacts {
    bool known{},user_widget{},owner_matches{};
    int visibility{-1};
    double opacity{},alpha{};
};
template<class Object,class Access> bool rendered_widget_chain(Object* current,const Access& access,Object* required_ancestor=nullptr) {
    std::array<Object*,64> seen{};std::size_t count=0;bool reached=required_ancestor==nullptr;
    while(access.valid(current)&&count<seen.size()) {
        if(std::find(seen.begin(),seen.begin()+count,current)!=seen.begin()+count)return false;
        seen[count++]=current;reached|=current==required_ancestor;
        const auto facts=access.facts(current);
        if(!facts.known||!facts.owner_matches||
           (facts.visibility!=0&&facts.visibility!=3&&facts.visibility!=4)||
           !std::isfinite(facts.opacity)||facts.opacity<=0||
           (facts.user_widget&&(!std::isfinite(facts.alpha)||facts.alpha<=0)))return false;
        const auto parent=access.parent(current);if(!parent)return false;
        if(*parent){current=*parent;continue;}
        const auto owner=access.tree_owner(current);if(!owner)return false;
        if(*owner){current=*owner;continue;}
        const auto viewport=access.in_viewport(current);
        return reached&&viewport&&*viewport;
    }
    return false;
}
// Native relation branch 2 (FUN_1451223b0): one filtered Goal/Map ID plus its
// native GetGroupMapIdListAtMap group. Areas stay per goal for forward checks.
struct GoalArea {int map{-1};std::vector<int> maps;};
template<class Query> std::optional<std::vector<GoalArea>> goal_areas(std::span<const FilteredPoint> points,Query query) {
    if(points.size()>256)return {};
    std::vector<int> goals;
    for(const auto& point:points) {
        if(!valid_filtered_point(point))return {};
        if(point.role==0&&point.map_kind==0)goals.push_back(point.map_id);
    }
    std::sort(goals.begin(),goals.end());goals.erase(std::unique(goals.begin(),goals.end()),goals.end());
    std::vector<GoalArea> result;std::size_t total=0;
    for(const int goal:goals) {
        const auto group=query(goal);if(!group||group->size()>512)return {};
        GoalArea area{goal,{goal}};
        for(int id:*group) {if(id<0)return {};area.maps.push_back(id);}
        std::sort(area.maps.begin(),area.maps.end());area.maps.erase(std::unique(area.maps.begin(),area.maps.end()),area.maps.end());
        if((total+=area.maps.size())>4096)return {};
        result.push_back(std::move(area));
    }
    return result;
}
inline bool in_area(const GoalArea& area,int map) {return std::binary_search(area.maps.begin(),area.maps.end(),map);}
inline bool pointed_target_at(std::span<const FilteredPoint> points,int map,bool goals_only=false) {
    return std::any_of(points.begin(),points.end(),[&](const auto& p) {
        return (!goals_only||p.role==0)&&p.map_kind==0&&p.map_id==map&&p.object_target;
    });
}
// Map-level objectives give no direction between exits inside their native group.
// Each entry keeps one in-area exit that exact-build cooked progress data proves forward.
struct AuditedStoryExit {int goal_map,current_map,related_map;};
inline constexpr std::array<AuditedStoryExit,2> audited_story_exits{{
    // Earth Shrine entrance: the objective's C01_008_01_B ({progress:0}==1030) and C01_008_02 (==1040)
    // triggers exist only in Dng_01Gra_40_EV; NoEntry 22 turns the player back from the world-map exit until flag 102.
    {3001,3001,3000},
    // Burning capital: the C01_017_01 (==1095) trigger separates the world-map arrival from gate uid 1 on the
    // baked navmesh; C01_018_01 (==1100) follows in 3020.
    {2010,2010,3020}}};
inline bool audited_story_exit(int current_map,int related_map,std::span<const FilteredPoint> points) {
    if(points.size()!=1||points[0].role!=0||points[0].map_kind!=0||points[0].object_target)return false;
    return std::any_of(audited_story_exits.begin(),audited_story_exits.end(),[&](const auto& e) {
        return e.goal_map==points[0].map_id&&e.current_map==current_map&&e.related_map==related_map;
    });
}
// A native relation by RelatedMapId says only that an exit leads somewhere in a
// destination area. Story keeps it when it enters that area from outside or leads
// to the map of a pointed goal while this map has no pointed goal or waypoint.
// A waypoint elsewhere may already be behind the player. Same-map exits never qualify.
inline bool story_exit_advances(int current_map,int related_map,std::span<const FilteredPoint> points,std::span<const GoalArea> areas) {
    if(current_map<0||related_map<0||related_map==current_map)return false;
    bool related=false;
    for(const auto& area:areas)if(in_area(area,related_map)) {
        if(!in_area(area,current_map))return true;
        related=true;
    }
    if(!related)return false;
    if(pointed_target_at(points,related_map,true)&&!pointed_target_at(points,current_map))return true;
    return audited_story_exit(current_map,related_map,points);
}
// The player is inside every filtered goal area, and nothing here or elsewhere is pointed.
inline bool destination_area_reached(int current_map,std::span<const FilteredPoint> points,std::span<const GoalArea> areas) {
    if(current_map<0||areas.empty())return false;
    for(const auto& area:areas)if(!in_area(area,current_map))return false;
    return std::none_of(points.begin(),points.end(),[](const auto& p){return p.map_kind==0&&p.object_target;});
}
inline std::string destination_area_reason(const std::string& title) {
    return title.empty()?
        tr("You are in the objective's destination area. The objective identifies no specific point or onward exit here."):
        trf("You are in the objective's destination area, {0}. The objective identifies no specific point or onward exit here.",{title});
}
inline bool point_authorized(const PointEvidence& value) {
    return (valid_object_key(value.key)||value.field_object_id>=0)&&value.source==DestinationSource::FilteredMainQuest&&
        value.current_objective&&value.current_main&&value.disclosed&&
        value.same_world&&value.local&&value.coordinates==CoordinateSpace::World&&
        std::isfinite(value.position.x)&&std::isfinite(value.position.y)&&std::isfinite(value.position.z);
}
inline std::optional<Vec3> unique_disclosed_point(std::span<const PointEvidence> candidates) {
    std::optional<Vec3> result;
    for(const auto& candidate:candidates)if(point_authorized(candidate)) {
        if(result)return {};
        result=candidate.position;
    }
    return result;
}
// Construct only after the current main-quest relation and filtered point have
// authorized this field candidate. Actor reads below this boundary may fail alone.
struct StoryCandidate {StoryDestination destination;ObjectKey key;bool direct{};};
inline void story_candidate_unavailable(StoryDestination& destination,std::string_view reason) {
    destination.position.reset();destination.actor_available=false;destination.interactable=false;
    destination.actor_id.clear();destination.actor_type.clear();
    destination.exit_source.clear();destination.exit_route_key.clear();
    destination.unavailable_reason=tr(reason);
}
template<class Capture> std::optional<StoryCandidate> capture_story_candidate(StoryCandidate candidate,Capture capture) {
    try {
        if(!capture(candidate.destination))return {};
    } catch(const std::exception& error) {
        story_candidate_unavailable(candidate.destination,error.what());
    }
    return candidate;
}
inline std::vector<StoryDestination> finalize_story_candidates(std::vector<StoryCandidate> candidates) {
    std::map<std::pair<unsigned,int>,std::vector<std::size_t>> by_key;
    for(std::size_t i=0;i<candidates.size();++i)if(candidates[i].direct)
        by_key[{candidates[i].key.type,candidates[i].key.id}].push_back(i);
    for(const auto& [key,indices]:by_key) {
        (void)key;if(indices.size()<2)continue;
        for(const auto i:indices)story_candidate_unavailable(candidates[i].destination,
            "More than one active target matches this story object. A route is unavailable.");
    }
    std::vector<StoryDestination> result;
    for(auto& candidate:candidates) {
        const auto& destination=candidate.destination;
        if(destination.exit&&destination.actor_available&&!destination.actor_id.empty()&&
           std::any_of(result.begin(),result.end(),[&](const auto& existing) {
               return existing.exit&&existing.actor_available&&existing.actor_id==destination.actor_id;
           }))continue;
        result.push_back(std::move(candidate.destination));
    }
    return result;
}
inline void fail_story_capture(StorySnapshot& snapshot) {
    snapshot.destinations.clear();
    snapshot.destination_epoch.clear();
    if(snapshot.ready&&!snapshot.world.empty()&&!snapshot.player.empty()&&
       !snapshot.objective.empty()&&!snapshot.objective_id.empty()) {
        snapshot.unavailable_reason=tr("The objective is available, but its local destinations could not be read safely.");
    } else {
        snapshot.ready=false;snapshot.objective.clear();snapshot.objective_id.clear();
        snapshot.unavailable_reason=tr("The native current story objective could not be read safely.");
    }
}
template<class Property> class ScopedNativeValue {
    Property* property_;
    void* value_;
public:
    ScopedNativeValue(Property& property,void* value):property_(&property),value_(value) {
        if(!value_)throw std::invalid_argument("Missing native output storage");
        property_->InitializeValue(value_);
    }
    ~ScopedNativeValue() {property_->DestroyValue(value_);}
    ScopedNativeValue(const ScopedNativeValue&)=delete;
    ScopedNativeValue& operator=(const ScopedNativeValue&)=delete;
};
}
