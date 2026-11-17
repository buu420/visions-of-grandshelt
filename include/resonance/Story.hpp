#pragma once
#include "resonance/Core.hpp"
#include "resonance/Navigation.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace resonance {
// Value-only native observations. A current filtered quest relation or audited
// enabled opening continuation authorizes a destination across the loaded area.
// An exit related only through the destination's map group must also advance the
// objective. A raw object ID or camera visibility alone never authorizes a Story point.
struct StoryDestination {
    std::string id,label,area;
    std::optional<Vec3> position;
    std::string actor_id;
    std::wstring actor_type;
    bool disclosed{},local{},actor_available{},interactable{},exit{};
    std::string unavailable_reason;
    std::optional<int> transition_map; // Revalidate an audited cross-section exit before use.
    bool castle_leg{}; // Revalidate the current castle room or passage before routing.
    bool event_trigger{}; // Active native progression trigger with an owned overlap box.
    bool mitra_leg{}; // Revalidate the return passage from the current Mitra walking area.
    std::wstring exit_source; // Quest-authorized actor or landmark owning the current gate.
    std::string exit_route_key;
};
enum class StoryCrossing {
    CaveFarBank, CaveNextPool, CaveUpperPassage, CaveHiddenReturn,
    ShrinePedestalPassage, ShrinePedestalLanding, ShrineReturnPassage,
    ShrineSecondChannelReturn, ShrineThirdChannelReturn, ShrineHomeLanding,
    ShrineTowerLanding, ShrineWestLanding
};
struct StoryFlightDestination {
    Vec3 position;
    std::string id,label,arrival_message;
};
struct StorySnapshot {
    std::string world,player,objective_id,objective,unavailable_reason;
    bool ready{};
    std::vector<StoryDestination> destinations;
    bool field_control{}; // Native ordinary field control, for passive objective speech.
    int map_id{-1};
    std::string destination_epoch; // Condition-filtered QuestData points; separate from visible objective identity.
    std::string description,destination_title; // Owned current QuestData journal text; title requires a filtered goal.
    std::string next_step; // Short, state-dependent instruction for a multi-stage local objective.
    std::string nearby_clue; // A visible, local environmental clue, spoken once per objective and area.
    std::optional<StoryCrossing> crossing; // Story-selected water leg while the native raft is controlled.
    std::optional<StoryFlightDestination> flight_destination; // Native activation volume for the current airship Story leg.
    std::optional<int> main_progress;
};
struct StorySpatialContext {
    Vec3 origin,right,up; // Current player position and native horizontal walking-input axes.
    double world_to_metres{}; // Verified current-world scale; zero means unavailable.
};
std::string describe_field_position(Vec3 point,const StorySpatialContext&);
struct StoryDestinationDescription {
    TargetSnapshot target;
    std::string detail; // Current point geometry only; never a walking-path instruction.
};
std::vector<StoryDestination> story_destinations(const StorySnapshot&);
std::optional<StoryDestination> continue_story_target(const StorySnapshot&,const std::string& selected);
TargetSnapshot story_target_snapshot(std::string id,const StoryDestination&);
StoryDestinationDescription describe_story_destination(std::string id,const StorySnapshot&,const StoryDestination&,
    std::optional<StorySpatialContext> = {});
bool story_route_available(const StorySnapshot&,const StoryDestination&);
std::string story_status(const StorySnapshot&);
std::string story_details_status(const StorySnapshot&);
std::string story_help_status(const StorySnapshot&);
bool story_announcement_allowed(const Frame&,bool foreground,bool output_ready,bool field_control,bool description_busy,bool native_voice_clear=true);
struct StoryUpdate {
    bool context_changed{},objective_changed{},navigation_invalidated{};
    std::vector<SpeechEvent> speech;
    bool destination_changed{};
};
std::optional<NavigationUpdate> stop_changed_story_navigation(const StoryUpdate&,bool story_selected,NavigationFollower&);
class StoryTracker {
public:
    StoryUpdate observe(const StorySnapshot&,bool can_announce,std::uint64_t output_epoch);
    bool needs_announcement(const StorySnapshot&,std::uint64_t output_epoch) const;
    const StorySnapshot& snapshot() const { return current_; }
    void reset();
private:
    std::string announcement_key(const StorySnapshot&) const;
    bool needs_objective_announcement(const StorySnapshot&,std::uint64_t) const;
    StorySnapshot current_;
    // Unknown current reads withdraw routes without replacing these known values.
    std::string last_world_,last_player_,last_objective_,last_destination_,announced_;
    int last_map_id_{-1};
    std::optional<SpeechEvent> pending_;
    std::vector<std::string> announced_clues_;
    std::uint64_t announced_epoch_{};
};
}
