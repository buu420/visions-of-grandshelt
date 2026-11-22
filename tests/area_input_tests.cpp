#include "../src/AreaInputValidation.hpp"
#include "../src/FieldLabels.hpp"
#include "../src/MapTitles.hpp"
#include "../src/TownInteractions.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace resonance;
using namespace resonance::area_input_detail;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void town_interactions_match_placements() {
    int boards=0,streetcars=0,warps=0;
    for(const auto& item:town_interactions::placements) {
        require(town_interactions::find(item.actor_path,item.id)==&item,
            "each supported interaction resolves to its unique native placement");
        require(town_interactions::find(item.actor_path,item.id+1000)==nullptr,
            "the actor path alone does not prove identity");
        if(item.interaction==7) {
            ++boards;
            require(item.category==2&&item.event_id.empty(),"boards use Objects and the current native notice");
        } else {
            require(item.category==3&&!item.event_id.empty(),"transit uses Exits and requires its native chooser");
            if(item.map==2130)++warps;
            else {
                ++streetcars;
                require(item.interaction==(item.map==2112?4:34),"Capital Market inherits the Check prompt");
            }
        }
    }
    require(boards==8&&streetcars==5&&warps==4,"the verified town placements are covered");
}
struct Candidate { TargetSnapshot snapshot; double distance{}; Vec3 position; };
Candidate candidate(const char* id,Vec3 origin,Vec3 position) {
    return {{id,"Person","",true,false},planar_distance(origin,position),position};
}
ActorFacts present() {return {true,true,false,false};}
PersonFacts active_person() {return {present(),true,true};}
ExitFacts open_exit() {
    ExitFacts f;f.actor=present();f.transition=TransitionResult{Availability::Available,{false,10040},nullptr};
    f.overlap=OverlapEvidence{true,true,true,true,1,3,1,2};
    return f;
}
// A registered Story CPP_Unit/CPP_MapUnit_Object with its own native m_IsVisible.
StoryActorFacts story_unit(bool visible=true) {return {present(),true,visible,true};}
bool verdict_is(const Verdict& v,Availability state,Rule rule=Rule::None) {
    return v.state==state&&(state==Availability::Unknown?v.issue!=nullptr:v.rule==rule);
}
TransitionResult unread(const char* issue) {return {Availability::Unknown,{},issue};}
void native_transition_resolution() {
    TransitionDefaults d{false,false,false,10041,10040};
    const std::array<TransitionRow,3> rows{{{false,20000},{false,20001},{true,-1}}};
    const auto no_query=[](std::size_t)->std::optional<bool>{throw std::runtime_error("default and same-map modes must not inspect conditional rows");};
    auto selected=select_transition(d,rows,no_query);
    require(selected.state==Availability::Available&&!selected.transition.event_only&&selected.transition.map_id==10041,
        "ordinary exits use the native default destination when conditional mode is off");
    d.use_conditions=true;d.same_map=true;d.map_id=-1;
    selected=select_transition(d,rows,no_query);
    require(selected.state==Availability::Available&&selected.transition.map_id==10040,
        "same-map transitions bypass conditions and use the current map, never the cached default");
    d.same_map=false;d.event_only=true;d.map_id=10041;
    std::vector<std::size_t> calls;
    selected=select_transition(d,rows,[&](std::size_t i)->std::optional<bool>{calls.push_back(i);return i!=0;});
    require(selected.state==Availability::Available&&!selected.transition.event_only&&selected.transition.map_id==20001&&
        selected.transition.condition_row==1&&calls==std::vector<std::size_t>({0,1}),
        "conditional exits retain the first passing native row as the selected route identity");
    auto alternate=selected.transition;
    alternate.condition_row=2;
    require(exit_route_key(selected.transition)!=exit_route_key(alternate),
        "a different conditional branch withdraws the old selection even when the map and point match");
    alternate=selected.transition;alternate.point_id=5;
    require(exit_route_key(selected.transition)!=exit_route_key(alternate),
        "a changed arrival point withdraws the old selection");
    std::array<TransitionRow,2> invalid_first{{{false,-1},{false,20001}}};
    calls.clear();selected=select_transition(d,invalid_first,[&](std::size_t i)->std::optional<bool>{calls.push_back(i);return true;});
    require(selected.state==Availability::Inactive&&calls==std::vector<std::size_t>({0}),
        "an invalid first match is natively unavailable and cannot be skipped to a later valid map");
    calls.clear();selected=select_transition(d,rows,[&](std::size_t i)->std::optional<bool>{calls.push_back(i);return {};});
    require(selected.state==Availability::Unknown&&selected.issue&&calls==std::vector<std::size_t>({0}),
        "an unreadable earlier condition is reported unknown, never treated as false or inactive");
    selected=select_transition(d,rows,[](std::size_t)->std::optional<bool>{return false;});
    require(selected.state==Availability::Inactive,"no passing row is natively unavailable without a cached/default fallback");
    selected=select_transition(d,rows,[](std::size_t i)->std::optional<bool>{return i==2;});
    require(selected.state==Availability::Available&&selected.transition.event_only&&selected.transition.map_id==-1,
        "a native scripted transition can be enabled without a map destination");
    const TransitionDefaults unknown_mode{std::nullopt,false,false,10041,10040};
    require(select_transition(unknown_mode,rows,no_query).state==Availability::Unknown,"unreadable trigger defaults are unknown, not inactive");
    d.same_map=true;d.event_only=false;d.current_map.reset();
    require(select_transition(d,rows,no_query).state==Availability::Unknown,"a same-map exit whose current map cannot be read is unknown");
}
void offscreen_and_prior_sight_independent() {
    // The predicates take only native registry/state facts: an NPC behind the camera,
    // occluded or never seen has exactly the same facts as one on screen.
    require(person_available(active_person()),"an active current-area NPC is available without camera, render or sight facts");
    require(field_object_available(present()),"a visible registered gimmick is available without camera, render or sight facts");
    const Vec3 origin{0,0,0};
    const std::array first{candidate("behind",origin,{-5000,0,0}),candidate("ahead",origin,{10,0,0})};
    auto selected=current_target<Candidate>(first,"behind");
    require(selected&&selected->distance==5000,"the first capture selects an off-camera target with no earlier sighting");
}
void live_position_movement() {
    const Vec3 origin{0,0,0};
    const std::array before{candidate("npc",origin,{100,0,0})};
    const std::array after{candidate("npc",origin,{300,400,0})};
    require(current_target<Candidate>(before,"npc")->position.x==100,"first refresh reports the captured position");
    auto moved=current_target<Candidate>(after,"npc");
    require(moved&&moved->position.x==300&&moved->position.y==400&&moved->distance==500,
        "a later refresh must report the live moved position and distance, never the earlier point");
}
void area_unload_flag_hide_and_tick() {
    auto other_area=active_person();other_area.tick_enabled=false;
    require(verdict_is(person_verdict(other_area),Availability::Inactive,Rule::Tick),"OnOverlapAreaBox tick-disabled other-area NPCs stay loaded but are verified inactive");
    auto unit_hidden=active_person();unit_hidden.unit_visible=false;
    require(verdict_is(person_verdict(unit_hidden),Availability::Inactive,Rule::Invisible),"m_IsVisible false keeps an NPC unavailable even with tick enabled");
    auto flag_hidden=active_person();flag_hidden.actor.hidden=true;
    require(verdict_is(person_verdict(flag_hidden),Availability::Inactive,Rule::Hidden),"SyncVisible/flag-hidden NPCs are unavailable");
    auto hidden_gimmick=present();hidden_gimmick.hidden=true;
    require(!field_object_available(hidden_gimmick),"a hidden gimmick is not listed");
    struct Case {int change;Availability state;Rule rule;};
    for(const auto& [change,state,rule]:std::array<Case,4>{{{0,Availability::Inactive,Rule::Removed},
        {1,Availability::Inactive,Rule::Foreign},{2,Availability::Inactive,Rule::Destroying},{3,Availability::Unknown,Rule::Unknown}}}) {
        auto facts=active_person();
        if(change==0)facts.actor.live=false;
        if(change==1)facts.actor.same_world=false;
        if(change==2)facts.actor.destroying=true;
        if(change==3){facts.actor.hidden.reset();facts.tick_enabled.reset();}
        require(!person_available(facts)&&verdict_is(person_verdict(facts),state,rule),
            "despawned, unloaded-world and destroying actors are inactive; unread state is unknown, and both fail closed");
    }
    const Vec3 origin{0,0,0};
    const std::array removed{candidate("other",origin,{1,1,0})};
    require(!current_target<Candidate>(removed,"npc"),"a removed or disabled target yields nothing; no last-seen point is substituted");
    require(!current_target<Candidate>(removed,""),"an empty selection never matches");
}
void verified_negative_outranks_unknown() {
    ActorFacts f=present();f.same_world.reset();f.hidden=true;
    require(verdict_is(actor_verdict(f),Availability::Inactive,Rule::Hidden),"a verified hidden flag excludes an actor even when another fact is unreadable");
    auto person=active_person();person.unit_visible.reset();person.tick_enabled=false;
    require(verdict_is(person_verdict(person),Availability::Inactive,Rule::Tick),"a verified inactive tick is not reported as an unreadable person");
    f=present();f.destroying.reset();
    const auto v=actor_verdict(f);
    require(v.state==Availability::Unknown&&v.issue&&std::string(v.issue)=="removal state could not be read",
        "an unread removal flag is unknown with its own reason");
}
void duplicate_and_foreign_arrays() {
    int a=0,b=0,c=0;
    std::vector<int*> main{&a,&b,nullptr,&a},attached{&b,&c};
    auto merged=unique_members<int>({std::span<int* const>(main),std::span<int* const>(attached)},8);
    require(merged&&*merged==std::vector<int*>({&a,&b,&c}),"main and attached registries are deduplicated in first-seen order without nulls");
    require(!unique_members<int>({std::span<int* const>(main),std::span<int* const>(attached)},2),"an oversized registry fails closed instead of truncating");
    auto foreign=present();foreign.same_world=false;
    require(verdict_is(actor_verdict(foreign),Availability::Inactive,Rule::Foreign),"a registry entry from another world is rejected");
    int storage[4]{};
    require(valid_array(storage,2,4,16)&&valid_array(nullptr,0,0,16),"valid native array shapes are accepted");
    require(!valid_array(storage,5,4,16)&&!valid_array(storage,-1,4,16)&&!valid_array(nullptr,0,4,16)&&
        !valid_array(storage,17,32,16)&&!valid_array(storage,1,70000,16000),"malformed or oversized native arrays are rejected");
}
void exit_predicates() {
    require(exit_available(open_exit()),"a collision-proven overlap trigger is an available exit without visibility facts");
    auto volume=open_exit();volume.actor.hidden=true;
    require(exit_available(volume),"a hidden-in-game trigger volume remains an exit");
    for(int mode:{3,5}){auto f=open_exit();f.overlap->trigger_mode=mode;require(exit_available(f),"query-enabled collision modes 1/3/5 are accepted");}
    for(int mode:{0,2,4}){auto f=open_exit();f.overlap->trigger_mode=mode;
        require(verdict_is(exit_verdict(f),Availability::Inactive,Rule::Collision),"no-query collision modes are verified inactive");}
    struct Case {int change;Availability state;Rule rule;};
    for(const auto& [change,state,rule]:std::array<Case,9>{{{0,Availability::Inactive,Rule::Scripted},
        {1,Availability::Inactive,Rule::Conditional},{2,Availability::Unknown,Rule::Unknown},{3,Availability::Inactive,Rule::Collision},
        {4,Availability::Inactive,Rule::Collision},{5,Availability::Inactive,Rule::Collision},{6,Availability::Inactive,Rule::Collision},
        {7,Availability::Inactive,Rule::Destroying},{8,Availability::Inactive,Rule::Foreign}}}) {
        auto f=open_exit();
        switch(change) {
        case 0:f.transition.transition.event_only=true;break;
        case 1:f.transition=TransitionResult{Availability::Inactive,{},nullptr};break;
        case 2:f.overlap.reset();break;
        case 3:f.overlap->actors_enabled=false;break;
        case 4:f.overlap->player_generates=false;break;
        case 5:f.overlap->trigger_response=2;break;
        case 6:f.overlap->player_response=0;break;
        case 7:f.actor.destroying=true;break;
        case 8:f.actor.same_world=false;break;
        }
        require(!exit_available(f)&&verdict_is(exit_verdict(f),state,rule),
            "scripted, conditionally inactive, collision-disabled, non-overlap, destroying or foreign exits are inactive; unread collision is unknown");
    }
    auto condition=open_exit();condition.transition=unread("current condition could not be read");
    const auto unknown_exit=exit_verdict(condition);
    require(unknown_exit.state==Availability::Unknown&&std::string(unknown_exit.issue)=="current condition could not be read",
        "an unreadable native condition is an unknown exit with its reason, not an inactive one");
    condition.overlap->actors_enabled=false;
    require(verdict_is(exit_verdict(condition),Availability::Inactive,Rule::Collision),"a verified collision-disabled exit is inactive even when its condition is unreadable");
    auto onward=open_exit();onward.transition.transition.map_id=10041;
    std::array linked{open_exit(),onward,condition};
    require(unique_linked_exit(linked,10041).index==1,"one current matching link can be selected beside other destinations and inactive links");
    linked[2]=onward;
    require(!unique_linked_exit(linked,10041).index,"ambiguous links cannot authorize the handoff");
    linked[2].overlap.reset();
    require(!unique_linked_exit(linked,10041).index,"an unreadable link cannot be discarded to prove uniqueness");
    linked[1].transition.transition.map_id=10042;
    linked[2]={};
    require(!unique_linked_exit(linked,10041).index,"no match remains after the game changes the destination");
    LandmarkFacts landmark{present(),true,open_exit()};
    require(landmark_available(landmark),"a shown landmark with an available linked transition is an exit");
    landmark.transition.transition.transition.event_only=true;
    require(!landmark_available(landmark),"a landmark whose linked transition is scripted is not an ordinary exit");
    require(landmark_available(landmark,true),"a current quest relation can authorize an enabled scripted transition separately from ordinary exits");
    landmark.linked=false;
    require(verdict_is(landmark_verdict(landmark,true),Availability::Inactive,Rule::NotExit),"a landmark without a linked transition is not an exit");
    landmark.linked.reset();
    require(landmark_verdict(landmark,true).state==Availability::Unknown,"an unreadable landmark link is unknown");
}
void whole_capture_and_entity_issues() {
    ScanTally whole;whole.fail("The current area's entity list could not be read.");
    require(whole.report.unavailable_reason=="The current area's entity list could not be read.","a registry, pawn or origin failure is a whole-capture reason");
    for(std::size_t i=0;i<whole.report.unreadable.size();++i)
        require(whole.report.unreadable[i]==0&&whole.report.first_issue[i].empty(),"a whole-capture failure is never counted as per-entity issues");
    ScanTally entity;
    require(!entity.admit(unknown("visibility could not be read"),AreaKind::Person),"an unreadable person is not listed");
    require(entity.report.unavailable_reason.empty(),"one unreadable entity is not a whole-capture failure");
    require(entity.report.unreadable[0]==1&&entity.report.unreadable[1]==1&&entity.report.first_issue[1]=="A person's visibility could not be read."&&
        entity.report.first_issue[0]==entity.report.first_issue[1],"an entity issue is counted in All and in its own category with its reason");
}
void bad_exit_keeps_people_clean() {
    ScanTally t;
    auto bad=open_exit();bad.transition=unread("current condition could not be read");
    require(!t.admit(exit_verdict(bad),AreaKind::Exit),"an exit whose condition cannot be read is not listed as active");
    require(t.admit(exit_verdict(open_exit()),AreaKind::Exit),"a verified exit is still listed beside an unreadable one");
    t.listed();
    require(t.report.unreadable[1]==0&&t.report.first_issue[1].empty(),"a bad exit never taints the (empty) People category");
    require(t.report.unreadable[3]==1&&t.report.unreadable[0]==1&&t.report.first_issue[3]=="An exit's current condition could not be read."&&
        t.report.first_issue[0]==t.report.first_issue[3]&&t.listed_count==1,"the exit issue is surfaced in a non-empty Exits list and in All");
}
void partial_counts_and_first_issues() {
    ScanTally t;
    auto unseen=active_person();unseen.unit_visible.reset();
    auto untimed=active_person();untimed.tick_enabled.reset();
    auto unknown_switch=present();unknown_switch.hidden.reset();
    auto outdoor=active_person();outdoor.tick_enabled=false;
    t.admit(person_verdict(unseen),AreaKind::Person);t.admit(person_verdict(untimed),AreaKind::Person);
    t.admit(actor_verdict(unknown_switch),AreaKind::Switch);t.admit(person_verdict(outdoor),AreaKind::Person);
    require(t.report.unreadable[0]==3&&t.report.unreadable[1]==2&&t.report.unreadable[2]==1&&t.report.unreadable[3]==0&&t.report.unreadable[4]==0,
        "partial unknown counts are kept per category and in All");
    require(t.report.first_issue[1]=="A person's visibility could not be read."&&t.report.first_issue[2]=="A switch's visibility could not be read."&&
        t.report.first_issue[0]==t.report.first_issue[1],"the first issue of each category is retained");
    require(t.rejected[rule_index(Rule::Tick)]==1&&t.rejected[rule_index(Rule::Unknown)]==3,"verified inactive and unknown entities are counted separately");
}
void reject_counters_in_summary() {
    ScanTally t;
    auto hidden=active_person();hidden.actor.hidden=true;
    auto invisible=active_person();invisible.unit_visible=false;
    auto outdoor=active_person();outdoor.tick_enabled=false;
    auto foreign=present();foreign.same_world=false;
    auto dying=present();dying.destroying=true;
    auto closed=open_exit();closed.transition=TransitionResult{Availability::Inactive,{},nullptr};
    auto unread_save=present();unread_save.hidden.reset();
    t.admit(person_verdict(hidden),AreaKind::Person);t.admit(person_verdict(invisible),AreaKind::Person);t.admit(person_verdict(outdoor),AreaKind::Person);
    t.admit(actor_verdict(foreign),AreaKind::Door);t.admit(actor_verdict(dying),AreaKind::Ladder);t.admit(exit_verdict(closed),AreaKind::Exit);
    t.admit(actor_verdict(unread_save),AreaKind::SavePoint);
    const auto summary=tally_summary(t);
    for(const char* part:{"hidden=1","invisible=1","tick=1","foreign=1","destroying=1","conditional=1","unknown=1","unreadable=all:1,people:0,objects:0,exits:0,save:1"})
        require(summary.find(part)!=std::string::npos,"per-rule reject counters and per-category unknown counts are logged");
}
void story_npc_ignores_area_tick() {
    auto outdoor=active_person();outdoor.tick_enabled=false;
    require(verdict_is(person_verdict(outdoor),Availability::Inactive,Rule::Tick),"ordinary People still exclude other-area tick-disabled NPCs");
    require(verdict_is(story_point_verdict(story_unit()),Availability::Available),
        "a Story NPC with current native visibility is available although its area tick is disabled");
    require(verdict_is(story_point_verdict(story_unit(false)),Availability::Inactive,Rule::Invisible),"a Story unit whose m_IsVisible is false stays unavailable");
}
void story_registered_actor_rules() {
    StoryActorFacts object{present(),true,true,true};
    require(verdict_is(story_point_verdict(object),Availability::Available),"an unhidden visible registered unit or MapUnit_Object is available");
    object.visible=false;
    require(verdict_is(story_point_verdict(object),Availability::Inactive,Rule::Invisible),"an invisible registered MapUnit_Object is unavailable");
    const StoryActorFacts actor{present(),false,std::nullopt,true};
    require(verdict_is(story_point_verdict(actor),Availability::Available),"a generic registered unhidden current-world actor is available");
    struct Case {int change;Rule rule;};
    for(const auto& [change,rule]:std::array<Case,4>{{{0,Rule::Hidden},{1,Rule::Foreign},{2,Rule::Destroying},{3,Rule::Removed}}}) {
        auto f=story_unit();
        if(change==0)f.actor.hidden=true;
        if(change==1)f.actor.same_world=false;
        if(change==2)f.actor.destroying=true;
        if(change==3)f.actor.live=false;
        require(verdict_is(story_point_verdict(f),Availability::Inactive,rule),"hidden, foreign, destroying or removed Story actors are verified inactive");
    }
    auto flag=story_unit();flag.visible.reset();
    const auto v=story_point_verdict(flag);
    require(v.state==Availability::Unknown&&issue_text("The story destination's",v.issue)=="The story destination's visibility could not be read.",
        "an unreadable unit visibility flag is unknown with a reason, never inactive");
    auto unplaced=story_unit();unplaced.positioned=false;
    const auto p=story_point_verdict(unplaced);
    require(p.state==Availability::Unknown&&std::string(p.issue)=="position could not be read","an unreadable Story position is unknown, never inactive");
    auto hidden_unread=story_unit();hidden_unread.actor.hidden=true;hidden_unread.visible.reset();
    require(verdict_is(story_point_verdict(hidden_unread),Availability::Inactive,Rule::Hidden),"a verified hidden flag outranks an unreadable visibility flag");
}
void story_exit_claims() {
    const StoryActorFacts landmark{present(),false,std::nullopt,true};
    require(verdict_is(story_point_verdict(landmark),Availability::Available),"a direct quest landmark remains a point without exit classification");
    auto scripted=open_exit();scripted.transition.transition.event_only=true;
    require(verdict_is(story_landmark_verdict(landmark,true,scripted),Availability::Available),"a claimed Story exit may use an enabled scripted transition");
    require(verdict_is(story_landmark_verdict(landmark,false,scripted),Availability::Inactive,Rule::NotExit),"a claimed exit needs a linked transition");
    require(story_landmark_verdict(landmark,std::nullopt,scripted).state==Availability::Unknown,"an unreadable exit link is unknown");
    auto closed=open_exit();closed.transition=TransitionResult{Availability::Inactive,{},nullptr};
    require(verdict_is(story_landmark_verdict(landmark,true,closed),Availability::Inactive,Rule::Conditional),
        "a claimed exit whose native selection is inactive is unavailable");
    auto hidden_point=landmark;hidden_point.actor.hidden=true;
    require(verdict_is(story_landmark_verdict(hidden_point,true,scripted),Availability::Inactive,Rule::Hidden),"a hidden landmark is not a claimed exit");
    auto volume=open_exit();volume.actor.hidden=true;
    require(verdict_is(story_trigger_verdict(volume,true),Availability::Available),"a claimed trigger exit ignores its hidden-in-game volume flag");
    require(story_trigger_verdict(volume,false).state==Availability::Unknown,"a claimed trigger exit without a readable position is unknown");
}
void labels_and_categories() {
    require(std::string_view(npc_appearance_label(20))=="Lasswell"&&
        std::string_view(npc_appearance_label(1060))=="Grandshelt soldier"&&
        std::string_view(npc_appearance_label(1121))=="Dwarf"&&
        std::string_view(npc_appearance_label(10037))=="Local resident",
        "field sprites identify recognized party members and visible roles without revealing undisclosed boss names");
    require(std::string(area_label(AreaKind::TreasureChest,true))=="Open treasure chest"&&
        std::string(area_label(AreaKind::TreasureChest,false))=="Closed treasure chest"&&
        std::string(area_label(AreaKind::TreasureChest))=="Treasure chest","chests describe only their visible open state");
    require(std::string(area_label(AreaKind::Exit))=="Connecting passage"&&area_kind_info(AreaKind::Exit).category==3,
        "an exit with no established destination has a generic label and shares the door and ladder category");
    require(area_kind_info(AreaKind::Person).category==1&&area_kind_info(AreaKind::SavePoint).category==4&&
        area_kind_info(AreaKind::Switch).category==2,"categories follow the reader's All/People/Objects/Doors and ladders/Save points order");
    require(!gimmick_kind(AreaKind::Person)&&!gimmick_kind(AreaKind::Exit)&&gimmick_kind(AreaKind::Ladder),"only registered gimmick classes come from the gimmick lists");
    constexpr std::array<int,14> sizes{2912,1272,1064,1024,1192,1088,1072,1464,1032,1056,1064,1312,1512,1016};
    for(std::size_t i=0;i<sizes.size();++i)require(area_kinds[i].native_size==sizes[i],"native class sizes match the exact-build audit");
}
void presented_person_service() {
    constexpr std::wstring_view inn=L"Field.Object.Interact.Type.Shop.Inn";
    constexpr std::wstring_view shop=L"Field.Object.Interact.Type.Shop.Tool";
    require(std::string(person_service_label(inn,true))=="Inn attendant","the displayed bed icon identifies the inn attendant");
    require(std::string_view(person_service_label(shop,true))=="Item shop attendant","the displayed icon identifies the type of shop");
    require(std::string_view(person_service_label(18))=="Weapon shop attendant"&&
        std::string_view(person_service_label(32))=="Vision shop attendant"&&
        std::string_view(person_service_label(7)).empty(),"facility roles do not promote generic checkpoint markers");
    require(std::string_view(person_service_label(inn,false)).empty()&&
        std::string_view(person_service_label(shop,false)).empty(),"an unpresented icon does not identify a service");
    require(std::string_view(person_service_label(L"Field.Object.Interact.Type.Shop.Bank",true)).empty()&&
        std::string_view(person_service_label(L"Field.Object.Interact.Type.Talk",true)).empty()&&
        std::string_view(person_service_label(L"Soldier",true)).empty(),"shared icons and non-icon identities do not identify a service");
    require(widget_presented(0,1.0f)&&widget_presented(3,1.0f)&&widget_presented(4,0.5f),
        "the rendered Slate visibility states can present a role");
    require(!widget_presented(1,1.0f)&&!widget_presented(2,1.0f)&&!widget_presented(5,1.0f)&&
        !widget_presented(0,0.0f)&&!widget_presented(std::nullopt,1.0f)&&!widget_presented(0,std::nullopt),
        "collapsed, hidden, unreadable and transparent widgets cannot present a role");
}
void bitfields_and_native_bools() {
    require(!masked(0x7F,hidden_mask)&&masked(0x80,hidden_mask),"bHidden is bit 0x80 regardless of neighbouring replication bits");
    require(!masked(0xFD,destroying_mask)&&masked(0x03,destroying_mask),"bActorIsBeingDestroyed is bit 0x02, not collision bit 0x01");
    require(native_bool(std::uint8_t{1})==true&&native_bool(std::uint8_t{0})==false,"native full-byte bools decode 0/1");
    require(!native_bool(std::uint8_t{2})&&!native_bool(std::nullopt),"other byte values or missing reads are unknown");
}
void deterministic_order() {
    const Vec3 origin{0,0,0};
    std::vector<Candidate> targets{candidate("b",origin,{3,4,0}),candidate("a",origin,{0,5,0}),candidate("c",origin,{1,0,0})};
    order_targets(targets);
    require(targets[0].snapshot.id=="c"&&targets[1].snapshot.id=="a"&&targets[2].snapshot.id=="b","nearest first, ties by stable id");
}
void castle_room_and_exit_legs() {
    auto room=castle_leg_spec(2014,5,{-11623,3213,1282.15});
    require(room&&room->doorway&&room->id==17,"Rain's live room spawn uses its inside doorway");
    auto old=castle_leg_spec(2014,5,{-11841.79,2524.90,1282.15});
    require(old&&old->id==17,"the reported position just inside the walking mesh edge stays in Rain's room");
    auto hall=castle_leg_spec(2014,5,{-12520,1305,1282.15});
    require(hall&&!hall->doorway&&hall->id==8&&hall->point_id==2,"leaving the room selects the passage from the hall");
    auto main=castle_leg_spec(2014,3,{-28600,-16,900});
    require(main&&main->id==6&&main->point_id==8,"the main hall connects to the entrance");
    auto entrance=castle_leg_spec(2014,1,{-38400,0,300});
    require(entrance&&entrance->id==2&&entrance->point_id==-1,"the entrance selects the conditionally available exterior exit");
    require(!castle_leg_spec(2013,5,{-11623,3213,1282.15})&&!castle_leg_spec(2014,7,{1000,0,100}),
        "unverified worlds and side areas cannot reuse the castle chain");
}
void mitra_labels_and_mailbox() {
    require(area_kind_info(AreaKind::Mailbox).category==2,"Mitra's postbox belongs in Objects");
    require(verdict_is(mailbox_verdict(present(),3,27),Availability::Available),
        "a live visible mailbox with an enabled query box is trackable");
    require(verdict_is(mailbox_verdict(present(),0,27),Availability::Inactive,Rule::Collision),
        "a disabled mailbox stays unavailable");
    require(verdict_is(mailbox_verdict(present(),std::nullopt,27),Availability::Unknown),
        "an unreadable mailbox query box is not listed");
    require(verdict_is(mailbox_verdict(present(),3,0),Availability::Inactive,Rule::Conditional),
        "a mailbox without the native interaction is not exposed");
    constexpr std::array<int,25> points{2,1,-1,130,131,28,23,127,121,21,123,122,22,24,25,26,124,20,129,125,126,27,120,29,128};
    for(std::size_t i=0;i<points.size();++i)
        if(points[i]>=0)require(mitra_passage(static_cast<int>(i),i<2?1000:2000,points[i])!=nullptr,
            "every authored ordinary Mitra exit must have a verified destination label");
    require(!mitra_passage(4,2000,-1)&&!mitra_passage(4,1000,131),
        "a conditional door label uses the selected destination and arrival point");
    TransitionDefaults defaults{true,false,false,-1,2000,-1};
    const std::array<TransitionRow,1> rows{{{false,2000,131}}};
    const auto selected=select_transition(defaults,rows,[](std::size_t){return std::optional{true};});
    require(selected.transition.point_id==131&&mitra_passage(4,selected.transition.map_id,*selected.transition.point_id),
        "transition capture must propagate the conditional arrival point");
    require(mitra_story_exit_id(9)==4&&mitra_story_exit_id(5)==5&&mitra_story_exit_id(1)==1,
        "Story must walk from the basement through Rain's house to the town entrance");
    require(mitra_story_exit_id(10)==1,"the terrace also returns through the town entrance");
}
void named_world_exits() {
    require(cross_map_exit_label(1000,2010)=="Exit to Grandshelt, the Royal Capital",
        "an active world exit uses the native title of its effective destination");
    require(cross_map_exit_label(2000,1000)=="Exit to World map",
        "a return to the world map uses its familiar field name");
    require(cross_map_exit_label(2010,2010).empty()&&cross_map_exit_label(0,2010).empty()&&
        cross_map_exit_label(1000,60000).empty()&&cross_map_exit_label(1000,5030).empty(),
        "same-map, unknown-current, invalid and unnamed destinations keep their fallback label");
    require(std::string_view(area_label(AreaKind::FieldItem))=="Ground pickup"&&
        std::string_view(area_label(AreaKind::Switch,true))=="Pressed switch"&&
        std::string_view(area_label(AreaKind::Switch))=="Switch",
        "field items and verified switch state have distinct labels");
    require(std::string_view(object_appearance_label(AreaKind::FieldItem,L"BP_MapGimmick_FieldItem_Medal_C"))=="Medal pickup"&&
        std::string_view(object_appearance_label(AreaKind::Door,L"BP_MapGimmick_Object_Door_Single_Slid_004_C"))=="Sliding door",
        "object labels describe the visible variant");
    require(std::string_view(facility_passage(2030,4,121))=="Door to the weapon and armor shop"&&
        !facility_passage(2030,4,120)&&!facility_passage(2090,4,121),"facility labels require their exact active arrival");
    require(facility_door_exit(2030,12)==6&&facility_door_exit(2030,13)==8&&
        !facility_door_exit(2030,14)&&!facility_door_exit(2000,12),
        "a visible facility door only borrows the name of its paired passage");
    require(std::string_view(facility_passage(2040,9,23))=="Door from the lower house to the street"&&
        !facility_passage(2040,9,24),"the two Felicitas houses retain distinct return passages");
}
void visible_interactive_props() {
    require(verdict_is(interactive_prop_verdict(true,0,4,true),Availability::Available),
        "a visible prop with an enabled Check prompt and box can be tracked");
    require(verdict_is(interactive_prop_verdict(false,0,4,true),Availability::Inactive,Rule::Invisible),
        "a hidden mesh stays out of navigation");
    require(verdict_is(interactive_prop_verdict(true,1,4,true),Availability::Inactive,Rule::Conditional)&&
        verdict_is(interactive_prop_verdict(true,0,0,true),Availability::Inactive,Rule::Conditional),
        "an inactive or unprompted prop stays out of navigation");
    require(verdict_is(interactive_prop_verdict(true,0,0,true,0),Availability::Available)&&
        verdict_is(interactive_prop_verdict(true,1,0,true,0),Availability::Inactive,Rule::Conditional),
        "a Pearly Pot uses native mode None and still follows its activation condition");
    require(verdict_is(interactive_prop_verdict(true,0,4,false),Availability::Inactive,Rule::Collision)&&
        interactive_prop_verdict(true,0,4,std::nullopt).state==Availability::Unknown,
        "unavailable and unreadable interaction boxes retain distinct verdicts");
}
void siren_tower_objects() {
    constexpr auto upper=L"/Game/Map/Dng/02Lan/02Lan_40/GD/Dng_02Lan_40_01_GD.Dng_02Lan_40_01_GD:"
        L"PersistentLevel.BP_MapGimmick_Floor_Elevator_009_C_13";
    constexpr auto west=L"/Game/Map/Dng/02Lan/02Lan_40/GD/Dng_02Lan_40_01_GD.Dng_02Lan_40_01_GD:"
        L"PersistentLevel.BP_MapGimmick_Floor_Elevator_009_C_4";
    constexpr auto pot=L"/Game/Map/Dng/02Lan/02Lan_40/GD/Dng_02Lan_40_01_GD.Dng_02Lan_40_01_GD:"
        L"PersistentLevel.BP_MapGimmick_BlockingBase_Child_MagicPot_C_1";
    const auto* upper_lift=siren_lift_placement(upper,13);
    const auto* west_lift=siren_lift_placement(west,14);
    require(upper_lift&&west_lift&&!siren_lift_placement(upper,14)&&!siren_lift_placement(L"Other.BP_MapGimmick_Floor_Elevator_009_C_13",13),
        "the automatic lifts are scoped to their registered tower placements");
    require(siren_lift_docked(*upper_lift,{9750.,2492.,6834.})&&siren_lift_docked(*upper_lift,{9750.,2492.,5424.})&&
        siren_lift_docked(*west_lift,{-1855.,277.5,3274.}),"lift entry geometry at either dock is available");
    require(!siren_lift_docked(*upper_lift,{9750.,2492.,6100.})&&!siren_lift_docked(*west_lift,{-2100.,277.5,4473.}),
        "a moving or misplaced lift has no current boarding route");
    const auto* label=exact_placement_label(AreaKind::Book,pot,3);
    require(label&&std::string_view(label)=="Pot"&&!exact_placement_label(AreaKind::Book,pot,15),
        "the visible pot is named without revealing the encounter");
}
void proximity_approach() {
    const StorySpatialContext control{{0,0,0},{1,0,0},{0,1,0},100.};
    std::array targets{
        FieldProximityTarget{"npc", {395,0,30},FieldCueKind::Npc,true},
        FieldProximityTarget{"chest", {-220,220,0},FieldCueKind::Interactable,true},
        FieldProximityTarget{"npc-two", {0,-130,0},FieldCueKind::Npc,true},
        FieldProximityTarget{"behind-wall", {90,0,0},FieldCueKind::Npc,false},
        FieldProximityTarget{"another-floor", {100,0,300},FieldCueKind::Interactable,true}};
    const auto before=field_entity_cues(targets,control);
    require(before[0].size()==2&&before[1].size()==1&&before[0][0].id=="npc-two"&&
        std::abs(before[0][1].azimuth_degrees-90.)<.01&&std::abs(before[1][0].azimuth_degrees+45.)<.01,
        "each audible same-floor NPC and object has its own bearing");
    const auto standing=field_entity_cues(targets,control);
    require(standing[0].size()==2&&standing[1].size()==1,
        "stationary targets stay in the repeating audio mix");
    targets[0].position={0,400,0};targets[1].audible=false;
    const auto moving=field_entity_cues(targets,control);
    require(moving[1].empty()&&moving[0].size()==2&&std::abs(moving[0][1].azimuth_degrees)<.01,
        "moving emitters update in place and occluded sources disappear");
    targets[0].position={0,401,0};
    require(field_entity_cues(targets,control)[0].size()==1,"emitters stop out of range");
    auto rotated=control;rotated.right={0,-2,0};rotated.up={2,0,0};
    require(std::abs(field_entity_cues(targets,rotated)[0][0].azimuth_degrees-90.)<.01,
        "camera-axis changes update the spatial bearings");

    std::vector<FieldProximityTarget> crowd;
    for(int i=32;i>0;--i)crowd.push_back({std::to_string(i),{static_cast<double>(i*10),0,0},FieldCueKind::Npc,true});
    const auto limited=field_entity_cues(crowd,control);
    require(limited[0].size()==16&&limited[0].front().id=="1"&&limited[0].back().id=="16",
        "dense scenes keep the nearest sixteen NPC sources");

    using namespace std::chrono_literals;
    require(field_pulse_interval(FieldCueKind::Npc,0.)>=512ms&&field_pulse_interval(FieldCueKind::Interactable,0.)>=1014ms&&
        field_pulse_interval(FieldCueKind::Npc,0.)<field_pulse_interval(FieldCueKind::Npc,4.)&&
        field_pulse_gain(FieldCueKind::Npc,0.)>field_pulse_gain(FieldCueKind::Npc,4.),
        "each complete cue repeats more quickly and loudly as it gets closer");
}
void field_wall_positions() {
    const StorySpatialContext control{{0,0,0},{1,0,0},{0,1,0},100.};
    const Vec3 east{1,0,0};
    const auto wall=field_wall_cue({190,0,65},{-1,0,0},east,1.4,.71,control,"right");
    require(wall&&wall->kind==FieldCueKind::Wall&&wall->id=="right"&&wall->azimuth_degrees==90.,
        "wall audio points at the live obstruction");
    const auto contact=field_wall_cue({0,0,0},{-1,0,0},east,0.,.71,control,"right");
    require(contact&&contact->azimuth_degrees==90.&&wall_tone_gain(0.)>wall_tone_gain(1.4)&&
        wall_tone_gain(1.4)>0.&&wall_tone_gain(field_wall_radius_metres)==0.,
        "a near-zero contact keeps its bearing and the tone fades to silence at the outer edge");
    require(!field_wall_cue({190,0,65},{-.6,0,.8},east,1.4,.71,control,"right")&&
        !field_wall_cue({190,0,65},{-.6,0,-.8},east,1.4,.71,control,"right")&&
        !field_wall_cue({190,0,65},{0,1,0},east,1.4,.71,control,"right")&&
        !field_wall_cue({290,0,65},{-1,0,0},east,2.5,.71,control,"right"),
        "walkable slopes, overhead faces, side grazes and distant obstructions are silent");
}
void wall_apertures() {
    // Player at the origin facing up (+y); a 40 cm capsule probes 240 cm ahead.
    const Vec3 up{0,1,0},origin{0,0,65};
    constexpr double radius=40.;
    require(wall_probe_lateral(origin,{32,67,65},up)==32.&&wall_probe_lateral(origin,{-18,67,65},up)==-18.&&
        wall_probe_lateral(origin,{67,12,65},{1,0,0})==-12.&&wall_probe_lateral(origin,{67,12,65},{0,0,0})==0.,
        "lateral offset is signed on the probe's right-hand side for every direction and zero for a degenerate probe");
    // The doorway trial: a right-of-axis clip of the frame tests a one-radius step to the left.
    require(wall_aperture_shifts(12.,radius)==std::vector<double>{-40.}&&wall_aperture_shifts(-31.,radius)==std::vector<double>{40.},
        "an off-centre clip is retried one radius away from the impact side only");
    require(wall_aperture_shifts(3.,radius)==std::vector<double>{-40.,40.}&&wall_aperture_shifts(-3.,radius)==std::vector<double>{40.,-40.}&&
        wall_aperture_shifts(0.,radius)==std::vector<double>{40.,-40.},
        "a near-centre hit is retried on both sides, the likelier opening first");
    require(wall_aperture_shifts(12.,0.).empty()&&wall_aperture_shifts(std::nan(""),radius).empty()&&
        wall_aperture_shifts(12.,-1.).empty(),"unusable radius or offset yields no side-step probes");
    // A side-stepped sweep that clears, or passes the frame by a full capsule, proves the opening.
    require(wall_aperture_open(67.,std::nullopt,radius)&&wall_aperture_open(67.,250.,radius)&&wall_aperture_open(67.,147.,radius),
        "an unobstructed or clearly deeper side-stepped sweep proves a usable aperture");
    require(!wall_aperture_open(67.,120.,radius)&&!wall_aperture_open(67.,146.9,radius)&&!wall_aperture_open(0.,79.,radius),
        "a side-stepped sweep stopped by the same frame or a second jamb keeps the wall tone");
    require(!wall_aperture_open(-1.,std::nullopt,radius)&&!wall_aperture_open(67.,-5.,radius)&&
        !wall_aperture_open(std::nan(""),std::nullopt,radius)&&!wall_aperture_open(67.,std::nullopt,0.),
        "invalid depths or radius never prove an opening");
    // The farther wall seen through the opening still renders as this direction's tone.
    const StorySpatialContext control{{0,0,0},{1,0,0},{0,1,0},100.};
    const auto beyond=field_wall_cue({-40,230,65},{0,-1,0},up,2.3,.71,control,"up");
    require(beyond&&beyond->distance_metres==2.3&&beyond->azimuth_degrees<0.&&beyond->azimuth_degrees>-15.&&
        wall_tone_gain(2.3)<wall_tone_gain(.67),
        "a wall past the aperture is reported from the side-stepped probe, farther and quieter");
}

}
int main() {
    const std::array<std::pair<const char*,void(*)()>,27> tests{{
        {"town_interactions_match_placements",town_interactions_match_placements},
        {"native_transition_resolution",native_transition_resolution},
        {"offscreen_and_prior_sight_independent",offscreen_and_prior_sight_independent},{"live_position_movement",live_position_movement},
        {"area_unload_flag_hide_and_tick",area_unload_flag_hide_and_tick},{"verified_negative_outranks_unknown",verified_negative_outranks_unknown},
        {"duplicate_and_foreign_arrays",duplicate_and_foreign_arrays},{"exit_predicates",exit_predicates},
        {"whole_capture_and_entity_issues",whole_capture_and_entity_issues},{"bad_exit_keeps_people_clean",bad_exit_keeps_people_clean},
        {"partial_counts_and_first_issues",partial_counts_and_first_issues},{"reject_counters_in_summary",reject_counters_in_summary},
        {"story_npc_ignores_area_tick",story_npc_ignores_area_tick},{"story_registered_actor_rules",story_registered_actor_rules},
        {"story_exit_claims",story_exit_claims},{"labels_and_categories",labels_and_categories},
        {"presented_person_service",presented_person_service},
        {"bitfields_and_native_bools",bitfields_and_native_bools},{"deterministic_order",deterministic_order},
        {"castle_room_and_exit_legs",castle_room_and_exit_legs},{"mitra_labels_and_mailbox",mitra_labels_and_mailbox},
        {"named_world_exits",named_world_exits},{"visible_interactive_props",visible_interactive_props},
        {"siren_tower_objects",siren_tower_objects},{"proximity_approach",proximity_approach},
        {"field_wall_positions",field_wall_positions},{"wall_apertures",wall_apertures}}};
    int failures=0;
    for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failures;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    std::cout<<tests.size()-failures<<"/"<<tests.size()<<" area input groups passed\n";
    return failures?1:0;
}
