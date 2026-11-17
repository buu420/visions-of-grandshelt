#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "NativeHook.hpp"
#include "resonance/NativeWalking.hpp"
#include "resonance/Speech.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>



namespace resonance {
namespace {
using resonance::unreal::UObject;
using MovementTick=void (*)(UObject*,float);
using ApplyMovement=void (*)(UObject*,float,const void*);
MovementTick original_tick{};
ApplyMovement original_apply{};
std::atomic<bool> ready{};
thread_local NativeWalkingPulse pending;
thread_local unsigned long long queued_at{};
thread_local const NativeWalkingPulse* active{};
struct IssuedInput { UObject* pawn{}; Vec3 vector{}; unsigned long long tick{}; };
thread_local IssuedInput issued;

template<class T> T load(const void* base,std::size_t offset=0) {
    T value;
    std::memcpy(&value,static_cast<const std::byte*>(base)+offset,sizeof(value));
    return value;
}
template<class T> void store(void* base,std::size_t offset,const T& value) {
    std::memcpy(static_cast<std::byte*>(base)+offset,&value,sizeof(value));
}
using Stick=std::array<double,2>;
bool zero(Vec3 value){return value.x==0.&&value.y==0.&&value.z==0.;}
bool same_input(Vec3 a,Vec3 b) {
    return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<.000001;
}

void apply_walking(UObject* pawn,float delta,const void* input) {
    if(!active||active->pawn!=pawn) {
        original_apply(pawn,delta,input);
        return;
    }
    if(!input||!load<unsigned char>(input)||!zero(load<Vec3>(active->accumulated_input)))return;
    // This exact routine reads the stack record through the gain at +0x30.
    // Reproject with the yaw selected by the game on this very tick.
    const auto yaw=walking_axes(load<double>(input,0x20));
    const auto stick=yaw?walking_stick(active->world_direction,*yaw):std::nullopt;
    if(!stick||load<double>(input,0x18)!=0.||load<double>(input,0x28)!=0.)return;
    const auto native_gain=load<float>(input,0x30);
    const auto live_maximum=load<float>(active->max_speed);
    if(!std::isfinite(native_gain)||native_gain<=0||native_gain>1.6f||
       !std::isfinite(live_maximum)||live_maximum<=0)return;
    std::optional<WalkingAnalogSpeed> analog;
    if(active->min_analog_speed)analog=WalkingAnalogSpeed{live_maximum,*active->min_analog_speed};
    // The movement component can consume the previous frame's input before
    // this Tick. Use the new velocity and only the untraveled checked ground.
    const auto position=load<Vec3>(active->position);
    const auto velocity=load<Vec3>(active->velocity);
    const auto direction=active->world_direction;
    const auto dx=position.x-active->sampled_position.x;
    const auto dy=position.y-active->sampled_position.y;
    if(std::abs(dx*direction.y-dy*direction.x)>active->side_margin)return;
    const auto remaining=active->clearance-std::hypot(dx,dy);
    const auto gain=capsule_input_scale(remaining,std::hypot(velocity.x,velocity.y,velocity.z),active->acceleration,
        active->braking,active->tick,analog,std::min(native_gain,1.f));
    if(!gain||*gain<=0)return;
    alignas(8) std::array<std::byte,0x38> bounded{};
    std::memcpy(bounded.data(),input,0x34);
    store(bounded.data(),8,stick->x);
    store(bounded.data(),16,stick->y);
    store(bounded.data(),0x30,*gain);
    original_apply(pawn,delta,bounded.data());
    const auto accumulated=load<Vec3>(active->accumulated_input);
    const Vec3 submitted{direction.x* *gain,direction.y* *gain,0.};
    if(same_input(accumulated,submitted))issued={pawn,accumulated,GetTickCount64()};
}

void invoke_with_stick(UObject* pawn,float delta,const NativeWalkingPulse* pulse) {
    const Stick intent{pulse->stick.x,pulse->stick.y};
    store(pulse->input,0,intent);
    active=pulse;
    __try { original_tick(pawn,delta); }
    __finally {
        active=nullptr;
        // A native input update during the call takes priority over our sample.
        if(load<Stick>(pulse->input)==intent)store(pulse->input,0,Stick{});
    }
}
void walking_tick(UObject* pawn,float delta) {
    if(pending.pawn!=pawn||!ready.load(std::memory_order_acquire)) {
        original_tick(pawn,delta);
        return;
    }
    const auto pulse=std::exchange(pending,{});
    DWORD foreground{};
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    // Verify the same component and native field state before substituting
    // one sample. The regular Tick still owns gait, sound, and movement.
    if(foreground!=GetCurrentProcessId()||GetTickCount64()-queued_at>250||
       load<UObject*>(pulse.movement_slot)!=pulse.movement||load<UObject*>(pulse.root_slot)!=pulse.root||
       load<unsigned char>(pulse.movement_mode)!=1||
       load<Stick>(pulse.input)!=Stick{}||
       load<unsigned char>(pawn,0x780)!=1||!load<unsigned char>(pawn,0xa08)||
       load<int>(pawn,0x938)!=0||load<unsigned char>(pawn,0x8b8)||load<unsigned char>(pawn,0xb74)) {
        original_tick(pawn,delta);
        return;
    }
    invoke_with_stick(pawn,delta,&pulse);
}
}

NativeWalkingInstall install_native_walking() {
    if(ready.load(std::memory_order_acquire))return NativeWalkingInstall::Ready;
    const auto failed=[](const char* stage,MH_STATUS status,bool cleanup_ok=true) {
        log(std::string("NAV native walking setup failed at ")+stage+": "+MH_StatusToString(status)
            +(cleanup_ok?"":" (rollback incomplete)"));
        return cleanup_ok&&status==MH_ERROR_MEMORY_ALLOC?NativeWalkingInstall::Retryable:NativeWalkingInstall::Unavailable;
    };
    auto* image=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if(!image) {
        log("NAV native walking setup failed: game image unavailable");
        return NativeWalkingInstall::Unavailable;
    }
    auto* tick=image+0x50e8830;
    auto* apply=image+0x50e97e0;
    // These complete prologues contain no instruction-pointer-relative operands.
    // The absolute fallback copies them verbatim and resumes at the next instruction.
    const UINT8 tick_start[]={0x40,0x55,0x53,0x48,0x8d,0x6c,0x24,0xc8,0x48,0x81,0xec,0x38,0x01,0x00,0x00};
    const UINT8 apply_start[]={0x48,0x8b,0xc4,0x53,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x41,0x80,0x38,0x00};
    constexpr std::array<UINT8,5> tick_offsets{0,2,3,8,sizeof(tick_start)};
    constexpr std::array<UINT8,5> apply_offsets{0,3,4,11,sizeof(apply_start)};
    if(std::memcmp(tick,tick_start,sizeof(tick_start))||std::memcmp(apply,apply_start,sizeof(apply_start))) {
        log("NAV native walking setup failed: instruction bytes changed");
        return NativeWalkingInstall::Unavailable;
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return failed("initialize MinHook",init);
    const auto create=[](void* site,void* detour,void** original,const std::array<UINT8,5>& offsets,const char* name) {
        auto status=MH_CreateHook(site,detour,original);
        if(status==MH_ERROR_MEMORY_ALLOC) {
            status=ResonanceCreateAbsoluteHook(site,detour,original,offsets.data(),static_cast<UINT>(offsets.size()));
            if(status==MH_OK)log(std::string("NAV native walking absolute hook prepared for ")+name);
        }
        return status;
    };
    auto status=create(apply,reinterpret_cast<void*>(&apply_walking),reinterpret_cast<void**>(&original_apply),
        apply_offsets,"movement input");
    if(status!=MH_OK)return failed("create movement input hook",status);
    status=create(tick,reinterpret_cast<void*>(&walking_tick),reinterpret_cast<void**>(&original_tick),
        tick_offsets,"movement tick");
    if(status!=MH_OK)return failed("create movement tick hook",status,MH_RemoveHook(apply)==MH_OK);

    const char* stage="enable movement input hook";
    status=MH_EnableHook(apply);
    if(status==MH_OK) {
        stage="enable movement tick hook";
        status=MH_EnableHook(tick);
    }
    if(status!=MH_OK) {
        const auto removed_tick=MH_RemoveHook(tick);
        const auto removed_apply=MH_RemoveHook(apply);
        return failed(stage,status,removed_tick==MH_OK&&removed_apply==MH_OK);
    }
    ready.store(true,std::memory_order_release);
    return NativeWalkingInstall::Ready;
}
bool native_walking_ready(){return ready.load(std::memory_order_acquire);}
void disable_native_walking(){ready.store(false,std::memory_order_release);}
void queue_native_walking(NativeWalkingPulse pulse){pending=pulse;queued_at=GetTickCount64();}
void clear_native_walking(){pending={};}
bool owns_native_walking_input(UObject* pawn,Vec3 input) {
    return issued.pawn==pawn&&GetTickCount64()-issued.tick<=250&&same_input(issued.vector,input);
}
}
