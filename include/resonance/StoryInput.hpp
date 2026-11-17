#pragma once
#include "resonance/ActivationGoal.hpp"
#include "resonance/Story.hpp"
#include "resonance/UnrealApi.hpp"
namespace resonance {
// Read-only game-thread capture of current main QuestData, active area-wide
// quest actors and audited opening continuations. Never loads future assets.
StorySnapshot capture_story_input(resonance::unreal::UObject* controlled_pawn);

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
