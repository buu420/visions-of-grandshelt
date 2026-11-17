#include "resonance/ControllerNavigation.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <tuple>

using namespace resonance;
namespace {
using Action=ControllerAction;
constexpr ControllerPolicy permitted{true,1};
void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
bool same(const ControllerPad& a,const ControllerPad& b) {
    return std::tie(a.connected,a.buttons,a.left_trigger,a.right_trigger,a.left_x,a.left_y,a.right_x,a.right_y)==
        std::tie(b.connected,b.buttons,b.left_trigger,b.right_trigger,b.left_x,b.left_y,b.right_x,b.right_y);
}
ControllerPad pad(std::uint16_t buttons=0,std::uint8_t left=0,std::uint8_t right=0) {
    ControllerPad p;p.connected=true;p.buttons=buttons;p.left_trigger=left;p.right_trigger=right;return p;
}
void silent(const ControllerDecision& out,const char* why) {require(out.action==Action::None,why);}
void captured(const ControllerDecision& out) {
    require(out.capturing&&same(out.game,pad()),"full capture must consume every native control and preserve connection");
}
void open(ControllerNavigation& nav) {
    silent(nav.observe(pad(),permitted),"neutral baseline must not issue a command");
    const auto out=nav.observe(pad(0,255),permitted);
    require(out.action==Action::Opened,"fresh LT must open a navigation transaction");captured(out);
}
struct Mapping {std::uint16_t button;Action action;bool closes;};
constexpr std::array<Mapping,8> mappings{{
    {0x0004,Action::PreviousCategory,false},{0x0008,Action::NextCategory,false},
    {0x0001,Action::PreviousTarget,false},{0x0002,Action::NextTarget,false},
    {0x0040,Action::Guidance,true},{0x0080,Action::Automatic,true},
    {0x2000,Action::Stop,true},{0x8000,Action::Help,false}}};

void native_bare_input_passthrough() {
    ControllerNavigation nav;
    for(std::uint32_t bit=1;bit<=0x8000;bit<<=1) {
        auto raw=pad(static_cast<std::uint16_t>(bit),0,255);
        raw.left_x=32767;raw.left_y=-32768;raw.right_x=12345;raw.right_y=-9876;
        const auto out=nav.observe(raw,permitted);
        require(out.action==Action::None&&!out.capturing&&same(out.game,raw),
            "bare buttons, sticks and RT must reach the game unchanged");
    }
    auto raw=pad(0xffff,63,255);raw.left_x=1;raw.right_y=-1;
    const auto out=nav.observe(raw,permitted);
    require(!out.capturing&&same(out.game,raw),"subthreshold LT must remain ordinary native input");
}
void opening_and_full_release() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0,1);raw.left_x=16000;raw.left_y=-17000;raw.right_x=18000;raw.right_y=-19000;
    auto out=nav.observe(raw,permitted);silent(out,"partial LT release must never reopen or repeat");captured(out);
    silent(nav.observe(pad(0,255),permitted),"LT jitter must not repeat Opened");
    silent(nav.observe(pad(),permitted),"full LT release must not issue a command");
    require(nav.observe(pad(0,64),permitted).action==Action::Opened,"a full release must permit the next LT press");
}
void initial_held_requires_release() {
    ControllerNavigation nav;
    for(auto value:{255,1,63,255}) {
        const auto raw=pad(0x0080,static_cast<std::uint8_t>(value));
        const auto out=nav.observe(raw,permitted);
        require(out.action==Action::None&&!out.capturing&&same(out.game,raw),
            "initially held LT must neither command nor borrow the controller before release");
    }
    nav.observe(pad(),permitted);
    silent(nav.observe(pad(0,50),permitted),"a fresh gradual press below threshold must not open early");
    require(nav.observe(pad(0,64),permitted).action==Action::Opened,"crossing the threshold after release must open");
}
bool dpad_button(std::uint16_t button) {return (button&0x000f)!=0;}
// D-pad commands settle for one confirming poll (no clock); the others are immediate.
ControllerDecision press(ControllerNavigation& nav,std::uint16_t button) {
    auto out=nav.observe(pad(button,255),permitted);
    if(dpad_button(button)) {
        silent(out,"a D-pad command waits for one confirming poll");captured(out);
        out=nav.observe(pad(button,255),permitted);
    }
    return out;
}
void mappings_and_no_repeat() {
    for(const auto& mapping:mappings) {
        ControllerNavigation nav;open(nav);
        auto out=press(nav,mapping.button);
        require(out.action==mapping.action,"each LT chord must invoke its documented command");captured(out);
        for(int repeat=0;repeat<8;++repeat)
            silent(nav.observe(pad(mapping.button,255),permitted),"holding a command must never repeat it");
        nav.observe(pad(0,255),permitted);
        out=mapping.closes?nav.observe(pad(mapping.button,255),permitted):press(nav,mapping.button);
        require(out.action==(mapping.closes?Action::None:mapping.action),
            "closing commands require modifier release; browsing commands allow fresh button edges");
    }
}
void buttons_held_before_open_need_fresh_edges() {
    for(const auto& mapping:mappings) {
        ControllerNavigation nav;
        nav.observe(pad(mapping.button),permitted);
        require(nav.observe(pad(mapping.button,255),permitted).action==Action::Opened,
            "a held button at LT opening must issue only the opening summary");
        for(int sample=0;sample<3;++sample)
            silent(nav.observe(pad(mapping.button,255),permitted),"buttons held before LT must not become commands");
        nav.observe(pad(0,255),permitted);
        require(press(nav,mapping.button).action==mapping.action,
            "releasing and pressing the held button must create a fresh command");
    }
}
void simultaneous_open_button_is_not_a_command() {
    ControllerNavigation nav;nav.observe(pad(),permitted);
    auto out=nav.observe(pad(0x0080,255),permitted);
    require(out.action==Action::Opened,"LT and R3 arriving together must open without starting automatic walking");
    captured(out);silent(nav.observe(pad(0x0080,255),permitted),"opening-sample button must not replay on the next sample");
    nav.observe(pad(0,255),permitted);
    require(nav.observe(pad(0x0080,255),permitted).action==Action::Automatic,"a later deliberate R3 edge must work");
}
void ambiguous_commands_require_new_edges() {
    ControllerNavigation nav;open(nav);
    silent(nav.observe(pad(0x000c,255),permitted),"opposing category commands must be ignored");
    silent(nav.observe(pad(0x0004,255),permitted),"releasing one ambiguous button must not replay the other");
    silent(nav.observe(pad(0x0084,255),permitted),"a fresh automatic command with another command held is ambiguous");
    nav.observe(pad(0,255),permitted);
    require(press(nav,0x0004).action==Action::PreviousCategory,
        "a released ambiguous chord must permit a later unambiguous edge");
}
void stop_has_priority_but_two_triggers_do_not_command() {
    ControllerNavigation nav;open(nav);
    require(nav.observe(pad(0x2084,255),permitted).action==Action::Stop,
        "a fresh Stop must take priority over ambiguous navigation commands");
    ControllerNavigation two;open(two);
    silent(two.observe(pad(0x2000,255,255),permitted),"both triggers must suppress even a fresh Stop command");
    silent(two.observe(pad(0x2000,255,0),permitted),"a command suppressed by both triggers must not replay later");
    two.observe(pad(0,255),permitted);
    require(two.observe(pad(0x2000,255),permitted).action==Action::Stop,"a later fresh Stop edge must work");
}
void both_triggers_do_not_open_late() {
    ControllerNavigation nav;nav.observe(pad(0,0,255),permitted);
    silent(nav.observe(pad(0,255,255),permitted),"both triggers held must not announce an opening");
    silent(nav.observe(pad(0x0004,255,0),permitted),"releasing RT while LT stays held must not open or command late");
    nav.observe(pad(),permitted);
    require(nav.observe(pad(0,255),permitted).action==Action::Opened,"full LT release must recover from the ambiguous trigger chord");
}
void closing_command_tails_preserve_new_controls() {
    for(const auto& mapping:mappings) if(mapping.closes) {
        ControllerNavigation nav;open(nav);
        auto raw=pad(mapping.button,255);raw.left_x=20000;raw.right_x=-22000;
        auto out=nav.observe(raw,permitted);require(out.action==mapping.action,"closing command must run once");captured(out);
        raw.buttons|=0x1000;raw.left_y=23000;raw.right_y=5000;raw.right_trigger=200;
        out=nav.observe(raw,permitted);
        require(out.action==Action::None&&!out.capturing,"closing command must end full capture immediately after its sample");
        require(out.game.buttons==0x1000&&out.game.left_trigger==0&&out.game.left_x==0&&out.game.right_x==0,
            "closing button, LT and captured axes must stay consumed while held");
        require(out.game.left_y==23000&&out.game.right_y==5000&&out.game.right_trigger==200,
            "new perpendicular axes and a new trigger must pass after closing");
        raw.buttons=0x8000;
        out=nav.observe(raw,permitted);
        require(out.action==Action::None&&out.game.buttons==0x8000,
            "new buttons must pass without reopening the closed browse transaction");
    }
}
void release_tails_clear_independently() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0x1000,255,180);raw.left_x=20000;raw.right_y=-22000;
    captured(nav.observe(raw,permitted));
    raw.left_trigger=0;raw.buttons|=0x2000;raw.left_y=17000;
    auto out=nav.observe(raw,permitted);
    require(out.action==Action::None&&!out.capturing&&out.game.buttons==0x2000,
        "LT release must consume old buttons while preserving newly pressed menu buttons");
    require(out.game.left_x==0&&out.game.right_y==0&&out.game.right_trigger==0&&out.game.left_y==17000,
        "only the held axes and trigger from capture may be consumed after release");
    // End the new perpendicular input as well, returning the stick to neutral drift.
    raw.buttons=0x2000;raw.left_x=1;raw.left_y=0;raw.right_y=-1;raw.right_trigger=0;
    out=nav.observe(raw,permitted);
    require(out.game.left_x==0&&out.game.right_y==0,"the neutralizing tail sample must be consumed");
    raw.buttons=0x3000;raw.left_x=24000;raw.right_y=-23000;raw.right_trigger=180;
    out=nav.observe(raw,permitted);
    require(same(out.game,raw),"released tail controls must all become native again independently");
}
void stick_drift_releases_the_tail() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0x0080,255);raw.left_x=20000;
    require(nav.observe(raw,permitted).action==Action::Automatic,"automatic command must close capture");
    raw.left_x=1;nav.observe(raw,permitted);
    raw.left_x=20000;
    require(nav.observe(raw,permitted).game.left_x==20000,"tiny stick drift must release a captured axis for fresh native movement");
}
void diagonal_release_tails_stay_consumed() {
    ControllerNavigation nav;open(nav);
    // 6000*sqrt(2)/32767 is about .259: outside the radial deadzone.
    auto raw=pad(0,255);raw.left_x=6000;raw.left_y=6000;raw.right_x=-6000;raw.right_y=-6000;
    captured(nav.observe(raw,permitted));raw.left_trigger=0;
    for(int sample=0;sample<5;++sample) {
        const auto out=nav.observe(raw,permitted);
        require(out.action==Action::None&&!out.capturing&&out.game.left_x==0&&out.game.left_y==0&&
            out.game.right_x==0&&out.game.right_y==0,
            "a captured diagonal beyond .25 must remain consumed on every LT-release tail sample");
    }
}
void diagonal_tails_do_not_release_at_component_threshold() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0,255);raw.left_x=10000;raw.left_y=10000;raw.right_x=-10000;raw.right_y=-10000;
    captured(nav.observe(raw,permitted));
    // The shorter diagonal still has radial magnitude about .302.
    raw.left_trigger=0;raw.left_x=7000;raw.left_y=7000;raw.right_x=-7000;raw.right_y=-7000;
    for(int sample=0;sample<5;++sample) {
        const auto out=nav.observe(raw,permitted);
        require(out.game.left_x==0&&out.game.left_y==0&&out.game.right_x==0&&out.game.right_y==0,
            "components dropping below .25 must not release a stick that remains radially active");
    }
}
void diagonal_drift_release_allows_fresh_walk() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0,255);raw.left_x=6000;raw.left_y=6000;raw.right_x=-6000;raw.right_y=-6000;
    captured(nav.observe(raw,permitted));raw.left_trigger=0;
    auto out=nav.observe(raw,permitted);
    require(out.game.left_x==0&&out.game.left_y==0&&out.game.right_x==0&&out.game.right_y==0,
        "diagonal movement must be held back until the stick returns to its radial deadzone");
    raw.left_x=1;raw.left_y=-1;raw.right_x=-1;raw.right_y=1;
    out=nav.observe(raw,permitted);
    require(out.game.left_x==0&&out.game.left_y==0&&out.game.right_x==0&&out.game.right_y==0,
        "the radially neutral drift sample must consume and release the old tails");
    raw.left_x=20000;raw.left_y=20000;raw.right_x=-20000;raw.right_y=-20000;
    require(same(nav.observe(raw,permitted).game,raw),"neutral drift must let a later fresh diagonal reach native movement");
}
void radial_tails_preserve_fresh_perpendicular_input() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0x0080,255);raw.left_x=20000;raw.right_y=-20000;
    auto out=nav.observe(raw,permitted);
    require(out.action==Action::Automatic,"automatic must close capture before testing its axis tails");captured(out);
    raw.left_x=6000;raw.left_y=20000;raw.right_x=-20000;raw.right_y=-6000;
    for(int sample=0;sample<5;++sample) {
        out=nav.observe(raw,permitted);
        require(out.game.left_x==0&&out.game.right_y==0,
            "captured components must stay hidden while the stick remains radially active");
        require(out.game.left_y==20000&&out.game.right_x==-20000,
            "a component not held during capture must pass fresh perpendicular input immediately");
    }
}
void zero_component_releases_independently_in_active_stick() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0,255);raw.left_x=6000;raw.left_y=6000;
    captured(nav.observe(raw,permitted));raw.left_trigger=0;raw.left_x=0;raw.left_y=20000;
    auto out=nav.observe(raw,permitted);
    require(out.game.left_x==0&&out.game.left_y==0,"a zeroed component must not release the other active captured component");
    raw.left_x=12000;
    out=nav.observe(raw,permitted);
    require(out.game.left_x==12000&&out.game.left_y==0,
        "an individually zeroed component must become native while the other captured component stays held");
}
void permission_loss_filters_prior_capture_only() {
    ControllerNavigation nav;open(nav);
    auto raw=pad(0x0004,255);raw.left_x=20000;captured(nav.observe(raw,permitted));
    raw.buttons|=0x1000;raw.left_y=-24000;raw.right_trigger=190;
    auto out=nav.observe(raw,{false,1});
    require(out.action==Action::None&&!out.capturing&&out.game.buttons==0x1000&&out.game.left_trigger==0&&out.game.left_x==0,
        "permission/focus loss must retain only the already captured held controls");
    require(out.game.left_y==-24000&&out.game.right_trigger==190,"new menu axes and RT must pass on the context-loss sample");
    raw.buttons=0x2000;
    out=nav.observe(raw,{false,1});
    require(out.game.buttons==0x2000,"fresh menu Cancel must remain usable while old LT is held");
    silent(nav.observe(raw,permitted),"regaining permission with held LT must not replay commands");
    raw.buttons=0x0080;
    silent(nav.observe(raw,permitted),"fresh buttons with stale LT after focus regain must not command");
    nav.observe(pad(),permitted);
    require(nav.observe(pad(0,255),permitted).action==Action::Opened,"focus regain must recover after full LT release");
}
void epoch_change_requires_release() {
    ControllerNavigation nav;open(nav);
    auto out=nav.observe(pad(0x1000,255),{true,2});
    require(out.action==Action::None&&!out.capturing&&out.game.buttons==0x1000&&out.game.left_trigger==0,
        "epoch change must close capture and preserve buttons first seen in the new context");
    silent(nav.observe(pad(0x0080,255),{true,2}),"epoch change must not allow a held-LT command in the new context");
    nav.observe(pad(),{true,2});
    require(nav.observe(pad(0,255),{true,2}).action==Action::Opened,"new context must accept a released then fresh LT");
    ControllerNavigation simultaneous;simultaneous.observe(pad(),permitted);
    silent(simultaneous.observe(pad(0,255),{true,2}),"a fresh LT on the epoch-change sample must wait for another full release");
}
void denied_context_does_not_borrow_native_controls() {
    ControllerNavigation nav;
    auto raw=pad(0xffff,255,255);raw.left_x=20000;raw.right_y=-22000;
    for(int i=0;i<3;++i) {
        const auto out=nav.observe(raw,{false,1});
        require(out.action==Action::None&&!out.capturing&&same(out.game,raw),"unpermitted native controls must pass unchanged without prior capture");
    }
    silent(nav.observe(raw,permitted),"permission regain must not activate held inputs");
    silent(nav.observe(pad(0x0080,255),permitted),"new R3 after permission regain needs LT release first");
    nav.observe(pad(),permitted);
    require(nav.observe(pad(0,255),permitted).action==Action::Opened,"permission regain must rearm after neutral");
}
void disconnect_and_reset_never_replay() {
    ControllerNavigation nav;open(nav);
    nav.observe(pad(0x0080,255),permitted);
    auto disconnected=pad(0xffff,255,255);disconnected.connected=false;disconnected.left_x=23456;
    auto out=nav.observe(disconnected,permitted);
    require(out.action==Action::None&&!out.capturing&&same(out.game,disconnected),"disconnect must abandon controller state without synthesizing input");
    const auto held=pad(0x0080,255);
    out=nav.observe(held,permitted);
    require(out.action==Action::None&&!out.capturing&&same(out.game,held),"reconnect must neither replay nor inherit old device tails");
    silent(nav.observe(pad(0x0040,255),permitted),"reconnected LT must require release before new commands");
    nav.observe(pad(),permitted);
    require(nav.observe(pad(0,255),permitted).action==Action::Opened,"reconnected neutral controller must allow a fresh opening");
    nav.reset();
    out=nav.observe(held,permitted);
    require(out.action==Action::None&&!out.capturing&&same(out.game,held),"reset must restore initial held-input protection");
}
void raw_override_requires_left_stick_movement() {
    require(!controller_physical_input(pad()),"neutral controller is not physical override");
    for(std::uint32_t bit=1;bit<=0x8000;bit<<=1)
        require(!controller_physical_input(pad(static_cast<std::uint16_t>(bit))),
            "a raw button alone cannot cancel walking before the game classifies its action");
    auto raw=pad(0,255,255);raw.right_x=32767;raw.right_y=-32768;
    require(!controller_physical_input(raw),"camera and trigger input can coexist with automatic walking");
    raw=pad();
    raw.left_x=8191;require(!controller_physical_input(raw),"positive axis within 0.25 must not override");
    raw.left_x=8192;require(controller_physical_input(raw),"positive axis above normalized 0.25 must override");
    raw.left_x=-8192;require(!controller_physical_input(raw),"negative axis exactly at normalized 0.25 must not override");
    raw.left_x=-8193;require(controller_physical_input(raw),"negative axis above normalized 0.25 must override");
    raw.left_x=5000;raw.left_y=5000;
    require(!controller_physical_input(raw),"small diagonal stick drift must not override");
    raw.left_x=6000;raw.left_y=6000;
    require(controller_physical_input(raw),"diagonal magnitude above 0.25 must override even when each component is smaller");
    raw.left_x=-32768;raw.left_y=32767;
    require(controller_physical_input(raw),"extreme signed stick values must not overflow");
    raw.connected=false;
    require(!controller_physical_input(raw),"disconnected controller data must not cancel walking");
}
// --- D-pad settling: rolled and diagonal presses on a real rocker D-pad -------------------
constexpr std::uint16_t dpad_up=0x0001,dpad_down=0x0002,dpad_left=0x0004,dpad_right=0x0008;
void open_at(ControllerNavigation& nav,std::uint64_t t) {
    silent(nav.observe(pad(),permitted,t),"neutral clocked baseline must not issue a command");
    const auto out=nav.observe(pad(0,255),permitted,t+16);
    require(out.action==Action::Opened,"fresh clocked LT must open a navigation transaction");captured(out);
}
void rolled_dpad_press_resolves_to_final_direction() {
    // The user presses Down; the rocker registers Right for one poll, then Right|Down, then Down.
    ControllerNavigation nav;open_at(nav,1000);
    auto out=nav.observe(pad(dpad_right,255),permitted,1100);
    require(out.action==Action::None,"a single poll of a perpendicular direction must not switch the category");
    captured(out);
    silent(nav.observe(pad(dpad_right|dpad_down,255),permitted,1108),"the rolled diagonal must stay silent");
    silent(nav.observe(pad(dpad_down,255),permitted,1116),"the final direction must wait for its settle window");
    silent(nav.observe(pad(dpad_down,255),permitted,1133),"the settle window is not over after 17 ms");
    out=nav.observe(pad(dpad_down,255),permitted,1116+controller_dpad_settle_ms);
    require(out.action==Action::NextTarget,"a rolled press must resolve to the direction that settled");
    require(out.dpad_diagnostic==ControllerDpadDiagnostic::RolledPress&&out.dpad_chord==dpad_right,
        "a rolled press must report the first chord it saw for diagnostics");
    captured(out);
    silent(nav.observe(pad(dpad_down,255),permitted,1200),"holding the settled direction must never repeat");
    silent(nav.observe(pad(0,255),permitted,1216),"releasing the D-pad after a resolved press must stay silent");
    out=nav.observe(pad(dpad_up,255),permitted,1300);silent(out,"a fresh press waits for its own settle window");
    require(nav.observe(pad(dpad_up,255),permitted,1300+controller_dpad_settle_ms).action==Action::PreviousTarget,
        "the next clean press must command again");
}
void diagonal_first_press_resolves_when_reduced() {
    ControllerNavigation nav;open_at(nav,2000);
    silent(nav.observe(pad(dpad_down|dpad_right,255),permitted,2100),"an adjacent diagonal must not command");
    silent(nav.observe(pad(dpad_down,255),permitted,2116),"the remaining direction must wait for its settle window");
    const auto out=nav.observe(pad(dpad_down,255),permitted,2116+controller_dpad_settle_ms);
    require(out.action==Action::NextTarget,"a press that starts diagonal must resolve to the direction left held");
    require(out.dpad_diagnostic==ControllerDpadDiagnostic::RolledPress&&out.dpad_chord==(dpad_down|dpad_right),
        "the diagnostic must name the diagonal that started the press");
    silent(nav.observe(pad(dpad_down,255),permitted,2300),"a resolved press must not repeat");
}
void clean_dpad_press_commands_once_after_settle() {
    ControllerNavigation nav;open_at(nav,3000);
    silent(nav.observe(pad(dpad_down,255),permitted,3100),"the first poll of a D-pad press is not yet a command");
    silent(nav.observe(pad(dpad_down,255),permitted,3100+controller_dpad_settle_ms-1),"one millisecond short of the window stays silent");
    auto out=nav.observe(pad(dpad_down,255),permitted,3100+controller_dpad_settle_ms);
    require(out.action==Action::NextTarget&&out.dpad_diagnostic==ControllerDpadDiagnostic::None,
        "a clean press commands exactly when the settle window elapses and reports nothing unusual");
    for(int sample=0;sample<8;++sample)
        silent(nav.observe(pad(dpad_down,255),permitted,3200+sample*16),"holding a settled D-pad command must never repeat it");
    silent(nav.observe(pad(0,255),permitted,3400),"release after a settled command is silent");
    silent(nav.observe(pad(dpad_left,255),permitted,3500),"a new press starts a new settle window");
    require(nav.observe(pad(dpad_left,255),permitted,3500+controller_dpad_settle_ms).action==Action::PreviousCategory,
        "a later clean horizontal press commands the category");
}
void quick_dpad_tap_commits_on_release() {
    ControllerNavigation nav;open_at(nav,4000);
    silent(nav.observe(pad(dpad_up,255),permitted,4100),"a tap's first poll is not yet a command");
    const auto out=nav.observe(pad(0,255),permitted,4110);
    require(out.action==Action::PreviousTarget,"a tap released before the settle window must commit on release");
    require(out.dpad_diagnostic==ControllerDpadDiagnostic::None,"a clean tap is not unusual");
    captured(out);
    silent(nav.observe(pad(0,255),permitted,4126),"the sample after a committed tap is silent");
    silent(nav.observe(pad(dpad_right|dpad_down,255),permitted,4200),"a diagonal tap start is ambiguous");
    silent(nav.observe(pad(0,255),permitted,4210),"a diagonal released at once must not guess a direction");
}
void ambiguous_dpad_presses_report_and_stay_silent() {
    ControllerNavigation nav;open_at(nav,5000);
    for(int sample=0;sample<8;++sample)
        silent(nav.observe(pad(dpad_down|dpad_right,255),permitted,5100+sample*16),"a diagonal held past the window must not command");
    auto out=nav.observe(pad(0,255),permitted,5300);
    require(out.action==Action::None&&out.dpad_diagnostic==ControllerDpadDiagnostic::AmbiguousAbandoned&&
        out.dpad_chord==(dpad_down|dpad_right),"a diagonal released without resolving must be reported once");
    captured(out);
    out=nav.observe(pad(0,255),permitted,5316);
    require(out.dpad_diagnostic==ControllerDpadDiagnostic::None,"the diagnostic must not repeat on later samples");
    silent(nav.observe(pad(dpad_left|dpad_right,255),permitted,5400),"opposing directions are ambiguous");
    silent(nav.observe(pad(dpad_left,255),permitted,5416),"an opposing chord reduced to one direction must not replay it");
    silent(nav.observe(pad(dpad_left,255),permitted,5500),"an abandoned opposing press stays silent through the window");
    out=nav.observe(pad(0,255),permitted,5600);
    require(out.dpad_diagnostic==ControllerDpadDiagnostic::AmbiguousAbandoned&&out.dpad_chord==(dpad_left|dpad_right),
        "an opposing press reports the chord that made it ambiguous");
}
void dpad_with_other_commands_keeps_existing_rules() {
    ControllerNavigation nav;open_at(nav,6000);
    silent(nav.observe(pad(dpad_down,255),permitted,6100),"the D-pad press is pending");
    silent(nav.observe(pad(dpad_down|0x0080,255),permitted,6108),"R3 arriving during a D-pad press is ambiguous");
    silent(nav.observe(pad(dpad_down,255),permitted,6116),"releasing R3 must not revive the D-pad press");
    silent(nav.observe(pad(dpad_down,255),permitted,6200),"the spoiled press stays silent past the window");
    silent(nav.observe(pad(0,255),permitted,6216),"the spoiled press ends quietly");
    silent(nav.observe(pad(dpad_down,255),permitted,6300),"a new press is pending again");
    require(nav.observe(pad(dpad_down|0x2000,255),permitted,6308).action==Action::Stop,"a fresh Stop keeps priority during a D-pad press");
    ControllerNavigation release;open_at(release,6400);
    silent(release.observe(pad(dpad_down,255),permitted,6500),"a short tap is still pending");
    require(release.observe(pad(0x2000,255),permitted,6508).action==Action::Stop,
        "a fresh Stop keeps priority when the D-pad releases on the same poll");
    ControllerNavigation trigger;open_at(trigger,6600);
    silent(trigger.observe(pad(dpad_down,255),permitted,6700),"a direction precedes the other trigger");
    silent(trigger.observe(pad(dpad_down,255,255),permitted,6708),"the other trigger blocks the pending direction");
    silent(trigger.observe(pad(0,255),permitted,6716),"releasing both controls cannot replay the direction");
    ControllerNavigation help;open_at(help,7000);
    require(help.observe(pad(0x8000,255),permitted,7100).action==Action::Help,"Help stays immediate");
    ControllerNavigation held;held.observe(pad(dpad_down),permitted,8000);
    require(held.observe(pad(dpad_down,255),permitted,8016).action==Action::Opened,"opening with a held D-pad only summarises");
    for(int sample=0;sample<8;++sample)
        silent(held.observe(pad(dpad_down,255),permitted,8032+sample*16),"a D-pad held before opening never settles into a command");
    held.observe(pad(0,255),permitted,8200);
    silent(held.observe(pad(dpad_down,255),permitted,8300),"a fresh edge after opening starts a settle window");
    require(held.observe(pad(dpad_down,255),permitted,8300+controller_dpad_settle_ms).action==Action::NextTarget,
        "the fresh edge commands after settling");
}
void sample_clock_confirms_with_one_extra_poll() {
    ControllerNavigation nav;open(nav);
    silent(nav.observe(pad(dpad_down,255),permitted),"without a clock the first poll is not yet a command");
    require(nav.observe(pad(dpad_down,255),permitted).action==Action::NextTarget,"without a clock one confirming poll commands");
    silent(nav.observe(pad(dpad_down,255),permitted),"no repeat without a clock either");
    nav.observe(pad(0,255),permitted);
    silent(nav.observe(pad(dpad_right,255),permitted),"a rolled first poll without a clock is silent");
    silent(nav.observe(pad(dpad_right|dpad_down,255),permitted),"the diagonal is silent");
    silent(nav.observe(pad(dpad_down,255),permitted),"the final direction needs its confirming poll");
    require(nav.observe(pad(dpad_down,255),permitted).action==Action::NextTarget,"the confirming poll resolves the rolled press");
}

