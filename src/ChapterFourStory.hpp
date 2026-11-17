#pragma once
#include "resonance/Navigation.hpp"
#include <array>
#include <cmath>
#include <optional>

namespace resonance::chapter_four {
struct Gate {
    int id;
    int destination;
    int point;
    const char* key;
    const char* label;
    const char* instruction;
    bool event_only{};
};

inline std::optional<Gate> escape_door(int map,int quest,int phase,Vec3 position) {
    if(map!=3120||quest!=1004||phase!=4030||
       !(position.x>=-750.&&position.x<=750.&&position.z>=-120.&&position.z<=225.))return {};
    // Each door loads the next car across a gap in the walking mesh.
    struct Car {double from,to;Gate forward;};
    constexpr std::array cars{
        Car{-4100,350,{0,3120,0,"train-car-1","Door to the next train car","Go through the door into the second train car."}},
        Car{650,5250,{2,3120,2,"train-car-2","Door to the next train car","Cross this car to the door into the third car."}},
        Car{5550,10150,{4,3120,4,"train-car-3","Door to the next train car","Go through the next door to reach the fourth car."}},
        Car{10450,15050,{6,3120,6,"train-escape","Train escape door","Head to the far door to escape the train."}}
    };
    for(const auto& car:cars)if(position.y>=car.from&&position.y<=car.to)return car.forward;
    return {};
}

struct FieldLeg {int map,phase,goal;Gate gate;};
inline constexpr std::array field_legs{
    FieldLeg{1000,4000,2100,{31,2100,0,"mozul-entrance","Mozul Station entrance",
        "Follow the canyon to Mozul Station. Interact at the entrance."}},
    FieldLeg{1000,4060,2110,{32,2110,0,"zoldaad-capital-entrance","Entrance to Zoldaad, the Imperial Capital",
        "Follow the railway toward the Imperial Capital."}},
    FieldLeg{2110,4100,3140,{0,3140,-1,"zoldaad-tunnel-entrance","Hidden tunnel entrance",
        "Enter the tunnel behind the bookcase."}},
    FieldLeg{3140,4100,3140,{2,2120,2121,"tunnel-rebel-hideout","Passage to the Rebel Hideout",
        "Follow the tunnel to the Rebel Hideout."}},
    FieldLeg{2120,4130,3131,{5,1000,2121,"hideout-desert","Hideout exit to the desert",
        "Take the desert exit to reach the Weapons Factory."}},
    FieldLeg{1000,4130,3131,{34,3131,0,"factory-entrance","Weapons Factory entrance",
        "Follow the desert road to the Weapons Factory."}},
    FieldLeg{3131,4130,3131,{0,3130,0,"factory-entry-gate","Gate into the Weapons Factory",
        "Follow Jake through the factory gate."}},
    FieldLeg{3131,4140,3130,{0,3130,0,"factory-entry-gate","Gate into the Weapons Factory",
        "Go through the gate into the factory."}},
    FieldLeg{3130,4150,2120,{4,3131,5000,"factory-return-gate","Passage to the factory entrance",
        "Return through the entrance on the way to the Rebel Hideout."}},
    FieldLeg{3131,4150,2120,{1,1000,3131,"factory-desert-return","Factory exit to the desert",
        "Return to the desert and head for the Rebel Hideout."}},
    FieldLeg{1000,4150,2120,{33,2120,0,"hideout-return","Entrance to the Rebel Hideout",
        "Return to the Rebel Hideout."}},
    FieldLeg{2120,4170,3140,{4,3140,0,"hideout-castle-tunnel","Tunnel entrance from the Rebel Hideout",
        "Take the tunnel from the hideout toward the castle."}},
    FieldLeg{3140,4170,3140,{1,3150,3151,"tunnel-castle","Passage to the castle dungeon",
        "Follow the tunnel to the castle dungeon."}}
};

inline constexpr Gate dungeon_castle{13,3160,0,"dungeon-castle","Passage into Zoldaad Castle",
    "Take the stairs up from the dungeon toward the throne room."};

inline std::optional<Gate> dungeon_passage(int phase,int section) {
    if(phase!=4180&&phase!=4190)return {};
    switch(section) {
    case 0:return Gate{1,3150,2,"dungeon-hall","Door to the jail corridor","Enter the jail corridor."};
    case 1:return Gate{2,3150,10,"dungeon-outer-cells","Door to the outer cells","Follow the corridor to the outer cells."};
    case 2:return Gate{4,3150,9,"dungeon-leave-cell","Exit from the cell wing","Leave the cell wing to reach the corridor."};
    case 3:return Gate{6,3150,5,"dungeon-next-wing","Door to the next wing","Go through to the next wing."};
    case 4:
        if(phase==4180)return Gate{12,3150,12,"dungeon-shera-wing","Passage to the guarded cells",
            "Continue to the guarded cells to look for Shera."};
        break;
    case 5:
        if(phase==4190)return Gate{5,3150,6,"dungeon-key-door-return","Passage back to the locked gate",
            "Return to the locked gate with the dungeon key."};
        break;
    case 6:return Gate{9,3150,13,"dungeon-return-guarded-cells","Passage back to the guarded cells",
        "Return to the guarded cells."};
    }
    return {};
}

inline constexpr Vec3 captain_hall_landing{29381,5600,1182.15};

inline bool captain_room_side(Vec3 position) {
    // The hall route turns at X 29376. Keep that turn on the hall side.
    return position.x>captain_hall_landing.x&&position.x<31800.&&position.y>4800.&&position.y<6400.&&
        std::abs(position.z-1282.)<130.;
}

struct FactoryLeg {
    int id;
    const char* label;
    const char* instruction;
    bool pedestal{};
};

inline std::optional<FactoryLeg> factory_conveyor_leg(Vec3 position,bool bridge_lowered) {
    const auto within=[&](double x0,double x1,double y0,double y1) {
        return position.x>=x0&&position.x<=x1&&position.y>=y0&&position.y<=y1;
    };
    constexpr FactoryLeg to_pedestal{13,"Conveyor to the bridge pedestal","Follow the platform to the conveyor to the bridge pedestal."};
    if(position.z>520.&&position.z<=860.&&within(2490,2940,15980,19020))return to_pedestal;
    if(position.z<65.||position.z>520.)return {};
    if(position.z>=350.) {
        if(within(3200,4020,15240,15980))
            return bridge_lowered?FactoryLeg{14,"Return conveyor from the pedestal","Take the return conveyor from the pedestal."}:
                FactoryLeg{53,"Bridge pedestal","Use the pedestal to lower the bridge section.",true};
        if(bridge_lowered&&within(2490,2940,15520,16600))
            return FactoryLeg{12,"Conveyor back to the bridge hall","Take the conveyor back to the bridge hall."};
        if(within(2490,2940,15980,19020))return to_pedestal;
        if(within(950,1450,18150,19050))
            return FactoryLeg{8,"Conveyor across the far gap","Take the conveyor across the far gap."};
        if(within(950,1450,17560,18000))
            return FactoryLeg{5,"Conveyor to the far platform","Take the conveyor to the far platform."};
        if(within(1720,2240,17160,17850))
            return FactoryLeg{4,"Conveyor across to the next platform","Take the conveyor across to the next platform."};
        if(within(950,1450,16680,17559))
            return FactoryLeg{17,"Conveyor across the middle gap","Take the conveyor across the middle gap."};
        if(within(1720,2120,16080,16750))
            return FactoryLeg{16,"Conveyor to the middle platforms","Take the conveyor to the middle platforms."};
    }
    if(!bridge_lowered&&within(220,1450,12800,16320))
        return FactoryLeg{7,"Conveyor from the bridge hall","Step onto the conveyor from the bridge hall."};
    return {};
}

struct StreetcarStop {int map,sign;const char* event;const wchar_t* level;};
inline constexpr std::array streetcar_stops{
    StreetcarStop{2110,0,"C04_06Zol_01_Choice",L"Town_06Zol_01_01_EV"},
    StreetcarStop{2111,0,"C04_06Zol_02_Choice",L"Town_06Zol_02_01_EV"},
    StreetcarStop{2112,0,"C04_06Zol_03_Choice",L"Town_06Zol_03_01_EV"},
    StreetcarStop{2113,1,"C04_06Zol_04_Choice",L"Town_06Zol_04_01_EV"},
    StreetcarStop{2114,0,"C04_06Zol_05_Choice",L"Town_06Zol_05_01_EV"}
};

inline const char* target_label(int map,int kind,int id) {
    if(kind==2)for(const auto& stop:streetcar_stops)
        if(stop.map==map&&stop.sign==id)return "Streetcar timetable";
    if(kind!=1)return nullptr;
    if(map==2100&&id==2)return "Station Attendant";
    if(map==2110&&id==2)return "Fina in the alley";
    if(map==2114&&id==1)return "Man in blue";
    if(map==2120) {
        switch(id) {
        case 7:return "Rebel soldier near the desert exit";
        case 10:return "Rebel soldier in the central hall";
        case 11:return "Rebel soldier near the entrance";
        case 12:return "Rebel soldier near the tunnel";
        case 13:return "Jake";
        case 14:return "Innkeeper";
        }
    }
    if(map==3120) {
        switch(id) {
        case 2:return "Lasswell";
        case 4:return "Fina";
        case 5:return "Lid";
        case 6:return "Nichol";
        }
    }
    return nullptr;
}
}
