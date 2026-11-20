#pragma once
// Read-only, game-thread capture of active loaded-area entities from the current
// world's native map registries (CPP_MapManager storages). Availability is the game's
// own hidden/visible/tick/collision state; no camera, render, line-of-sight or prior
// sight is used. Nothing is cached between calls, loaded, written, evaluated or executed.
#include "resonance/Core.hpp"
#include "resonance/DestinationTravel.hpp"
#include "resonance/ActivationGoal.hpp"
#include "resonance/FieldProximity.hpp"
#include "resonance/UnrealApi.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace resonance {
// actor is valid only during the calling game-thread frame; never retain it.
struct AreaTarget { TargetSnapshot snapshot; resonance::unreal::UObject* actor{}; double distance{}; std::size_t category{}; std::wstring type; Vec3 position; };
// Latest capture only, indexed like the reader's categories: All (0), People, Objects,
// Exits/doors/ladders, Save points, Story (5, never counted by area captures).
// unavailable_reason: the pawn, its position or the registries could not be read, so
// nothing was listed. unreadable/first_issue: listed-category entities whose state or
// position could not be read, counted in All and their own category only; verified
// inactive or removed entities are never counted here.
struct AreaCaptureReport { std::string unavailable_reason; std::array<std::size_t,6> unreadable{}; std::array<std::string,6> first_issue; };
// Active NPCs, supported gimmicks and normal exits in the pawn's world, nearest first.
std::vector<AreaTarget> capture_area_targets(resonance::unreal::UObject* pawn);
struct FieldLocation {int map,area;};
std::optional<FieldLocation> capture_field_location(resonance::unreal::UObject* pawn);
std::optional<TravelLocation> capture_travel_location(resonance::unreal::UObject* pawn);
struct LiveTravelPassage {AreaTarget target;TravelPassage passage;};
std::vector<LiveTravelPassage> capture_travel_passages(resonance::unreal::UObject* pawn,const TravelLocation&);
std::optional<bool> evaluate_travel_condition(std::wstring_view condition);
// Short-range positioned targets and verified automatic transition boxes in the same field frame.
std::vector<FieldProximityTarget> capture_field_proximity(resonance::unreal::UObject* pawn,
    const StorySpatialContext&,std::vector<ActivationBox>& passages);
std::string field_npc_label(resonance::unreal::UObject* actor);
// Re-resolves one target from the registries with its live position. Disabled,
// despawned or unregistered actors return nothing and leave the report clean; a target
// whose state or position cannot be read returns nothing and is counted in the report.
std::optional<AreaTarget> capture_area_target(resonance::unreal::UObject* pawn,const std::string& target_id);
AreaCaptureReport area_capture_report();
// Live root transform for registered tracking/Story actors. Never queries the
// actor's movement agent; rootless or unreadable positions remain unavailable.
std::optional<Vec3> tracked_actor_position(resonance::unreal::UObject* actor);
// Owned box of a live transition or story trigger.
std::optional<ActivationBox> capture_activation_box(resonance::unreal::UObject* actor);
std::optional<ActivationBox> capture_swing_door_trigger(resonance::unreal::UObject* pawn,
    resonance::unreal::UObject* actor);
std::optional<ActivationBox> capture_factory_conveyor(resonance::unreal::UObject* pawn,resonance::unreal::UObject* actor);
// Owned interaction geometry for a supported live target. Overlap is only a
// route candidate. The player's native interaction list confirms arrival.
std::optional<ActivationBox> capture_interaction_box(resonance::unreal::UObject* actor,
    resonance::unreal::UObject* pawn);
