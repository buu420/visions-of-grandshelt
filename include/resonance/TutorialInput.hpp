#pragma once
#include "resonance/UnrealApi.hpp"
#include "resonance/Core.hpp"
#include <string>
namespace resonance {
struct TutorialControls {
    bool supported{},ready{};
    std::string instructions;
    bool readiness_known{};
};
inline void apply_tutorial_controls(MenuSnapshot& menu,const TutorialControls& controls) {
    if(!controls.supported)return;
    if(controls.readiness_known&&!controls.ready){menu.ready=false;menu.value.clear();return;}
    menu.value=controls.instructions;
    if(!controls.readiness_known&&menu.value.empty())menu.value="Tutorial controls are unavailable.";
}
// Read-only game-thread capture. Never closes a tutorial or generates input.
TutorialControls capture_tutorial_controls(resonance::unreal::UObject* active_layout);
}
