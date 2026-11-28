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
enum class BurningCastleLeg { Hall, UpperLadder, EastWing, KingChamber };
inline std::optional<BurningCastleLeg> burning_castle_leg(int map,int quest,std::optional<int> progress,
        int area,Vec3 position) {
    if(map!=3020||quest!=1001||(progress!=1100&&progress!=1110)||
       !std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))return {};
    if(area==1)return BurningCastleLeg::Hall;
    if(progress!=1110)return {};
    if(area==2||area==3) {
        if(position.x<-27200.&&position.z<1200.)return BurningCastleLeg::UpperLadder;
        if(position.x<-22000.)return BurningCastleLeg::EastWing;
    }
    if((area==3||area==5||area==8)&&position.x>-16000.&&position.x<-10000.&&position.z>850.)
        return BurningCastleLeg::KingChamber;
    return {};
}
enum class UltimaStoryLeg { Deck, Helm, Flight, Crater };
inline std::optional<UltimaStoryLeg> ultima_story_leg(
    std::optional<bool> cleared, std::optional<int> progress, int map,
    std::optional<unsigned char> movement) {
    // The Enterprise radar scene sets progress 24 to 10. The encounter
    // and reward advance it again, so the approach expires automatically.
    if(cleared!=true||progress!=10)return {};
    if(map==1000&&movement==2)return UltimaStoryLeg::Flight;
    if(movement!=1)return {};
    switch(map) {
    case 20002:return UltimaStoryLeg::Deck;
    case 20001:return UltimaStoryLeg::Helm;
    case 1000:return UltimaStoryLeg::Crater;
    default:return {};
    }
}

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
enum class KolobosStoryLeg { BridgePassage, Bridge, Ladder, LowerPassage, DeeperPassage,
    SideRoomReturn, InnerPassage, InnerRoomReturn };
