#pragma once
#include "resonance/Core.hpp"
#include <functional>
#include <optional>
namespace resonance {
// An owned native activation (trigger) box in world units: its centre, scaled
// half-extents (every component finite and above zero) and its world yaw. Only yaw is
// modelled; the caller rejects pitched or rolled native boxes before capture.
struct ActivationBox { Vec3 center, half_extent; double yaw_degrees{}; };
// The walking character's live capsule in world units: radius > 0 and
// half_height >= radius. Its centre is the feet (nav agent position) plus half_height.
struct WalkingCapsule { double radius{}, half_height{}; };
// The point of the box nearest to the feet: the feet in the box's yaw frame, each local
// axis clamped to the half-extents, turned back to world. Feet already inside the box
// are returned unmodified, and so is every unclamped world component. nullopt for an
// invalid box or non-finite feet; never the box centre or any other stand-in.
std::optional<Vec3> closest_activation_point(const ActivationBox&,Vec3 feet);
// How far the capsule standing on these feet reaches into the box, in world units:
// radius minus the exact distance from the capsule's vertical core segment (half
// length half_height-radius) to the box. Positive overlaps; zero only touches; negative
// is clear of it. Inside the box the result is the radius. nullopt for invalid input.
std::optional<double> activation_clearance(const ActivationBox&,WalkingCapsule,Vec3 feet);
// Checks the swept capsule's conservative envelope against a yawed trigger.
// This includes contact at either end of the proposed walking segment.
bool walking_segment_crosses_activation_box(const ActivationBox&,WalkingCapsule,Vec3 from,Vec3 to);
// True when a horizontal walking capsule enters the box before reaching a blocker.
// The caller supplies a verified, active automatic transition and the first blocking position.
bool reaches_activation_before_wall(const ActivationBox&,WalkingCapsule,Vec3 feet,Vec3 stopped_feet,double world_to_metres);
// A navigation endpoint whose capsule overlaps the box. point is the supplied projected
// feet, unmodified. arrival_metres = min(0.5, clearance/(2*world_to_metres)): stopping
// within that 3D distance of point still overlaps the box by at least half the clearance.
struct ActivationApproach { Vec3 point; double arrival_metres{}; };
// The approach at already projected feet. nullopt unless world_to_metres is finite and
// positive, all input is valid, the clearance is strictly positive and arrival_metres is
// at least 0.02 (an overlap margin below 4 cm cannot be promised by stopping precision).
std::optional<ActivationApproach> activation_approach(const ActivationBox&,WalkingCapsule,Vec3 projected_feet,double world_to_metres);
// Candidate feet outside the box corners. A corner can overlap the trigger
// with more room around its owning actor than a point in front of a face.
// The caller still needs to prove the walking surface and capsule clearance.
std::vector<Vec3> box_corner_approaches(const ActivationBox&,WalkingCapsule,double feet_z);
// Candidate feet at the four vertical face centers, at the player's clamped
// height. The caller must project them and verify capsule overlap.
std::vector<Vec3> box_face_approaches(const ActivationBox&,double feet_z);
// Native adapter supplied by the caller: the explicit navmesh projection of one point
// with the default (zero) query extent; nullopt when there is no projection.
using ActivationProjection = std::function<std::optional<Vec3>(Vec3)>;
enum class ActivationAnchor { Nearest, Centerline, CapsuleHeight, BoxCenter };
// Project one anchor and validate overlap. Centerline keeps the player's height,
// clamped to the box, and uses the box's horizontal center. CapsuleHeight clamps
// the capsule center to the box and requests projection for the feet below it.
std::optional<ActivationApproach> projected_activation_approach(const ActivationBox&,WalkingCapsule,Vec3 feet,
    double world_to_metres,const ActivationProjection& project,ActivationAnchor anchor=ActivationAnchor::Nearest);
}