void filtering_never_synthesizes_input() {
    ControllerNavigation nav;std::uint32_t state=0x72ac31b5;
    const auto next=[&]() {state=1664525*state+1013904223;return state;};
    for(std::uint64_t i=0;i<12000;++i) {
        auto raw=pad(static_cast<std::uint16_t>(next()),static_cast<std::uint8_t>(next()),static_cast<std::uint8_t>(next()));
        raw.connected=(i%29)!=0;raw.left_x=static_cast<std::int16_t>(next());raw.left_y=static_cast<std::int16_t>(next());
        raw.right_x=static_cast<std::int16_t>(next());raw.right_y=static_cast<std::int16_t>(next());
        if(i%7==0)raw.left_trigger=0;
        const auto out=nav.observe(raw,{i%19<15,i/73});
        const auto& game=out.game;
        require(game.connected==raw.connected&&(game.buttons&~raw.buttons)==0,"filter must never create connection or button input");
        require((game.left_trigger==0||game.left_trigger==raw.left_trigger)&&(game.right_trigger==0||game.right_trigger==raw.right_trigger)&&
            (game.left_x==0||game.left_x==raw.left_x)&&(game.left_y==0||game.left_y==raw.left_y)&&
            (game.right_x==0||game.right_x==raw.right_x)&&(game.right_y==0||game.right_y==raw.right_y),
            "filter must only preserve or consume original analog values");
    }
}
}

