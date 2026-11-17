#pragma once
#include "AreaInputValidation.hpp"

namespace resonance::area_input_detail {
// Steam 24250053 MapUnitAsset sprite roles corroborated by the placed field models.
// Use names for recognizable party members and describe unnamed sprites by appearance.
inline const char* npc_appearance_label(int sprite) {
    switch(sprite) {
    case 20:return "Lasswell";
    case 30:return "Fina";
    case 40:return "Lid";
    case 50:return "Nichol";
    case 70:return "Jake";
    case 80:return "Sakura";
    case 100:return "Raegen";
    case 200:return "King of Grandshelt";
    case 260:return "Evan";
    case 420:return "Ayaka";
    case 470:return "Charlotte";
    case 710:case 1450:return "Chocobo";
    case 720:return "Moogle";
    case 910:case 911:case 912:case 920:case 921:case 922:case 923:
    case 930:case 931:case 932:case 933:case 940:return "Man";
    case 960:case 961:case 962:case 970:case 1530:return "Boy";
    case 980:case 981:case 982:case 983:case 1080:return "Older man";
    case 1010:case 9000:return "Blacksmith";
    case 1012:case 1013:return "Fisherman";
    case 1050:return "Sailor";
    case 1051:return "Ship captain";
    case 1060:return "Grandshelt soldier";
    case 1061:return "Dilmagian soldier";
    case 1062:return "Olderion soldier";
    case 1063:return "Zoldaad soldier";
    case 1110:case 1320:return "Researcher";
    case 1120:case 1121:return "Dwarf";
    case 1130:case 1131:case 1132:case 1133:case 1134:case 1135:
    case 1260:case 1265:return "Rebel soldier";
    case 1150:return "Black mage";
    case 1170:case 1171:case 1172:case 1180:case 1181:
    case 1220:case 1221:case 1222:case 1223:return "Woman";
    case 1200:case 1202:case 1210:case 1360:case 1500:return "Girl";
    case 1240:case 1241:case 1242:case 1243:return "Older woman";
    case 1270:case 1271:case 1272:case 1273:return "White mage";
    case 1540:return "Robed figure";
    case 1570:case 1571:case 1572:case 1573:return "Workshop technician";
    case 1680:return "Miner";
    case 1690:return "Merchant";
    case 1720:case 1721:return "Fisherwoman";
    case 1760:return "Station attendant";
    case 9010:case 9011:return "Cat";
    case 9012:return "Calico cat";
    case 10064:return "Adamandelion";
    case 10087:return "Ashasaur";
    default:return area_label(AreaKind::Person);
    }
}

inline const char* object_appearance_label(AreaKind kind,std::wstring_view type,std::optional<bool> state={}) {
    if(kind==AreaKind::FieldItem)
        return type==L"BP_MapGimmick_FieldItem_Medal_C"?"Medal pickup":"Sparkling ground pickup";
    if(kind==AreaKind::TreasureChest&&type.starts_with(L"BP_MapGimmick_TreasureBox_Safe"))
        return state.value_or(false)?"Open treasure safe":"Treasure safe";
    if(kind==AreaKind::MovableObject&&type.starts_with(L"BP_MapGimmick_PushObject_WoodenBox"))
        return "Pushable wooden crate";
    if(kind==AreaKind::Door||kind==AreaKind::TriggerDoor) {
        if(type.find(L"Slid")!=std::wstring_view::npos)return "Sliding door";
        if(type.find(L"Single")!=std::wstring_view::npos)return "Single-panel door";
    }
    return area_label(kind,state);
}

// These placements are native quest objectives or prompts once their own
// display and interaction conditions are live.
inline const char* exact_placement_label(AreaKind kind,std::wstring_view actor,int id) {
    struct Placement {AreaKind kind;int id;std::wstring_view actor;const char* label;};
    constexpr Placement placements[]{
        {AreaKind::Interaction,133,L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
            L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1","White teleport portal"},
        {AreaKind::Book,142,L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
            L"PersistentLevel.BP_MapGimmick_BlockingBase_Child_MagicPot_C_1","Pearly Pot"},
        {AreaKind::Book,143,L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
            L"PersistentLevel.BP_MapGimmick_BlockingBase_Child_MagicPot_C_2","Pearly Pot"},
        {AreaKind::Interaction,4,L"/Game/Map/Dng/05Old/05Old_43/EV/Dng_05Old_43_01_EV.Dng_05Old_43_01_EV:"
            L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_2","Water ley line"},
        {AreaKind::Interaction,9,L"/Game/Map/Dng/04Dir/04Dir_40/EV/Dng_04Dir_40_01_EV.Dng_04Dir_40_01_EV:"
            L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_3","Lightning ley line"},
        {AreaKind::Interaction,8,L"/Game/Map/Town/08Gro/08Gro_01/EV/Town_08Gro_01_01_EV.Town_08Gro_01_01_EV:"
            L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1","Quest prompt in the inn"},
        {AreaKind::Interaction,2,L"/Game/Map/Dng/01Gra/01Gra_44/EV/Dng_01Gra_44_01_EV.Dng_01Gra_44_01_EV:"
            L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1","Twin Titleholders quest marker"},
        {AreaKind::FieldItem,7,L"/Game/Map/Dng/05Old/05Old_43/EV/Dng_05Old_43_01_EV.Dng_05Old_43_01_EV:"
            L"PersistentLevel.BP_MapGimmick_FieldItem_C_1","Waterfall Way flower"},
        {AreaKind::FieldItem,6,L"/Game/Map/Town/01Gra/01Gra_21/EV/Town_01Gra_21_01_EV.Town_01Gra_21_01_EV:"
            L"PersistentLevel.BP_MapGimmick_FieldItem_C_2","Library bookshelf clue"}
    };
    for(const auto& placement:placements)
        if(placement.kind==kind&&placement.id==id&&placement.actor==actor)return placement.label;
    return nullptr;
}

// These exact arrival points have matching facility markers on the minimap.
inline const char* facility_passage(int map,int exit,int arrival) {
    struct Passage {int map,exit,arrival;const char* label;};
    static constexpr Passage passages[]={
        {2015,17,120,"Door to the inn"}, {2015,18,20,"Door from the inn to the street"},
        {2020,5,121,"Door to the inn"}, {2020,0,21,"Door from the inn to the village"},
        {2030,1,120,"Door to the item shop"}, {2030,0,20,"Door from the item shop to the harbor"},
        {2030,4,121,"Door to the weapon and armor shop"}, {2030,3,21,"Door from the weapon and armor shop to the harbor"},
        {2030,6,122,"Door to the inn"}, {2030,5,22,"Door from the inn to the harbor"},
        {2040,1,120,"Door to the inn"}, {2040,3,121,"Door to the weapon and armor shop"}, {2040,4,122,"Door to the item shop"},
        {2040,0,20,"Door from the inn to the street"}, {2040,7,21,"Door from the weapon and armor shop to the street"},
        {2040,8,22,"Door from the item shop to the street"},
        {2060,3,121,"Door to the inn"}, {2060,4,122,"Door to the item shop"}, {2060,5,123,"Door to the weapon and armor shop"},
        {2060,0,21,"Door from the inn to the street"}, {2060,7,22,"Door from the item shop to the street"},
        {2060,8,23,"Door from the weapon and armor shop to the street"},
        {2070,15,30,"Door to the weapon shop"}, {2070,14,31,"Door from the weapon shop to the street"},
        {2090,1,122,"Door to the weapon and armor shop"}, {2090,9,120,"Door to the inn"}, {2090,10,121,"Door to the item shop"},
        {2090,5,22,"Door from the weapon and armor shop to the street"},
        {2090,3,20,"Door from the inn to the street"}, {2090,4,21,"Door from the item shop to the street"},
        {2140,1,120,"Door to the inn"}, {2140,4,122,"Door to the item shop"}, {2140,5,121,"Door to the weapon and armor shop"},
        {2140,9,20,"Door from the inn to the village"}, {2140,0,22,"Door from the item shop to the village"},
        {2140,7,21,"Door from the weapon and armor shop to the village"},
        {2150,2,2152,"Door to the weapon and armor shop"}, {2150,4,2154,"Door to the inn"},
        {2150,5,2153,"Door from the weapon and armor shop to the street"}, {2150,6,2155,"Door from the inn to the street"},
        {2160,4,121,"Door to the vision shop"}, {2160,6,123,"Door to the inn"},
        {2160,0,21,"Door from the vision shop to the street"}, {2160,3,23,"Door from the inn to the street"},
        {2170,6,5,"Door to the inn"}, {2170,5,6,"Door from the inn to the street"},
        {3280,1,10003,"Upper Reach landing"}, {3280,2,10001,"Portal to the next tower section"},
        {3280,3,10004,"Passage from Upper Reach into the tower"},
        {3280,4,9001,"Passage to the entrance landing"}, {3280,5,9000,"Entrance to Crystal Tower"},
        {3280,6,8000,"Portal to the lower side chamber"}, {3280,7,8001,"Portal back to the lower tower"},
        {3280,8,30004,"Passage back to the Middle Reach stairs"},
        {3280,9,30001,"Passage into the crystal platform hall"},
        {3280,10,10002,"Portal to the lower tower"}, {3280,11,30003,"Stairs beyond Middle Reach"},
        {3280,12,40000,"Middle Reach landing"}, {3280,13,40001,"Passage from Middle Reach into the tower"},
        {3281,0,10001,"Passage back to the spiral"}, {3281,3,10000,"Passage to the summit rest stop"},
    };
    for(const auto& passage:passages)
        if(passage.map==map&&passage.exit==exit&&passage.arrival==arrival)return passage.label;
    return nullptr;
}
inline std::optional<int> facility_door_exit(int map,int door) {
    struct Pair {int map,door,exit;};
    static constexpr Pair pairs[]={
        {2015,11,17},{2020,8,5},
        {2030,10,4},{2030,11,1},{2030,12,6},
        {2060,11,3},{2060,12,4},{2060,13,5},{2070,74,15},
        {2090,10,9},{2090,11,10},{2090,12,1},
        {2140,11,4},{2140,12,5},{2140,13,1},
        {2150,2,4},{2150,3,2},{2170,11,6},
    };
    for(const auto& pair:pairs)if(pair.map==map&&pair.door==door)return pair.exit;
    return {};
}
}
