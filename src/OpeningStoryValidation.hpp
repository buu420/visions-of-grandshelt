#pragma once
#include "StoryInputValidation.hpp"
#include "resonance/Overlap.hpp"
#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace resonance::story_input_detail {
struct OpeningContinuationSpec {int id;const char* condition;const char* event;};
inline constexpr std::array<OpeningContinuationSpec,3> opening_specs{{
    {0,"C01_001B","C01_001B"},{1,"C01_001D","C01_001D_Encount"},{2,"C01_001F","C01_001F_Encount"}}};
inline constexpr OpeningContinuationSpec opening_next_section{6,"C01_001F","C01_001F_Encount"};
inline const OpeningContinuationSpec* opening_continuation_spec(int map,int id) {
    if(map==10040)for(const auto& spec:opening_specs)if(spec.id==id)return &spec;
    if(map==10041&&id==opening_next_section.id)return &opening_next_section;
    return nullptr;
}
struct OpeningContinuationEvidence {
    int unique_id{-1};
    ObjectKey key;
    std::string condition,event,matched_event;
    Vec3 position;
    bool current_registered_actor{},active{},overlap_enabled{};
    bool in_walking_section{true};
};
inline std::vector<std::size_t> eligible_opening_continuations(int chapter,int map,
        std::span<const OpeningContinuationEvidence> candidates) {
    std::vector<std::size_t> result;
    if(chapter!=1||(map!=10040&&map!=10041)||candidates.size()>1024)return result;
    const auto specs=map==10040?std::span<const OpeningContinuationSpec>(opening_specs):std::span<const OpeningContinuationSpec>(&opening_next_section,1);
    for(const auto& spec:specs) {
        std::size_t count=0,index=0;
        for(std::size_t i=0;i<candidates.size();++i)if(candidates[i].current_registered_actor&&candidates[i].unique_id==spec.id) {
            ++count;index=i;
        }
        if(count!=1)continue;
        const auto& e=candidates[index];
        if(e.key!=ObjectKey{2,spec.id}||e.condition!=spec.condition||e.event!=spec.event||e.matched_event!=spec.event||
           !e.active||!e.overlap_enabled||!e.in_walking_section||!std::isfinite(e.position.x)||!std::isfinite(e.position.y)||!std::isfinite(e.position.z))continue;
        result.push_back(index);
    }
    return result;
}
inline bool opening_needs_next_section(int chapter,int map,std::span<const OpeningContinuationEvidence> candidates) {
    if(chapter!=1||map!=10040||candidates.size()>1024)return false;
    for(const int earlier:{0,1}) {
        const auto matches=std::count_if(candidates.begin(),candidates.end(),[earlier](const auto& e) {
            return e.current_registered_actor&&e.unique_id==earlier;
        });
        if(matches>1)return false;
    }
    // The staged F event can authorize the handoff only after the earlier events
    // are resolved. Inspect native eligibility independently of section geometry.
    auto anywhere=std::vector<OpeningContinuationEvidence>(candidates.begin(),candidates.end());
    for(auto& candidate:anywhere)candidate.in_walking_section=true;
    const auto eligible=eligible_opening_continuations(chapter,map,anywhere);
    return eligible.size()==1&&candidates[eligible.front()].unique_id==2&&!candidates[eligible.front()].in_walking_section;
}
inline std::string opening_destination_epoch(std::string base,int chapter,int map,
        std::span<const OpeningContinuationEvidence> evidence) {
    if(chapter!=1||(map!=10040&&map!=10041))return base;
    base+="|opening:";
    for(const auto index:eligible_opening_continuations(chapter,map,evidence))
        base+=std::to_string(evidence[index].unique_id)+",";
    return base;
}
// Native eligibility remains observable through a menu. Announcements and movement
// keep their separate field-control gates.
template<class Capture> void capture_opening_story(StorySnapshot& snapshot,Capture capture) {
    if(!snapshot.ready||snapshot.world.empty()||snapshot.player.empty()||
        snapshot.objective.empty()||snapshot.objective_id.empty()||(snapshot.map_id!=10040&&snapshot.map_id!=10041))return;
    snapshot.destinations=capture(snapshot.destination_epoch);
}

}
