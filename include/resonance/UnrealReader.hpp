#pragma once
#include "resonance/Core.hpp"
#include "resonance/Speech.hpp"
#include "resonance/UnrealApi.hpp"
#include "resonance/Narration.hpp"
#include "resonance/Navigation.hpp"
#include "resonance/DestinationTravel.hpp"
#include "resonance/RouteBeaconOutput.hpp"
#include "resonance/Story.hpp"
#include "resonance/BattleInput.hpp"
#include "resonance/ActivationGoal.hpp"
#include "resonance/AccessibilityMenu.hpp"
#include "resonance/Hotkeys.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <filesystem>
#include <unordered_map>
#include <utility>
namespace resonance {
enum Command : unsigned { Repeat=1, Previous=2, Next=4, Route=8, Screen=16, Help=32,
    PreviousCategory=64,NextCategory=128,ToggleGuidance=256,ToggleAutomatic=512,
    PreviousPageTarget=1024,NextPageTarget=2048,ControllerOrigin=4096,ReviewNavigation=8192,StopNavigation=16384,
    BattleHp=1u<<15,BattleMp=1u<<16,BattleGauges=1u<<17,BattleBuffs=1u<<18,BattleDebuffs=1u<<19,
    BattlePanelOpen=1u<<20,BattlePanelClose=1u<<21,BattleVitals=1u<<22,BattleEffects=1u<<23,
    BattleInterrupt=1u<<24,BattleInputKey=1u<<25,ResultUp=1u<<26,ResultDown=1u<<27 };
inline constexpr unsigned battle_only_commands=BattleInterrupt|BattleInputKey|ResultUp|ResultDown;
struct JournalScrollInput {
    unreal::UObject* layout{};
    int quest{};
    unsigned long long at{};
};
const std::string& reader_stage();
class UnrealReader {
public:
    UnrealReader(Speech&,RouteBeaconOutput&,std::filesystem::path audio_settings_file);
    void tick(unsigned commands);
    void movement_tick();
    unsigned controller_commands();
    unsigned area_browse_commands();
    bool battle_commands_available() const {return battle_control_layout_||battle_results_layout_;}
    void language_changed(bool read_subtitles) {
        read_subtitles_=read_subtitles;
        core_.reset(); battle_core_.reset(); battle_result_core_.reset();
    }
    bool take_menu_focus_change() { return std::exchange(menu_focus_changed_,false); }
    // Install on the game thread. Called with the latest captured frame/clocks;
    // true defers automatic dialogue. Menu and choice reading remains immediate.
    void set_description_observer(std::function<bool(const Frame&)> observer) {description_observer_=std::move(observer);}
    void on_event(resonance::unreal::UObject*,resonance::unreal::UFunction*,void*);
    bool before_event(resonance::unreal::UObject*,resonance::unreal::UFunction*,void*);
    const std::vector<NarrationClock>& narration_clocks() const { return narration_clocks_; }
private:
    Speech& speech_;
    RouteBeaconOutput& beacon_;
    std::filesystem::path audio_settings_file_;
    AudioSettings audio_settings_;
    AccessibilityMenu accessibility_menu_;
    ReaderCore core_;
    BattleCore battle_core_;
    BattleResultCore battle_result_core_;
    BattleInput battle_input_;
    std::string battle_context_,battle_status_,battle_result_status_;
    unsigned long long battle_output_epoch_{};
    unreal::UObject* battle_review_layout_{};
    unreal::UObject* battle_control_layout_{};
    unreal::UObject* battle_results_layout_{};
    KeyEdges battle_pad_edges_;
    bool battle_command_visible_{};
    std::uint64_t battle_review_epoch_{1};
    unsigned long long battle_binding_checked_{};
    bool battle_controller_binding_{};
    std::string context_, target_id_;
    std::vector<TargetSnapshot> targets_;
    unsigned long long last_diagnostic_{};
    std::unordered_map<std::string,std::string> speakers_;
    std::unordered_map<std::string,unsigned long long> lines_;
    std::atomic<unsigned long> game_thread_{};
    bool menu_focus_changed_{};
    JournalScrollInput journal_scroll_;
    std::vector<NarrationClock> narration_clocks_;
    std::function<bool(const Frame&)> description_observer_;
    bool description_busy_{};
    bool read_subtitles_{};
    std::chrono::steady_clock::time_point proximity_sample_{};
    std::string proximity_world_,proximity_player_;
    StoryTracker story_;
    std::atomic<bool> story_dirty_{true};
    struct TargetSelection {std::array<std::string,7> targets;};
    std::unordered_map<std::string,TargetSelection> navigation_selections_;
    std::string navigation_selection_context_;
    std::size_t navigation_category_{};
    struct AreaRouteVerdict { Vec3 origin,position; std::uint64_t sampled; bool reachable; };
    struct AreaBrowse {
        std::string area;
        std::uint64_t revision{};
        std::unordered_map<std::string,AreaRouteVerdict> routes;
        std::vector<std::string> checks;
        std::size_t next{}, category{};
        unsigned request{};
        bool announced_wait{};
        std::uint64_t probes{}, total_us{}, max_us{};
    };
    std::optional<AreaBrowse> area_browse_;
    std::uint64_t area_browse_revision_{};
    std::wstring target_type_;
    int destination_map_{-1};
    struct DestinationLeg {
        TravelLocation departure;
        TravelPassage passage;
        NavigationMode mode;
        bool controller{};
        TravelContinuation continuation;
    };
    std::optional<DestinationLeg> destination_leg_;
    std::vector<TravelPassage> destination_crossed_;
    void destination_commands(resonance::unreal::UObject*,unsigned,bool);
    bool suspend_auto_walk_trip();
    bool pause_field_guidance();
    void cancel_auto_walk_trip();
    void observe_destination(resonance::unreal::UObject*,bool battle);
    void follow_destination_leg(resonance::unreal::UObject*,const TravelLocation&,const TravelPassage&,
        const std::string& target,NavigationMode,bool controller,TravelHandoff handoff=TravelHandoff::Wait);
    struct StoryWalk {
        TravelLocation departure;
        std::optional<TravelPassage> passage;
        std::string selected,epoch;
        std::optional<int> progress;
        bool controller{},at_target{};
        TravelContinuation continuation;
        std::uint64_t retry_until{},retry_at{};
        bool recovering_route{};
        std::optional<Vec3> recovery_origin;
    };
    std::optional<StoryWalk> story_walk_;
    void arm_story_walk(resonance::unreal::UObject*,bool controller,const NavigationUpdate&);
    void observe_story_walk(resonance::unreal::UObject*,bool battle);
    struct KnownTarget {
        std::string id,world,player,label;
        std::wstring type;
        Vec3 position;
    };
    std::optional<KnownTarget> active_target_;
    NavigationFollower navigation_;
    struct FieldExit { TravelLocation departure; TravelPassage passage; };
    std::optional<FieldExit> navigation_field_exit_;
    PathSnapshot navigation_path_;
    double navigation_path_sample_{};
    std::optional<StoryCrossing> navigation_crossing_;
    std::optional<double> navigation_activation_arrival_metres_;
    std::string navigation_room_door_;
    bool navigation_door_approach_{},antlion_warning_announced_{};
    std::string navigation_path_reason_;
    std::string navigation_target_reason_,area_refresh_status_,area_read_status_;
    std::optional<GroundCoverage> navigation_ground_;
    std::wstring navigation_capsule_mesh_;
    std::optional<ActivationBox> navigation_capsule_room_;
    std::wstring navigation_swept_mesh_;
    std::size_t navigation_bypass_rejoin_{};
    Vec3 navigation_route_goal_{};
    std::uint64_t navigation_path_id_{};
    std::uint64_t navigation_input_path_id_{};
    std::uint64_t navigation_start_wait_until_{};
    std::optional<Vec3> navigation_motion_origin_;
    bool controller_start_pending_{};
    std::string navigation_ground_reason_;
    bool navigation_controller_{},controller_modifier_seen_{};
    std::string controller_context_,controller_gate_status_,controller_unavailable_reason_;
    std::uint64_t controller_epoch_{1};
    std::string navigation_speech_stop_pending_;
    resonance::unreal::UObject* refresh_navigation_target(resonance::unreal::UObject*);
    NavigationFrame flight_frame(resonance::unreal::UObject*,NavigationFrame,bool refresh);
    NavigationFrame rowing_frame(resonance::unreal::UObject*,NavigationFrame,bool refresh);
    NavigationFrame navigation_frame(resonance::unreal::UObject*,bool refresh,bool eligible,bool foreground,bool physical_input,bool automatic);
    bool recheck_floor_turn(resonance::unreal::UObject*,NavigationFrame&,std::size_t next);
    void navigation_output(const NavigationUpdate&,bool passive=false);
    void stop_navigation(std::string reason,bool passive=false);
    void apply_story_update(const StoryUpdate&);
};
}
