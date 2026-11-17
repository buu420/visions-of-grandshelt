#pragma once
#include "resonance/Navigation.hpp"
#include <cmath>
#include <optional>

namespace resonance::chapter_two {
inline constexpr auto mobliz_cargo_actor_path=
    L"/Game/Map/Dng/04Dir/04Dir_41/EV/Dng_04Dir_41_01_EV.Dng_04Dir_41_01_EV:PersistentLevel.BP_MapGimmick_EventTrigger_C_1";
struct LiftLeg {
    int lift,call,floor;
    Vec3 dock;
    const char* name;
    const char* ride;
};
enum class LiftStep { Unavailable, Call, Wait, Board };
struct LiftFacts {
    bool lift_known, enabled, docked, boarding_enabled, call_link_ready, moving{};
};
inline LiftStep lift_step(const LiftLeg& leg,LiftFacts facts) {
    if(!facts.lift_known||!facts.enabled)return LiftStep::Unavailable;
    if(facts.moving)return LiftStep::Wait;
    if(facts.docked)return facts.boarding_enabled?LiftStep::Board:LiftStep::Wait;
    return leg.call>=0&&facts.call_link_ready?LiftStep::Call:LiftStep::Unavailable;
}
inline bool near_dock(Vec3 platform,Vec3 landing) {
    return std::hypot(platform.x-landing.x,platform.y-landing.y)<=120&&
        std::abs(platform.z-landing.z)<=90;
}
inline bool wind_at_other_dock(int lift,Vec3 p) {
    switch(lift) {
    case 71:return near_dock(p,{7202,37000,2802});
    case 72:return near_dock(p,{13600,37000,1206})||near_dock(p,{13600,37000,5201});
    case 73:return near_dock(p,{10400,40200,5203});
    case 74:return near_dock(p,{14024,43404,2804})||near_dock(p,{14024,43404,4802});
    default:return false;
    }
}
// Build 24250053, Dng_04Dir_42_01_GD and _NAV. The first upper landing
// and the main upper walkway share area 3. The lower floor and the
// walkway above it likewise share area 1, but are separated by a lift.
inline std::optional<LiftLeg> wind_leg(int map,int quest,int phase,int area,Vec3 p) {
    if(map!=3070||quest!=1002||phase!=2130||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return {};
    switch(area) {
    case 0:return LiftLeg{71,55,0,{7202,37000,802},"first elevator","up to the upper landing"};
    case 3:
        if(p.x<9000)return LiftLeg{69,-1,0,{8098,37882,2800},"crossing lift","across to the upper walkway"};
        return LiftLeg{72,54,1,{13600,37000,3194},"walkway elevator","down to the lower walkway"};
    case 1:
        if(p.y<39500&&p.z<2100)return LiftLeg{70,-1,0,{11630,38600,1592},"lower crossing lift","across the lower hall"};
        [[fallthrough]];
    case 8:return LiftLeg{68,-1,0,{8000,41330,1992},"west crossing lift","to the summit elevator's landing"};
    case 4:
        // The upper treasure balcony shares area 4 with the chamber path,
        // but a gap separates them. Its own lift is the way back down.
        if(p.x>=13200&&p.x<=14900&&p.y>=42190&&p.z<5050)
            return LiftLeg{74,-1,1,{14024,43404,4802},"treasure balcony elevator","back down to the main walkway"};
        return LiftLeg{41,-1,0,{17585,40201,5602},"Wind Crystal chamber elevator","up to the Wind Crystal chamber"};
    case 5:return LiftLeg{73,57,0,{10400,40200,1203},"summit elevator","up to the highest walkway"};
    default:return {};
    }
}
enum class EscapeStep {
    None, Rooftop, PowerFailure, Fence, LeverBank, RestoredLift, EntrancePassage,
    EntranceBlockade, ReturnPassage, CargoDiscovery
};
struct EscapeFacts {
    bool power_failure{},fence{},levers{},restored{},entrance_blockade{},cargo_discovery{},return_passage{};
};
inline EscapeStep escape_step(int map,int quest,int phase,int area,Vec3 p,EscapeFacts f) {
    if(quest!=1002||phase!=2095||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return EscapeStep::None;
    if(map==3061)return EscapeStep::Rooftop;
    if(map!=3060)return EscapeStep::None;
    if(area==4&&p.y<1000) {
        if(f.entrance_blockade)return EscapeStep::EntranceBlockade;
        if(f.return_passage)return EscapeStep::ReturnPassage;
    }
    if((area==1||area==2)&&p.y>6000&&p.z<2900) {
        if(f.power_failure)return EscapeStep::PowerFailure;
        if(f.fence)return EscapeStep::Fence;
        if(f.restored)return EscapeStep::RestoredLift;
        if(f.levers)return EscapeStep::LeverBank;
    }
    if(area==0&&p.y>8500) {
        if(f.cargo_discovery)return EscapeStep::CargoDiscovery;
        if(f.entrance_blockade)return EscapeStep::EntrancePassage;
    }
    return EscapeStep::None;
}
}
