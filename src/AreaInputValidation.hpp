#pragma once
#include "resonance/Localization.hpp"
// Private area-entity availability decisions; no engine objects or retained pointers.
// Exact-build evidence: area-navigation-fix-20261103/AREA-ENTITY-AUDIT.md and
// EXIT-AVAILABILITY.md.
#include "resonance/AreaInput.hpp"
#include "resonance/Core.hpp"
#include "resonance/Overlap.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
namespace resonance::area_input_detail {
enum class AreaKind : std::uint8_t {
    Person, TreasureChest, FieldItem, SavePoint, Door, Ladder, MovableObject, Switch, Mailbox, Exit,
    TriggerDoor, ElevatorCall, Elevator, Book, Interaction
};
// native_size is the exact-build native class PropertiesSize checked before any read.
// owner begins a generic read-failure sentence; it never names the entity.
struct AreaKindInfo { AreaKind kind; const wchar_t* type; const char* label; std::size_t category; int native_size; const char* owner; };
// Categories match the reader's All/People/Objects/Exits, doors and ladders/Save points/Story order.
inline constexpr std::array<AreaKindInfo,15> area_kinds{{
    {AreaKind::Person,L"CPP_MapUnit_NPC","Local resident",1,2912,"A person's"},
    {AreaKind::TreasureChest,L"CPP_MapGimmick_TreasureBox","Treasure chest",2,1272,"A treasure chest's"},
    {AreaKind::FieldItem,L"CPP_MapGimmick_FieldItem","Ground pickup",2,1064,"A field item's"},
    {AreaKind::SavePoint,L"CPP_MapGimmick_SavePoint","Save point",4,1024,"A save point's"},
    {AreaKind::Door,L"CPP_MapGimmick_Object_DoorBase","Passage door",3,1192,"A door's"},
    {AreaKind::Ladder,L"CPP_MapGimmickLadder","Ladder",3,1088,"A ladder's"},
    {AreaKind::MovableObject,L"CPP_MapGimmick_PushObject","Pushable obstacle",2,1072,"A movable object's"},
    {AreaKind::Switch,L"CPP_MapGimmick_Switch","Switch",2,1464,"A switch's"},
    {AreaKind::Mailbox,L"CPP_MapGimmick_Object_DLC","Mailbox by the inn",2,1032,"The mailbox's"},
    {AreaKind::Exit,L"CPP_MapTransitionTrigger","Connecting passage",3,1056,"An exit's"},
    {AreaKind::TriggerDoor,L"CPP_MapGimmick_Trigger_DoorBase","Automatic doorway",3,1064,"A door's"},
    {AreaKind::ElevatorCall,L"CPP_MapGimmick_CallSwitch","Elevator call button",2,1312,"An elevator button's"},
    {AreaKind::Elevator,L"CPP_MapGimmick_Elevator_Interact","Elevator",3,1512,"An elevator's"},
    {AreaKind::Book,L"CPP_MapGimmick_BlockingBase","Book",2,1016,"A book's"},
    {AreaKind::Interaction,L"CPP_MapGimmick_InteractEventTrigger","Quest marker",2,984,"A quest marker's"}}};
inline constexpr bool gimmick_kind(AreaKind kind) {return kind!=AreaKind::Person&&kind!=AreaKind::Exit;}
inline constexpr bool area_kinds_ordered() {
    for(std::size_t i=0;i<area_kinds.size();++i)if(static_cast<std::size_t>(area_kinds[i].kind)!=i)return false;
    return true;
}
static_assert(area_kinds_ordered());
inline const AreaKindInfo& area_kind_info(AreaKind kind) {
    return area_kinds[static_cast<std::size_t>(kind)];
}
// The fallback identifies the type or its verified visible state.
inline const char* area_label(AreaKind kind,std::optional<bool> state={}) {
    if(kind==AreaKind::TreasureChest&&state)return *state?"Open treasure chest":"Closed treasure chest";
    if(kind==AreaKind::Switch&&state&&*state)return "Pressed switch";
    return area_kind_info(kind).label;
}
inline const char* person_service_label(std::wstring_view icon_tag,bool presented) {
    if(presented) {
        if(icon_tag==L"Field.Object.Interact.Type.Shop.Inn")return "Inn attendant";
        if(icon_tag==L"Field.Object.Interact.Type.Shop.Tool")return "Item shop attendant";
        if(icon_tag==L"Field.Object.Interact.Type.Shop.Weapon")return "Weapon shop attendant";
        if(icon_tag==L"Field.Object.Interact.Type.Shop.Armor")return "Armor shop attendant";
        if(icon_tag==L"Field.Object.Interact.Type.Shop.Accessory")return "Accessory shop attendant";
        if(icon_tag==L"Field.Object.Interact.Type.Shop.DLC")return "Delivery attendant";
        if(icon_tag==L"Field.Object.Interact.Type.Shop.Vision")return "Vision shop attendant";
    }
    return "";
}
// The native minimap and nameplate use these same public facility roles.
inline const char* person_service_label(std::optional<std::uint8_t> interaction) {
    switch(interaction.value_or(0)) {
    case 17:return "Item shop attendant";
    case 18:return "Weapon shop attendant";
    case 19:return "Armor shop attendant";
    case 20:return "Accessory shop attendant";
    case 21:return "Inn attendant";
    case 27:return "Delivery attendant";
    case 32:return "Vision shop attendant";
    default:return "";
    }
}
inline bool positive_ui_alpha(std::optional<float> alpha) {
    return alpha&&std::isfinite(*alpha)&&*alpha>0.0f;
}
inline bool widget_presented(std::optional<std::uint8_t> visibility,std::optional<float> opacity) {
    return visibility&&(*visibility==0||*visibility==3||*visibility==4)&&positive_ui_alpha(opacity);
}
// AActor bitfields share bytes with unrelated flags; only the exact-build mask counts.
inline constexpr std::uint8_t hidden_mask=0x80,destroying_mask=0x02;
inline bool masked(std::uint8_t byte,std::uint8_t mask) {return (byte&mask)!=0;}
// Native full-byte bool: anything other than 0/1 is not a trustworthy fact.
inline std::optional<bool> native_bool(std::optional<std::uint8_t> byte) {
    if(!byte||*byte>1)return std::nullopt;
    return *byte==1;
}
inline bool valid_array(const void* data,int count,int capacity,int limit) {
    return limit>=0&&count>=0&&count<=limit&&capacity>=count&&capacity<=65536&&
        ((capacity==0&&data==nullptr)||(capacity>0&&data!=nullptr));
}

// Inactive is a verified native state (removed, foreign, hidden, invisible, culled,
// conditionally closed). Unknown is an unreadable fact: never listed, never reported
// as inactive, and always carries a reason fragment completed by issue_text().
enum class Availability : std::uint8_t { Available, Inactive, Unknown };
enum class Rule : std::uint8_t { None, Removed, Foreign, Destroying, Hidden, Invisible, Tick, Conditional, Scripted, Collision, NotExit, Unknown };
inline constexpr std::size_t rule_count=12;
inline constexpr std::size_t rule_index(Rule rule) {return static_cast<std::size_t>(rule);}
inline constexpr std::array<const char*,rule_count> rule_names{
    "none","removed","foreign","destroying","hidden","invisible","tick","conditional","scripted","collision","not_exit","unknown"};
struct Verdict { Availability state{Availability::Unknown}; Rule rule{Rule::Unknown}; const char* issue{"state could not be read"}; };
inline Verdict available() {return {Availability::Available,Rule::None,nullptr};}
inline Verdict inactive(Rule rule) {return {Availability::Inactive,rule,nullptr};}
inline Verdict unknown(const char* issue) {return {Availability::Unknown,Rule::Unknown,issue};}
inline std::string issue_text(const char* owner,const char* detail) {
    return trf("{0} {1}.",{tr(owner),tr(detail?detail:"state could not be read")});
}
// ok: the requirement holds; nullopt: it could not be read.
inline Verdict check(std::optional<bool> ok,Rule rule,const char* issue) {
    if(!ok)return unknown(issue);
    return *ok?available():inactive(rule);
}
inline std::optional<bool> negate(std::optional<bool> value) {
    return value?std::optional<bool>(!*value):std::nullopt;
}
// Any verified failure excludes the entity even when another fact is unreadable;
// otherwise the first unreadable fact makes it unknown.
inline Verdict combine(std::initializer_list<Verdict> parts) {
    const Verdict* first_unknown=nullptr;
    for(const auto& part:parts) {
        if(part.state==Availability::Inactive)return part;
        if(part.state==Availability::Unknown&&!first_unknown)first_unknown=&part;
    }
    return first_unknown?*first_unknown:available();
}

// same_world false: another or unloaded world; nullopt: the outer chain is unreadable.
struct ActorFacts { bool live{}; std::optional<bool> same_world,destroying,hidden; };
inline bool removed_or_foreign(const ActorFacts& f) {return !f.live||f.same_world==false||f.destroying==true;}
inline Verdict presence_verdict(const ActorFacts& f) {
    if(!f.live)return inactive(Rule::Removed);
    return combine({check(f.same_world,Rule::Foreign,"area could not be verified"),
        check(negate(f.destroying),Rule::Destroying,"removal state could not be read")});
}
// Visible gimmicks and other registered FieldObject actors: SyncVisible already
// applied VisibleCondition to bHidden, so the condition text is never evaluated here.
inline Verdict actor_verdict(const ActorFacts& f) {
    if(!f.live)return inactive(Rule::Removed);
    return combine({presence_verdict(f),check(negate(f.hidden),Rule::Hidden,"visibility could not be read")});
}
inline bool field_object_available(const ActorFacts& f) {return actor_verdict(f).state==Availability::Available;}
inline Verdict mailbox_verdict(const ActorFacts& f,std::optional<std::uint8_t> mode,
        std::optional<std::uint8_t> interaction) {
    const auto collision=mode&&*mode<=5?std::optional<bool>{query_collision(*mode)}:std::nullopt;
    const auto enabled=interaction?std::optional<bool>{*interaction==27}:std::nullopt; // DLCShop
    return combine({actor_verdict(f),check(enabled,Rule::Conditional,"interaction state could not be read"),
        check(collision,Rule::Collision,"interaction box could not be read")});
}
inline Verdict interactive_prop_verdict(std::optional<bool> visible,std::optional<std::uint8_t> state,
        std::optional<std::uint8_t> interaction,std::optional<bool> queryable,std::uint8_t expected_interaction=4) {
    const auto active=state?std::optional<bool>(*state==0):std::nullopt;
    const auto check_prompt=interaction?std::optional<bool>(*interaction==expected_interaction):std::nullopt;
    return combine({check(visible,Rule::Invisible,"mesh visibility could not be read"),
        check(active,Rule::Conditional,"interaction state could not be read"),
        check(check_prompt,Rule::Conditional,"interaction prompt could not be read"),
        check(queryable,Rule::Collision,"interaction box could not be read")});
}
// OnOverlapAreaBox re-enables tick only for current-area NPCs with m_IsVisible; it
// never changes bHidden, so a loaded other-area NPC is excluded by its tick state.
struct PersonFacts { ActorFacts actor; std::optional<bool> unit_visible,tick_enabled; };
inline Verdict person_verdict(const PersonFacts& f) {
    if(!f.actor.live)return inactive(Rule::Removed);
    return combine({actor_verdict(f.actor),check(f.unit_visible,Rule::Invisible,"visibility could not be read"),
        check(f.tick_enabled,Rule::Tick,"current-area state could not be read")});
}
inline bool person_available(const PersonFacts& f) {return person_verdict(f).state==Availability::Available;}

struct TransitionDefaults {
    std::optional<bool> use_conditions,same_map,event_only;
    std::optional<int> map_id,current_map,point_id;
};
struct TransitionRow {std::optional<bool> event_only;std::optional<int> map_id,point_id;};
struct EffectiveTransition {
    bool event_only{};
    int map_id{-1};
    std::optional<int> point_id;
    std::optional<std::size_t> condition_row;
};
inline constexpr std::string_view exit_key_prefix="|exit:";
inline std::string exit_route_key(const EffectiveTransition& transition) {
    return std::string(exit_key_prefix)+std::to_string(transition.event_only)+":"+std::to_string(transition.map_id)+":"+
        (transition.point_id?std::to_string(*transition.point_id):"unknown")+":"+
        (transition.condition_row?std::to_string(*transition.condition_row):"default");
}
// Available: the native resolver would select transition. Inactive: no row passes, or
// the selected ordinary row has no destination. Unknown: an input could not be read.
struct TransitionResult { Availability state{Availability::Unknown}; EffectiveTransition transition; const char* issue{"current availability could not be read"}; };
inline TransitionResult unread_transition(const char* issue) {return {Availability::Unknown,{},issue};}
// Mirrors resolver 0x1450E3C20 (EXIT-AVAILABILITY.md): default mode or same-map uses the
// trigger defaults; conditional mode takes the FIRST passing row in order and never
// skips an unreadable row or falls back to defaults.
template<class Evaluate> TransitionResult select_transition(const TransitionDefaults& defaults,
        std::span<const TransitionRow> rows,Evaluate evaluate) {
    if(!defaults.use_conditions||!defaults.same_map)return unread_transition("current availability could not be read");
    if(rows.size()>512)return unread_transition("conditional destination list is unreadable");
    const auto choice=[](std::optional<bool> event,std::optional<int> map,std::optional<int> point,
            std::optional<std::size_t> row={})->TransitionResult {
        if(!event||!map)return unread_transition("current destination could not be read");
        if(!*event&&*map<0)return {Availability::Inactive,{},nullptr};
        return {Availability::Available,EffectiveTransition{*event,*map,point,row},nullptr};
    };
    if(!*defaults.use_conditions||*defaults.same_map)
        return choice(defaults.event_only,*defaults.same_map&&defaults.event_only==false?defaults.current_map:defaults.map_id,
            defaults.point_id);
    for(std::size_t i=0;i<rows.size();++i) {
        const auto matches=evaluate(i);
        if(!matches)return unread_transition("current condition could not be read");
        if(*matches)return choice(rows[i].event_only,rows[i].map_id,rows[i].point_id,i);
    }
    return {Availability::Inactive,{},nullptr};
}
// overlap nullopt: components, owners, channels or enum values could not be read.
struct ExitFacts { ActorFacts actor; TransitionResult transition; std::optional<OverlapEvidence> overlap; };
// A trigger box is deliberately hidden/inactive, so bHidden, render and component
// activity are not exit facts. Classification happens after native selection; an
// event-only (scripted) selection is an ordinary exit only when allow_scripted.
inline Verdict exit_verdict(const ExitFacts& f,bool allow_scripted=false) {
    if(!f.actor.live)return inactive(Rule::Removed);
    Verdict transition=unknown(f.transition.issue?f.transition.issue:"current availability could not be read");
    if(f.transition.state==Availability::Inactive)transition=inactive(Rule::Conditional);
    else if(f.transition.state==Availability::Available)
        transition=f.transition.transition.event_only&&!allow_scripted?inactive(Rule::Scripted):available();
    const auto collision=f.overlap?(enabled_overlap(*f.overlap)?available():inactive(Rule::Collision)):unknown("collision could not be read");
    return combine({presence_verdict(f.actor),transition,collision});
}
inline bool exit_available(const ExitFacts& f,bool allow_scripted=false) {return exit_verdict(f,allow_scripted).state==Availability::Available;}
struct CastleLegSpec {int id,point_id;bool doorway;const char* label;};
inline std::optional<CastleLegSpec> castle_leg_spec(int map,int area,Vec3 position) {
    if(map!=2014||!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))return {};
    if(area==5&&position.z>=1200&&position.z<=1400) {
        // Three upper rooms are separate LocalMap components. These ranges
        // include their doorway wall and one capsule radius past the baked edges.
        struct Room {int door;double min_x,max_x,min_y,max_y;};
        constexpr std::array<Room,3> rooms{{
            {16,-13180,-10876,4876,6326},
            {17,-12379,-10810,2476,3926},
            {18,-12379,-10810,-3926,-2476}
        }};
        for(const auto& room:rooms) {
            if(position.x>=room.min_x&&position.x<=room.max_x&&position.y>=room.min_y&&position.y<=room.max_y)
                return CastleLegSpec{room.door,-1,true,"Door to the hall"};
        }
    }
    if(area==4||area==5)return CastleLegSpec{8,2,false,"Passage to the main hall"};
    if(area==2||area==3)return CastleLegSpec{6,8,false,"Passage to the castle entrance"};
    if(area==1)return CastleLegSpec{2,-1,false,"Castle exit"};
    return {};
}
struct LinkedExitMatch {std::optional<std::size_t> index;const char* issue{};};
// Town_01Gra_20_GD and the matching transition points on map 2000 and map 1000.
struct MitraPassage {
    int exit,map,point;
    const char* label;
};
inline constexpr MitraPassage mitra_passages[]{
    {0,1000,2,"World map via the far town gate"},
    {1,1000,1,"World map via the town entrance"},
    {3,2000,130,"Door to Rain's basement"},
    {4,2000,131,"Door from the basement to Rain's house"},
    {5,2000,28,"Door from Rain's house to the street"},
    {6,2000,23,"Door from the item shop to the street"},
    {7,2000,127,"Door to the weapon and armor shop"},
    {8,2000,121,"Side entrance to the inn"},
    {9,2000,21,"Side door from the inn to the street"},
    {10,2000,123,"Door to the item shop"},
    {11,2000,122,"Door to the house by the item shop"},
    {12,2000,22,"Door from the house by the item shop to the street"},
    {13,2000,24,"Front door from the back-lane house to the street"},
    {14,2000,25,"Door from the courtyard house to the street"},
    {15,2000,26,"Door to the chocobo yard"},
    {16,2000,124,"Front entrance to the back-lane house"},
    {17,2000,20,"Front door from the inn to the street"},
    {18,2000,129,"Back entrance to the back-lane house"},
    {19,2000,125,"Front entrance to the courtyard house"},
    {20,2000,126,"Door from the chocobo yard into the house"},
    {21,2000,27,"Door from the weapon and armor shop to the street"},
    {22,2000,120,"Front entrance to the inn"},
    {23,2000,29,"Door from the house to the back lane"},
    {24,2000,128,"Entrance to Rain's house"},
};
inline const MitraPassage* mitra_passage(int exit,int map,int point) {
    for(const auto& passage:mitra_passages)
        if(passage.exit==exit&&passage.map==map&&passage.point==point)return &passage;
    return nullptr;
}
inline const char* shrine_passage(int exit,int point) {
    if(exit==1&&point==1)return "Entrance to the Shrine side chamber";
    if(exit==2&&point==2)return "Return to the Shrine main passage";
    return nullptr;
}
struct MitraDoor {std::wstring_view suffix;const char* label;bool requires_route{};};
inline constexpr MitraDoor mitra_doors[]{
    {L"0","Front door of the inn"},{L"1","Door of Rain's house"},{L"2","Basement door inside Rain's house"},
    {L"3","Front door of the weapon and armor shop"},{L"4","Front door of the item shop"},
    {L"5","Front door of the house by the item shop"},{L"6","Front door of the courtyard house",true},
    {L"7","Door beside the chocobo yard",true},{L"8","Inn side door from inside"},
    {L"9","Front door of the house by the back lane",true},{L"10","Back door beside the lane",true}
};
// Next authored exit from each walking area on the way back to the world map.
// The main street and the terrace return through the town entrance.
inline std::optional<int> mitra_story_exit_id(int area) {
    constexpr std::array exits{-1,1,17,21,6,5,12,13,14,4,1};
    if(area<1||area>=static_cast<int>(exits.size()))return {};
    return exits[area];
}
inline LinkedExitMatch unique_linked_exit(std::span<const ExitFacts> exits,int destination_map) {
    std::optional<std::size_t> index;
    for(std::size_t i=0;i<exits.size();++i) {
        const auto state=exit_verdict(exits[i]).state;
        if(state==Availability::Unknown)return {{},"A linked exit's current availability could not be read."};
        if(state!=Availability::Available||exits[i].transition.transition.map_id!=destination_map)continue;
        if(index)return {{},"More than one active exit leads to the next Battlefield section."};
        index=i;
    }
    if(!index)return {{},"No active exit leads to the next Battlefield section."};
    return {index};
}
// linked false: no live TransitionTrigger; nullopt: the link could not be read.
struct LandmarkFacts { ActorFacts landmark; std::optional<bool> linked; ExitFacts transition; };
inline Verdict link_verdict(std::optional<bool> linked,const ExitFacts& transition,bool allow_scripted) {
    if(!linked)return unknown("linked exit could not be read");
    return *linked?exit_verdict(transition,allow_scripted):inactive(Rule::NotExit);
}
inline Verdict landmark_verdict(const LandmarkFacts& f,bool allow_scripted=false) {
    if(!f.landmark.live)return inactive(Rule::Removed);
    return combine({actor_verdict(f.landmark),link_verdict(f.linked,f.transition,allow_scripted)});
}
inline bool landmark_available(const LandmarkFacts& f,bool allow_scripted=false) {return landmark_verdict(f,allow_scripted).state==Availability::Available;}

