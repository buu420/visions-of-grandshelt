#pragma once

#include "MapTitles.hpp"
#include "resonance/DestinationTravel.hpp"
#include <optional>
#include <map>
#include <string_view>

namespace resonance::travel_catalog {
struct MapLevel {int id;std::wstring_view world;};
struct Variant {TravelPoint arrival;std::wstring_view condition;bool walkable,condition_known;};
struct Gate {
    int map,trigger;
    std::wstring_view condition,streaming;
    unsigned first_variant,variant_count,first_entry,entry_count;
};

// Generated from the shipped map data and walking-connection evidence.
#include "TravelCatalogData.hpp"

inline bool matches_world(int map,std::wstring_view world) {
    const auto level=std::lower_bound(std::begin(map_levels),std::end(map_levels),map,
        [](const MapLevel& item,int id){return item.id<id;});
    return level!=std::end(map_levels)&&level->id==map&&level->world==world;
}

template<class ReadCondition>
std::vector<TravelLink> available_links(ReadCondition read_condition) {
    std::map<std::wstring_view,std::optional<bool>> conditions;
    const auto matches=[&](std::wstring_view text) {
        if(text.empty())return std::optional<bool>{true};
        const auto [found,added]=conditions.try_emplace(text);
        if(added)found->second=read_condition(text);
        return found->second;
    };
    std::vector<TravelLink> links;
    for(const auto& gate:gates) {
        if(!gate.entry_count||matches(gate.condition)!=true||matches(gate.streaming)!=true)continue;
        for(unsigned index=gate.first_variant;index<gate.first_variant+gate.variant_count;++index) {
            const auto& row=variants[index];
            if(!row.condition_known)break;
            const auto match=matches(row.condition);
            if(!match)break;
            if(!*match)continue;
            // Keep native first-match semantics even for scripted, disabled,
            // and non-walking variants. They can block the following rows.
            if(row.walkable)
                for(unsigned i=gate.first_entry;i<gate.first_entry+gate.entry_count;++i)
                    links.push_back({{gate.map,departure_entries[i]},{gate.map,gate.trigger,row.arrival}});
            break;
        }
    }
    return links;
}
}
