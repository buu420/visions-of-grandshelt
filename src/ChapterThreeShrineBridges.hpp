#pragma once
#include "ChapterThreeWaterShrine.hpp"
#include "resonance/ActivationGoal.hpp"
#include <span>

namespace resonance::chapter_three {
enum class ShrineBridge { CrateRoom, BasinLong, BasinShort };
struct ShrineBridgeLeg { ShrineBridge bridge; bool returning; };
struct ShrineCrateAnchor { int suffix; double x,y; int movable_id{-1}; };
struct ShrineBridgeSpec {
    int floor;
    std::span<const ShrineCrateAnchor> crates;
    Vec3 near_bank,far_bank;
    ActivationBox bounds;
    const char* outbound_label;
    const char* return_label;
};

// Crate actor names and axes come from the cooked floor's buoyancy settings.
// Dry landing points were projected on the exact LocalMap Recast mesh.
inline constexpr std::array shrine_crate_room_row{
    ShrineCrateAnchor{0,-160,60050},ShrineCrateAnchor{4,-160,60200},ShrineCrateAnchor{1,-160,60350},
    ShrineCrateAnchor{3,-160,60500},ShrineCrateAnchor{2,-160,60650,45}};
inline constexpr std::array shrine_long_basin_row{
    ShrineCrateAnchor{12,1855,13290},ShrineCrateAnchor{13,1855,13130},ShrineCrateAnchor{21,1855,12970},
    ShrineCrateAnchor{22,1855,12810},ShrineCrateAnchor{23,1855,12650},ShrineCrateAnchor{24,1855,12490}};
inline constexpr std::array shrine_short_basin_row{
    ShrineCrateAnchor{25,34,11347.683},ShrineCrateAnchor{26,-121,11347.683},ShrineCrateAnchor{27,-276,11347.683},
    ShrineCrateAnchor{28,-431,11347.683},ShrineCrateAnchor{29,-587,11347.683}};
inline constexpr ActivationBox shrine_basin_bridge_bounds{{440,13105,502.15},{1780,2160,120},0};
inline constexpr std::array shrine_bridge_specs{
    ShrineBridgeSpec{74,shrine_crate_room_row,{-160,59880,102.15},{-160,60830,102.15},
        {{480,60150,102.15},{850,1560,115},0},"Far terrace across the crates","Entrance terrace across the crates"},
    ShrineBridgeSpec{73,shrine_long_basin_row,{1855,13565,502.15},{1855,12295,502.15},shrine_basin_bridge_bounds,
        "Far walkway across the crates","Upper stairs across the crates"},
    ShrineBridgeSpec{73,shrine_short_basin_row,{260,11348,502.15},{-920,11348,502.15},shrine_basin_bridge_bounds,
        "Dock pedestal landing across the crates","Upper walkway across the crates"}};
inline constexpr std::array shrine_basin_walkway{
    Vec3{1855,12295,502.15},Vec3{1855,11255,502.15},Vec3{350,11255,502.15},Vec3{260,11348,502.15}};

inline const ShrineBridgeSpec& shrine_bridge_spec(ShrineBridge bridge) {
    return shrine_bridge_specs[static_cast<std::size_t>(bridge)];
}

inline std::optional<ShrineBridgeLeg> shrine_bridge_leg(int area,Vec3 position,HallWater hall,RoomWater room) {
    if(room!=RoomWater::High||!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))return {};
    if(area==10&&position.z>=70&&position.z<=360&&position.x>=-370&&position.x<=1330&&
        position.y>=58790&&position.y<=61680) {
        if(hall==HallWater::FirstDrop&&!shrine_crate_far_bank(position))return ShrineBridgeLeg{ShrineBridge::CrateRoom,false};
        if((hall==HallWater::SecondDrop||hall==HallWater::Drained)&&position.y>59898)
            return ShrineBridgeLeg{ShrineBridge::CrateRoom,true};
    }
    if(area!=14||position.z<465||position.z>800||position.x<-1350||position.x>2200||
        position.y<10960||position.y>15250)return {};
    if(hall==HallWater::SecondDrop) {
        if(position.x>=1560&&position.y>12312)return ShrineBridgeLeg{ShrineBridge::BasinLong,false};
        if(position.x>-902&&position.y<=12312)return ShrineBridgeLeg{ShrineBridge::BasinShort,false};
    }
    if(hall==HallWater::Drained) {
        if(position.x<242&&position.y<=11600)return ShrineBridgeLeg{ShrineBridge::BasinShort,true};
        if(position.x>=242&&position.y<13547)return ShrineBridgeLeg{ShrineBridge::BasinLong,true};
    }
    return {};
}

struct ShrineCrateBounds { Vec3 center,half; bool collision; };
inline bool shrine_crate_supports(const ShrineBridgeSpec& bridge,std::span<const ShrineCrateBounds> crates) {
    if(crates.size()<2||crates.size()!=bridge.crates.size())return false;
    const bool x_axis=bridge.crates.front().x!=bridge.crates.back().x;
    const auto along=[&](Vec3 p){return x_axis?p.x:p.y;};
    for(std::size_t i=0;i<crates.size();++i) {
        const auto& crate=crates[i];
        const auto& anchor=bridge.crates[i];
        const auto top=crate.center.z+crate.half.z;
        if(!crate.collision||!(std::hypot(crate.center.x-anchor.x,crate.center.y-anchor.y)<=28)||
            !(crate.half.x>=50&&crate.half.x<=115&&crate.half.y>=50&&crate.half.y<=115&&
              crate.half.z>=2&&crate.half.z<=115)||!(std::abs(top-bridge.near_bank.z)<=65))return false;
        if(i>0) {
            const auto& previous=crates[i-1];
            const double gap=std::abs(along(previous.center)-along(crate.center))-along(previous.half)-along(crate.half);
            if(gap>32||std::abs(top-previous.center.z-previous.half.z)>48)return false;
        }
    }
    return true;
}

inline Vec3 shrine_bridge_goal(ShrineBridgeLeg leg) {
    const auto& bridge=shrine_bridge_spec(leg.bridge);
    return leg.returning?bridge.near_bank:bridge.far_bank;
}
inline const ShrineCrateAnchor& shrine_bridge_marker(ShrineBridgeLeg leg) {
    const auto crates=shrine_bridge_spec(leg.bridge).crates;
    return leg.returning?crates.front():crates.back();
}
}
