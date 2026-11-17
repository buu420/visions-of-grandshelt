#pragma once
#include "resonance/Core.hpp"
#include <array>
#include <cmath>
#include <optional>

namespace resonance::chapter_six_lab {
inline constexpr int map_id = 3220;
inline constexpr int quest_id = 1006;
inline constexpr int jade_objective = 6030;
inline constexpr int return_objective = 6040;

enum class Place {
    Unknown, Hall, WestCorridor, Card2Room, WestLab, Containers, ContainerShelf, FencedConsole,
    EastEntry, EastMiddle, EastWestPocket, EastCenterPocket, EastEastPocket, EastLadderWell,
    EastUpper, Card3Entrance, Card3Room, EastReturnShelf, UpperLift, LowerLift, LowerLabDoor, JadeLab
};

inline bool between(double value, double lower, double upper) {
    return value >= lower && value <= upper;
}

// Native MapAreaId scopes the observation. Elevation and horizontal bounds
// distinguish adjacent RecastNavMesh-LocalMap components within that area.
inline Place locate(int area, Vec3 feet) {
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z))
        return Place::Unknown;
    const auto x = [&](double lower, double upper) { return between(feet.x, lower, upper); };
    const auto y = [&](double lower, double upper) { return between(feet.y, lower, upper); };
    const auto z = [&](double lower, double upper) { return between(feet.z, lower, upper); };
    switch (area) {
    case 0:
        if (x(-900,3100) && y(-1450,1450) && z(-80,210)) return Place::Hall;
        break;
    case 1:
        if (x(750,1630) && y(-6140,-4760) && z(-80,215)) return Place::Card2Room;
        if (x(-400,750) && y(-6820,-4175) && z(-80,130)) return Place::WestCorridor;
        if (x(750,1100) && y(-6820,-6430) && z(-80,130)) return Place::WestCorridor;
        break;
    case 3:
        if (x(-300,2070) && y(-10070,-8500) && z(-135,220)) return Place::WestLab;
        break;
    case 4:
    case 6:
        if (x(-800,1940) && y(-15100,-13290) && z(260,385)) return Place::ContainerShelf;
        if (area != 4 || !x(-820,1940) || !y(-17350,-13290) || !z(-65,195)) break;
        if (x(-820,-495) && y(-13860,-13290)) return Place::FencedConsole;
        return Place::Containers;
    case 7:
        if (x(1160,1690) && y(5580,6135) && z(250,495)) return Place::EastReturnShelf;
        if (!z(-250,100)) break;
        if (y(8650,10060)) {
            if (x(-1480,-535)) return Place::EastWestPocket;
            if (x(-338,550)) return Place::EastCenterPocket;
            if (x(728,1690)) return Place::EastEastPocket;
        }
        if (x(728,1690) && y(7270,7800)) return Place::EastLadderWell;
        if (x(-315,1635) && y(4900,6390)) return Place::EastEntry;
        if (x(570,725) && y(7300,7960)) break;
        if (x(-1465,1665) && y(5270,8430) &&
            (feet.y > 6390 || feet.x < -315)) return Place::EastMiddle;
        break;
    case 8:
        if (x(-1770,1700) && y(5250,9190) && z(1450,1660)) return Place::EastUpper;
        break;
    case 10:
        if (x(-1010,-225) && y(14075,15380) && z(45,285)) return Place::Card3Room;
        if (x(-225,300) && y(14350,14660) && z(45,285)) return Place::Card3Entrance;
        break;
    case 11:
        if (x(-1400,275) && y(19080,19920) && z(3070,3250)) return Place::UpperLift;
        [[fallthrough]];
    case 12:
        if (x(-425,1530) && y(19080,19920) && z(35,310)) return Place::LowerLift;
        if (x(1530,2075) && y(19080,19920) && z(35,310)) return Place::LowerLabDoor;
        break;
    case 13:
        if (x(-260,1700) && y(25850,28400) && z(15,215)) return Place::JadeLab;
        break;
    }
    return Place::Unknown;
}