inline bool kolobos_story_objective(int map,int quest,std::optional<int> progress,
        std::span<const FilteredPoint> points) {
    return map==3040&&quest==1001&&progress==1190&&std::any_of(points.begin(),points.end(),[](const auto& point) {
        return point.role==0&&point.map_kind==0&&point.map_id==3040;
    });
}
inline std::optional<KolobosStoryLeg> kolobos_story_leg(int map,int quest,std::optional<int> progress,
        int area,Vec3 position,std::span<const FilteredPoint> points,std::optional<bool> bridge_event) {
    if(!kolobos_story_objective(map,quest,progress,points)||!std::isfinite(position.x)||
       !std::isfinite(position.y)||!std::isfinite(position.z))return {};
    using Leg=KolobosStoryLeg;
    switch(area) {
    case 0:
        // One area ID covers both the entrance and the lower hall.
        if(position.z<-1000.)return Leg::DeeperPassage;
        [[fallthrough]];
    case 5:
        if(position.z>-500.)return Leg::BridgePassage;
        break;
    case 1:
    case 6:
        if(bridge_event==false)return Leg::Bridge;
        if(bridge_event==true)return Leg::LowerPassage;
        break;
    case 2:return Leg::InnerPassage;
    case 3:
        if(position.z<220.)return Leg::Ladder;
        break;
    case 4:return Leg::InnerRoomReturn;
    case 7:return Leg::SideRoomReturn;
    }
    return {};
}
inline bool grandport_from_felicitas(int map,int quest,std::optional<int> progress,
        std::span<const FilteredPoint> points) {
    return map==2040&&quest==1001&&progress==1205&&std::any_of(points.begin(),points.end(),[](const auto& point) {
        return point.role==0&&point.map_kind==0&&point.map_id==2030;
    });
}
enum class SirenStoryLeg { EntranceDoor, Ascent, SummitReturn, ReturnSavePoint, WorldExit };
inline bool siren_story_objective(int map,int quest,std::optional<int> progress,
        std::span<const FilteredPoint> points) {
    if(map!=3030||quest!=1001||(progress!=1220&&progress!=1230&&progress!=1240))return false;
    const int goal=progress==1240?2030:3030;
    return std::any_of(points.begin(),points.end(),[goal](const auto& point) {
        return point.role==0&&point.map_kind==0&&point.map_id==goal;
    });
}
inline bool thunderclap_crossing(int map,int quest,std::optional<int> progress,
        std::span<const FilteredPoint> points) {
    return (map==1000||map==3050)&&quest==1002&&progress==2040&&std::any_of(points.begin(),points.end(),[](const auto& point) {
        return point.role==0&&point.map_kind==0&&point.map_id==3050&&!point.object_target;
    });
}
inline std::optional<SirenStoryLeg> siren_story_leg(int map,int quest,std::optional<int> progress,int area,
        Vec3 position,std::span<const FilteredPoint> points,std::optional<bool> entrance_open) {
    if(!siren_story_objective(map,quest,progress,points)||area<0||area>7||
       !std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))return {};
    using Leg=SirenStoryLeg;
    if(progress==1240) {
        if(area==6)return Leg::SummitReturn;
        if(area==5)return Leg::ReturnSavePoint;
        return Leg::WorldExit;
    }
    if(area==0&&position.x<-5300.&&(progress==1220||entrance_open==false))return Leg::EntranceDoor;
    if(progress==1230&&area!=6)return Leg::Ascent;
    return {};
}
struct KolStoryLeg {
    enum class Kind { InnApproach, Innkeeper, InnExit, Stablehand, HouseExit };
    Kind kind;
    const FilteredPoint* contact{};
    int exit_id{-1},exit_point{-1};
};
inline std::optional<KolStoryLeg> kol_story_leg(std::optional<int> progress,int area,
        std::span<const FilteredPoint> points,std::optional<Vec3> position={}) {
    if(progress!=1140&&progress!=1150)return {};
    using Kind=KolStoryLeg::Kind;
    const auto village_goal=[](const FilteredPoint& point) {
        return point.role==0&&point.map_kind==0&&point.map_id==2020;
    };
    if(!std::any_of(points.begin(),points.end(),village_goal))return {};
    if(position&&std::abs(position->x+2500.)<=1280.&&std::abs(position->z)<=340.) {
        // The two houses share area identifiers with other parts of Kol.
        // The room's authored bounds distinguish its return door.
        if((area==2||area==3)&&std::abs(position->y-26000.)<=1280.)
            return KolStoryLeg{.kind=Kind::HouseExit,.exit_id=1,.exit_point=22};
        if((area==1||area==4)&&std::abs(position->y-31650.)<=1280.)
            return KolStoryLeg{.kind=Kind::HouseExit,.exit_id=2,.exit_point=20};
    }
    if(area!=0&&area!=3)return {};
    if(progress==1140&&area==0)return KolStoryLeg{Kind::InnApproach};
    const ObjectKey contact{1,progress==1140?10:11};
    const auto goal=std::find_if(points.begin(),points.end(),[&](const FilteredPoint& point) {
        return village_goal(point)&&point.object_target&&point.object==contact;
    });
    if(goal==points.end())return {};
    if(progress==1140)return KolStoryLeg{Kind::Innkeeper,&*goal};
    if(area==3)return KolStoryLeg{Kind::InnExit};
    return KolStoryLeg{Kind::Stablehand,&*goal};
}
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
// Native minimap markers (story-marker-presentation-20261101/PRESENTATION.md section 3).
// Edge-pinned entries are always drawn; other entries are clipped by the range
// rectangle after projection and rotation. The inscribed circle never admits an
// entry the game clips, so a conservative test discloses no undrawn marker.
inline bool marker_within_minimap_range(double planar_distance,double projection_scale,double zoom_scale,
        double range_x,double range_y) {
    if(!std::isfinite(planar_distance)||planar_distance<0||!std::isfinite(projection_scale)||projection_scale<=0||
       !std::isfinite(zoom_scale)||zoom_scale<=0||!std::isfinite(range_x)||!std::isfinite(range_y)||
       range_x<=0||range_y<=0)return false;
    return planar_distance*projection_scale*zoom_scale<=std::min(range_x,range_y)*.5;
}
// Shipped minimap appearance and icon bindings. Capture also checks that the game draws the entry.
// nullptr: not a default-displayed kind; "": a person marker labelled from the actor.
inline const char* marker_kind_label(std::wstring_view interact_tag) {
    constexpr std::wstring_view prefix=L"Field.Object.Interact.Type.";
    if(!interact_tag.starts_with(prefix))return nullptr;
    const auto kind=interact_tag.substr(prefix.size());
    if(kind==L"Shop.Vision")return "Vision shop";
    if(kind==L"Shop.Inn")return "Inn";
    if(kind==L"Shop.Tool")return "Item shop";
    if(kind==L"Shop.Weapon")return "Weapon shop";
    if(kind==L"Shop.Armor")return "Armor shop";
    if(kind==L"Shop.Accessory")return "Accessory shop";
    if(kind==L"Shop.Chocobo")return "Chocobo shop";
    if(kind==L"Shop.DLC")return "Delivery shop";
    if(kind==L"Shop.Bank")return "Bank";
    if(kind==L"Shop.Fortunetelling")return "Fortune teller";
    if(kind==L"Shop.Music")return "Music shop";
    if(kind==L"Shop.Race")return "Chocobo race";
    if(kind==L"Save")return "Save point";
    if(kind==L"MythicalBeast")return "Esper";
    if(kind==L"Colosseum")return "Colosseum";
    if(kind==L"Landmark")return "Landmark";
    if(kind==L"Talk")return "";
    return nullptr;
}
// Stable marker identity: the registry id, typed map key and, for a tracked
// side quest, the quest id. Registry ids are stable while the area is loaded.
struct MarkerIdentity { bool side_quest{}; int quest{-1}; int object{-1}; std::uint8_t type{}; int key{-1}; };
inline std::string marker_identity_text(const MarkerIdentity& m) {
    return (m.side_quest?"side-quest:"+std::to_string(m.quest)+":":std::string("marker:"))+
        std::to_string(m.object)+":"+std::to_string(m.type)+":"+std::to_string(m.key);
}
// The registry id inside a marker identity, so a refresh can read one entry.
inline std::optional<int> marker_identity_object(std::string_view id) {
    std::size_t skip=0;
    if(id.starts_with("marker:"))skip=1;
    else if(id.starts_with("side-quest:"))skip=2;
    else return {};
    std::size_t start=0;
    for(std::size_t i=0;i<skip;++i) {
        const auto colon=id.find(':',start);
        if(colon==std::string_view::npos)return {};
        start=colon+1;
    }
    const auto end=id.find(':',start);
    const auto digits=id.substr(start,end==std::string_view::npos?std::string_view::npos:end-start);
    if(digits.empty()||digits.size()>9||!std::all_of(digits.begin(),digits.end(),[](char c){return c>='0'&&c<='9';}))return {};
    int value=0;
    for(const char c:digits)value=value*10+(c-'0');
    return value;
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
