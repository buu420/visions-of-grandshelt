#pragma once
#include "resonance/ControllerInput.hpp"
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace resonance {
// Exact-build player-mappable defaults. Fixed menu/battle mappings remain
// native because the field-control gate is required before any capture.
// Unknown reassignments stay unavailable; an empty profile is not proof that
// fixed input mappings are absent. LT is explicitly preserved by user choice.
// RB defaults (nav-controller-fix-20261103/profile-rows.json 7738/9512/12422):
// the user-approved field Journal shortcut, battle Libra, and UICommon_NextPage,
// the UI_Common next-tab page (imc-mappings.tsv:163/179, consumed by menu tab
// frames) that stays native in menus because their contexts close the gate.
inline bool audited_modifier_mapping(ControllerModifier modifier,std::string_view name) {
    if(modifier==ControllerModifier::RightShoulder)return name=="Map_OpenMainMenu_Journal"||
        name=="UIBattle_DetailLibra_Pad"||name=="UIBattle_DetailLibra"||name=="UICommon_NextPage";
    return name=="MapUnit_Pinger"||name=="UIWorldMap_QuestList";
}
// reason is empty when supported; rejected lists the exact unaudited native names.
struct ControllerBindingDecision { bool supported{}; std::string reason; std::vector<std::string> rejected; };
inline ControllerBindingDecision controller_binding_decision(ControllerModifier modifier,bool button_known,
        std::span<const std::string> button_names,bool axis_known,
        std::span<const std::string> axis_names) {
    if(modifier==ControllerModifier::LeftTrigger)return {false,"LT is kept native",{}};
    if(!button_known)return {false,"binding could not be read",{}};
    if(modifier==ControllerModifier::RightTrigger) {
        if(!axis_known)return {false,"analog binding could not be read",{}};
        if(!axis_names.empty())return {false,"analog trigger assigned",{axis_names.begin(),axis_names.end()}};
    }
    ControllerBindingDecision result{true,{},{}};
    for(const auto& name:button_names)if(!audited_modifier_mapping(modifier,name))result.rejected.push_back(name);
    if(!result.rejected.empty()) {result.supported=false;result.reason="assigned to unaudited action";}
    return result;
}
inline bool controller_bindings_supported(ControllerModifier modifier,bool button_known,
        std::span<const std::string> button_names,bool axis_known,
        std::span<const std::string> axis_names) {
    return controller_binding_decision(modifier,button_known,button_names,axis_known,axis_names).supported;
}
// One stable log line: the modifier key's native mappings and the gate verdict.
inline std::string controller_binding_summary(ControllerModifier modifier,std::string_view key,bool known,
        std::span<const std::string> names,const ControllerBindingDecision& decision) {
    const auto join=[](std::span<const std::string> items) {
        std::string text;
        for(const auto& item:items){if(!text.empty())text+=',';text+=item;}
        return text;
    };
    const char* label=modifier==ControllerModifier::RightShoulder?"RB":modifier==ControllerModifier::RightTrigger?"RT":"LT";
    return std::string("modifier=")+label+" key="+std::string(key)+" read="+(known?"1":"0")+" mappings="+join(names)+
        " supported="+(decision.supported?"1":"0")+" reason="+decision.reason+" rejected="+join(decision.rejected);
}
}
