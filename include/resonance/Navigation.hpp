#pragma once
#include "resonance/Core.hpp"
#include "resonance/GroundCoverage.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace resonance {
enum class NavigationMode { Off, Manual, Automatic };
enum class NavigationReplan { None, OffRoute, Blocked };
// Audited ordinary field input: positive X rotates world +Y, positive Y
// rotates world +X, using the native movement yaw with pitch/roll set to zero.
enum class WalkingYawSource { Control, UnitCamera, ViewTarget };
struct WalkingBasisFlags {
    std::optional<bool> internal,external,force_internal,vertical;
};
struct WalkingAxes { Vec3 right,up; };
std::optional<WalkingYawSource> walking_yaw_source(const WalkingBasisFlags&);
std::optional<WalkingAxes> walking_axes(double yaw_degrees);
std::optional<Vec3> walking_stick(Vec3 direction,const WalkingAxes&);
inline constexpr double navigation_path_max_age=.6;
struct WalkingGroundPolicy { GroundBudget budget; double stop_margin_metres{}; };
std::optional<WalkingGroundPolicy> walking_ground_policy(double speed_world,double velocity_world,
    double braking_world,double world_to_metres);
std::optional<GroundBudget> ground_window_budget(const PathSnapshot&,GroundBudget);
// Convert a surface budget's physical XY lookahead to the native chord arc
// used for prefix identity, then fund that local inspection window.
std::optional<GroundBudget> surface_window_budget(const PathSnapshot&,GroundBudget);
struct WalkingAnalogSpeed { double maximum{},minimum{}; };
std::optional<float> capsule_input_scale(double clearance,double speed,double acceleration,double braking,double tick,
    std::optional<WalkingAnalogSpeed> analog={},float native_gain=1.f);
bool navmesh_supports_capsule(double config_radius,double config_height,double mesh_radius,double mesh_height,
    double capsule_radius,double capsule_half_height);
// Ground probes for a real walking capsule. The floor query starts at the
// previous floor height, and the sweep verifies each successive pose.
std::optional<Vec3> checked_walking_leg(Vec3 from,Vec3 to,double world_to_metres,
    double walkable_floor_z,const GroundProbe&);
struct WalkingBypass { PathSnapshot path; std::size_t rejoin; };
// Find a nearby lane around an unusable edge and rejoin the same route.
// The normal can be zero when no contact was reported. The supplied probe
// verifies both floor and capsule clearance on the original mesh.
// A positive lookahead also permits a farther rejoin inside that window.
std::optional<WalkingBypass> checked_walking_bypass(const PathSnapshot&,RouteLocation,
    Vec3 supported_start,Vec3 contact_normal,double capsule_radius,double walkable_floor_z,const GroundProbe&,
    double rejoin_lookahead_metres=0.);
struct WalkingDetourEntry { RouteLocation location; Vec3 feet; };
// Choose a supported entry before contact on the already swept prefix.
// Keep any earlier detour rejoin in that prefix.
std::optional<WalkingDetourEntry> walking_detour_entry(const PathSnapshot&,RouteLocation,
    double backtrack_world,std::size_t preserve_through,const GroundProbe&);
// Search several connected lanes beside the native route when one pivot cannot
// rejoin it. The caller funds the probes and rechecks the complete splice.
std::optional<WalkingBypass> checked_walking_corridor(const PathSnapshot&,RouteLocation,
    Vec3 supported_start,Vec3 contact_normal,double capsule_radius,double walkable_floor_z,const GroundProbe&,
    double rejoin_lookahead_metres,double rejoin_after_metres=0.);
