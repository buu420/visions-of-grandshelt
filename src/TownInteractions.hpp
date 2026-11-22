#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace resonance::town_interactions {
struct Placement {
    int map;
    int id;
    std::wstring_view actor_path;
    std::uint8_t interaction;
    const char* label;
    std::size_t category;
    std::wstring_view event_id;
};

inline constexpr std::array placements{
    // The Capital Market timetable inherits Check (4). The other timetables use Transition (34).
    Placement{2110,0,L"/Game/Map/Town/06Zol/06Zol_01/EV/Town_06Zol_01_01_EV.Town_06Zol_01_01_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1",34,"Streetcar timetable",3,L"C04_06Zol_01_Choice"},
    Placement{2111,0,L"/Game/Map/Town/06Zol/06Zol_02/EV/Town_06Zol_02_01_EV.Town_06Zol_02_01_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1",34,"Streetcar timetable",3,L"C04_06Zol_02_Choice"},
    Placement{2112,0,L"/Game/Map/Town/06Zol/06Zol_03/EV/Town_06Zol_03_01_EV.Town_06Zol_03_01_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1",4,"Streetcar timetable",3,L"C04_06Zol_03_Choice"},
    Placement{2113,1,L"/Game/Map/Town/06Zol/06Zol_04/EV/Town_06Zol_04_01_EV.Town_06Zol_04_01_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1",34,"Streetcar timetable",3,L"C04_06Zol_04_Choice"},
    Placement{2114,0,L"/Game/Map/Town/06Zol/06Zol_05/EV/Town_06Zol_05_01_EV.Town_06Zol_05_01_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1",34,"Streetcar timetable",3,L"C04_06Zol_05_Choice"},

    // Each chooser shares its XY position with a Transition_Event icon in the GD sublevel.
    Placement{2130,7,L"/Game/Map/Town/07Mys/07Mys_01/EV/Town_07Mys_01_01_EV.Town_07Mys_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_1",34,"Mysidia warp by the entrance",3,L"Gim_EV_07Mys_Warp_01_Choice"},
    Placement{2130,8,L"/Game/Map/Town/07Mys/07Mys_01/EV/Town_07Mys_01_01_EV.Town_07Mys_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_0",34,"Mysidia warp by the shops",3,L"Gim_EV_07Mys_Warp_02_Choice"},
    Placement{2130,11,L"/Game/Map/Town/07Mys/07Mys_01/EV/Town_07Mys_01_01_EV.Town_07Mys_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_2",34,"Mysidia warp by the potion factory",3,L"Gim_EV_07Mys_Warp_03_Choice"},
    Placement{2130,13,L"/Game/Map/Town/07Mys/07Mys_01/EV/Town_07Mys_01_01_EV.Town_07Mys_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_3",34,"Mysidia warp by Sakura's house",3,L"Gim_EV_07Mys_Warp_04_Choice"},

    // Check_Point (7). The native event selector reads the board's current event, including alternate rows.
    Placement{2030,5,L"/Game/Map/Town/02Lan/02Lan_21/EV/Town_02Lan_21_01_EV.Town_02Lan_21_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_1",7,"Bulletin board",2,{}},
    Placement{2060,4,L"/Game/Map/Town/04Dir/04Dir_01/EV/Town_04Dir_01_04_EV.Town_04Dir_01_04_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_1",7,"Bulletin board",2,{}},
    Placement{2070,3,L"/Game/Map/Town/05Old/05Old_01/EV/Town_05Old_01_01_EV.Town_05Old_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_1",7,"Bulletin board",2,{}},
    Placement{2110,5,L"/Game/Map/Town/06Zol/06Zol_01/EV/Town_06Zol_01_01_EV.Town_06Zol_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_1",7,"Bulletin board",2,{}},
    Placement{2130,32,L"/Game/Map/Town/07Mys/07Mys_01/EV/Town_07Mys_01_01_EV.Town_07Mys_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_5",7,"Bulletin board",2,{}},
    Placement{2140,16,L"/Game/Map/Town/07Mys/07Mys_20/EV/Town_07Mys_20_EV.Town_07Mys_20_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_1",7,"Bulletin board",2,{}},
    Placement{2150,13,L"/Game/Map/Town/08Gro/08Gro_01/EV/Town_08Gro_01_01_EV.Town_08Gro_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_InteractEventTrigger_1",7,"Bulletin board",2,{}},
    Placement{2170,14,L"/Game/Map/Town/09Pha/09Pha_01/EV/Town_09Pha_01_01_EV.Town_09Pha_01_01_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1",7,"Bulletin board",2,{}},
};

constexpr const Placement* find(std::wstring_view actor_path,int id) {
    for(const auto& placement:placements)
        if(placement.actor_path==actor_path&&placement.id==id)return &placement;
    return nullptr;
}
}