// Story destinations (caller-established registry membership and quest relation).
// flagged: CPP_Unit or CPP_MapUnit_Object, whose own native m_IsVisible applies.
// Ordinary NPC area tick culling is deliberately not a Story fact.
struct StoryActorFacts { ActorFacts actor; bool flagged{}; std::optional<bool> visible; bool positioned{}; };
inline Verdict story_point_verdict(const StoryActorFacts& f) {
    if(!f.actor.live)return inactive(Rule::Removed);
    return combine({actor_verdict(f.actor),f.flagged?check(f.visible,Rule::Invisible,"visibility could not be read"):available(),
        f.positioned?available():unknown("position could not be read")});
}
// A claimed exit: the landmark point itself plus its linked native exit, where the
// current quest relation may authorize an enabled scripted selection.
inline Verdict story_landmark_verdict(const StoryActorFacts& point,std::optional<bool> linked,const ExitFacts& transition) {
    if(!point.actor.live)return inactive(Rule::Removed);
    return combine({story_point_verdict(point),link_verdict(linked,transition,true)});
}
// A claimed trigger exit is a hidden-in-game volume: native exit facts, not bHidden.
inline Verdict story_trigger_verdict(const ExitFacts& trigger,bool positioned) {
    return combine({exit_verdict(trigger,true),positioned?available():unknown("position could not be read")});
}

