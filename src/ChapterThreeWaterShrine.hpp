#pragma once
#include "ChapterThreeStory.hpp"
#include <cmath>

namespace resonance::chapter_three {
enum class HallWater { Unknown, High, FirstDrop, SecondDrop, Drained };
enum class RoomWater { Unknown, Low, High };

inline HallWater shrine_hall_water(double height) {
    // UID 75's four vertical spline endpoints in Dng_05Old_44_01_GD.
    if(std::abs(height-45.)<=18.)return HallWater::High;
    if(std::abs(height+1655.)<=18.)return HallWater::FirstDrop;
    if(std::abs(height+1885.)<=18.)return HallWater::SecondDrop;
    if(std::abs(height+6485.)<=18.)return HallWater::Drained;
    return HallWater::Unknown;
}

inline RoomWater shrine_room_water(int floor,double height) {
    if(floor!=73&&floor!=74)return RoomWater::Unknown;
    const double low=floor==73?130.:-260.;
    const double high=floor==73?530.:132.;
    if(std::abs(height-low)<=18.)return RoomWater::Low;
    if(std::abs(height-high)<=18.)return RoomWater::High;
    return RoomWater::Unknown;
}

enum class BasinDeck { Other, East, West, Tower };
inline BasinDeck shrine_basin_deck(int area,Vec3 position) {
    if(area!=14||position.z<465||position.z>800)return BasinDeck::Other;
    if(position.x>=1440&&position.y>=13200)return BasinDeck::East;
    if(position.x>=-1350&&position.x<=-800&&position.y>=10950&&position.y<=11600)return BasinDeck::West;
    if(position.x>=-800&&position.x<=-200&&position.y>=11980&&position.y<=12700)return BasinDeck::Tower;
    return BasinDeck::Other;
}

inline bool shrine_hall_lowered(HallWater water) {
    return water==HallWater::FirstDrop||water==HallWater::SecondDrop||water==HallWater::Drained;
}

inline bool shrine_crate_far_bank(Vec3 position) {
    return position.y>=60812&&position.z>=45&&position.z<=365;
}
inline bool shrine_crate_aligned(Vec3 crate) {
    return std::hypot(crate.x+160.,crate.y-60650.)<=28.;
}
inline std::optional<Vec3> shrine_crate_push_side(Vec3 crate) {
    if(!std::isfinite(crate.x)||!std::isfinite(crate.y)||!std::isfinite(crate.z)||
       crate.x<=-132.||crate.x>780.||std::abs(crate.y-60650.)>28.||
       std::abs(crate.z+225.)>35.)return {};
    return Vec3{crate.x+230.,crate.y,-289.85};
}

inline std::optional<int> shrine_large_pedestal(int area,Vec3 position,HallWater water) {
    if(water==HallWater::High&&area==9&&position.y>=50075&&position.y<=50950&&position.x>=230)return 56;
    if(water==HallWater::FirstDrop&&area==10&&shrine_crate_far_bank(position))return 57;
    if(water==HallWater::SecondDrop&&shrine_basin_deck(area,position)==BasinDeck::Tower)return 55;
    return {};
}

struct ShrineLadder {int id;bool top;const char* key;const char* label;const char* instruction;};
inline std::optional<ShrineLadder> shrine_ladder(int area,Vec3 position,HallWater water,RoomWater room) {
    if(area==1&&shrine_hall_lowered(water))return ShrineLadder{2,true,"shrine-hall-ladder","Ladder to the lower hall",
        "Go to the ladder and walk toward it to climb down."};
    if(area==2&&shrine_hall_lowered(water))return ShrineLadder{30,true,"shrine-middle-ladder","Ladder to the central walkway",
        "Climb down the next ladder to the central walkway."};
    if((area==3||area==4)&&water==HallWater::Drained)return ShrineLadder{1,true,"shrine-lower-ladder","Ladder to the elevator landing",
        "The lower stairs are open. Take the ladder down to the elevator landing."};
    if(room!=RoomWater::Low)return {};
    if(water==HallWater::SecondDrop&&area==13&&position.x>=-1170&&position.x<=130&&
        position.y>=11810&&position.y<=12810&&position.z<450)
        return ShrineLadder{76,false,"shrine-tower-ladder","Tower ladder","Climb the ladder on the tower."};
    if(water==HallWater::Drained&&shrine_basin_deck(area,position)==BasinDeck::Tower)
        return ShrineLadder{76,true,"shrine-tower-ladder-down","Tower ladder down","Return to the landing below the tower."};
    return {};
}

inline std::optional<GateLeg> water_shrine_gate(int phase,int area,Vec3 position,HallWater water,RoomWater room) {
    if(phase==3190) {
        if(area==0)return GateLeg{1,1000,3111,"water-shrine-exit","Water Shrine exit",
            "Leave the Water Shrine and return to Olderion."};
        if(area==16)return GateLeg{18,3110,20,"shrine-chamber-return","Passage back to the lower save point",
            "Leave the chamber through the arch."};
    }
    if(phase!=3180)return {};
    if(area==0)return GateLeg{9,3110,1,"water-shrine-first-passage","Passage to the central hall",
        "Follow the entrance path to the central hall."};
    if(area==1&&water==HallWater::High)return GateLeg{15,3110,5,"shrine-first-boat-room","Passage to the first boat room",
        "Follow the upper walkway into the boat room."};
    if(area==7&&shrine_hall_lowered(water))return GateLeg{10,3110,6,"shrine-return-to-hall","Passage back to the central hall",
        "Take the passage from this bank back to the central hall."};
    if(area==3&&water==HallWater::FirstDrop)return GateLeg{11,3110,13,"shrine-crate-room","Passage to the crate room",
        "Follow the lower walkway to the crate room."};
    if(area==3&&water==HallWater::SecondDrop)return GateLeg{16,3110,15,"shrine-basin","Newly uncovered arch",
        "Go through the arch that the water uncovered."};
    if(area==10&&(water==HallWater::SecondDrop||water==HallWater::Drained)&&room==RoomWater::Low&&position.y<=60340)
        return GateLeg{2,3110,14,"shrine-crate-room-exit","Passage back to the central walkway",
            "Leave the crate room through the entrance passage."};
    if(area==13&&position.x<-1980&&position.y>=12720&&position.y<=13350&&position.z<420)
        return GateLeg{14,3110,17,"shrine-basin-side-exit","Passage back to the central hall",
            "Return to the central hall through the arch on this platform."};
    const bool basin_exit_deck=(area==13&&position.x>=-460&&position.y>=13950)||
        shrine_basin_deck(area,position)==BasinDeck::East;
    if(basin_exit_deck&&water==HallWater::Drained&&room==RoomWater::Low)
        return GateLeg{4,3110,16,"shrine-basin-exit","Passage back to the central walkway",
            "Return through the arch to the lower stairs in the central hall."};
    if(area==6)return GateLeg{17,3110,19,"shrine-lower-arch","Arch beyond the lower save point",
        "Follow the lower walkway through the arch.",true};
    return {};
}
}
