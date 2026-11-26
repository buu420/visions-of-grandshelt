#pragma once
#include "resonance/ActivationGoal.hpp"
#include "resonance/Story.hpp"
#include "resonance/UnrealApi.hpp"
#include <string>
#include <string_view>
#include <vector>
namespace resonance {
// A marker the native minimap presents right now (story-marker-presentation-20261101):
// a default-displayed base icon of a verified kind, or a marker of the one Sub quest the
// player tracked in the journal. actor is valid only during the calling game-thread frame.
struct NativeMapMarker { std::string id; std::string label; unreal::UObject* actor{}; Vec3 position; bool side_quest{}; bool interactable{}; };
// minimap_vm is the caller's current, owned and displayed CPP_VM_MiniMap. only_id limits the
// read to one registry entry. Nothing is cached, loaded, written or executed.
std::vector<NativeMapMarker> capture_map_markers(unreal::UObject* pawn,unreal::UObject* game_instance,
    unreal::UObject* minimap_vm,std::string_view only_id={},std::string_view selected_esper={});
bool thunderclap_esper_marker(unreal::UObject* actor);
std::optional<NativeMapMarker> capture_thunderclap_esper(unreal::UObject* pawn,std::string_view selected_esper);
// Read-only game-thread capture of current main QuestData, active area-wide
// quest actors and audited opening continuations. Never loads future assets.
StorySnapshot capture_story_input(resonance::unreal::UObject* controlled_pawn);
std::optional<ActivationBox> capture_antlion_sinkhole(resonance::unreal::UObject* pawn,Vec3 feet);
// Resolve the currently active native interaction event without starting it.
bool field_gimmick_offers_event(resonance::unreal::UObject* actor,std::string_view expected);

// Crate and water observations authorize a lane. The caller must still prove
// the complete walking route using the live pawn capsule and floor queries.
struct StoryShrineBridgeRoute {
    ActivationBox bounds;
    std::vector<Vec3> turns;
};
bool is_story_shrine_bridge_goal(resonance::unreal::UObject* target,Vec3 goal);
std::optional<StoryShrineBridgeRoute> capture_story_shrine_bridge(resonance::unreal::UObject* pawn,
    resonance::unreal::UObject* target,Vec3 goal);
}
