#include "resonance/FieldProximity.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace resonance {
namespace {
struct Bearing {
    Vec3 origin,right,forward;
    double azimuth(Vec3 point) const {
        const double x=point.x-origin.x,y=point.y-origin.y;
        return std::atan2(x*right.x+y*right.y,x*forward.x+y*forward.y)*180./std::numbers::pi;
    }
};

std::optional<Bearing> bearing_from(const StorySpatialContext& listener) {
    const auto r=std::hypot(listener.right.x,listener.right.y),u=std::hypot(listener.up.x,listener.up.y);
    if(!std::isfinite(r)||!std::isfinite(u)||r<.01||u<.01||
       !std::isfinite(listener.world_to_metres)||listener.world_to_metres<=0)return {};
    const Vec3 right{listener.right.x/r,listener.right.y/r,0},forward{listener.up.x/u,listener.up.y/u,0};
    const double alignment=right.x*forward.x+right.y*forward.y;
    if(!std::isfinite(alignment)||std::abs(alignment)>.1)return {};
    return Bearing{listener.origin,right,forward};
}

double distance_fraction(FieldCueKind kind,double distance) {
    if(!std::isfinite(distance))return 1.;
    const double radius=kind==FieldCueKind::Wall?field_wall_radius_metres:field_cue_radius_metres;
    return std::clamp(distance/radius,0.,1.);
}
}

FieldEntityCues field_entity_cues(std::span<const FieldProximityTarget> targets,
        const StorySpatialContext& listener) {
    FieldEntityCues cues;
    const auto bearing=bearing_from(listener);
    if(!bearing)return cues;
    for(const auto& target:targets) {
        if(target.id.empty()||!target.audible||target.kind==FieldCueKind::Wall)continue;
        const double scale=listener.world_to_metres;
        const double distance=std::hypot(target.position.x-listener.origin.x,target.position.y-listener.origin.y)/scale;
        const double height=std::abs(target.position.z-listener.origin.z)/scale;
        if(!std::isfinite(distance)||!std::isfinite(height)||distance>field_cue_radius_metres||height>field_cue_height_metres)continue;
        const auto type=target.kind==FieldCueKind::Npc?0u:1u;
        const auto angle=distance<.1?0.:bearing->azimuth(target.position);
        cues[type].push_back({target.kind,angle,distance,target.id});
    }
    for(auto& group:cues) {
        const auto count=std::min(group.size(),field_emitters_per_kind);
        std::partial_sort(group.begin(),group.begin()+count,group.end(),[](const auto& a,const auto& b) {
            return a.distance_metres==b.distance_metres?a.id<b.id:a.distance_metres<b.distance_metres;
        });
        group.resize(count);
    }
    return cues;
}

std::optional<FieldProximityCue> field_wall_cue(Vec3 impact,Vec3 normal,Vec3 probe,
        double distance,double floor_z,const StorySpatialContext& listener,std::string_view direction) {
    const auto bearing=bearing_from(listener);
    const auto length=std::hypot(normal.x,normal.y,normal.z);
    if(!bearing||!std::isfinite(distance)||distance<0||distance>field_wall_radius_metres||
       !std::isfinite(length)||length<.9||length>1.1||!std::isfinite(floor_z)||floor_z<=0||floor_z>1||
       normal.z/length>=floor_z||normal.z/length<-.65||
       normal.x*probe.x+normal.y*probe.y>-.38*length)return {};
    if(!std::isfinite(impact.x)||!std::isfinite(impact.y))return {};
    const double x=impact.x-listener.origin.x,y=impact.y-listener.origin.y;
    // A starting contact can put the impact at the capsule centre.
    if(std::hypot(x,y)<.1*listener.world_to_metres||x*probe.x+y*probe.y<=0.)
        impact={listener.origin.x-normal.x,listener.origin.y-normal.y,listener.origin.z};
    const auto azimuth=bearing->azimuth(impact);
    if(!std::isfinite(azimuth))return {};
    return FieldProximityCue{FieldCueKind::Wall,azimuth,distance,std::string(direction)};
}

double wall_probe_lateral(Vec3 origin,Vec3 impact,Vec3 direction) {
    const double length=std::hypot(direction.x,direction.y);
    if(!std::isfinite(length)||length<.01||!std::isfinite(origin.x)||!std::isfinite(origin.y)||
       !std::isfinite(impact.x)||!std::isfinite(impact.y))return 0.;
    const double side_x=direction.y/length,side_y=-direction.x/length;
    return (impact.x-origin.x)*side_x+(impact.y-origin.y)*side_y;
}

std::vector<double> wall_aperture_shifts(double lateral,double radius) {
    if(!std::isfinite(lateral)||!std::isfinite(radius)||radius<=0.)return {};
    // An impact near the axis could be either jamb; test both sides. An impact
    // clearly to one side means the opening, if any, is on the other side.
    if(std::abs(lateral)<radius*.25)return lateral<=0.?std::vector<double>{radius,-radius}:std::vector<double>{-radius,radius};
    return {lateral>0.?-radius:radius};
}

bool wall_aperture_open(double hit_depth,std::optional<double> shifted_hit_depth,double radius) {
    if(!std::isfinite(hit_depth)||hit_depth<0.||!std::isfinite(radius)||radius<=0.)return false;
    if(!shifted_hit_depth)return true;
    if(!std::isfinite(*shifted_hit_depth)||*shifted_hit_depth<0.)return false;
    return *shifted_hit_depth>=hit_depth+2.*radius;
}

std::chrono::milliseconds field_pulse_interval(FieldCueKind kind,double distance) {
    const double range=distance_fraction(kind,distance);
    switch(kind) {
    case FieldCueKind::Npc:return std::chrono::milliseconds(620+std::lround(580*range));
    case FieldCueKind::Interactable:return std::chrono::milliseconds(1080+std::lround(620*range));
    case FieldCueKind::Wall:break;
    }
    return std::chrono::seconds(2);
}

double field_pulse_gain(FieldCueKind kind,double distance) {
    return .88-.65*distance_fraction(kind,distance);
}

double wall_tone_gain(double distance) {
    return .68*(1.-distance_fraction(FieldCueKind::Wall,distance));
}
}
