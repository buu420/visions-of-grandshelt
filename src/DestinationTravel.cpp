#include "resonance/DestinationTravel.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace resonance {
std::vector<DestinationRoute> plan_destinations(std::span<const TravelPassage> first,
        std::span<const TravelLink> onward,std::span<const TravelPassage> completed) {
    std::map<TravelPoint,std::vector<const TravelPassage*>> connections;
    for(const auto& link:onward)
        if(link.departure.map==link.passage.from_map)
            connections[link.departure].push_back(&link.passage);

    struct Visit {TravelPoint point;std::size_t first_leg,leg_count;};
    std::vector<Visit> pending;
    std::set<TravelPoint> seen_points;
    const auto enqueue=[&](const TravelPassage& passage,std::size_t first_leg,std::size_t legs) {
        if(passage.arrival.map<=0||passage.arrival.entry<-1||
           std::find(completed.begin(),completed.end(),passage)!=completed.end())return;
        if(seen_points.insert(passage.arrival).second)pending.push_back({passage.arrival,first_leg,legs});
    };
    for(std::size_t index=0;index<first.size();++index)enqueue(first[index],index,1);

    std::vector<DestinationRoute> result;
    std::set<int> seen_maps;
    for(std::size_t index=0;index<pending.size();++index) {
        const auto visit=pending[index];
        if(seen_maps.insert(visit.point.map).second)
            result.push_back({visit.point.map,visit.first_leg,visit.leg_count});
        if(const auto found=connections.find(visit.point);found!=connections.end())
            for(const auto* edge:found->second)enqueue(*edge,visit.first_leg,visit.leg_count+1);
    }
    return result;
}

bool travel_arrived(const TravelLocation& departure,const TravelPassage& passage,const TravelLocation& current) {
    return departure.point.map==passage.from_map&&current.previous_map==passage.from_map&&
        current.point==passage.arrival&&(current.point!=departure.point||current.area!=departure.area);
}

TravelHandoff TravelContinuation::observe(const TravelLocation& departure,const std::optional<TravelPassage>& passage,
        const std::optional<TravelLocation>& field,bool battle,bool story_advanced) {
    battle_seen_|=battle;
    auto handoff=TravelHandoff::Wait;
    if(field&&!battle) {
        if(passage&&travel_arrived(departure,*passage,*field))handoff=TravelHandoff::Crossed;
        else if(story_advanced)handoff=TravelHandoff::StoryAdvance;
        else if(field->point!=departure.point||(pending()&&field->area!=departure.area))handoff=TravelHandoff::DifferentArrival;
        else if(battle_seen_)handoff=TravelHandoff::BattleReturn;
        else if(interrupted_)handoff=TravelHandoff::Stopped;
    }
    if(handoff==TravelHandoff::Wait){candidate_.reset();return handoff;}
    if(candidate_!=field||candidate_handoff_!=handoff){
        candidate_=field;candidate_handoff_=handoff;return TravelHandoff::Wait;
    }
    return handoff;
}
}