std::optional<ActivationBox> capture_ladder_bottom_box(resonance::unreal::UObject* actor);
// These interactables need an approach to the live prompt box rather than the actor origin.
bool requires_checked_interaction_box(resonance::unreal::UObject* actor);
std::optional<bool> burning_castle_door_open(resonance::unreal::UObject* actor);
std::optional<bool> kol_inn_door_open(resonance::unreal::UObject* actor);
std::optional<bool> olderion_inn_door_open(resonance::unreal::UObject* actor);
std::optional<bool> chapter_four_story_door_open(resonance::unreal::UObject* actor);
std::optional<bool> gaberada_elder_door_open(resonance::unreal::UObject* actor);
std::optional<bool> research_lab_door_open(resonance::unreal::UObject* actor);
std::optional<bool> research_lab_rope_lowered(resonance::unreal::UObject* pawn,
    resonance::unreal::UObject* rope,resonance::unreal::UObject* ladder);
std::optional<ActivationBox> capture_research_lab_lift_entry(resonance::unreal::UObject* pawn,resonance::unreal::UObject* actor);
std::optional<ActivationBox> capture_research_lab_doorway(resonance::unreal::UObject* pawn,resonance::unreal::UObject* actor);
struct RaftBoardingPoint {
    Vec3 position;
    bool level_with_bank;
};
std::optional<RaftBoardingPoint> capture_raft_boarding_point(resonance::unreal::UObject* actor,
    resonance::unreal::UObject* pawn);
// Named physical doors used to reach Mitra's enclosed outdoor areas.
const char* mitra_passage_door(resonance::unreal::UObject* actor);
struct WalkingSection {
    int id{-1};
    ActivationBox bounds;
    std::optional<std::vector<resonance::unreal::UObject*>> exits; // Game-thread capture only.
};
std::optional<WalkingSection> capture_walking_section(resonance::unreal::UObject* actor,bool include_links=true,
    std::optional<Vec3> feet=std::nullopt);
// Unique linked exit with this current native destination. The caller owns the
// same-frame section capture and checks that it is the expected section.
resonance::unreal::UObject* active_section_exit(resonance::unreal::UObject* pawn,const WalkingSection& section,int destination_map,std::string& reason);
struct CastleLeg {
    resonance::unreal::UObject* actor{}; // Same-frame capture only.
    Vec3 position;
    std::string label;
    std::optional<Vec3> hall_side;
};
std::optional<CastleLeg> capture_castle_leg(resonance::unreal::UObject* pawn);
struct MitraLeg {
    resonance::unreal::UObject* actor{};
    Vec3 position;
    const char* label{};
};
std::optional<MitraLeg> capture_mitra_leg(resonance::unreal::UObject* pawn);
struct StoryExitRoute {
    resonance::unreal::UObject* actor{}; // Same-frame capture only.
    Vec3 position;
    std::string label,route_key;
    int destination_map{-1};
    std::optional<int> destination_point;
    bool event_only{};
};
// Resolve a quest-authorized exit or map landmark to its live activation volume.
std::optional<StoryExitRoute> capture_story_exit_route(resonance::unreal::UObject* pawn,resonance::unreal::UObject* actor);
// Story actors whose registry membership and quest relation the caller established.
// Live, same world, not destroying and !bHidden; CPP_Unit and CPP_MapUnit_Object also
// need their native m_IsVisible. Ordinary NPC area tick is not required. The linked
// native exit predicate applies only when the caller claims the actor as an exit.
bool story_actor_available(resonance::unreal::UObject* pawn,resonance::unreal::UObject* actor,bool require_exit=false);
// Why the latest story_actor_available/area_actor_available query could not read its
// actor; empty when the answer was verified. Never changes area_capture_report().
std::string area_actor_unavailable_reason();
// Transitional ordinary predicate (NPC tick required) until Story callers migrate.
bool area_actor_available(resonance::unreal::UObject* pawn,resonance::unreal::UObject* actor);
// Diagnostic of the latest capture: counts, per-rule rejects, unreadable counts; no pointers.
std::string last_area_capture_summary();
// Transitional: latest capture's unavailable_reason or latest actor query's reason.
std::string area_capture_unavailable_reason();
}
