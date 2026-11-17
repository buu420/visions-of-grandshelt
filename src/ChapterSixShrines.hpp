#pragma once

#include <array>
#include <cmath>
#include <optional>

// Exact cooked maps 08Gro_42 and 08Gro_43, Steam build 24250053.
namespace chapter_six_shrines {

enum class Kind { Passage, WalkEvent, Stele, Ladder, Lift, ReturnSave };

struct Location {
    int map;
    int phase;
    int area;
    double z;
};

struct TrialState {
    std::optional<bool> won;
    std::optional<bool> read_stele;
};

struct Leg {
    Kind kind;
    int id;
    const char* label;
    const char* instruction;
    const char* event = nullptr;
    int destination_map = -1;
    int point = -1;
    int door = -1;
    bool from_top = false;
    double lift_height = 0;
};

constexpr Leg passage(int id, int map, int point, const char* label, const char* instruction, int door = -1) {
    return {Kind::Passage, id, label, instruction, nullptr, map, point, door};
}

constexpr Leg lift(int id, double height) {
    Leg leg{Kind::Lift, id, "Shrine elevator", "Use the elevator at this landing."};
    leg.lift_height = height;
    return leg;
}

struct Trial {
    int area;
    int win_flag;
    int stele_flag;
    Leg approach;
    Leg stele;
    Leg onward;
};

constexpr Leg stele(int id, const char* event) {
    return {Kind::Stele, id, "Trial stele", "Examine the stele before approaching the trial.", event};
}

constexpr Leg trial_event(int id, const char* event) {
    return {Kind::WalkEvent, id, "Trial platform", "Approach the platform to continue the trial.", event};
}

inline constexpr std::array trials{
    Trial{1, 608, -1, trial_event(72, "Dng_08Gro_43_Encount_01"), {},
        passage(16, 3250, 3, "Next trial room", "Continue through the door to the next trial.", 92)},
    Trial{2, 609, 617, trial_event(73, "Dng_08Gro_43_Encount_02"), stele(101, "C06_619_01_A"),
        passage(19, 3250, 5, "Next trial room", "Continue through the door to the next trial.", 89)},
    Trial{3, 610, 618, trial_event(74, "Dng_08Gro_43_Encount_03"), stele(102, "C06_619_01_B"),
        passage(21, 3250, 9, "Passage to the lower trial", "Continue through the door to the next trial.", 91)},
    Trial{10, 611, 619, trial_event(75, "Dng_08Gro_43_Encount_04"), stele(103, "C06_619_01_C"),
        passage(4, 3250, 11, "Stairs to the next trial", "Continue through the upper passage.")},
    Trial{11, 612, 620, trial_event(76, "Dng_08Gro_43_Encount_05"), stele(104, "C06_619_01_D"),
        passage(13, 3250, 13, "Stairs to the next trial", "Continue through the upper passage.")},
    Trial{12, 613, -1, trial_event(77, "Dng_08Gro_43_Encount_06"), {},
        {Kind::Ladder, 80, "Ladder beyond the trial", "Climb the ladder to the next section."}},
};

constexpr bool shrine_phase(int map, int phase) {
    if (map == 3240) return phase == 6110 || phase == 6120;
    return map == 3250 && (phase == 6121 || phase == 6122 || phase == 6123 || phase == 6130 ||
        phase == 6140 || phase == 6160);
}

constexpr const Trial* current_trial(Location location) {
    if (location.map != 3250) return nullptr;
    for (const auto& trial : trials) {
        if (trial.area != location.area) continue;
        if (trial.area == 1 && (location.phase == 6122 || location.phase == 6123 || location.phase == 6130))
            return &trial;
        if (trial.area == 12 && location.phase == 6130) return &trial;
        if (trial.area != 1 && trial.area != 12 && (location.phase == 6123 || location.phase == 6130))
            return &trial;
        return nullptr;
    }
    return nullptr;
}

inline std::optional<Leg> gronoa_leg(Location location) {
    const auto [map, phase, area, z] = location;
    if (phase == 6120) {
        if (area == 6 && z < -7500)
            return Leg{Kind::ReturnSave, 15, "Save point with a route to the entrance", "Use the save point to return to the shrine entrance."};
        if (area == 1 && z > -600)
            return passage(4, map, 4, "Stairs to the entrance", "Continue up to the shrine entrance.");
        if (area == 0)
            return passage(0, 1000, 3241, "Exit to Gronoa", "Leave the shrine to continue on Gronoa.");
        return {};
    }
    if (area == 0)
        return passage(3, map, 3, "Stairs down into the shrine", "Take the stairs down into the shrine.");
    if ((area == 1 || area == 2 || area == 8) && z >= -1575 && z <= 550) {
        Leg leg{Kind::Ladder, 0, "Ladder to the lower floor", "Take the ladder down to the lower floor."};
        leg.from_top = true;
        return leg;
    }
    if ((area == 3 || area == 4 || area == 5) && z >= -5650 && z <= -3000) return lift(14, -5590);
    if (area == 6 && z < -7500)
        return Leg{Kind::WalkEvent, 25, "Lower shrine story marker", "Continue into the chamber at the bottom of the shrine.", "C06_616_01"};
    return {};
}

inline std::optional<Leg> lift_room_leg(Location location) {
    struct Landing { int area; int lift_id; double platform_height; };
    constexpr std::array landings{
        Landing{13, 116, 150}, Landing{14, 117, 650}, Landing{18, 119, 2650},
        Landing{16, 118, 1650}, Landing{15, 96, 1145}, Landing{25, 96, 1145},
        Landing{17, 120, 2150}, Landing{20, 120, 2150}, Landing{26, 120, 2150},
    };
    for (const auto& landing : landings) {
        if (location.area == landing.area && std::abs(location.z - landing.platform_height) <= 200)
            return lift(landing.lift_id, landing.platform_height);
    }
    if (location.area == 19 && std::abs(location.z - 3150) <= 200)
        return passage(7, 3250, 17, "Door to the courtyard", "Continue through the door at the top of the lifts.", 90);
    return {};
}

inline std::optional<Leg> next_leg(Location location, TrialState state = {}) {
    const auto [map, phase, area, z] = location;
    if (!shrine_phase(map, phase) || !std::isfinite(z)) return {};
    if (map == 3240) return gronoa_leg(location);
    if (phase == 6121 && area == 0)
        return passage(11, map, 1, "Door to the first trial", "Continue into the first trial room.", 87);
    if (const auto* trial = current_trial(location)) {
        if (!state.won) return {};
        if (*state.won) {
            if (area == 12 && z >= 950)
                return passage(2, map, 15, "Passage to the elevators", "Continue into the elevator room.");
            return trial->onward;
        }
        if (trial->stele_flag < 0) return trial->approach;
        if (!state.read_stele) return {};
        return *state.read_stele ? trial->approach : trial->stele;
    }
    if ((phase == 6123 || phase == 6130) && area == 4 && z < 320)
        return passage(22, map, 10, "Passage back to the trials", "Return to the trial rooms.");
    if (phase == 6130) return lift_room_leg(location);
    if (phase == 6140 && area == 21)
        return passage(8, map, 19, "Passage to the duel", "Continue toward the trial in the courtyard.");
    if (phase == 6160 && area == 21)
        return passage(23, map, 19, "Passage onward", "Continue toward the inner chamber.");
    if (phase == 6160 && area == 22)
        return passage(3, map, 23, "Inner chamber door", "Continue through the doors to the inner chamber.", 88);
    return {};
}


#ifdef RESONANCE_STORY_INPUT_SHRINE_CAPTURE

bool capture(StorySnapshot& story, UObject* pawn, UObject* world, int map, int quest) {
    if (quest != 1006 || !story.main_progress || !shrine_phase(map, *story.main_progress) ||
        read<unsigned char>(pawn, L"m_MapMovementMethod", 1920) != 1) return false;
    const auto field = capture_field_location(pawn);
    const auto position = tracked_actor_position(pawn);
    if (!field || field->map != map || !position) return false;

    const Location location{map, *story.main_progress, field->area, position->z};
    TrialState trial_state;
    if (const auto* trial = current_trial(location)) {
        trial_state.won = story_progress_flag(trial->win_flag);
        if (trial_state.won == false && trial->stele_flag >= 0)
            trial_state.read_stele = story_progress_flag(trial->stele_flag);
    }
    const auto leg = next_leg(location, trial_state);
    if (!leg) return false;

    const auto key = "chapter-six-shrine:" + std::to_string(map) + ":" + std::to_string(leg->id);
    const auto select = [&](UObject* actor, Vec3 point, bool overlap = false) {
        story_step(story, key, leg->instruction);
        story_step_actor(story, actor, point, key, leg->label, overlap);
        return true;
    };
    if (leg->kind == Kind::Passage && leg->door < 0) {
        const auto manager = world_scoped(L"CPP_MapManager", world);
        return !manager.ambiguous && story_gate(story, pawn, world, manager.object, leg->id,
            leg->destination_map, leg->point, key, leg->label, leg->instruction);
    }

    const auto registry = registered_story_gimmicks(world);
    const auto find = [&](int id, const wchar_t* native_class) {
        return registered_gimmick(registry, world, id, native_class);
    };
    switch (leg->kind) {
    case Kind::Passage: {
        auto* door = find(leg->door, L"/Script/mucha.CPP_MapGimmick_Object_DoorBase");
        if (!door || !story_actor_available(pawn, door)) return false;
        const auto door_position = tracked_actor_position(door);
        auto* box = object_getter(door, L"GetRootBoxComponent");
        if (!door_position || !is(box, L"/Script/Engine.BoxComponent") ||
            box != object_value(door, L"m_pRootBoxComponent", 760) || object_getter(box, L"GetOwner") != door) return false;
        const auto collision = getter<unsigned char>(box, L"GetCollisionEnabled");
        if (!collision || *collision > 5) return false;
        // Each gate runs in the positive X direction from the door on its own floor.
        if (*collision == 0 || position->x > door_position->x + 45) {
            const auto manager = world_scoped(L"CPP_MapManager", world);
            return !manager.ambiguous && story_gate(story, pawn, world, manager.object, leg->id,
                leg->destination_map, leg->point, key, leg->label, leg->instruction);
        }
        const auto prompt = capture_interaction_box(door, pawn);
        if (!prompt) return false;
        const auto door_key = key + ":door";
        story_step(story, door_key, "Open the door when the game's interaction appears.");
        story_step_actor(story, door, prompt->center, door_key, "Shrine door");
        return true;
    }
    case Kind::WalkEvent: {
        auto* event = find(leg->id, L"/Script/mucha.CPP_MapGimmick_EventTrigger");
        const auto point = active_story_trigger(pawn, event, leg->event);
        return point && select(event, *point, true);
    }
    case Kind::Stele: {
        auto* stele_actor = find(leg->id, L"/Script/mucha.CPP_MapGimmick_InteractEventTrigger");
        if (!stele_actor || !story_actor_available(pawn, stele_actor) ||
            read<unsigned char>(stele_actor, L"m_MapInteractType", 833) != 4 ||
            getter<unsigned char>(stele_actor, L"GetGimmickCondition") != 0 ||
            !story_event_matches(stele_actor, leg->event) || opening_overlap(pawn, stele_actor) != true) return false;
        // The checked root is the native prompt volume for this stele.
        const auto prompt = getter<Vec3>(object_getter(stele_actor, L"GetRootBoxComponent"), L"K2_GetComponentLocation");
        return prompt && std::isfinite(prompt->x) && std::isfinite(prompt->y) && std::isfinite(prompt->z) &&
            select(stele_actor, *prompt);
    }
    case Kind::Ladder: {
        auto* ladder_actor = find(leg->id, L"/Script/mucha.CPP_MapGimmickLadder");
        const auto approach = story_ladder_approach(pawn, ladder_actor, leg->from_top);
        if (!approach || !select(ladder_actor, *approach)) return false;
        story.destinations.back().interactable = false;
        return true;
    }
    case Kind::Lift: {
        auto* lift_actor = find(leg->id, L"/Script/mucha.CPP_MapGimmick_Elevator_Interact");
        const auto platform = tracked_actor_position(lift_actor);
        if (!platform || std::abs(platform->z - leg->lift_height) > 80 ||
            getter<unsigned char>(lift_actor, L"GetGimmickCondition") != 0 ||
            !story_actor_available(pawn, lift_actor)) return false;
        const auto boarding = capture_interaction_box(lift_actor, pawn);
        if (!boarding || std::abs(boarding->center.z - leg->lift_height) > 160) return false;
        return select(lift_actor, boarding->center);
    }
    case Kind::ReturnSave: {
        auto* save = find(leg->id, L"/Script/mucha.CPP_MapGimmick_SavePoint");
        auto* entrance = find(4, L"/Script/mucha.CPP_MapGimmick_SavePoint");
        if (!save || !entrance || bool_getter(save, L"CanTeleprot") != true ||
            read<unsigned char>(entrance, L"m_IsTeleport", 1001) != 1 || !story_actor_available(pawn, save)) return false;
        const auto* link = static_cast<const std::byte*>(property(entrance, L"m_ParentSavePointData", 1008, 16));
        auto* reflected = link ? entrance->GetPropertyByNameInChain(L"m_ParentSavePointData") : nullptr;
        if (!reflected || !reflected->field_class() || reflected->field_class()->GetName() != L"StructProperty" ||
            !output_struct(reflected, L"/Script/mucha.MAP_GIMMICK_SAVEPOINT_PARENT_DATA", 16) ||
            pod<UObject*>(link, 8) != save) return false;
        const auto prompt = capture_interaction_box(save, pawn);
        return prompt && select(save, prompt->center);
    }
    }
    return false;
}

#endif

}  // namespace chapter_six_shrines