// One capture's report and diagnostics. Whole-capture failures and per-entity issues
// are separate; an entity issue counts in All and its own category only.
struct ScanTally {
    AreaCaptureReport report;
    std::array<std::size_t,rule_count> rejected{};
    std::size_t listed_count{};
    void fail(std::string reason) {report=AreaCaptureReport{};report.unavailable_reason=std::move(reason);}
    void unreadable(AreaKind kind,const char* detail) {
        const auto& info=area_kind_info(kind);
        const auto text=issue_text(info.owner,detail);
        const auto count=[&](std::size_t index) {
            ++report.unreadable[index];
            if(report.first_issue[index].empty())report.first_issue[index]=text;
        };
        count(0);
        if(info.category!=0&&info.category<report.unreadable.size())count(info.category);
        ++rejected[rule_index(Rule::Unknown)];
    }
    void reject(Rule rule) {++rejected[rule_index(rule)];}
    // True only for an available entity; inactive and unknown verdicts are tallied.
    bool admit(const Verdict& verdict,AreaKind kind) {
        if(verdict.state==Availability::Available)return true;
        if(verdict.state==Availability::Unknown)unreadable(kind,verdict.issue);
        else reject(verdict.rule);
        return false;
    }
    void listed() {++listed_count;}
};
inline std::string tally_summary(const ScanTally& t) {
    std::string result="rejected";
    for(std::size_t i=1;i<rule_count;++i)result+=" "+std::string(rule_names[i])+"="+std::to_string(t.rejected[i]);
    const auto& u=t.report.unreadable;
    result+=" unreadable=all:"+std::to_string(u[0])+",people:"+std::to_string(u[1])+",objects:"+std::to_string(u[2])+
        ",exits:"+std::to_string(u[3])+",save:"+std::to_string(u[4]);
    if(!t.report.first_issue[0].empty())result+=" first_issue=\""+t.report.first_issue[0]+"\"";
    return result;
}