enum class LegId {
    Card1, Door1, WestWing, ContainersCorridor, ContainersWarp,
    ConsoleWest, ConsoleSouth, ShelfLadderUp, FencedLadderDown, ConsoleFenced,
    FencedLadderUp, ShelfLadderDown, CenterWarp, Card2, CardReturnDoor, WestReturn,
    Door2, EastWing, EastWarp1, EastWarp2, EastWarp3, EastWarp4, EastWarp5, EastLadderUp,
    EastUpperExit, Card3, Card3Warp, LowerRope, EastLadderDown, EastReturn,
    Door3, NorthWing, LabLiftDown, JadeLabExit, WhiteJade, JadeReturn, LabLiftUp,
    NorthReturn, LeaveRuins, Card3Entrance, JadeHallDoor, JadeHallReturnDoor, Count
};
enum class Kind { Card, KeyDoor, Exit, Warp, Console, Ladder, Rope, Lift, Jade, AutoDoor };
struct Leg {
    Kind kind;
    int unique_id, argument;
    const char* key;
    const char* label;
    const char* instruction;
};
inline constexpr std::array<Leg, static_cast<std::size_t>(LegId::Count)> legs{{
    {Kind::Card,32,20009,"lab-card-1","Level 1 Lab Key Card","Pick up the Level 1 Lab Key Card."},
    {Kind::KeyDoor,41,20009,"lab-door-1","Level 1 door","Check the west door with the Level 1 Lab Key Card."},
    {Kind::Exit,1,1,"lab-west-wing","Passage into the west wing","Continue through the open Level 1 door."},
    {Kind::Exit,9,11,"lab-containers-corridor","Passage into the next laboratory","Continue through the west wing."},
    {Kind::Warp,18,1,"lab-containers-warp","Portal in the west laboratory","Check the portal in the west laboratory."},
    {Kind::Console,28,0,"lab-console-west","West console","Activate the west console."},
    {Kind::Console,30,0,"lab-console-south","Console between the containers","Activate the console between the containers."},
    {Kind::Ladder,3,0,"lab-shelf-up","Ladder onto the shelf","Climb the ladder onto the shelf."},
    {Kind::Ladder,2,1,"lab-fenced-down","Ladder to the fenced console","Climb down to the fenced console."},
    {Kind::Console,29,0,"lab-console-fenced","Fenced console","Activate the console within the fence."},
    {Kind::Ladder,2,0,"lab-fenced-up","Ladder back onto the shelf","Climb back onto the shelf."},
    {Kind::Ladder,3,1,"lab-shelf-down","Ladder back to the container floor","Climb down to the container room floor."},
    {Kind::Warp,17,1,"lab-center-warp","Center portal","Check the powered portal in the center of the room."},
    {Kind::Card,33,20010,"lab-card-2","Level 2 Lab Key Card","Pick up the Level 2 Lab Key Card."},
    {Kind::KeyDoor,40,20010,"lab-card-return-door","Door back to the west corridor","Check the door back to the west corridor."},
    {Kind::Exit,8,2,"lab-west-return","Passage back to the entrance hall","Return to the entrance hall."},
    {Kind::KeyDoor,42,20010,"lab-door-2","Level 2 door","Check the east door with the Level 2 Lab Key Card."},
    {Kind::Exit,10,3,"lab-east-wing","Passage into the east wing","Continue through the open Level 2 door."},
    {Kind::Warp,11,1,"lab-east-warp-1","Entrance portal","Check the portal ahead."},
    {Kind::Warp,13,1,"lab-east-warp-2","Center-room portal","Check the portal near the far side of the room."},
    {Kind::Warp,14,1,"lab-east-warp-3","Portal in the west pocket","Check the portal at the far side of this enclosure."},
    {Kind::Warp,9,1,"lab-east-warp-4","Portal in the central pocket","Check the portal on this side of the enclosure."},
    {Kind::Warp,10,1,"lab-east-warp-5","Portal in the east pocket","Check the portal on this side of the enclosure."},
    {Kind::Ladder,21,0,"lab-east-ladder-up","Ladder to the upper walkway","Climb the ladder to the upper walkway."},
    {Kind::Exit,13,5,"lab-east-upper-exit","Passage from the upper walkway","Follow the upper walkway to the laboratory."},
    {Kind::Card,37,20011,"lab-card-3","Level 3 Lab Key Card","Pick up the Level 3 Lab Key Card."},
    {Kind::Warp,19,1,"lab-card-3-warp","Portal back to the east wing","Check the nearby portal to return to the east wing."},
    {Kind::Rope,6,0,"lab-lower-rope","Rolled ladder","Check the rolled ladder to lower it."},
    {Kind::Ladder,20,1,"lab-east-ladder-down","Ladder down from the shelf","Climb down from the shelf."},
    {Kind::Exit,11,4,"lab-east-return","Passage back to the entrance hall","Return to the entrance hall."},
    {Kind::KeyDoor,43,20011,"lab-door-3","Level 3 door","Check the north door with the Level 3 Lab Key Card."},
    {Kind::Exit,3,7,"lab-north-wing","Passage to the elevator","Continue through the open Level 3 door."},
    {Kind::Lift,47,0,"lab-lift-down","Elevator down to the laboratory","Walk onto the elevator."},
    {Kind::Exit,5,9,"lab-jade-exit","Passage into the lower laboratory","Follow the lower passage to the laboratory."},
    {Kind::Jade,31,0,"lab-white-jade","White Jade Stone on the center table","Check the White Jade Stone on the center table."},
    {Kind::Exit,6,10,"lab-jade-return","Passage out of the lower laboratory","Leave the laboratory and return to the elevator."},
    {Kind::Lift,47,1,"lab-lift-up","Elevator up to the entrance passage","Walk onto the elevator."},
    {Kind::Exit,4,8,"lab-north-return","Passage back to the entrance hall","Return to the entrance hall."},
    {Kind::Exit,0,3221,"lab-leave-ruins","Research Center Ruins exit","Leave the Research Center Ruins."},
    {Kind::AutoDoor,49,0,"lab-card-3-entrance","Automatic door into the laboratory","Walk through the automatic door into the laboratory."},
    {Kind::AutoDoor,48,0,"lab-jade-hall-door","Automatic door to the lower laboratory","Walk through the automatic door."},
    {Kind::AutoDoor,48,0,"lab-jade-hall-return-door","Automatic door back to the elevator","Walk through the automatic door toward the elevator."}
}};
inline const Leg& describe(LegId id) { return legs[static_cast<std::size_t>(id)]; }

