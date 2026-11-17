#pragma once
#include "resonance/Core.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
namespace resonance {
// Why ground verification of a native route prefix stopped. Horizon and Budget are
// per-update lookahead limits only: they shorten coverage, never reject a journey.
enum class GroundLimit : std::uint8_t { None, Horizon, Budget, Obstruction, VerticalLink, Surface, Projection, Unreadable, Mesh };
// A point on a PathSnapshot: segment is the endpoint index 1..n-1 of
// points[segment-1] -> points[segment]; fraction is 0..1 along that segment.
struct RouteLocation { std::size_t segment{}; double fraction{}; };
struct SweptGroundPrefix {
    RouteLocation end;
    double max_anchor_offset_metres{};
};
// The verified prefix of one native route, from points[0] to end (inclusive).
// end_point is the route point at end; metres equals route_arc_metres(path,end);
// reaches_end is true only when the real final route point itself was verified.
// follow_surface records that the budget's surface-following mode verified it.
struct GroundCoverage {
    std::uint64_t path_id{};
    RouteLocation end;
    Vec3 end_point;
    double metres{};
    bool reaches_end{};
    GroundLimit limit{};
    std::size_t native_calls{};
    bool follow_surface{};
    // The coarse pass used slope relaxation. Automatic walking requires a
    // subsequent fine surface proof; coarse samples can hide terrace risers.
    bool requires_surface{};
    // Retain the extent of live capsule/floor proof separately. A longer
    // mesh-only fallback cannot authorize treating a baked height as floor.
    std::optional<SweptGroundPrefix> swept_ground;
};
// Per-update work bounds supplied by the caller (sized from speed/braking, never
// from total journey length). Distances are metres along the route's 3D arc.
// walkable_slope_degrees is the verified native walkable limit, read live: the smaller
// of the character's WalkableFloorAngle and the selected Recast mesh's AgentMaxSlope,
// in [0,90). nullopt keeps the flat rule: sampled ground height changes at most 0.025 m
// between samples. With a limit, the route's grade between every pair of samples must
// be within it (a near-vertical route step of at most 0.025 m excepted), and a larger
// height change is also ordinary ground when the surface rise matches the route's own
// rise within 0.025 m.
// follow_surface (from surface_following_budget; requires a walkable limit) verifies
// the ground under the route's horizontal trace instead of the native chord between
// vertices: each sample is projected at the route's XY from the last verified surface
// height; every successive surface pair must be within the walkable grade (a native
// jog under 0.01 m XY keeps the 0.025 m height tolerance) and joined by a clear ray;
// the start, every genuine corner, a vertical link's start and the real end must lie
// within 0.025 m of their native heights. The horizon edge is not a height anchor.
struct GroundBudget { double horizon_metres{12},spacing_metres{.2}; std::size_t max_native_calls{160}; std::optional<double> walkable_slope_degrees; bool follow_surface{}; };
// The surface-following form of a budget with a valid walkable limit: spacing becomes
// min(spacing,0.02,0.0225/tan(limit)) m (0.02 for a zero limit), so a step over 0.025 m
// between samples exceeds the limit. Calls are unchanged: fund about two per sample.
// nullopt without a valid limit or for an otherwise invalid budget.
std::optional<GroundBudget> surface_following_budget(GroundBudget);
// Native adapters only. blocked: true = obstructed, false = clear, nullopt =
// unreadable. project: surface point, nullopt = no projection.
struct GroundProbe {
    std::function<std::optional<bool>(Vec3,Vec3)> blocked;
    std::function<std::optional<Vec3>(Vec3)> project;
    // project finds floor for the actual capsule on the native route's mesh.
    // blocked sweeps that same capsule between the supported feet. This permits
    // at most 0.2 m samples and uses the live maximum step height (metres) to
    // match the physical floor to baked height anchors.
    std::optional<double> swept_floor_tolerance;
    // Exact walkways with native steps can accept the full live step height.
    // Support and the full capsule sweep still apply on each side.
    std::function<bool(Vec3,Vec3)> native_step;
};
// Verifies the route prefix from points[0], in route order, up to the horizon, the
// budget, the real end or the first failed check (coverage then ends at the last
// successful sample with that limit). Samples are spaced along the arc across
// exactly collinear vertices; every genuine corner is a sample and a ray boundary.
// Rays never span a corner and no probe lies beyond the checked extent. Invalid input
// (not a valid complete path, <2 or >4096 points, non-finite data, bad budget or
// missing probe) returns nullopt; a bad budget has a non-positive or non-finite horizon,
// spacing not above 1e-9 m, no calls or a walkable slope that is non-finite or outside
// [0,90), or a surface-following budget without a limit or with a coarser spacing than
// surface_following_budget derives. Valid input may return zero verified metres.
std::optional<GroundCoverage> plan_ground_coverage(const PathSnapshot&,std::uint64_t path_id,const GroundBudget&,const GroundProbe&);
// 3D arc length in metres from points[0] to a location; nullopt for invalid input.
std::optional<double> route_arc_metres(const PathSnapshot&,RouteLocation);
// XY arc length for a surface-following walking route.
std::optional<double> horizontal_arc_metres(const PathSnapshot&,RouteLocation);
// The player's location on segment next_waypoint (points[next_waypoint-1] ->
// points[next_waypoint]), clamped to that segment; nullopt for invalid input.
// horizontal measures it along the segment's XY trace (3D when it has no XY extent).
std::optional<RouteLocation> route_progress(const PathSnapshot&,Vec3 position,std::size_t next_waypoint,bool horizontal=false);
}
