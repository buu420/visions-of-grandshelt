#pragma once
#include "resonance/Story.hpp"
#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace resonance {
enum class FieldCueKind { Npc, Interactable, Wall };

struct FieldProximityTarget {
    std::string id;
    Vec3 position;
    FieldCueKind kind;
    bool audible;
};

struct FieldProximityCue {
    FieldCueKind kind;
    double azimuth_degrees;
    double distance_metres;
    std::string id;
};

inline constexpr double field_cue_radius_metres=4.;
inline constexpr double field_cue_height_metres=1.85;
inline constexpr double field_wall_radius_metres=2.4;
inline constexpr std::size_t field_emitters_per_kind=16;
using FieldEntityCues=std::array<std::vector<FieldProximityCue>,2>;
using FieldWallCues=std::array<std::optional<FieldProximityCue>,4>;

struct FieldAudioFrame {
    FieldEntityCues entities;
    FieldWallCues walls;
};

FieldEntityCues field_entity_cues(std::span<const FieldProximityTarget>,const StorySpatialContext&);
std::optional<FieldProximityCue> field_wall_cue(Vec3 impact,Vec3 normal,Vec3 probe,
    double distance_metres,double walkable_floor_z,const StorySpatialContext&,std::string_view direction);
// A wall probe is as wide as the player's capsule, so beside an opening it clips the
// jamb at the edge of its own width. These helpers decide whether a hit was such a
// clip: side-step by up to one radius and sweep again; a usable aperture lets the
// shifted probe travel at least one full capsule farther than the original hit.
// Signed lateral offset of an impact from the probe axis, in world units, positive
// on the probe direction's right-hand side (side axis = {direction.y, -direction.x}).
double wall_probe_lateral(Vec3 origin,Vec3 impact,Vec3 direction);
// Side-steps worth testing, same sign convention, nearest to the likely opening first.
// Empty when the radius or offset is unusable.
std::vector<double> wall_aperture_shifts(double lateral,double radius);
// shifted_hit_depth is the side-stepped probe's own hit distance along the axis;
// nullopt means it reached its full length unobstructed.
bool wall_aperture_open(double hit_depth,std::optional<double> shifted_hit_depth,double radius);
std::chrono::milliseconds field_pulse_interval(FieldCueKind kind,double distance_metres);
double field_pulse_gain(FieldCueKind kind,double distance_metres);
double wall_tone_gain(double distance_metres);
}
