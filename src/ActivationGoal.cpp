#include "resonance/ActivationGoal.hpp"
#include <algorithm>
#include <cmath>
namespace resonance {
namespace {
// Arrival bounds (metres). Below the minimum, stopping precision cannot promise contact.
constexpr double minimum_arrival=.02,maximum_arrival=.5;
constexpr double radians_per_degree=3.14159265358979323846/180;
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool valid(const ActivationBox& b) {
    return finite(b.center)&&finite(b.half_extent)&&std::isfinite(b.yaw_degrees)&&
        b.half_extent.x>0&&b.half_extent.y>0&&b.half_extent.z>0;
}
bool valid(WalkingCapsule c){return std::isfinite(c.radius)&&std::isfinite(c.half_height)&&c.radius>0&&c.half_height>=c.radius;}
// Cosine and sine of the yaw reduced to [-180,180]; quarter turns are exact.
struct Turn { double c{1},s{}; };
Turn turn(double degrees) {
    const double r=std::remainder(degrees,360.);
    if(r==0)return {1,0};
    if(r==90)return {0,1};
    if(r==-90)return {0,-1};
    if(r==180||r==-180)return {-1,0};
    return {std::cos(r*radians_per_degree),std::sin(r*radians_per_degree)};
}
// A world point relative to the box centre, in the box's local (unyawed) axes.
Vec3 local(const ActivationBox& b,Turn t,Vec3 p) {
    const double dx=p.x-b.center.x,dy=p.y-b.center.y;
    return {dx*t.c+dy*t.s,dy*t.c-dx*t.s,p.z-b.center.z};
}
}
std::optional<Vec3> closest_activation_point(const ActivationBox& box,Vec3 feet) {
    if(!valid(box)||!finite(feet))return std::nullopt;
    const Turn t=turn(box.yaw_degrees);
    const Vec3 l=local(box,t,feet),h=box.half_extent;
    if(!finite(l))return std::nullopt;
    const Vec3 c{std::clamp(l.x,-h.x,h.x),std::clamp(l.y,-h.y,h.y),std::clamp(l.z,-h.z,h.z)};
    Vec3 result=feet; // Components the box does not clamp stay the feet, bit for bit.
    if(c.x!=l.x||c.y!=l.y)result={box.center.x+c.x*t.c-c.y*t.s,box.center.y+c.x*t.s+c.y*t.c,result.z};
    if(c.z!=l.z)result.z=box.center.z+c.z;
    if(!finite(result))return std::nullopt;
    return result;
}
std::optional<double> activation_clearance(const ActivationBox& box,WalkingCapsule capsule,Vec3 feet) {
    if(!valid(box)||!valid(capsule)||!finite(feet))return std::nullopt;
    // The capsule is every point within radius of its vertical core segment.
    const Vec3 l=local(box,turn(box.yaw_degrees),{feet.x,feet.y,feet.z+capsule.half_height}),h=box.half_extent;
    if(!finite(l))return std::nullopt;
    const double dx=std::max(0.,std::abs(l.x)-h.x),dy=std::max(0.,std::abs(l.y)-h.y);
    const double dz=std::max(0.,std::abs(l.z)-h.z-(capsule.half_height-capsule.radius));
    const double clearance=capsule.radius-std::hypot(dx,dy,dz);
    if(!std::isfinite(clearance))return std::nullopt;
    return clearance;
}
bool walking_segment_crosses_activation_box(const ActivationBox& box,WalkingCapsule capsule,Vec3 from,Vec3 to) {
    if(!valid(box)||!valid(capsule)||!finite(from)||!finite(to))return false;
    const Turn yaw=turn(box.yaw_degrees);
    from.z+=capsule.half_height;
    to.z+=capsule.half_height;
    const Vec3 start=local(box,yaw,from),end=local(box,yaw,to);
    if(!finite(start)||!finite(end))return false;
    double first=0.,last=1.;
    const auto clip=[&](double a,double b,double extent) {
        const double step=b-a;
        if(!std::isfinite(step)||!std::isfinite(extent))return false;
        if(step==0.)return std::abs(a)<=extent;
        const double entry=(-extent-a)/step,exit=(extent-a)/step;
        first=std::max(first,std::min(entry,exit));
        last=std::min(last,std::max(entry,exit));
        return first<=last;
    };
    return clip(start.x,end.x,box.half_extent.x+capsule.radius)&&
        clip(start.y,end.y,box.half_extent.y+capsule.radius)&&
        clip(start.z,end.z,box.half_extent.z+capsule.half_height);
}
bool reaches_activation_before_wall(const ActivationBox& box,WalkingCapsule capsule,Vec3 feet,
        Vec3 stopped_feet,double world_to_metres) {
    const double margin=minimum_arrival*world_to_metres;
    const double dx=stopped_feet.x-feet.x,dy=stopped_feet.y-feet.y,distance=std::hypot(dx,dy);
    const auto initial=activation_clearance(box,capsule,feet);
    if(!initial||*initial>0.||!std::isfinite(distance)||!std::isfinite(margin)||margin<=0.||distance<=margin)
        return false;
    const auto clearance_at=[&](double along) {
        const double fraction=along/distance;
        return activation_clearance(box,capsule,{feet.x+dx*fraction,feet.y+dy*fraction,feet.z}).value_or(-1.);
    };
    double first=0.,last=distance-margin;
    if(clearance_at(last)>margin)return true;
    // Capsule clearance on a straight, level segment is concave. Check for a
    // transition crossed before the stop point and exited again by the end.
    for(int i=0;i<14&&last-first>margin;++i) {
        const double step=(last-first)/3.;
        const double a=clearance_at(first+step),b=clearance_at(last-step);
        if(a>margin||b>margin)return true;
        if(a<b)first+=step;
        else last-=step;
    }
    return clearance_at(first+(last-first)/2.)>margin;
}
std::optional<ActivationApproach> activation_approach(const ActivationBox& box,WalkingCapsule capsule,Vec3 projected_feet,double world_to_metres) {
    if(!std::isfinite(world_to_metres)||world_to_metres<=0)return std::nullopt;
    const auto clearance=activation_clearance(box,capsule,projected_feet);
    if(!clearance||*clearance<=0)return std::nullopt;
    // Distance to the box changes no faster than the capsule moves, so stopping within
    // half the clearance keeps at least the other half of the overlap.
    const double arrival=std::min(maximum_arrival,*clearance/(2*world_to_metres));
    if(arrival<minimum_arrival)return std::nullopt;
    return ActivationApproach{projected_feet,arrival};
}
std::vector<Vec3> box_corner_approaches(const ActivationBox& box,WalkingCapsule capsule,double feet_z) {
    if(!valid(box)||!valid(capsule)||!std::isfinite(feet_z))return {};
    const auto [c,s]=turn(box.yaw_degrees);
    std::vector<Vec3> points;
    for(const double depth:{.55,.8}) {
        const double offset=depth*capsule.radius/std::sqrt(2.);
        for(const double x_sign:{-1.,1.})for(const double y_sign:{-1.,1.}) {
            const double x=x_sign*(box.half_extent.x+offset);
            const double y=y_sign*(box.half_extent.y+offset);
            const Vec3 point{box.center.x+x*c-y*s,box.center.y+x*s+y*c,feet_z};
            if(finite(point))points.push_back(point);
        }
    }
    return points;
}
std::vector<Vec3> box_face_approaches(const ActivationBox& box,double feet_z) {
    if(!valid(box)||!std::isfinite(feet_z))return {};
    const auto [c,s]=turn(box.yaw_degrees);
    const auto z=std::clamp(feet_z,box.center.z-box.half_extent.z,box.center.z+box.half_extent.z);
    const auto point=[&](double x,double y) {
        return Vec3{box.center.x+x*c-y*s,box.center.y+x*s+y*c,z};
    };
    std::vector<Vec3> points{point(box.half_extent.x,0),point(-box.half_extent.x,0),
        point(0,box.half_extent.y),point(0,-box.half_extent.y)};
    std::erase_if(points,[](Vec3 candidate){return !finite(candidate);});
    return points;
}
std::optional<ActivationApproach> projected_activation_approach(const ActivationBox& box,WalkingCapsule capsule,Vec3 feet,
        double world_to_metres,const ActivationProjection& project,ActivationAnchor anchor) {
    if(!project||!valid(capsule)||!finite(feet)||!std::isfinite(world_to_metres)||world_to_metres<=0)return std::nullopt;
    Vec3 from=feet;
    if(anchor==ActivationAnchor::Centerline)from={box.center.x,box.center.y,feet.z};
    if(anchor==ActivationAnchor::CapsuleHeight)from.z+=capsule.half_height;
    if(anchor==ActivationAnchor::BoxCenter)from=box.center;
    auto closest=closest_activation_point(box,from);
    if(!closest)return std::nullopt;
    if(anchor==ActivationAnchor::CapsuleHeight)closest->z-=capsule.half_height;
    if(!finite(*closest))return std::nullopt;
    const auto projected=project(*closest);
    if(!projected)return std::nullopt;
    return activation_approach(box,capsule,*projected,world_to_metres);
}
}
