#include "resonance/NativeBoundary.hpp"
#include "resonance/NativeWalking.hpp"
#include <iostream>
#include <stdexcept>
using namespace resonance;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try {
    need(native_ground_travel(1,1,false)==NativeGroundTravel::Walking,"ordinary walking remains eligible");
    need(native_ground_travel(3,1,true)==NativeGroundTravel::Chocobo,"the verified grounded chocobo is eligible");
    need(!native_ground_travel(3,1,false),"method 3 alone does not identify the mounted player");
    need(!native_ground_travel(1,1,true),"a chocobo in a travel transition does not accept foot input");
    need(!native_ground_travel(3,3,true),"a falling mount cannot receive a ground movement pulse");
    need(!native_ground_travel(2,1,false)&&!native_ground_travel(4,1,false),"airships and rafts use their own movement checks");
    need(native_walking_gain_valid(NativeGroundTravel::Walking,.8f)&&
        native_walking_gain_valid(NativeGroundTravel::Walking,1.6f)&&
        !native_walking_gain_valid(NativeGroundTravel::Walking,8.f),"the walking tick retains its native gain boundary");
    need(native_walking_gain_valid(NativeGroundTravel::Chocobo,8.f)&&
        native_walking_gain_valid(NativeGroundTravel::Chocobo,16.f),"the native mounted tick uses gains of eight and sixteen");
    need(!native_walking_gain_valid(NativeGroundTravel::Chocobo,0.f)&&
        !native_walking_gain_valid(NativeGroundTravel::Chocobo,16.1f),"unverified mounted gains cannot submit movement");

    NativeWalkingStartup startup;
    need(startup.due(1000,false),"native walking gets an initial setup attempt");
    startup.complete(NativeWalkingInstall::Retryable,1000);
    need(!startup.due(1749,true),"a busy startup cannot immediately repeat hook setup");
    need(!startup.due(2000,false),"a retry needs an automatic walking request");
    need(startup.due(2000,true),"a later automatic walking request must retry a temporary setup failure");
    startup.complete(NativeWalkingInstall::Ready,2000);
    need(!startup.due(3000,true),"successful hook setup must not be repeated");

    NativeWalkingStartup exhausted;
    exhausted.complete(NativeWalkingInstall::Retryable,1000);
    need(exhausted.due(2000,true),"the first temporary failure permits another attempt");
    exhausted.complete(NativeWalkingInstall::Retryable,2000);
    need(exhausted.due(3000,true),"two temporary failures permit the last attempt");
    exhausted.complete(NativeWalkingInstall::Retryable,3000);
    need(!exhausted.due(4000,true),"temporary hook failures have a finite retry budget");

    NativeWalkingStartup unavailable;
    unavailable.complete(NativeWalkingInstall::Unavailable,1000);
    need(!unavailable.due(2000,true),"a rejected hook must stay unavailable");

    // Values observed independently in the running exact-build game's metadata.
    need(native_call_extent(44,48,48,40,40,44,512),"current key mappings must be callable with44 bytes of parameters in48 bytes of storage");
    need(native_call_extent(25,32,32,24,24,25,512),"physical digital controller keys must be readable");
    need(native_call_extent(28,32,32,24,24,28,512),"physical analog controller axes must be readable");
    need(native_call_extent(29,32,32,65535,-1,29,512),"native movement input must accept its exact29-byte parameter extent");
    need(native_call_extent(97,104,104,96,96,97,512),"ground projection and raycast must accept the native bool return extent");
    need(native_call_extent(42,48,48,41,41,42,512),"visible target screen projection must remain readable");
    need(native_call_extent(34,40,40,33,33,34,512),"visible target line of sight must remain readable");
    need(native_call_extent(5,8,8,4,4,5,256),"a finished native voice must be observable so Story announcements can resume");
    need(native_call_extent(48,48,48,40,40,44,512),"the audited padded representation remains valid");
    for(int bad:{0,24,26,31,33,1000})need(!native_call_extent(bad,32,32,24,24,25,512),"unrelated parameter extents must be refused");
    need(!native_call_extent(25,40,32,24,24,25,512),"an allocation change must be refused");
    need(!native_call_extent(25,32,32,16,24,25,512),"a return offset change must be refused");
    need(!native_call_extent(29,32,32,0,-1,29,512),"a void call cannot secretly return a value");
    need(!native_call_extent(25,32,32,24,24,33,512),"out of range fields cannot be called");
    need(!native_call_extent(25,32,32,24,24,25,16),"storage smaller than the audited native allocation must be refused");
    need(getter_call_extent(false,4,8,4,0,0,4,4096),"the existing Blueprint Options combo getter must keep its eight-byte local storage and four-byte return");
    need(!getter_call_extent(true,4,8,4,0,0,4,4096),"a native allocation change still cannot pass as Blueprint locals");
    need(!getter_call_extent(false,5,8,4,0,0,4,4096),"Blueprint local storage cannot relax the parameter extent");
    need(!getter_call_extent(false,4,4097,4,0,0,4,4096),"Blueprint locals remain bounded");
    need(native_metadata_shape(0x41,L"/Script/CoreUObject.ScriptStruct",NativeMetadataKind::ScriptStruct),"native QuestData metadata must pass even though FindXOf excludes it");
    need(native_metadata_shape(0x41,L"/Script/CoreUObject.Class",NativeMetadataKind::Class),"MVVM native class metadata must be available for current HUD ownership");
    need(native_metadata_shape(0x41,L"/Script/CoreUObject.Function",NativeMetadataKind::Function),"audited interface UFunctions must be queryable");
    for(auto flags:{0x10u,0x20u,0x8000u,0x10000u,0x40000000u})
        need(!native_metadata_shape(0x41|flags,L"/Script/CoreUObject.ScriptStruct",NativeMetadataKind::ScriptStruct),"dead objects, CDOs and archetypes are never struct metadata");
    need(!native_metadata_shape(0x41,L"/Script/Engine.Actor",NativeMetadataKind::ScriptStruct),"ordinary actors cannot masquerade as metadata");
    need(!native_metadata_shape(0x41,L"/Script/CoreUObject.Class",NativeMetadataKind::ScriptStruct),"metadata kind mismatches must be rejected");
    std::cout<<"Native input, route, movement and metadata boundary checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
