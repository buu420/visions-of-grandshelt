#include "resonance/ControllerNavigation.hpp"

namespace resonance {
namespace {
constexpr std::uint8_t left_x=1,left_y=2,right_x=4,right_y=8,left_trigger=16,right_trigger=32;
constexpr std::uint16_t dpad=ControllerDpadUp|ControllerDpadDown|ControllerDpadLeft|ControllerDpadRight;
constexpr auto commands=dpad|ControllerL3|ControllerR3|ControllerB|ControllerY;
double normalized(std::int16_t value) {return value/(value<0?32768.0:32767.0);}
bool stick_held(std::int16_t x,std::int16_t y) {
    const auto nx=normalized(x),ny=normalized(y);return nx*nx+ny*ny>.25*.25;
}
std::uint8_t held_axes(const ControllerPad& raw) {
    const bool left=stick_held(raw.left_x,raw.left_y),right=stick_held(raw.right_x,raw.right_y);
    return static_cast<std::uint8_t>((left&&raw.left_x!=0?left_x:0)|(left&&raw.left_y!=0?left_y:0)|
        (right&&raw.right_x!=0?right_x:0)|(right&&raw.right_y!=0?right_y:0)|
        (raw.left_trigger?left_trigger:0)|(raw.right_trigger?right_trigger:0));
}
bool single(std::uint16_t bits) {return bits!=0&&(bits&(bits-1))==0;}
// Up with Down or Left with Right cannot be one deliberate direction. An adjacent
// diagonal can be the rocker on its way to one direction and is given time to settle.
bool opposing(std::uint16_t chord) {
    return (chord&(ControllerDpadUp|ControllerDpadDown))==(ControllerDpadUp|ControllerDpadDown)||
        (chord&(ControllerDpadLeft|ControllerDpadRight))==(ControllerDpadLeft|ControllerDpadRight);
}
ControllerAction action_for(std::uint16_t button) {
    switch(button) {
    case ControllerDpadLeft:return ControllerAction::PreviousCategory;
    case ControllerDpadRight:return ControllerAction::NextCategory;
    case ControllerDpadUp:return ControllerAction::PreviousTarget;
    case ControllerDpadDown:return ControllerAction::NextTarget;
    case ControllerL3:return ControllerAction::Guidance;
    case ControllerR3:return ControllerAction::Automatic;
    case ControllerB:return ControllerAction::Stop;
    case ControllerY:return ControllerAction::Help;
    default:return ControllerAction::None;
    }
}
bool closes(ControllerAction action) {
    return action==ControllerAction::Guidance||action==ControllerAction::Automatic||action==ControllerAction::Stop;
}
}

ControllerDecision ControllerNavigation::observe(const ControllerPad& raw,ControllerPolicy policy,std::uint64_t now_ms) {
    return observe_sample(raw,policy,true,now_ms);
}
ControllerDecision ControllerNavigation::observe(const ControllerPad& raw,ControllerPolicy policy) {
    return observe_sample(raw,policy,false,0);
}
ControllerDecision ControllerNavigation::observe_sample(const ControllerPad& raw,ControllerPolicy policy,bool has_clock,std::uint64_t now_ms) {
    ControllerDecision out{ControllerAction::None,raw,false};
    const bool boundary=!observed_||previous_.connected!=raw.connected||
        policy_.permitted!=policy.permitted||policy_.epoch!=policy.epoch;
    // LT opens above .25, then stays held through a partial release. Only zero
    // rearms it; trigger noise cannot open a second transaction.
    const bool modifier=raw.connected&&raw.left_trigger!=0&&(modifier_||raw.left_trigger>=64);
    const auto pressed=static_cast<std::uint16_t>(raw.buttons&~previous_.buttons);
    const auto end_dpad_press=[&]() {dpad_pressed_=dpad_spent_=dpad_ambiguous_=false;dpad_chord_=dpad_first_chord_=dpad_ambiguous_chord_=0;dpad_stable_polls_=0;};
    const auto require_dpad_release=[&]() {
        end_dpad_press();
        dpad_pressed_=dpad_spent_=(raw.buttons&dpad)!=0;
    };
    const auto close_capture=[&]() {capturing_=false;end_dpad_press();};

    if(!raw.connected) {
        close_capture();waiting_for_release_=true;tail_buttons_=tail_axes_=0;
    } else {
        if(boundary||!policy.permitted) {
            close_capture();waiting_for_release_=raw.left_trigger!=0;
        }
        if(raw.left_trigger==0) {close_capture();waiting_for_release_=false;}
        if(!boundary&&policy.permitted&&!capturing_&&!waiting_for_release_&&!modifier_&&modifier) {
            if(raw.right_trigger>=64) waiting_for_release_=true;
            else {
                capturing_=true;out.action=ControllerAction::Opened;
                // A D-pad held while opening only produces the summary. It must be
                // released and pressed again before it can command.
                require_dpad_release();
            }
        }
        if(capturing_) {
            if(raw.right_trigger>=64)require_dpad_release();
            else if(out.action!=ControllerAction::Opened) {
                const auto held=static_cast<std::uint16_t>(raw.buttons&commands);
                const auto chord=static_cast<std::uint16_t>(raw.buttons&dpad);
                const auto other=static_cast<std::uint16_t>(held&~dpad);
                if((pressed&ControllerB)!=0) out.action=ControllerAction::Stop;
                else if(other!=0&&single(held)&&(pressed&held)!=0) out.action=action_for(held);
                if(chord!=0) {
                    // A D-pad press is one transaction from its first non-zero chord to
                    // release. It commands at most once, when a single direction has
                    // settled, so a rocker transient cannot pick the wrong direction.
                    if(!dpad_pressed_) {
                        dpad_pressed_=true;dpad_spent_=dpad_ambiguous_=false;
                        dpad_first_chord_=chord;dpad_chord_=0;dpad_ambiguous_chord_=0;
                    }
                    if(chord!=dpad_chord_) {dpad_chord_=chord;dpad_stable_since_=now_ms;dpad_stable_polls_=0;}
                    else ++dpad_stable_polls_;
                    if(other!=0) dpad_spent_=true; // Another command in the chord spoils this press.
                    if(opposing(chord)&&!dpad_spent_) {dpad_spent_=dpad_ambiguous_=true;dpad_ambiguous_chord_=chord;}
                    if(!dpad_spent_&&single(chord)) {
                        const bool settled=dpad_stable_polls_>=1&&
                            (!has_clock||now_ms-dpad_stable_since_>=controller_dpad_settle_ms);
                        if(settled) {
                            out.action=action_for(chord);dpad_spent_=true;
                            if(dpad_first_chord_!=chord) {out.dpad_diagnostic=ControllerDpadDiagnostic::RolledPress;out.dpad_chord=dpad_first_chord_;}
                        }
                    }
                } else if(dpad_pressed_) {
                    if(other==0&&!dpad_spent_&&single(dpad_chord_)) {
                        // A tap shorter than the settle window ended as one clear direction.
                        out.action=action_for(dpad_chord_);
                        if(dpad_first_chord_!=dpad_chord_) {out.dpad_diagnostic=ControllerDpadDiagnostic::RolledPress;out.dpad_chord=dpad_first_chord_;}
                    } else if(other==0&&(!dpad_spent_||dpad_ambiguous_)) {
                        out.dpad_diagnostic=ControllerDpadDiagnostic::AmbiguousAbandoned;
                        out.dpad_chord=dpad_ambiguous_?dpad_ambiguous_chord_:dpad_chord_;
                    }
                    end_dpad_press();
                }
            }
            // Snapshot only controls actually captured. New controls arriving
            // after closing/context loss must remain usable in native menus.
            tail_buttons_=raw.buttons;tail_axes_=held_axes(raw);
            out.game=ControllerPad{};out.game.connected=raw.connected;out.capturing=true;
            if(closes(out.action)) {close_capture();waiting_for_release_=true;}
        } else {
            out.game.buttons&=static_cast<std::uint16_t>(~tail_buttons_);
            tail_buttons_&=raw.buttons;
            const auto held=held_axes(raw);
            const auto filter=[&](auto& game,std::uint8_t axis) {
                if((tail_axes_&axis)!=0) {
                    game=0;
                    if((held&axis)==0)tail_axes_&=static_cast<std::uint8_t>(~axis);
                }
            };
            // A diagonal remains held until its radial magnitude is inside .25.
            // Neutral drift releases the stick; an individually zeroed component
            // also releases without capturing a new perpendicular component.
            filter(out.game.left_x,left_x);
            filter(out.game.left_y,left_y);
            filter(out.game.right_x,right_x);
            filter(out.game.right_y,right_y);
            filter(out.game.left_trigger,left_trigger);
            filter(out.game.right_trigger,right_trigger);
        }
    }
    previous_=raw;policy_=policy;modifier_=modifier;observed_=true;
    return out;
}
void ControllerNavigation::reset() {*this=ControllerNavigation{};}
bool controller_physical_input(const ControllerPad& raw) {
    // Only the stick needs a fresh physical poll. Button-driven movement
    // comes from the game's mapped action after navigation capture.
    return raw.connected&&stick_held(raw.left_x,raw.left_y);
}
}