// Registry lists may repeat an actor (main and attached gimmick lists). First
// occurrence wins; an oversized capture fails closed rather than truncating.
template<class T> std::optional<std::vector<T*>> unique_members(std::initializer_list<std::span<T* const>> lists,std::size_t limit) {
    std::vector<T*> result;std::unordered_set<T*> seen;
    for(auto list:lists)for(auto* item:list) {
        if(!item||!seen.insert(item).second)continue;
        if(result.size()==limit)return std::nullopt;
        result.push_back(item);
    }
    return result;
}
inline bool finite_position(Vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline double planar_distance(Vec3 from,Vec3 to) {return std::hypot(to.x-from.x,to.y-from.y);}
template<class Target> void order_targets(std::vector<Target>& targets) {
    std::sort(targets.begin(),targets.end(),[](const auto& a,const auto& b) {
        return a.distance==b.distance?a.snapshot.id<b.snapshot.id:a.distance<b.distance;});
}
// A selected target is only ever the current capture's entry; a removed, despawned
// or disabled actor yields nothing and no last-seen position is substituted.
template<class Target> std::optional<Target> current_target(std::span<const Target> current,std::string_view id) {
    if(id.empty())return std::nullopt;
    for(const auto& target:current)if(target.snapshot.id==id)return target;
    return std::nullopt;
}
}