struct WarpSite {
    int id;
    const wchar_t* actor_name;
    Vec3 source;
};
inline constexpr std::array warp_sites{
    WarpSite{9,L"BP_MapGimmick_Warp_Child04_C_0",{419,8783,-93}},
    WarpSite{10,L"BP_MapGimmick_Warp_Child04_C_1",{1536,8785,-93}},
    WarpSite{11,L"BP_MapGimmick_Warp_Child04_C_2",{103,6208,-93}},
    WarpSite{13,L"BP_MapGimmick_Warp_Child04_C_4",{445,7566,-93}},
    WarpSite{14,L"BP_MapGimmick_Warp_Child04_C_5",{-607,9884,-93}},
    WarpSite{17,L"BP_MapGimmick_Warp_Child03_C_2",{260,-15808,124}},
    WarpSite{18,L"BP_MapGimmick_Warp_Child02_C_1",{0,-9900,120}},
    WarpSite{19,L"BP_MapGimmick_Warp_Child02_C_3",{-838,14174,220}}
};
inline constexpr auto gd_path =
    L"/Game/Map/Dng/08Gro/08Gro_40/GD/Dng_08Gro_40_01_GD.Dng_08Gro_40_01_GD:PersistentLevel.";
inline constexpr auto jade_path =
    L"/Game/Map/Dng/08Gro/08Gro_40/EV/Dng_08Gro_40_01_EV.Dng_08Gro_40_01_EV:PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1";

