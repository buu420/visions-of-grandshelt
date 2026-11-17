#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include "resonance/ControllerInput.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace resonance {void log(const std::string& text){std::cout<<text<<'\n';}}
namespace {
std::atomic<bool> focused{true};
bool foreground(){return focused.load();}
void need(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
}
int main(int argc,char** argv){try {
    need(argc==3,"the test requires its private fake input DLL and modifier");
    const auto modifier=std::string(argv[2])=="RB"?resonance::ControllerModifier::RightShoulder:
        std::string(argv[2])=="RT"?resonance::ControllerModifier::RightTrigger:resonance::ControllerModifier::LeftTrigger;
    const auto file=std::filesystem::absolute(argv[1]);
    need(GetModuleHandleW(L"XINPUT1_4.dll")==nullptr,"the private provider must not already be loaded");
    const auto legacy_file=file.parent_path()/L"XINPUT1_3.dll";
    auto legacy=LoadLibraryExW(legacy_file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    need(legacy!=nullptr,"the unrelated private legacy provider must load");
    need(GetModuleHandleW(L"XINPUT1_4.dll")==nullptr,"an unrelated provider must not satisfy the game's modern provider lookup");
    need(!resonance::initialize_controller_input(foreground,modifier),"startup without a loaded provider must remain unavailable");
    need(resonance::controller_modifier()==modifier,"the configured RB/RT choice must survive delayed provider loading");
    auto module=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    need(module!=nullptr,"private fake XInput DLL must load");
    using Set=void(*)(DWORD,BOOL,const XINPUT_STATE*);
    using Get=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    auto set=reinterpret_cast<Set>(GetProcAddress(module,"FakeSetController"));
    auto get=reinterpret_cast<Get>(GetProcAddress(module,"XInputGetState"));
    need(set&&get,"fake input test API is complete");
    need(!resonance::retry_controller_input(),"provider retries must not spin every frame");
    Sleep(2050);
    need(resonance::retry_controller_input(),"the real backend must recover after the game loads its provider later");
    XINPUT_STATE raw{},out{};raw.dwPacketNumber=100;
    auto sample=[&](WORD buttons=0,BYTE lt=0,BYTE rt=0,SHORT lx=0){
        ++raw.dwPacketNumber;raw.Gamepad={buttons,lt,rt,lx,0,0,0};
        if(modifier==resonance::ControllerModifier::RightTrigger){raw.Gamepad.bRightTrigger=lt;raw.Gamepad.bLeftTrigger=rt;}
        if(modifier==resonance::ControllerModifier::RightShoulder){raw.Gamepad.bLeftTrigger=rt;raw.Gamepad.bRightTrigger=0;if(lt>=64)raw.Gamepad.wButtons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;}
        set(0,TRUE,&raw);
        need(get(0,&out)==ERROR_SUCCESS,"a successful original input result is preserved");
    };
    resonance::publish_controller_permission(true,1);sample();
    need(out.dwPacketNumber==raw.dwPacketNumber&&out.Gamepad.wButtons==0,"ordinary input must pass through including the native packet number");
    sample();
    need(out.dwPacketNumber==raw.dwPacketNumber,"a provider packet change without a visible button change remains native before capture");
    sample(XINPUT_GAMEPAD_A);
    need(out.dwPacketNumber==raw.dwPacketNumber&&out.Gamepad.wButtons==XINPUT_GAMEPAD_A,"bare Confirm retains its complete native sample");
    sample();sample(0,255);
    auto actions=resonance::take_controller_actions(1);
    need(actions.size()==1&&actions[0]==resonance::ControllerAction::Opened,"fresh permitted LT opens navigation");
    const auto modifier_down=[&](){return modifier==resonance::ControllerModifier::RightShoulder?
        (out.Gamepad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0:modifier==resonance::ControllerModifier::RightTrigger?out.Gamepad.bRightTrigger!=0:out.Gamepad.bLeftTrigger!=0;};
    need(!modifier_down(),"the captured navigation modifier is not passed to the game");
    sample(XINPUT_GAMEPAD_DPAD_RIGHT,255);
    need(resonance::take_controller_actions(1).empty(),"a D-pad press settles before it can command");
    need(out.Gamepad.wButtons==0,"a settling D-pad press is already captured");
    Sleep(resonance::controller_dpad_settle_ms+5);
    sample(XINPUT_GAMEPAD_DPAD_RIGHT,255);
    actions=resonance::take_controller_actions(1);
    need(actions.size()==1&&actions[0]==resonance::ControllerAction::NextCategory,"LT plus D-pad reaches the shared category action");
    need(out.Gamepad.wButtons==0,"category browsing cannot move the character");
    sample(0,255);sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    actions=resonance::take_controller_actions(1);
    need(actions.size()==1&&actions[0]==resonance::ControllerAction::Automatic,"a fresh stick click requests automatic walking");
    need(out.Gamepad.wButtons==0,"the start click cannot reach its native action");
    resonance::controller_automatic_starter_grace();
    need(!resonance::controller_walking_state().physical_input,"the held starting LT/R3 chord cannot immediately cancel its own automatic walking");
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255,0,30000);
    need(resonance::controller_walking_state().physical_input,"fresh physical movement cancels automatic walking during starter grace");
    sample();need(!resonance::controller_walking_state().physical_input,"released starter controls clear grace");
    sample(XINPUT_GAMEPAD_RIGHT_THUMB);
    need(!resonance::controller_walking_state().physical_input&&out.Gamepad.wButtons==XINPUT_GAMEPAD_RIGHT_THUMB,"a later bare R3 is native input and leaves automatic walking active");
    sample();sample(0,255);
    need(!resonance::controller_walking_state().physical_input,"opening navigation again does not take over walking");
    actions=resonance::take_controller_actions(1);
    need(actions.size()==1&&actions[0]==resonance::ControllerAction::Opened,"the new modifier press still opens navigation");
    focused=false;sample(XINPUT_GAMEPAD_A,255);
    need((out.Gamepad.wButtons&XINPUT_GAMEPAD_A)!=0,"a new unrelated input passes through after focus loss; only closing tails remain consumed");
    need(resonance::take_controller_actions(1).empty(),"focus loss cannot emit a navigation action");
    focused=true;resonance::publish_controller_permission(true,1);sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    need(resonance::take_controller_actions(1).empty(),"returning with a held trigger cannot start automatic walking");
    sample();sample(0,255);resonance::take_controller_actions(1);
    resonance::publish_controller_permission(false,2);sample(XINPUT_GAMEPAD_A,255);
    need((out.Gamepad.wButtons&XINPUT_GAMEPAD_A)!=0&&resonance::take_controller_actions(2).empty(),"menus preserve new native input and discard pending field actions");
    sample();resonance::publish_controller_permission(true,2);sample(0,255);resonance::take_controller_actions(2);
    Sleep(175);sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    need(resonance::take_controller_actions(2).empty(),"expired native permission cannot start automatic walking");
    sample();resonance::publish_controller_permission(true,3);sample();
    XINPUT_STATE second{};set(1,TRUE,&second);need(get(1,&out)==ERROR_SUCCESS,"second controller result remains successful");
    need(resonance::controller_backend_status().connected==2,"multiple connected controllers must remain explicit");
    sample(0,255);need(resonance::take_controller_actions(3).empty(),"ambiguous controller ownership cannot navigate");
    set(1,FALSE,nullptr);need(get(1,&out)==ERROR_DEVICE_NOT_CONNECTED,"native disconnection errors remain unchanged");
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    need(resonance::take_controller_actions(3).empty(),"returning to a single controller with held LT cannot navigate");
    sample();resonance::publish_controller_permission(true,4);sample();sample(0,255);
    if(modifier!=resonance::ControllerModifier::LeftTrigger) {
        sample(XINPUT_GAMEPAD_RIGHT_THUMB,255,200);
        need(out.Gamepad.bLeftTrigger==200,"LT retains its native sample even while the replacement navigation modifier is held");
        need(resonance::take_controller_actions(4).empty(),"native LT takes priority and revokes pending navigation actions");
    } else {
        focused=false;sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);focused=true;
        need(resonance::take_controller_actions(4).empty(),"asynchronous focus loss drops queued navigation before the next native tick");
    }
    sample();resonance::publish_controller_permission(true,5);sample();sample(0,255);resonance::take_controller_actions(5);
    resonance::publish_controller_permission(false,5);
    resonance::publish_controller_permission(true,5); // No GetState call during denial.
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    need(resonance::take_controller_actions(5).empty(),"a brief unsampled permission loss must require a fresh modifier press");
    sample();resonance::publish_controller_permission(true,6);sample();sample(0,255);resonance::take_controller_actions(6);
    Sleep(175);resonance::publish_controller_permission(true,6); // Resume before the next input sample.
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    need(resonance::take_controller_actions(6).empty(),"an unsampled expired permission interval must not resume a held modifier");
    sample();resonance::publish_controller_permission(true,10);sample();sample(0,255);
    resonance::take_controller_actions(10);
    auto read_started=GetTickCount64();
    resonance::begin_controller_field_read(read_started);
    Sleep(175);
    sample(0,255); // Steam/Input can poll while the game thread is reading a route.
    resonance::renew_controller_after_field_read(read_started);
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    actions=resonance::take_controller_actions(10);
    need(actions.size()==1&&actions[0]==resonance::ControllerAction::Automatic,
        "a game-thread route read keeps the focused field gesture captured");

    sample();resonance::publish_controller_permission(true,17);sample();sample(0,255);
    resonance::take_controller_actions(17);
    read_started=GetTickCount64();
    resonance::begin_controller_field_read(read_started);
    Sleep(110);
    resonance::renew_controller_after_field_read(read_started);
    Sleep(65);
    sample(XINPUT_GAMEPAD_DPAD_RIGHT,255);
    Sleep(resonance::controller_dpad_settle_ms+5);
    sample(XINPUT_GAMEPAD_DPAD_RIGHT,255);
    actions=resonance::take_controller_actions(17);
    need(actions.size()==1&&actions[0]==resonance::ControllerAction::NextCategory,
        "a completed field read leaves a full permission window for the next game frame");

    sample();resonance::publish_controller_permission(true,11);sample();sample(0,255);
    resonance::take_controller_actions(11);
    Sleep(175);
    read_started=GetTickCount64();
    resonance::begin_controller_field_read(read_started);
    Sleep(175);
    resonance::renew_controller_after_field_read(read_started);
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    need(resonance::take_controller_actions(11).empty(),"a route read cannot revive previously expired permission");

    sample();resonance::publish_controller_permission(true,7);sample();sample(0,255);resonance::take_controller_actions(7);
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);resonance::take_controller_actions(7);
    resonance::controller_automatic_starter_grace();
    sample();sample(XINPUT_GAMEPAD_RIGHT_THUMB,255); // No override query between release and re-press.
    need(!resonance::controller_walking_state().starter_held,"a new chord must not inherit starter grace after an observed release");
    sample();resonance::publish_controller_permission(true,8);sample();sample(0,255);resonance::take_controller_actions(8);
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255); // Automatic request queued, not dispatched yet.
    sample();sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
    resonance::take_controller_actions(8);resonance::controller_automatic_starter_grace();
    need(!resonance::controller_walking_state().starter_held,"delayed automatic dispatch cannot grant starter grace to a later physical chord");
    sample();
    Sleep(525); // The field tick can spend this long in a synchronous terrain query.
    need(resonance::controller_backend_status().connected==0,"the normal game poll expires during the ground check");
    const auto fresh=resonance::controller_walking_state();
    need(fresh.ready&&fresh.connected==1&&!fresh.physical_input,
        "movement can verify a still-connected controller without waiting for the game's next input poll");
    XINPUT_STATE newly_connected{};newly_connected.Gamepad.sThumbLY=30000;
    set(1,TRUE,&newly_connected);
    const auto refreshed=resonance::controller_walking_state(true);
    need(refreshed.ready&&refreshed.connected==2&&refreshed.physical_input,
        "a check after a new ground query sees a controller attached before the game polled its slot");
    need(resonance::controller_walking_state().connected==2,
        "a slot discovered during the query stays in the walking check");
    set(1,FALSE,nullptr);
    raw.Gamepad.wButtons=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_DPAD_UP;set(0,TRUE,&raw);
    need(!resonance::controller_walking_state().physical_input,"raw Interact and D-pad buttons leave walking active until the native action is known");
    raw.Gamepad.sThumbLY=30000;set(0,TRUE,&raw);
    need(resonance::controller_walking_state().physical_input,"a stick movement during the synchronous query cancels walking");
    need(resonance::take_controller_actions(8).empty(),"a movement safety read cannot enqueue a navigation gesture");
    set(0,FALSE,nullptr);
    need(resonance::controller_walking_state().connected==0,"a real disconnect still prevents controller movement");
    sample();resonance::publish_controller_permission(true,9);sample();sample(0,255);resonance::take_controller_actions(9);
    sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);resonance::take_controller_actions(9);resonance::controller_automatic_starter_grace();
    auto starter=resonance::controller_walking_state();
    need(starter.starter_held&&!starter.physical_input,"movement waits while the original starter is held");
    raw.Gamepad.wButtons&=static_cast<WORD>(~XINPUT_GAMEPAD_RIGHT_THUMB);
    ++raw.dwPacketNumber;set(0,TRUE,&raw); // R3 release before the game polls the provider.
    starter=resonance::controller_walking_state();
    need(starter.starter_held&&!starter.physical_input,"releasing R3 first keeps the original modifier pending");
    raw.Gamepad.wButtons|=XINPUT_GAMEPAD_A;
    ++raw.dwPacketNumber;set(0,TRUE,&raw);
    starter=resonance::controller_walking_state();
    need(starter.starter_held&&!starter.physical_input,"Interact leaves the pending walk active while the starter is held");
    raw.Gamepad={};
    ++raw.dwPacketNumber;set(0,TRUE,&raw); // Modifier releases, still no hooked poll.
    starter=resonance::controller_walking_state();
    need(!starter.starter_held&&!starter.physical_input,"the fresh neutral sample releases the starter");
    need(!resonance::controller_walking_state().physical_input,
        "the old hook sample cannot cancel after both buttons were released");
    raw.Gamepad.wButtons=XINPUT_GAMEPAD_RIGHT_THUMB;
    ++raw.dwPacketNumber;set(0,TRUE,&raw);
    raw.Gamepad.bRightTrigger=220;raw.Gamepad.sThumbRX=14000;
    set(0,TRUE,&raw);
    need(!resonance::controller_walking_state().physical_input,
        "a later camera gesture or trigger press cannot cancel movement");
    raw.Gamepad.sThumbLX=30000;
    ++raw.dwPacketNumber;set(0,TRUE,&raw);
    need(resonance::controller_walking_state().physical_input,
        "a fresh movement stick takes over even without a hooked game poll");
    sample();sample(0,255);resonance::take_controller_actions(9);
    sample(XINPUT_GAMEPAD_B,255);
    actions=resonance::take_controller_actions(9);
    need(actions.size()==1&&actions[0]==resonance::ControllerAction::Stop,
        "the explicit controller stop remains available");
    using resonance::BattleControllerAction;
    const auto battle_action=[&](BattleControllerAction expected) {
        const auto items=resonance::take_battle_controller_actions(20);
        return items.size()==1&&items.front()==expected;
    };
    resonance::publish_controller_permission(false,12);
    sample();
    sample(XINPUT_GAMEPAD_LEFT_THUMB);
    need(out.Gamepad.wButtons==XINPUT_GAMEPAD_LEFT_THUMB,"L3 remains native in the field and menus");
    if(modifier==resonance::ControllerModifier::RightShoulder) {
        sample();
        resonance::publish_controller_permission(true,13);
        sample(0,255);
        need(!modifier_down()&&resonance::take_controller_actions(13).empty(),
            "RB on the first recovered field sample must neither open the native Journal nor replay navigation");
        sample();
        sample(0,255);
        actions=resonance::take_controller_actions(13);
        need(!modifier_down()&&actions.size()==1&&actions.front()==resonance::ControllerAction::Opened,
            "a fresh RB after release must open field navigation normally");
        resonance::publish_controller_permission(false,14);
        sample(0,255);
        need(!modifier_down(),"a captured field hold must not become a native menu tab on context loss");
        sample();
        sample(XINPUT_GAMEPAD_LEFT_SHOULDER,255);
        need(out.Gamepad.wButtons==(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER)&&
            resonance::take_controller_actions(14).empty(),"fresh shoulder presses in a menu must reach the native tabs");
        sample();
        resonance::publish_controller_permission(false,15,true);
        sample(XINPUT_GAMEPAD_B,255,200);
        need(!modifier_down()&&out.Gamepad.wButtons==XINPUT_GAMEPAD_B&&out.Gamepad.bLeftTrigger==200&&
            resonance::take_controller_actions(15).empty(),
            "temporary field unavailability must reserve only RB while leaving Back and native LT usable");
        resonance::publish_controller_permission(true,15);
        sample(XINPUT_GAMEPAD_RIGHT_THUMB,255);
        need(!modifier_down()&&resonance::take_controller_actions(15).empty(),
            "restoring field permission while holding reserved RB cannot queue an old gesture");
        resonance::publish_controller_permission(false,16);
        sample(0,255);
        need(!modifier_down(),"the reserved RB hold must remain consumed when the game opens a menu");
        sample();
        sample(0,255);
        need(modifier_down(),"physical release must return RB to the menu");
        sample(XINPUT_GAMEPAD_LEFT_THUMB);
    }
    resonance::publish_battle_controller_permission(true,20);
    sample(XINPUT_GAMEPAD_LEFT_THUMB);
    need(resonance::take_battle_controller_actions(20).empty(),"entering battle with held L3 cannot open status");
    sample();
    if(modifier==resonance::ControllerModifier::RightShoulder) {
        sample(0,255);
        need(modifier_down()&&resonance::take_battle_controller_actions(20).empty(),
            "a fresh RB outside the field must still reach native battle Libra");
        sample();
    }
    sample(XINPUT_GAMEPAD_Y);
    need(out.Gamepad.wButtons==XINPUT_GAMEPAD_Y,"battle speed remains native when the status panel is closed");
    sample();
    sample(XINPUT_GAMEPAD_LEFT_THUMB);
    need(out.Gamepad.wButtons==0&&battle_action(BattleControllerAction::Opened),"L3 opens party status without leaking to the game");
    sample();sample(XINPUT_GAMEPAD_DPAD_RIGHT);
    need(out.Gamepad.wButtons==0,"status cycling does not move the native target cursor");
    Sleep(resonance::controller_dpad_settle_ms+5);
    sample(XINPUT_GAMEPAD_DPAD_RIGHT);
    need(battle_action(BattleControllerAction::Next),"right cycles the party review selection");
    sample();sample(XINPUT_GAMEPAD_Y);
    need(out.Gamepad.wButtons==0&&battle_action(BattleControllerAction::Gauges),"Y reviews gauges inside the panel");
    sample();sample(XINPUT_GAMEPAD_B);
    need(out.Gamepad.wButtons==0&&battle_action(BattleControllerAction::Closed),"B closes the panel without pressing native Back");
    sample(XINPUT_GAMEPAD_B);
    need(out.Gamepad.wButtons==0,"a consumed Back is held through its release");
    sample();sample(XINPUT_GAMEPAD_B);
    need(out.Gamepad.wButtons==XINPUT_GAMEPAD_B,"a new Back after closing goes to the game");
    sample();sample(XINPUT_GAMEPAD_LEFT_THUMB);
    need(battle_action(BattleControllerAction::Opened),"a new L3 can reopen status");
    resonance::publish_battle_controller_permission(false,20);
    resonance::publish_battle_controller_permission(true,20);
    sample(XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_DPAD_UP);
    need(out.Gamepad.wButtons==XINPUT_GAMEPAD_DPAD_UP&&resonance::take_battle_controller_actions(20).empty(),
        "an unsampled native menu transition ends review and forwards fresh inputs");
    sample();resonance::publish_battle_controller_permission(false,20);
    resonance::disable_controller_input();sample(XINPUT_GAMEPAD_A,255);
    need(!resonance::retry_controller_input(),"a disabled backend cannot be resurrected by the retry loop");
    need(out.dwPacketNumber==raw.dwPacketNumber&&out.Gamepad.wButtons==raw.Gamepad.wButtons&&out.Gamepad.bLeftTrigger==raw.Gamepad.bLeftTrigger&&out.Gamepad.bRightTrigger==raw.Gamepad.bRightTrigger,"a disabled hook is an exact transparent forwarder");
    std::cout<<"Native XInput capture, isolation, lifecycle and override checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
