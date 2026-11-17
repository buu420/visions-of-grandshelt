#include "resonance/DestinationTravel.hpp"
#include "../src/TravelCatalog.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace resonance;

namespace {
void need(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}

void reachable_places_use_the_right_exit() {
    const std::array first{
        TravelPassage{2000,17,{2000,20}},
        TravelPassage{2000,21,{2000,27}}
    };
    const std::array links{
        TravelLink{{2000,20},{2000,1,{1000,1}}},
        TravelLink{{2000,27},{2000,1,{1000,1}}},
        TravelLink{{1000,1},{1000,2,{2010,-1}}},
        TravelLink{{1000,4},{1000,8,{2060,0}}},
        TravelLink{{2010,-1},{2010,0,{1000,1}}}
    };
    const auto result=plan_destinations(first,links,{});
    const auto capital=std::find_if(result.begin(),result.end(),[](const auto& route){return route.map==2010;});
    need(capital!=result.end()&&capital->first_leg==0&&capital->leg_count==3,"The same destination leads from the room to the street, world and capital");
    need(std::none_of(result.begin(),result.end(),[](const auto& route){return route.map==2060;}),"An unrelated shore is unreachable");
    need(result.size()==3,"Different arrivals in one map do not duplicate the destination");
}

void a_completed_exit_can_be_excluded_when_replanning() {
    const std::array first{
        TravelPassage{2000,0,{1000,2}},
        TravelPassage{2000,1,{1000,1}}
    };
    const std::array links{
        TravelLink{{1000,2},{1000,2,{2010,-1}}},
        TravelLink{{1000,1},{1000,2,{2010,-1}}},
    };
    const auto result=plan_destinations(first,links,std::span{first}.first(1));
    const auto capital=std::find_if(result.begin(),result.end(),[](const auto& route){return route.map==2010;});
    need(capital!=result.end()&&capital->first_leg==1,"Replanning can choose the other live exit");
}

void the_audited_gate_uses_the_current_native_variant() {
    const auto gate_at_mitra=[](const TravelLink& link) {
        return link.departure==TravelPoint{1000,1}&&link.passage.trigger==2;
    };
    const auto routes=travel_catalog::available_links([](std::wstring_view condition)->std::optional<bool> {
        return condition==L"{progress:0}>=1090&{progress:0}<=1120";
    });
    const auto gate=std::find_if(routes.begin(),routes.end(),gate_at_mitra);
    need(gate!=routes.end()&&gate->passage.arrival==TravelPoint{2010,-1},
        "The native progress 1090 row connects Mitra's world entry to the capital");
    const auto unreadable=travel_catalog::available_links([](std::wstring_view condition)->std::optional<bool> {
        if(condition==L"{progress:0}>=1090&{progress:0}<=1120")return {};
        return condition==L"{progress:0}<1090";
    });
    need(std::none_of(unreadable.begin(),unreadable.end(),gate_at_mitra),
        "An unreadable earlier row never exposes the later Castle route");
}

void a_different_arrival_cannot_advance_the_trip() {
    const TravelLocation room{{2000,120},2000,2};
    const TravelPassage exit{2000,17,{2000,20}};
    const TravelLocation street{{2000,20},2000,1};
    need(!travel_arrived(room,exit,room),"Same-map travel waits for an actual arrival");
    need(!travel_arrived(room,exit,{{2000,23},2000,1}),"Different door does not count");
    need(travel_arrived(room,exit,street),"Expected same-map doorway advances");
    const TravelPassage town_gate{2000,1,{1000,1}};
    need(!travel_arrived(street,town_gate,{{1000,1},2010,-1}),"Matching entry from the wrong map cannot advance");
    need(travel_arrived(street,town_gate,{{1000,1},2000,-1}),"The correct world entry advances");
}

void an_interrupted_leg_restarts_only_at_a_verified_handoff() {
    const TravelLocation room{{2000,120},2000,2};
    const TravelPassage exit{2000,17,{2000,20}};
    const TravelLocation street{{2000,20},2000,1};
    TravelContinuation trip;
    trip.interrupt();
    need(trip.observe(room,exit,{},false)==TravelHandoff::Wait,"A loading screen is not an arrival");
    need(trip.observe(room,exit,street,false)==TravelHandoff::Wait,"A doorway needs two consecutive field captures");
    need(trip.observe(room,exit,{},false)==TravelHandoff::Wait,"Losing field control discards the first capture");
    need(trip.observe(room,exit,street,false)==TravelHandoff::Wait,"A stale candidate cannot advance the trip");
    need(trip.observe(room,exit,street,false)==TravelHandoff::Crossed,"The verified doorway advances the trip");

    trip={};
    trip.observe(room,exit,{},true);
    const TravelLocation another_room{{2000,23},2000,1};
    trip.observe(room,exit,another_room,false);
    need(trip.observe(room,exit,another_room,false)==TravelHandoff::DifferentArrival,
        "A battle cannot authorize travel from an unexpected doorway");
}

void battle_return_and_menu_return_are_different() {
    const TravelLocation world{{1000,1},2000,-1};
    const TravelPassage exit{1000,2,{2010,-1}};
    TravelContinuation trip;
    trip.interrupt();
    trip.observe(world,exit,{},false);
    need(trip.observe(world,exit,world,false)==TravelHandoff::Wait,"The first field capture never moves the party");
    need(trip.observe(world,exit,world,false)==TravelHandoff::Stopped,"Closing an ordinary menu does not restart a walk");

    trip={};
    need(trip.observe(world,exit,{},true)==TravelHandoff::Wait,"No route while the battle is active");
    need(trip.observe(world,exit,world,true)==TravelHandoff::Wait,"No route while results still cover the field");
    need(trip.observe(world,exit,world,false)==TravelHandoff::Wait,"Wait for settled field control after results");
    need(trip.observe(world,exit,world,false)==TravelHandoff::BattleReturn,"Resume at the same field position context after battle");
}

void story_can_advance_through_an_authored_transition() {
    const TravelLocation world{{1000,5004},20002,-1};
    const TravelLocation town{{2150,0},1000,1};
    TravelContinuation trip;
    trip.interrupt();
    need(trip.observe(world,{},town,false,true)==TravelHandoff::Wait,"Story waits for the new field to settle");
    need(trip.observe(world,{},town,true,true)==TravelHandoff::Wait,"An active battle discards an earlier field sample");
    need(trip.observe(world,{},town,false,true)==TravelHandoff::Wait,"The previous battle sample cannot advance Story");
    need(trip.observe(world,{},town,false,true)==TravelHandoff::StoryAdvance,"Confirmed native progress can follow a scripted new area");

    trip={};
    trip.observe(world,{},town,false);
    need(trip.observe(world,{},town,false)==TravelHandoff::DifferentArrival,"An unrelated transition does not authorize a new Story route");
}
}

int main() {
    try {
        reachable_places_use_the_right_exit();
        a_completed_exit_can_be_excluded_when_replanning();
        a_different_arrival_cannot_advance_the_trip();
        the_audited_gate_uses_the_current_native_variant();
        an_interrupted_leg_restarts_only_at_a_verified_handoff();
        battle_return_and_menu_return_are_different();
        story_can_advance_through_an_authored_transition();
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
