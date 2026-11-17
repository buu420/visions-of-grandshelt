#pragma once
#include "resonance/NavigationInput.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace resonance {
struct InputContextPolicy {
    bool known{};
    InputFact map_unit_context{InputFact::Unknown};
    InputFact blocking_context{InputFact::Unknown};
    InputFact text_entry{InputFact::Unknown};
};

inline InputContextPolicy evaluate_input_contexts(std::span<const InputContext> contexts) {
    // Exact-build GetAllIMCStackList keeps each parent mode separate from its
    // concatenated child capabilities. Unknown or malformed observations deny.
    if(contexts.empty()||contexts.size()>64)return {};
    constexpr std::array<std::uint8_t,26> blocked{
        3,4,5,6,7,8,9,10,11,13,15,16,18,19,20,21,22,23,24,25,26,28,29,30,31,33};
    constexpr std::array<std::uint8_t,7> map_capabilities{2,26,14,17,18,23,12};
    bool map=false,blocking=false,text=false;
    const auto check=[&](std::uint8_t type) {
        blocking|=std::find(blocked.begin(),blocked.end(),type)!=blocked.end();text|=type==25;
    };
    for(const auto& context:contexts) {
        if(context.parent>=34||context.children.size()>64||
           std::any_of(context.children.begin(),context.children.end(),[](auto type){return type>=34;}))return {};
        map|=context.parent==1;check(context.parent);
        for(auto child:context.children) {
            // These are present during ordinary field play, including menu-open
            // shortcuts. The same types under any other parent still block.
            if(context.parent==1&&std::find(map_capabilities.begin(),map_capabilities.end(),child)!=map_capabilities.end())continue;
            check(child);
        }
    }
    const auto fact=[](bool yes){return yes?InputFact::Yes:InputFact::No;};
    return {true,fact(map),fact(blocking),fact(text)};
}
}
