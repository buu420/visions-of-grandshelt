#include "resonance/ControllerBindings.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace resonance;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try {
    // The exact names GetMappingNamesForKey(Gamepad_RightShoulder) returns from the
    // exact-build default key profile (nav-controller-fix-20261103/profile-rows.json,
    // offsets 7738/9512/12422; controller-facts.json profile list).
    const std::array<std::string,3> rb_profile{"Map_OpenMainMenu_Journal","UIBattle_DetailLibra","UICommon_NextPage"};
    const std::array<std::string,2> rb{"Map_OpenMainMenu_Journal","UIBattle_DetailLibra_Pad"};
    const std::array<std::string,2> rt{"MapUnit_Pinger","UIWorldMap_QuestList"};
    const std::array<std::string,1> confirm{"UICommon_Decide"},unknown{"FutureFieldAction"};
    const std::array<std::string,1> next_page{"UICommon_NextPage"},previous_page{"UICommon_PreviewPage"};
    const std::array<std::string,4> rb_and_interact{"Map_OpenMainMenu_Journal","UIBattle_DetailLibra","UICommon_NextPage","MapUnit_Interact"};
    need(controller_bindings_supported(ControllerModifier::RightShoulder,true,rb_profile,false,{}),
        "the exact-build default RB profile (Journal shortcut, battle Libra, menu next page) is supportable during eligible field play");
    need(controller_bindings_supported(ControllerModifier::RightShoulder,true,rb,false,{}),"the audited RB defaults remain supportable during eligible field play");
    need(controller_bindings_supported(ControllerModifier::RightTrigger,true,rt,true,{}),"the audited RT defaults remain supportable during eligible field play");
    need(!controller_bindings_supported(ControllerModifier::LeftTrigger,true,{},true,{}),"LT cannot be reserved after the user explicitly kept its native actions");
    need(!controller_bindings_supported(ControllerModifier::RightShoulder,false,{},false,{}),"failed profile reads cannot enable capture");
    need(!controller_bindings_supported(ControllerModifier::RightShoulder,true,confirm,false,{}),"an unexpected Confirm reassignment cannot be swallowed as an audited shortcut");
    need(!controller_bindings_supported(ControllerModifier::RightShoulder,true,unknown,false,{}),"unknown native actions stay protected");
    need(!controller_bindings_supported(ControllerModifier::RightShoulder,true,rb_and_interact,false,{}),
        "a field action reassigned onto RB still blocks capture even beside the audited defaults");
    need(!controller_bindings_supported(ControllerModifier::RightShoulder,true,previous_page,false,{}),
        "the LB previous-page default moved onto RB is not RB's audited default");
    need(!controller_bindings_supported(ControllerModifier::RightTrigger,true,next_page,true,{}),
        "RB's next-page default does not make an RT reassignment supportable");
    need(!controller_bindings_supported(ControllerModifier::RightTrigger,true,rt,false,{}),"RT requires its analog binding to be readable too");
    need(!controller_bindings_supported(ControllerModifier::RightTrigger,true,rt,true,unknown),"a custom analog trigger mapping cannot be captured");

    // The decision explains a refusal with the exact rejected native names.
    const auto accepted=controller_binding_decision(ControllerModifier::RightShoulder,true,rb_profile,false,{});
    need(accepted.supported&&accepted.reason.empty()&&accepted.rejected.empty(),"an accepted RB profile has no refusal reason");
    const auto interact=controller_binding_decision(ControllerModifier::RightShoulder,true,rb_and_interact,false,{});
    need(!interact.supported&&interact.rejected==std::vector<std::string>{"MapUnit_Interact"}&&
        interact.reason=="assigned to unaudited action","a refusal names only the unaudited mapping");
    const auto unreadable=controller_binding_decision(ControllerModifier::RightShoulder,false,{},false,{});
    need(!unreadable.supported&&unreadable.reason=="binding could not be read","an unreadable profile is reported as unreadable, not as a conflict");
    const auto kept=controller_binding_decision(ControllerModifier::LeftTrigger,true,{},true,{});
    need(!kept.supported&&kept.reason=="LT is kept native","LT refusals say LT stays native");
    const auto analog=controller_binding_decision(ControllerModifier::RightTrigger,true,rt,true,unknown);
    need(!analog.supported&&analog.reason=="analog trigger assigned"&&analog.rejected==std::vector<std::string>{"FutureFieldAction"},
        "an RT analog mapping is reported with its name");
    const auto analog_unread=controller_binding_decision(ControllerModifier::RightTrigger,true,rt,false,{});
    need(!analog_unread.supported&&analog_unread.reason=="analog binding could not be read","an unreadable RT analog binding is reported");

    // One stable diagnostic line for the log: modifier, key, read state, names, verdict.
    const auto line=controller_binding_summary(ControllerModifier::RightShoulder,"Gamepad_RightShoulder",true,rb_and_interact,interact);
    need(line=="modifier=RB key=Gamepad_RightShoulder read=1 mappings=Map_OpenMainMenu_Journal,UIBattle_DetailLibra,UICommon_NextPage,MapUnit_Interact"
        " supported=0 reason=assigned to unaudited action rejected=MapUnit_Interact","the binding diagnostic lists every mapping and the rejected ones");
    const auto unread_line=controller_binding_summary(ControllerModifier::RightShoulder,"Gamepad_RightShoulder",false,{},unreadable);
    need(unread_line=="modifier=RB key=Gamepad_RightShoulder read=0 mappings= supported=0 reason=binding could not be read rejected=",
        "an unreadable binding is visible in the diagnostic");
    std::cout<<"Controller modifier binding and LT preservation checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