struct NavigationFrame {
    std::string world, player, target, target_label;
    Vec3 position; // Native nav-agent position, in the path's world coordinates.
    PathSnapshot path;
    double sample_seconds{}, path_sample_seconds{};
    // field_eligible includes native control, menu/dialogue and travel-mode guards.
    // walking_path positively excludes unsupported links or special traversal.
    // manual_input reports physical input only; exclude the mod's command keys.
    bool field_eligible{}, foreground{true}, manual_input{}, interaction_available{}, walking_path{};
    // Current loaded-area availability. Camera visibility never gates tracking.
    // An inactive, unloaded or otherwise revoked target cannot reuse its old path.
    bool target_available{true};
    std::string route_unavailable_reason; // Specific native query failure, when captured.
    std::string target_unavailable_reason; // A read error is distinct from a vanished target.
    std::uint64_t path_id{}; // Changes for each native query, including identical-point replans.
    std::optional<GroundCoverage> ground; // Verified prefix of this same full path.
    double ground_stop_margin_metres{1.5};
    std::string ground_unavailable_reason;
    double arrival_metres{.5}; // An owned activation-volume approach may require a smaller stop radius.
    bool activation_reached{}; // Current standing capsule overlaps the verified target volume.
    std::string arrival_message; // A local leg may end at a doorway the player crosses.
    bool beginning_movement{}; // The native walking input is starting.
    std::size_t bypass_rejoin{}; // Last waypoint of the capsule-checked detour, or zero.
    bool check_route_turns{}; // Require a live swept proof before following each outgoing world-map leg.
    bool hold_floor_turn{}; // Keep approaching the pivot when its outgoing chord could not be checked.
    bool use_beacon{};
};
struct RouteBeaconCue {
    double azimuth_degrees{}; // Zero is forward, positive is to the right of the native walking axes.
    double remaining_metres{};
};
unsigned route_beacon_interval_ms(double remaining_metres);
struct NavigationUpdate {
    NavigationMode mode{NavigationMode::Off};
    std::optional<Vec3> movement; // Unit XY world direction, automatic mode only.
    std::vector<SpeechEvent> speech;
    bool stopped_auto{}; // Release mod input; never consume the player's input.
    std::optional<Vec3> movement_waypoint;
    std::optional<double> movement_ground_metres; // Distance to the end of a checked ground prefix.
    NavigationReplan replan{NavigationReplan::None};
    std::size_t floor_turn_waypoint{}; // Recheck the route from the live feet to this waypoint before steering.
    std::optional<RouteBeaconCue> beacon;
    bool reached_target{};
    bool stopped_for_projection{};
};

// Passive stop/arrival notices follow any native caption already queued in this
// frame. Explicit commands and instructions while still running keep priority.
std::vector<SpeechEvent> navigation_speech_for_output(const NavigationUpdate&,bool passive);

// A deliberate start binds the current world/player/target. observe never starts
// or retargets on its own. Native complete paths must refresh at least every .6s.
// A fresh path must begin within 2m of the nav agent; cached path following must
// remain within 2m of its remaining corridor. A deviation requests a route from
// the current position. A failed query holds an existing route request.
// Surface-following proof uses the native route's XY corridor/progress because
// terrain can lie below its chord; start, corner and actual-end heights remain checked.
// Corners advance within .15m or after passing their plane inside a .3m corridor.
// Stop at native interaction, verified live activation overlap, or the route's
// arrival distance along its remaining legs. Automatic mode requests one fresh
// route after 1.5s of blocked progress and stops if that attempt also stalls.
// Manual guidance has no stationary timeout.
// Automatic walking also requires walking_path and rejects vertical legs; manual
// guidance accepts any valid, complete, fresh native path, including elevation.
// The beacon follows the next valid leg and uses the full remaining route length.
// For spoken guidance, a changed next route point (a real turn) is spoken at once and
// interrupts, as does a fresh route after leaving the spoken corridor by 2m.
// A clear control-relative change (>=30 degrees and another octant, at
// least 1m before the next point), a changed later point or an endpoint moved over
// 1m is spoken quietly at most every 1.5s. Distance-only changes, replans that keep
// the same points within .3m, and lateral sway are never re-announced.
class NavigationFollower {
public:
    NavigationUpdate start(NavigationMode,const NavigationFrame&);
    NavigationUpdate toggle(NavigationMode,const NavigationFrame&);
    NavigationUpdate stop(std::string reason="Navigation stopped.");
    void pause();
    NavigationUpdate observe(const NavigationFrame&,bool explicit_status=false);
    NavigationMode mode() const { return mode_; }
    const std::string& target() const { return target_; }
    std::optional<std::pair<Vec3,Vec3>> leg() const;
    // Keep the untraversed detour only for the path the follower is using.
    // The caller must check its ground again from the new position.
    std::optional<WalkingBypass> remaining_bypass(const PathSnapshot&,std::uint64_t path_id,
        Vec3 position,std::size_t rejoin,bool advance=true,const GroundCoverage* ground=nullptr) const;
private:
    struct RoutePoint { Vec3 point; bool endpoint{}; };
    NavigationUpdate arrive(std::string reason);
    NavigationUpdate wait_for_route(const std::string& reason);
    std::size_t advance_waypoint(Vec3 position,double scale,const GroundCoverage* ground) const;
    NavigationMode mode_{NavigationMode::Off};
    std::string world_,player_,target_;
    bool announced_{},waiting_for_route_{},retried_blockage_{},beacon_audio_{};
    Vec3 announced_origin_{};
    std::vector<RoutePoint> announced_points_;
    std::optional<double> announced_angle_;
    double announced_sample_{};
    std::vector<Vec3> points_;
    std::size_t waypoint_{1};
    double path_sample_{-1},last_sample_{-1},progress_sample_{};
    std::uint64_t path_id_{};
    Vec3 progress_position_;
};
}
