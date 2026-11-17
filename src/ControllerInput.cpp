#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <MinHook.h>
#include "resonance/ControllerInput.hpp"
#include "resonance/Speech.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>

namespace resonance {
namespace {
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
constexpr unsigned long long permission_lifetime=150,connection_lifetime=500;
constexpr std::uint16_t battle_dpad=ControllerDpadUp|ControllerDpadDown|ControllerDpadLeft|ControllerDpadRight;
constexpr std::uint16_t battle_panel_buttons=battle_dpad|ControllerL3|ControllerB|ControllerY;

struct BattleButtons {
    std::pair<BattleControllerAction,std::uint16_t> observe(
        const ControllerPad& raw,ControllerPolicy policy,std::uint64_t now) {
        const bool stable=raw.connected&&previous_.connected&&policy.permitted&&
            policy_.permitted&&policy.epoch==policy_.epoch;
        const auto pressed=raw.buttons&~previous_.buttons;
        previous_=raw;
        policy_=policy;
        tail_&=raw.buttons;
        auto action=BattleControllerAction::None;
        if(!stable) {
            open_=false;
            direction_=0;
            spent_=false;
        } else if(!open_) {
            if((pressed&ControllerL3)&&(raw.buttons&battle_panel_buttons)==ControllerL3) {
                open_=true;
                tail_|=ControllerL3;
                action=BattleControllerAction::Opened;
            }
        } else {
            tail_|=raw.buttons&battle_panel_buttons;
            if(pressed&(ControllerL3|ControllerB)) {
                open_=false;
                direction_=0;
                spent_=false;
                action=BattleControllerAction::Closed;
            } else {
                const auto direction=static_cast<std::uint16_t>(raw.buttons&battle_dpad);
                if(direction!=direction_) {
                    direction_=direction;
                    since_=now;
                }
                if(!direction) {
                    spent_=false;
                    if(pressed&ControllerY)action=BattleControllerAction::Gauges;
                } else if(!spent_&&std::has_single_bit(direction)&&now-since_>=controller_dpad_settle_ms) {
                    spent_=true;
                    switch(direction) {
                    case ControllerDpadLeft: action=BattleControllerAction::Previous;break;
                    case ControllerDpadRight: action=BattleControllerAction::Next;break;
                    case ControllerDpadUp: action=BattleControllerAction::Vitals;break;
                    case ControllerDpadDown: action=BattleControllerAction::Effects;break;
                    }
                }
            }
        }
        return {action,tail_};
    }
private:
    ControllerPad previous_{};
    ControllerPolicy policy_{};
    std::uint16_t tail_{},direction_{};
    std::uint64_t since_{};
    bool open_{},spent_{};
};
struct Device {
    ControllerNavigation navigation;
    BattleButtons battle;
    ControllerPad raw;
    unsigned long long sampled{};
    bool grace_trigger{},grace_r3{};
    bool field_rb_held{};
    std::uint64_t modifier_press{},r3_press{};
    XINPUT_GAMEPAD returned{};
    DWORD packet{};
    bool has_returned{},virtual_packet{};
    bool walking_connected{};
    unsigned dpad_diagnostics{};
};
struct Pending {ControllerAction action;std::uint64_t epoch,generation;unsigned device;std::uint64_t modifier_press,r3_press;};
struct Backend {
    std::mutex mutex;
    std::array<Device,4> devices;
    std::array<Pending,16> pending{};
    std::size_t count{};
    Pending starter{};
    bool starter_valid{};
    GetState original{};
    std::atomic<bool> enabled{};
    std::atomic<bool> stopped{};
    bool ready{},permitted{},reserve_field_rb{};
    bool configured{},retryable{};
    unsigned long long attempted{};
    bool sampled_permission{};
    ControllerModifier modifier{ControllerModifier::LeftTrigger};
    bool (*foreground)(){};
    std::uint64_t epoch{},generation{1};
    unsigned long long published{};
    bool reading_field{};
    bool battle_permitted{},battle_sampled{};
    std::uint64_t battle_epoch{},battle_generation{1},battle_published{};
    std::array<BattleControllerAction,8> battle_pending{};
    std::size_t battle_count{};
};
// The DLL and its hook stay resident, matching the reader's existing pinned
// callback lifetime. Disabling makes the hook a transparent forwarder.
Backend& backend(){static auto* value=new Backend;return *value;}
ControllerPad pad(const XINPUT_GAMEPAD& p,bool connected) {
    return {connected,p.wButtons,p.bLeftTrigger,p.bRightTrigger,p.sThumbLX,p.sThumbLY,p.sThumbRX,p.sThumbRY};
}
XINPUT_GAMEPAD gamepad(const ControllerPad& p) {
    return {p.buttons,p.left_trigger,p.right_trigger,p.left_x,p.left_y,p.right_x,p.right_y};
}
ControllerPad gesture_pad(const ControllerPad& raw,ControllerModifier modifier) {
    auto result=raw;
    if(modifier==ControllerModifier::RightTrigger) {
        result.left_trigger=raw.right_trigger;result.right_trigger=raw.left_trigger;
    } else if(modifier==ControllerModifier::RightShoulder) {
        result.left_trigger=(raw.buttons&XINPUT_GAMEPAD_RIGHT_SHOULDER)?255:0;
        result.buttons&=static_cast<std::uint16_t>(~XINPUT_GAMEPAD_RIGHT_SHOULDER);
    }
    return result;
}
ControllerPad native_pad(const ControllerPad& filtered,const ControllerPad& raw,ControllerModifier modifier) {
    auto result=filtered;
    if(modifier==ControllerModifier::RightTrigger)result.right_trigger=filtered.left_trigger;
    else if(modifier==ControllerModifier::RightShoulder) {
        result.buttons&=static_cast<std::uint16_t>(~XINPUT_GAMEPAD_RIGHT_SHOULDER);
        if(filtered.left_trigger)result.buttons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;
    }
    // The chosen replacement modifier must never take ownership of native LT.
    if(modifier!=ControllerModifier::LeftTrigger)result.left_trigger=raw.left_trigger;
    return result;
}
bool same(const XINPUT_GAMEPAD& a,const XINPUT_GAMEPAD& b) {
    return a.wButtons==b.wButtons&&a.bLeftTrigger==b.bLeftTrigger&&a.bRightTrigger==b.bRightTrigger&&
        a.sThumbLX==b.sThumbLX&&a.sThumbLY==b.sThumbLY&&a.sThumbRX==b.sThumbRX&&a.sThumbRY==b.sThumbRY;
}
unsigned connections(const Backend& b,unsigned long long now) {
    unsigned count=0;
    for(const auto& d:b.devices)if(d.raw.connected&&now>=d.sampled&&now-d.sampled<=connection_lifetime)++count;
    return count;
}
void update_starter_release(Device& device,const ControllerPad& raw,ControllerModifier modifier) {
    if(!gesture_pad(raw,modifier).left_trigger)device.grace_trigger=false;
    if(!(raw.buttons&XINPUT_GAMEPAD_RIGHT_THUMB))device.grace_r3=false;
}
DWORD WINAPI hooked_get_state(DWORD index,XINPUT_STATE* state) noexcept {
    auto& b=backend();
    if(!b.original)return ERROR_DEVICE_NOT_CONNECTED;
    const auto result=b.original(index,state);
    if(b.stopped.load(std::memory_order_acquire)||!b.enabled.load(std::memory_order_acquire)||index>=b.devices.size())return result;
    try {
        const auto now=GetTickCount64();
        const bool focus=b.foreground&&b.foreground();
        std::string diagnostic;
        {
        std::lock_guard lock(b.mutex);
        if(b.stopped.load(std::memory_order_acquire)||!b.enabled.load(std::memory_order_acquire))return result;
        auto& d=b.devices[index];
        const auto before=gesture_pad(d.raw,b.modifier);
        d.raw=result==ERROR_SUCCESS&&state?pad(state->Gamepad,true):ControllerPad{};
        d.sampled=now;
        const auto gesture=gesture_pad(d.raw,b.modifier);
        if(gesture.connected&&gesture.left_trigger&&(!before.connected||!before.left_trigger))++d.modifier_press;
        if(gesture.connected&&(gesture.buttons&XINPUT_GAMEPAD_RIGHT_THUMB)&&
           (!before.connected||!(before.buttons&XINPUT_GAMEPAD_RIGHT_THUMB)))++d.r3_press;
        // Release belongs to the physical sample, even if the game thread has
        // not queried override between this release and a later re-press.
        if(!gesture.left_trigger)d.grace_trigger=false;
        if(!(gesture.buttons&XINPUT_GAMEPAD_RIGHT_THUMB))d.grace_r3=false;
        bool native_lt=false;
        if(b.modifier!=ControllerModifier::LeftTrigger)for(const auto& device:b.devices)
            native_lt|=device.raw.connected&&now>=device.sampled&&now-device.sampled<=connection_lifetime&&device.raw.left_trigger>=64;
        const bool focused_pad=focus&&connections(b,now)==1;
        const bool field_sample=focused_pad&&now>=b.published&&
            (b.reading_field||now-b.published<=permission_lifetime);
        const bool permitted=b.permitted&&field_sample&&!native_lt;
        if(b.sampled_permission&&!permitted&&before.left_trigger&&gesture.left_trigger) {
            const char* reason=!focus?"lost focus":!focused_pad?"controller connection changed":
                native_lt?"native LT":!b.permitted?"field control changed":"field sample expired";
            diagnostic=std::string("CONTROLLER held navigation input interrupted: ")+reason;
        }
        if(permitted!=b.sampled_permission){++b.generation;b.sampled_permission=permitted;}
        if(!permitted)b.count=0;
        const auto decision=d.navigation.observe(gesture,{permitted,b.generation},now);
        if(decision.action!=ControllerAction::None&&permitted&&b.count<b.pending.size())
            b.pending[b.count++]={decision.action,b.epoch,b.generation,index,d.modifier_press,d.r3_press};
        const bool battle=b.battle_permitted&&focused_pad&&!permitted&&now>=b.battle_published&&
            now-b.battle_published<=permission_lifetime;
        if(battle!=b.battle_sampled) {
            b.battle_sampled=battle;
            ++b.battle_generation;
            b.battle_count=0;
        }
        const auto [battle_action,battle_tail]=d.battle.observe(d.raw,{battle,b.battle_generation},now);
        if(battle_action!=BattleControllerAction::None&&b.battle_count<b.battle_pending.size())
            b.battle_pending[b.battle_count++]=battle_action;
        if(b.modifier==ControllerModifier::RightShoulder) {
            const bool was_held=d.field_rb_held;
            const bool reserved=field_sample&&!battle&&(b.permitted||b.reserve_field_rb);
            // The first sample of a restored field context can require a
            // release before it can command. Do not send that press to Journal
            // or promote its held tail into a fresh menu or Libra press.
            d.field_rb_held=(d.raw.buttons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0&&(reserved||was_held);
            if(d.field_rb_held&&!was_held&&decision.game.left_trigger)
                diagnostic="CONTROLLER reserved field RB until release";
        }
        // One line per unusual D-pad press, so a report of "the wrong category
        // changed" can be checked against what the controller actually sent.
        if(decision.dpad_diagnostic!=ControllerDpadDiagnostic::None&&d.dpad_diagnostics<64) {
            ++d.dpad_diagnostics;
            char chord[8];std::snprintf(chord,sizeof chord,"0x%04x",decision.dpad_chord);
            diagnostic=decision.dpad_diagnostic==ControllerDpadDiagnostic::RolledPress?
                "CONTROLLER dpad rolled press first_chord="+std::string(chord)+" resolved_action="+std::to_string(static_cast<int>(decision.action)):
                "CONTROLLER dpad ambiguous press chord="+std::string(chord)+" released without a command";
            if(d.dpad_diagnostics==64)diagnostic+=" (further D-pad diagnostics suppressed)";
        }
        if(result==ERROR_SUCCESS&&state) {
            auto filtered=gamepad(native_pad(decision.game,d.raw,b.modifier));
            filtered.wButtons&=static_cast<WORD>(~battle_tail);
            if(d.field_rb_held)filtered.wButtons&=static_cast<WORD>(~XINPUT_GAMEPAD_RIGHT_SHOULDER);
            // The native packet number describes raw state. Capture/release can
            // change the game-visible state without a raw packet change.
            // Keep a private monotonic packet stream for this hooked consumer.
            if(!d.virtual_packet&&!same(state->Gamepad,filtered))d.virtual_packet=true;
            if(!d.virtual_packet||!d.has_returned)d.packet=state->dwPacketNumber;
            else if(!same(d.returned,filtered))++d.packet;
            d.has_returned=true;
            d.returned=filtered;state->Gamepad=filtered;state->dwPacketNumber=d.packet;
        } else {d.grace_trigger=d.grace_r3=false;d.has_returned=d.virtual_packet=false;}
        }
        if(!diagnostic.empty())log(diagnostic); // Outside the backend lock; the logger has its own.
    }catch(...) {
        b.enabled.store(false,std::memory_order_release);
    }
    return result;
}
}
bool initialize_controller_input(bool (*foreground)(),ControllerModifier modifier) {
    auto& b=backend();
    std::lock_guard lock(b.mutex);
    if(b.stopped.load(std::memory_order_acquire))return false;
    if(b.ready)return b.enabled.load(std::memory_order_acquire);
    const bool first_attempt=!b.configured;
    b.foreground=foreground;b.modifier=modifier;b.configured=true;b.retryable=false;b.attempted=GetTickCount64();
    HMODULE module=nullptr;
    // The exact executable delay-imports only this provider. A legacy provider
    // loaded by another component must not make initialization succeed early.
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"XINPUT1_4.dll",&module);
    if(!module){b.retryable=true;if(first_attempt)log("CONTROLLER waiting for the game to load its XInput provider");return false;}
    auto* target=reinterpret_cast<void*>(GetProcAddress(module,"XInputGetState"));
    if(!target){log("CONTROLLER unavailable: provider has no XInputGetState");return false;}
    HMODULE owner{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(target),&owner)){log("CONTROLLER unavailable: provider function could not be pinned");return false;}
    const auto initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){log("CONTROLLER hook init failed status="+std::to_string(initialized));return false;}
    const auto created=MH_CreateHook(target,reinterpret_cast<void*>(&hooked_get_state),reinterpret_cast<void**>(&b.original));
    if(created!=MH_OK){log("CONTROLLER hook create failed status="+std::to_string(created));return false;}
    const auto enabled=MH_EnableHook(target);
    if(enabled!=MH_OK){MH_RemoveHook(target);b.original=nullptr;log("CONTROLLER hook enable failed status="+std::to_string(enabled));return false;}
    b.ready=true;b.enabled.store(!b.stopped.load(std::memory_order_acquire),std::memory_order_release);
    if(b.stopped.load(std::memory_order_acquire))return false;
    log("CONTROLLER XInput state capture ready; native field permission and modifier binding validation are still required");
    return true;
}
bool retry_controller_input() {
    auto& b=backend();
    bool (*foreground)(){};ControllerModifier modifier{};
    {
        std::lock_guard lock(b.mutex);
        if(b.stopped.load(std::memory_order_acquire)||!b.configured)return false;
        if(b.ready)return b.enabled.load(std::memory_order_acquire);
        const auto now=GetTickCount64();
        if(!b.retryable||now<b.attempted||now-b.attempted<2000)return false;
        foreground=b.foreground;modifier=b.modifier;
    }
    return initialize_controller_input(foreground,modifier);
}
ControllerModifier controller_modifier(){return backend().modifier;}
void disable_controller_input() {
    auto& b=backend();b.stopped.store(true,std::memory_order_release);
    std::lock_guard lock(b.mutex);b.enabled.store(false,std::memory_order_release);b.permitted=false;b.reserve_field_rb=false;b.count=0;
    b.reading_field=false;
    b.battle_permitted=false;b.battle_count=0;
}
ControllerBackendStatus controller_backend_status() {
    auto& b=backend();const auto now=GetTickCount64();std::lock_guard lock(b.mutex);
    ControllerBackendStatus result{b.ready&&b.enabled.load(std::memory_order_acquire),connections(b,now),false};
    for(const auto& d:b.devices)if(d.raw.connected&&now-d.sampled<=connection_lifetime) {
        result.modifier_down|=gesture_pad(d.raw,b.modifier).left_trigger>=64;
        if(result.connected==1)result.buttons=d.raw.buttons;
    }
    return result;
}
void publish_controller_permission(bool permitted,std::uint64_t epoch,bool reserve_field_rb) {
    auto& b=backend();std::lock_guard lock(b.mutex);
    const auto now=GetTickCount64();
    if(b.permitted!=permitted||b.epoch!=epoch||
       (permitted&&(now<b.published||now-b.published>permission_lifetime))) {
        ++b.generation;b.count=0;
    }
    if(!permitted)b.count=0;
    b.permitted=permitted;b.reserve_field_rb=reserve_field_rb;b.epoch=epoch;b.published=now;b.reading_field=false;
}
void begin_controller_field_read(std::uint64_t read_started) {
    auto& b=backend();
    std::lock_guard lock(b.mutex);
    b.reading_field=(b.permitted||b.reserve_field_rb)&&read_started>=b.published&&
        read_started-b.published<=permission_lifetime;
}
void renew_controller_after_field_read(std::uint64_t read_started) {
    const auto now=GetTickCount64();
    auto& b=backend();
    std::lock_guard lock(b.mutex);
    const bool reading=b.reading_field;
    b.reading_field=false;
    if(reading&&now>=read_started)
        b.published=now;
}
std::vector<ControllerAction> take_controller_actions(std::uint64_t epoch) {
    auto& b=backend();std::lock_guard lock(b.mutex);std::vector<ControllerAction> result;
    b.starter_valid=false;
    for(std::size_t i=0;i<b.count;++i)if(b.pending[i].epoch==epoch&&b.pending[i].generation==b.generation) {
        result.push_back(b.pending[i].action);
        if(b.pending[i].action==ControllerAction::Automatic){b.starter=b.pending[i];b.starter_valid=true;}
    }
    b.count=0;return result;
}
void publish_battle_controller_permission(bool permitted,std::uint64_t epoch) {
    auto& b=backend();
    std::lock_guard lock(b.mutex);
    const auto now=GetTickCount64();
    if(b.battle_permitted!=permitted||b.battle_epoch!=epoch||
       (permitted&&(now<b.battle_published||now-b.battle_published>permission_lifetime))) {
        ++b.battle_generation;
        b.battle_count=0;
    }
    b.battle_permitted=permitted;
    b.battle_epoch=epoch;
    b.battle_published=now;
}
std::vector<BattleControllerAction> take_battle_controller_actions(std::uint64_t epoch) {
    auto& b=backend();
    std::lock_guard lock(b.mutex);
    std::vector<BattleControllerAction> result;
    if(b.battle_permitted&&b.battle_epoch==epoch)
        result.assign(b.battle_pending.begin(),b.battle_pending.begin()+b.battle_count);
    b.battle_count=0;
    return result;
}
ControllerWalkingState controller_walking_state(bool refresh_connections) {
    auto& b=backend();
    std::lock_guard lock(b.mutex);
    if(!b.ready||!b.enabled.load(std::memory_order_acquire)||!b.original)return {};
    ControllerWalkingState result{.ready=true};
    for(DWORD index=0;index<b.devices.size();++index) {
        auto& device=b.devices[index];
        if(!refresh_connections&&!device.raw.connected&&!device.walking_connected)continue;
        XINPUT_STATE current{};
        const auto status=b.original(index,&current);
        device.walking_connected=status==ERROR_SUCCESS;
        if(status==ERROR_DEVICE_NOT_CONNECTED) {
            device.grace_trigger=device.grace_r3=false;
            continue;
        }
        if(status!=ERROR_SUCCESS) {result.read_failed=true;continue;}
        ++result.connected;
        const auto raw=pad(current.Gamepad,true);
        update_starter_release(device,raw,b.modifier);
        result.physical_input|=controller_physical_input(raw);
        result.starter_held|=device.grace_trigger||device.grace_r3;
    }
    return result;
}
void controller_automatic_starter_grace() {
    auto& b=backend();const auto now=GetTickCount64();std::lock_guard lock(b.mutex);
    for(auto& d:b.devices)d.grace_trigger=d.grace_r3=false;
    if(b.starter_valid&&b.starter.generation==b.generation&&b.starter.epoch==b.epoch&&b.starter.device<b.devices.size()) {
        auto& d=b.devices[b.starter.device];
        if(d.raw.connected&&now>=d.sampled&&now-d.sampled<=connection_lifetime) {
            d.grace_trigger=d.modifier_press==b.starter.modifier_press&&gesture_pad(d.raw,b.modifier).left_trigger!=0;
            d.grace_r3=d.r3_press==b.starter.r3_press&&(d.raw.buttons&XINPUT_GAMEPAD_RIGHT_THUMB)!=0;
        }
    }
    b.starter_valid=false;
}
void clear_controller_starter_grace() {
    auto& b=backend();std::lock_guard lock(b.mutex);b.starter_valid=false;for(auto& d:b.devices)d.grace_trigger=d.grace_r3=false;
}
}