struct State {
    std::array<std::optional<bool>,3> cards{};
    std::array<std::optional<bool>,4> doors{};
    std::array<std::optional<bool>,3> consoles{};
    std::optional<bool> rope_lowered;
};

inline std::optional<LegId> choose_verified(std::optional<bool> observation, LegId yes, LegId no) {
    if (!observation) return {};
    return *observation ? yes : no;
}

inline bool warp_allowed(unsigned char selection, int side) {
    return (side == 1 || side == 2) && (selection == 0 || selection == side);
}

inline std::optional<LegId> next_leg(int phase, Place place, const State& state) {
    if (phase == return_objective) {
        switch (place) {
        case Place::JadeLab: return LegId::JadeReturn;
        case Place::LowerLabDoor: return LegId::JadeHallReturnDoor;
        case Place::LowerLift: return LegId::LabLiftUp;
        case Place::UpperLift: return LegId::NorthReturn;
        case Place::Hall: return LegId::LeaveRuins;
        default: return {};
        }
    }
    if (phase != jade_objective) return {};
    switch (place) {
    case Place::Hall:
        if (state.cards[2] != false)
            return state.cards[2] == true ?
                choose_verified(state.doors[2], LegId::NorthWing, LegId::Door3) : std::nullopt;
        if (state.cards[1] != false)
            return state.cards[1] == true ?
                choose_verified(state.doors[1], LegId::EastWing, LegId::Door2) : std::nullopt;
        if (state.cards[0] == false) return LegId::Card1;
        if (state.cards[0] == true) return choose_verified(state.doors[0], LegId::WestWing, LegId::Door1);
        return {};
    case Place::WestCorridor:
        return choose_verified(state.cards[1], LegId::WestReturn, LegId::ContainersCorridor);
    case Place::Card2Room:
        if (state.cards[1] == false) return LegId::Card2;
        if (state.cards[1] == true) return choose_verified(state.doors[3], LegId::WestReturn, LegId::CardReturnDoor);
        return {};
    case Place::WestLab: return LegId::ContainersWarp;
    case Place::Containers:
        if (state.consoles[0] == false) return LegId::ConsoleWest;
        if (state.consoles[0] != true) return {};
        if (state.consoles[1] == false) return LegId::ConsoleSouth;
        if (state.consoles[1] != true) return {};
        return choose_verified(state.consoles[2], LegId::CenterWarp, LegId::ShelfLadderUp);
    case Place::ContainerShelf:
        return choose_verified(state.consoles[2], LegId::ShelfLadderDown, LegId::FencedLadderDown);
    case Place::FencedConsole:
        return choose_verified(state.consoles[2], LegId::FencedLadderUp, LegId::ConsoleFenced);
    case Place::EastEntry:
        return choose_verified(state.cards[2], LegId::EastReturn, LegId::EastWarp1);
    case Place::EastMiddle: return LegId::EastWarp2;
    case Place::EastWestPocket: return LegId::EastWarp3;
    case Place::EastCenterPocket: return LegId::EastWarp4;
    case Place::EastEastPocket: return LegId::EastWarp5;
    case Place::EastLadderWell: return LegId::EastLadderUp;
    case Place::EastUpper: return LegId::EastUpperExit;
    case Place::Card3Entrance: return LegId::Card3Entrance;
    case Place::Card3Room: return choose_verified(state.cards[2], LegId::Card3Warp, LegId::Card3);
    case Place::EastReturnShelf: return choose_verified(state.rope_lowered, LegId::EastLadderDown, LegId::LowerRope);
    case Place::UpperLift: return LegId::LabLiftDown;
    case Place::LowerLift: return LegId::JadeHallDoor;
    case Place::LowerLabDoor: return LegId::JadeLabExit;
    case Place::JadeLab: return LegId::WhiteJade;
    default: return {};
    }
}
}