int main() {
    const std::array tests{
        std::pair{"native_bare_input_passthrough",native_bare_input_passthrough},
        std::pair{"opening_and_full_release",opening_and_full_release},
        std::pair{"initial_held_requires_release",initial_held_requires_release},
        std::pair{"mappings_and_no_repeat",mappings_and_no_repeat},
        std::pair{"buttons_held_before_open_need_fresh_edges",buttons_held_before_open_need_fresh_edges},
        std::pair{"simultaneous_open_button_is_not_a_command",simultaneous_open_button_is_not_a_command},
        std::pair{"ambiguous_commands_require_new_edges",ambiguous_commands_require_new_edges},
        std::pair{"stop_has_priority_but_two_triggers_do_not_command",stop_has_priority_but_two_triggers_do_not_command},
        std::pair{"both_triggers_do_not_open_late",both_triggers_do_not_open_late},
        std::pair{"closing_command_tails_preserve_new_controls",closing_command_tails_preserve_new_controls},
        std::pair{"release_tails_clear_independently",release_tails_clear_independently},
        std::pair{"stick_drift_releases_the_tail",stick_drift_releases_the_tail},
        std::pair{"diagonal_release_tails_stay_consumed",diagonal_release_tails_stay_consumed},
        std::pair{"diagonal_tails_do_not_release_at_component_threshold",diagonal_tails_do_not_release_at_component_threshold},
        std::pair{"diagonal_drift_release_allows_fresh_walk",diagonal_drift_release_allows_fresh_walk},
        std::pair{"radial_tails_preserve_fresh_perpendicular_input",radial_tails_preserve_fresh_perpendicular_input},
        std::pair{"zero_component_releases_independently_in_active_stick",zero_component_releases_independently_in_active_stick},
        std::pair{"permission_loss_filters_prior_capture_only",permission_loss_filters_prior_capture_only},
        std::pair{"epoch_change_requires_release",epoch_change_requires_release},
        std::pair{"denied_context_does_not_borrow_native_controls",denied_context_does_not_borrow_native_controls},
        std::pair{"disconnect_and_reset_never_replay",disconnect_and_reset_never_replay},
        std::pair{"rolled_dpad_press_resolves_to_final_direction",rolled_dpad_press_resolves_to_final_direction},
        std::pair{"diagonal_first_press_resolves_when_reduced",diagonal_first_press_resolves_when_reduced},
        std::pair{"clean_dpad_press_commands_once_after_settle",clean_dpad_press_commands_once_after_settle},
        std::pair{"quick_dpad_tap_commits_on_release",quick_dpad_tap_commits_on_release},
        std::pair{"ambiguous_dpad_presses_report_and_stay_silent",ambiguous_dpad_presses_report_and_stay_silent},
        std::pair{"dpad_with_other_commands_keeps_existing_rules",dpad_with_other_commands_keeps_existing_rules},
        std::pair{"sample_clock_confirms_with_one_extra_poll",sample_clock_confirms_with_one_extra_poll},
        std::pair{"raw_override_requires_left_stick_movement",raw_override_requires_left_stick_movement},
        std::pair{"filtering_never_synthesizes_input",filtering_never_synthesizes_input}};
    int failed=0;
    for(const auto& [name,test]:tests) {
        try {test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e) {++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    std::cout<<tests.size()-failed<<'/'<<tests.size()<<" controller navigation tests passed\n";
    return failed?1:0;
}
