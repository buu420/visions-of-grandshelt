#pragma once
#include "resonance/Navigation.hpp"
#include <array>
#include <optional>

namespace resonance::chapter_three {
struct GateLeg {
    int id,destination,point;
    const char* key;
    const char* label;
    const char* instruction;
    bool event_only{};
};

// The reader also checks the live transition's selected destination.
struct FieldLeg {int quest,map,phase,goal;GateLeg gate;};
struct PurificationLeg {int phase,id;const char* instruction;};
inline constexpr std::array purification_legs{
    PurificationLeg{3042,17,"Check the water near the city entrance so Luka can purify it."},
    PurificationLeg{3044,18,"Check the water by the orphanage so Luka can purify it."},
    PurificationLeg{3046,19,"Check the water on the way to Old Town so Luka can purify it."}
};
inline constexpr std::array field_legs{
    FieldLeg{1002,2050,2120,3070,{1,1000,45,"forge-north","North exit of the Dwarven Forge",
        "Leave by the north exit, then take the east fork toward the Wind Shrine."}},
    FieldLeg{1002,1000,2140,2060,{27,2060,0,"dilmagia","Entrance to Dilmagia",
        "Return to Dilmagia so Lid can finish the airship."}},
    FieldLeg{1002,2060,2150,2061,{1,2061,0,"dilmagia-shipyard","Gate to the Shipyard",
        "Go to the Shipyard to meet Lid. Follow the main road to the gate."}},
    FieldLeg{1002,2060,2160,2061,{1,2061,0,"dilmagia-shipyard","Gate to the Shipyard",
        "Return to the Shipyard to board Lid's airship."}},
    FieldLeg{1003,2070,3020,3080,{9,1000,2071,"olderion-cave","South exit of Olderion",
        "Leave Olderion by the south exit for the Cave of Waters."}},
    FieldLeg{1003,2070,3050,2070,{0,3090,-1,"olderion-old-town","Gate to Old Town",
        "Cross into Old Town."}},
    FieldLeg{1003,2070,3085,3080,{9,1000,2071,"olderion-lake","South exit of Olderion",
        "Leave Olderion for Lake Dorr by way of the Cave of Waters."}},
    FieldLeg{1003,1000,3020,3080,{22,3080,0,"cave-south-entrance","South entrance to the Cave of Waters",
        "Go to the cave entrance northeast of Olderion."}},
    FieldLeg{1003,1000,3085,3080,{22,3080,0,"cave-south-entrance","South entrance to the Cave of Waters",
        "Take the south entrance to the cave for Lake Dorr."}},
    FieldLeg{1003,2070,3130,3100,{9,1000,2071,"olderion-waterfall","South exit of Olderion",
        "Leave Olderion for Waterfall Way."}},
    FieldLeg{1003,3091,3130,3100,{0,2070,10,"flooded-old-town-exit","Exit to Olderion",
        "Leave Old Town through Olderion on the way to Waterfall Way."}},
    FieldLeg{1003,1000,3130,3100,{20,3100,0,"waterfall-entrance","South entrance to Waterfall Way",
        "Go to the waterfall entrance west of Olderion."}},
    FieldLeg{1003,2090,3150,1000,{0,1000,2092,"amore-bridge","North bridge out of Amore",
        "Leave Amore by the north bridge toward Warden's Cape."}},
    FieldLeg{1003,2090,3160,1000,{0,1000,2092,"amore-bridge","North bridge out of Amore",
        "Leave Amore by the north bridge toward Warden's Cape."}},
    FieldLeg{1003,2090,3190,2070,{15,1000,2091,"amore-south-return","South exit of Amore",
        "Leave Amore by the south exit for Waterfall Way."}}
};

inline std::optional<GateLeg> world_gate(int phase,int goal,Vec3 position) {
    // The world-map entries on these shores land on disconnected native
    // walking components. Bounds come from the cooked world Recast mesh and
    // allow for the capsule height. The live gate and route are checked separately.
    const auto within=[&](Vec3 lower,Vec3 upper) {
        return position.x>=lower.x&&position.x<=upper.x&&position.y>=lower.y&&position.y<=upper.y&&
            position.z>=lower.z&&position.z<=upper.z;
    };
    if(within({3100,5000,305},{5490,6660,525})) {
        if(phase==3100&&goal==2081)return GateLeg{43,2081,0,"lake-dorr","Lake Dorr entrance",
            "Follow the path to Lake Dorr."};
        if(phase==3110&&goal==2070)return GateLeg{23,3080,13,"cave-north-return","North entrance to the Cave of Waters",
            "Return to Olderion through the Cave of Waters."};
    }
    const bool cave_road=(phase==3040||phase==3110)&&within({-2200,1760,-100},{2350,5660,280});
    const bool waterfall_road=phase==3190&&within({-2200,-600,-100},{2180,2820,80});
    if(goal==2070&&(cave_road||waterfall_road))
        return GateLeg{19,2070,0,"olderion-return","Entrance to Olderion",
            "Follow the lower road back to Olderion."};
    if(within({2810,-675,305},{3774,-375,485})) {
        if(phase==3150&&goal==1000)return GateLeg{24,2090,0,"amore-entrance","South entrance to Amore",
            "Go through Amore on the way to Warden's Cape."};
        if(phase==3190&&goal==2070)return GateLeg{21,3100,1,"waterfall-north-return","North entrance to Waterfall Way",
            "Return through Waterfall Way."};
    }
    if(within({3775,-2860,-125},{6730,410,500})) {
        if(phase==3170&&goal==3110)return GateLeg{26,3110,0,"water-shrine-entrance","Entrance to the Water Shrine",
            "Cross the bridge to the Water Shrine."};
        if(phase==3190&&goal==2070)return GateLeg{25,2090,1,"amore-north-return","North entrance to Amore",
            "Take the bridge back to Amore."};
    }
    return {};
}

inline std::optional<int> cave_return_ladder(int phase,int area,Vec3 position) {
    if(phase!=3110||(area!=1&&area!=7)||position.y>=-8100||position.y<-10550||
        position.x<550||position.x>3430)return {};
    if(position.z>=1500&&position.z<=2050)return 3;
    if(position.z>=950&&position.z<1500)return 2;
    if(position.z>=500&&position.z<950)return 1;
    return {};
}

inline std::optional<GateLeg> cave_return_gate(int phase,int area,Vec3 position) {
    if(phase!=3110)return {};
    if((area==1||area==7)&&position.y<-8100&&position.y>=-10550&&position.z>=0&&position.z<500)
        return GateLeg{3,3080,12,"cave-return-passage","Passage at the foot of the ladders",
            "Take the passage at the foot of the ladders."};
    if((area==2||area==5||area==6)&&position.y>=-5300&&position.y<=-3550)
        return GateLeg{17,3080,10,"cave-return-entrance","Passage to the entrance hall",
            "Follow the drained passage to the entrance hall."};
    if((area==0||area==3)&&position.y>=-1700&&position.y<=2500&&position.z>=-150&&position.z<500)
        return GateLeg{9,1000,3081,"cave-south-exit","South exit of the Cave of Waters",
            "Leave the cave by the south exit and return to Olderion."};
    return {};
}

inline const char* exit_label(int map,int id,int destination,int point) {
    if(destination!=1000)return nullptr;
    if(map==2070&&id==9&&point==2071)return "South exit of Olderion";
    if(map==2090&&id==15&&point==2091)return "South exit of Amore";
    if(map==2090&&id==0&&point==2092)return "North bridge out of Amore";
    if(map==3080&&id==9&&point==3081)return "South exit of the Cave of Waters";
    if(map==3080&&id==2&&point==3082)return "North exit toward Lake Dorr";
    if(map==3100&&id==11&&point==3101)return "South exit of Waterfall Way";
    if(map==3100&&id==4&&point==3102)return "North exit toward Amore";
    return nullptr;
}

inline std::optional<GateLeg> cave_gate(int phase,int area,Vec3 position) {
    if(phase==3030&&(area==1||area==5)&&position.y>=10800&&position.y<=12300&&position.x>=2100&&
        position.z>=80&&position.z<=300)
        return GateLeg{5,3080,10000,"cave-second-pool","Passage to the second pool",
            "Take the passage on this bank to the next pool."};
    if(phase==3030&&area==9&&position.x>=300&&position.x<=1500&&
        position.y>=65500&&position.y<=67410&&position.z>=125&&position.z<=730)
        return GateLeg{12,3080,5,"cave-upper-passage","Passage to the upper ledge",
            "Take the upper passage back above the entrance."};
    if(phase==3030&&(area==0||area==3)&&position.y>=-1700&&position.y<=2500) {
        if(position.z<500)
            return GateLeg{0,3080,1,"cave-first-pool","Passage to the first pool",
                "Take the passage from the entrance to the first pool."};
        if(position.z>730)
            return GateLeg{14,-1,-1,"cave-maiden","Passage to the Water Maiden",
                "Follow the upper path to the Water Maiden.",true};
    }
    if(phase==3085&&(area==0||area==3)&&position.y>=-1700&&position.y<=2500&&position.z<500)
        return GateLeg{16,3080,9,"cave-lake-passage","Passage to Lake Dorr",
            "Take the lower passage from the cave entrance toward Lake Dorr."};
    if(phase==3040&&area==4)
        return GateLeg{15,3080,8,"cave-maiden-return","Passage back to the upper save point",
            "Leave the Water Maiden's chamber through the passage."};
    if(phase==3040&&(area==0||area==3)&&position.y>=-1700&&position.y<=2500&&position.z<500)
        return GateLeg{9,1000,3081,"cave-south-exit","South exit of the Cave of Waters",
            "Leave the cave by the south exit and return to Olderion."};
    if(phase==3100&&(area==2||area==5||area==6)&&position.y>=-5200&&position.y<=-3800)
        return GateLeg{1,3080,11,"cave-drained-passage","End of the drained passage",
            "Follow the drained passage to the ladder chamber."};
    if(phase==3100&&(area==1||area==7)&&position.y<-8100&&position.z>=1500)
        return GateLeg{2,1000,3082,"cave-north-exit","North exit toward Lake Dorr",
            "Leave the cave by the high exit toward Lake Dorr."};
    return {};
}

inline std::optional<GateLeg> waterfall_gate(int phase,int area) {
    if(phase==3150) {
        if(area==12)return GateLeg{18,3100,27,"waterfall-altar-return","Passage back from the altar",
            "Return from the altar to the path leading to the north exit."};
        if(area==11)return GateLeg{4,1000,3102,"waterfall-north","North exit toward Amore",
            "Take the north exit toward Amore."};
    }
    if(phase==3190) {
        if(area==12)return GateLeg{18,3100,27,"waterfall-altar-return","Passage back from the altar",
            "Return from the altar passage to the north landing."};
        if(area==0)return GateLeg{11,1000,3101,"waterfall-south-return","South exit of Waterfall Way",
            "Take the south exit to the road toward Olderion."};
    }
    if(phase!=3140)return {};
    switch(area) {
    case 0:return GateLeg{12,3100,12,"waterfall-first-passage","Passage behind the waterfall",
        "Take the first passage into Waterfall Way."};
    case 3:return GateLeg{1,3100,14,"waterfall-high-passage","Passage along the upper path",
        "Follow the upper path into the next passage."};
    case 6:return GateLeg{15,3100,16,"waterfall-crossing","Passage past the waterfall",
        "Cross the walkway to the passage on the far side."};
    case 8:return GateLeg{2,3100,18,"waterfall-bridges","Passage to the upper bridges",
        "Go through to the upper bridges."};
    case 10:return GateLeg{7,3100,22,"waterfall-altar","Passage to the altar",
        "Continue across the upper bridges to the altar."};
    case 13:return GateLeg{9,3100,25,"waterfall-lower-nook-return","Passage back to the lower path",
        "Return to the lower path toward the long ladder."};
    case 14:return GateLeg{6,3100,21,"waterfall-upper-nook-return","Passage back to the upper path",
        "Return to the upper path toward the altar."};
    default:return {};
    }
}

struct Pool {
    int pedestal,boat;
    const char* key;
    const char* name;
    const char* rowing;
};
inline std::optional<Pool> cave_pool(int area,Vec3 position) {
    if((area==1||area==2||area==5)&&position.y>=8400&&position.y<15500)
        return Pool{36,44,"cave-first-water","first pool","Row to the passage on the far bank. Disembark at the landing by the exit."};
    if(area==8&&position.y>=36500&&position.y<41500)
        return Pool{35,48,"cave-second-water","second pool","Row to the forward passage into the third pool. Keep the boat for the crossing."};
    return {};
}

inline const char* olderion_label(int type,int id) {
    if(type==6&&id==5)return "Olderion inn entrance";
    if(type==1) {
        switch(id) {
        case 16:return "Olderion innkeeper";
        case 18:return "Local by the tree";
        case 21:return "Soldier by the bridge";
        case 26:return "Warden of the Waters";
        case 32:return "Arsha";
        default:return nullptr;
        }
    }
    if(type==2) {
        switch(id) {
        case 17:return "Water near the city entrance";
        case 18:return "Water by the orphanage";
        case 19:return "Water on the way to Old Town";
        default:return nullptr;
        }
    }
    return nullptr;
}
}
