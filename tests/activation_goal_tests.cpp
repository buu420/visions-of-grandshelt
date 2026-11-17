#include "resonance/ActivationGoal.hpp"
#include "../src/CrystalTowerSpiral.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace resonance;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
constexpr double scale=100; // Live field WorldToMeters.
constexpr double pi=3.14159265358979323846;
constexpr double not_a_number=std::numeric_limits<double>::quiet_NaN(),infinite=std::numeric_limits<double>::infinity();
// The observed Story/shared exit trigger (owned box component, scaled world extents).
constexpr ActivationBox exit_box{{11500,51200,5702.43},{800,200,200},0};
constexpr WalkingCapsule capsule{56,100}; // Live player capsule from the native audit.
constexpr Vec3 feet{7936.77246,50000,5302.15};      // Observed nav agent position.
constexpr Vec3 closest{10700,51000,5502.43};        // Nearest point of the box to those feet.
constexpr Vec3 projected{10700,50970,5502.43};      // A walkable projection 30 units off the Y face.
bool near(double a,double b,double tolerance=1e-9){return std::abs(a-b)<=tolerance;}
bool same(Vec3 a,Vec3 b,double tolerance=1e-9){return near(a.x,b.x,tolerance)&&near(a.y,b.y,tolerance)&&near(a.z,b.z,tolerance);}
double& component(Vec3& v,int axis){return axis==0?v.x:axis==1?v.y:v.z;}
// Test-side oracle: p turned by Unreal yaw about c (local X -> (cos,sin), local Y -> (-sin,cos)).
Vec3 turned(Vec3 p,Vec3 c,double degrees) {
    const double a=degrees*pi/180,dx=p.x-c.x,dy=p.y-c.y;
    return {c.x+dx*std::cos(a)-dy*std::sin(a),c.y+dx*std::sin(a)+dy*std::cos(a),p.z};
}
ActivationBox yawed(double degrees){ActivationBox box=exit_box;box.yaw_degrees=degrees;return box;}

void observed_exit_closest_point_is_on_the_owned_box() {
    const auto p=closest_activation_point(exit_box,feet);
    require(p.has_value(),"the observed exit box and feet are valid");
    require(same(*p,closest),"the nearest box point to the observed feet is (10700,51000,5502.43)");
    require(!same(*p,exit_box.center,1),"the box centre is never substituted for the nearest point");
    const auto east=closest_activation_point(exit_box,{13000,51100,5800});
    require(east&&same(*east,{12300,51100,5800},0),"beyond the +X face only X moves onto the face");
    const auto above=closest_activation_point(exit_box,{11000,51300,6500});
    require(above&&above->x==11000&&above->y==51300&&near(above->z,5902.43),"above the box only Z moves onto the top face");
}
void feet_inside_the_box_are_returned_unmodified() {
    constexpr Vec3 inside{11234.5678,51123.4567,5600.1234};
    auto p=closest_activation_point(exit_box,inside);
    require(p&&same(*p,inside,0),"feet inside an axis-aligned box are returned bit for bit");
    p=closest_activation_point(exit_box,{12300,51100,5800});
    require(p&&same(*p,{12300,51100,5800},0),"feet exactly on a face are inside");
    for(double yaw:{37.5,90.,-123.25}) {
        const ActivationBox box=yawed(yaw);
        const Vec3 rotated=turned(inside,box.center,yaw);
        p=closest_activation_point(box,rotated);
        require(p&&same(*p,rotated,0),"feet inside a yawed box are returned bit for bit");
        const Vec3 under{rotated.x,rotated.y,5000};
        p=closest_activation_point(box,under);
        require(p&&p->x==under.x&&p->y==under.y&&near(p->z,5502.43),"under the footprint the feet X and Y are kept bit for bit");
    }
}
void observed_projection_overlaps_the_capsule() {
    const auto c=activation_clearance(exit_box,capsule,projected);
    require(c&&near(*c,26),"30 units off the Y face the radius-56 capsule overlaps by 26");
    const auto a=activation_approach(exit_box,capsule,projected,scale);
    require(a.has_value(),"the projected exit endpoint is a usable activation goal");
    require(same(a->point,projected,0),"the approach point is the projected feet, unmodified");
    require(near(a->arrival_metres,.13,1e-12)&&a->arrival_metres<.5,"arrival is half the 26 cm overlap");
    const auto now=activation_clearance(exit_box,capsule,feet);
    require(now&&*now<-2800,"the player's current feet are far clear of the box");
}
void outside_projection_is_rejected() {
    require(activation_approach(exit_box,capsule,projected,scale).has_value(),"control: the observed projection is accepted");
    constexpr Vec3 outside{10700,50900,5502.43};
    const auto c=activation_clearance(exit_box,capsule,outside);
    require(c&&near(*c,-44),"100 units off the Y face the capsule is 44 clear of the box");
    require(!activation_approach(exit_box,capsule,outside,scale),"a projection clear of the box is no activation goal");
}
void tangent_contact_is_rejected() {
    constexpr Vec3 tangent{10700,50944,5502.43};
    const auto c=activation_clearance(exit_box,capsule,tangent);
    require(c&&near(*c,0),"56 units off the Y face the capsule only touches the box");
    require(!activation_approach(exit_box,capsule,tangent,scale),"touching is not overlapping");
    require(activation_approach(exit_box,capsule,{10700,50970,5502.43},scale).has_value(),"control: a real overlap on the same line is accepted");
}
void other_floors_are_rejected() {
    require(activation_approach(exit_box,capsule,projected,scale).has_value(),"control: the exit floor is accepted");
    const auto under=activation_clearance(exit_box,capsule,{11500,51200,5302.15});
    require(under&&near(*under,-.28,1e-9),"on the current floor the capsule top stays 0.28 below the box");
    for(double z:{5302.15,5000.,6000.,7000.}) {
        const Vec3 floor{projected.x,projected.y,z};
        const auto c=activation_clearance(exit_box,capsule,floor);
        require(c&&*c<0,"a capsule on another floor does not reach the box");
        require(!activation_approach(exit_box,capsule,floor,scale),"a projection onto another floor is rejected");
    }
}
void rotated_boxes_match_the_axis_aligned_case() {
    const auto quarter=closest_activation_point(yawed(90),{12700,47636.77246,5302.15});
    require(quarter&&same(*quarter,{11700,50400,5502.43},1e-6),"a quarter-turned box turns the nearest point with it");
    require(!activation_approach(yawed(90),capsule,projected,scale),"a yawed box is not tested as if axis-aligned");
    for(double yaw:{90.,-90.,180.,37.5,757.5,-322.5}) {
        const ActivationBox box=yawed(yaw);
        const auto p=closest_activation_point(box,turned(feet,box.center,yaw));
        require(p&&same(*p,turned(closest,box.center,yaw),1e-6),"the nearest point turns with the box");
        const auto ok=activation_approach(box,capsule,turned(projected,box.center,yaw),scale);
        require(ok&&near(ok->arrival_metres,.13,1e-9),"the observed projection overlaps the yawed box identically");
        const auto out=activation_clearance(box,capsule,turned({10700,50900,5502.43},box.center,yaw));
        require(out&&near(*out,-44,1e-6),"clearance does not depend on yaw");
        require(!activation_approach(box,capsule,turned({10700,50900,5502.43},box.center,yaw),scale),"outside stays outside");
        require(!activation_approach(box,capsule,turned({10700,50944,5502.43},box.center,yaw),scale),"tangent stays rejected");
    }
}
void corners_use_the_euclidean_capsule_radius() {
    constexpr Vec3 diagonal{12340,50960,5502.43}; // 40 beyond both the +X and -Y faces.
    const auto c=activation_clearance(exit_box,capsule,diagonal);
    require(c&&near(*c,56-std::sqrt(3200.))&&*c<0,"a corner is measured by Euclidean distance, 56.57 here");
    require(!activation_approach(exit_box,capsule,diagonal,scale),"a radius-expanded box false positive at a corner is rejected");
    const auto nearer=activation_approach(exit_box,capsule,{12330,50970,5502.43},scale);
    require(nearer&&near(nearer->arrival_metres,(56-std::sqrt(1800.))/200),"a corner within the radius is accepted");
    // (650,-650) from the centre lies inside the world bounding box (+-707) of the 45-degree box.
    const ActivationBox diamond=yawed(45);
    const auto d=activation_clearance(diamond,capsule,{12150,50550,5502.43});
    require(d&&near(*d,56-(1300/std::sqrt(2.)-200),1e-9),"a 45-degree box is measured in its own frame");
    require(!activation_approach(diamond,capsule,{12150,50550,5502.43},scale),"the world bounding box of a yawed box is not the box");
}
void invalid_input_is_rejected() {
    require(closest_activation_point(exit_box,feet)&&activation_clearance(exit_box,capsule,projected)&&
        activation_approach(exit_box,capsule,projected,scale),"control: valid input succeeds");
    require(activation_approach(exit_box,{56,56},projected,scale).has_value(),"control: a spherical capsule is valid");
    std::vector<ActivationBox> boxes;
    for(double bad:{not_a_number,infinite,-infinite}) {
        for(int axis=0;axis<3;++axis) {
            ActivationBox box=exit_box;component(box.center,axis)=bad;boxes.push_back(box);
            box=exit_box;component(box.half_extent,axis)=bad;boxes.push_back(box);
        }
        boxes.push_back(yawed(bad));
    }
    for(double bad:{0.,-0.,-200.})for(int axis=0;axis<3;++axis){ActivationBox box=exit_box;component(box.half_extent,axis)=bad;boxes.push_back(box);}
    for(const auto& box:boxes) {
        require(!closest_activation_point(box,feet),"an invalid box has no nearest point");
        require(!activation_clearance(box,capsule,projected),"an invalid box has no clearance");
        require(!activation_approach(box,capsule,projected,scale),"an invalid box has no approach");
    }
    for(double bad:{not_a_number,infinite,-infinite})for(int axis=0;axis<3;++axis) {
        Vec3 point=projected;component(point,axis)=bad;
        require(!closest_activation_point(exit_box,point)&&!activation_clearance(exit_box,capsule,point)&&
            !activation_approach(exit_box,capsule,point,scale),"non-finite feet are rejected");
    }
    constexpr std::array<WalkingCapsule,10> capsules{{{0,100},{-0.,100},{-56,100},{not_a_number,100},{infinite,100},{56,not_a_number},{56,infinite},{56,55.9},{56,0},{56,-100}}};
    for(const auto& bad:capsules)
        require(!activation_clearance(exit_box,bad,projected)&&!activation_approach(exit_box,bad,projected,scale),"an invalid capsule is rejected");
    for(double bad:{0.,-0.,-100.,not_a_number,infinite,-infinite})
        require(!activation_approach(exit_box,capsule,projected,bad),"an invalid world scale is rejected");
    require(!activation_clearance({{1e308,0,0},{800,200,200},37.5},capsule,{-1e308,0,0}),"an overflowing distance is no clearance");
}
void overlap_margin_below_four_centimetres_is_rejected() {
    constexpr Vec3 shallow{10700,50947.9,5502.43}; // 52.1 off the Y face.
    const auto c=activation_clearance(exit_box,capsule,shallow);
    require(c&&near(*c,3.9,1e-9),"the capsule overlaps by 3.9 cm");
    require(!activation_approach(exit_box,capsule,shallow,scale),"an overlap below 4 cm cannot be promised");
    const auto enough=activation_approach(exit_box,capsule,{10700,50948.1,5502.43},scale);
    require(enough&&near(enough->arrival_metres,.0205),"a 4.1 cm overlap gives a 2.05 cm arrival");
    const auto coarse=activation_approach(exit_box,capsule,shallow,50);
    require(coarse&&near(coarse->arrival_metres,.039),"the margin is in metres at the live world scale");
}
void arrival_is_half_the_clearance_capped_at_half_a_metre() {
    constexpr Vec3 within{11500,51200,5502.43}; // Standing inside the box on its floor.
    const auto c=activation_clearance(exit_box,capsule,within);
    require(c&&near(*c,56),"inside the box the clearance is the radius");
    const auto a=activation_approach(exit_box,capsule,within,scale);
    require(a&&near(a->arrival_metres,.28,1e-12),"arrival is half the 56 cm clearance");
    const auto wide=activation_approach(exit_box,{150,150},within,scale);
    require(wide&&wide->arrival_metres==.5,"a 75 cm half-clearance is capped at half a metre");
    const auto coarse=activation_approach(exit_box,capsule,within,50);
    require(coarse&&coarse->arrival_metres==.5,"a 56 cm half-clearance at scale 50 is capped at half a metre");
}
void clearance_is_measured_from_the_capsule_segment() {
    const auto below=activation_clearance(exit_box,capsule,{11500,51200,5312.43});
    require(below&&near(*below,10),"from below, the capsule's top reaches 10 into the box");
    require(activation_approach(exit_box,capsule,{11500,51200,5312.43},scale).has_value(),"10 cm of vertical overlap is usable");
    const auto above=activation_clearance(exit_box,capsule,{11500,51200,5892.43});
    require(above&&near(*above,10),"from above, the capsule's feet stand 10 inside the box");
    const auto low=activation_clearance(exit_box,capsule,{10700,50970,5362.43});
    require(low&&near(*low,26),"a capsule centred 40 below the box still overlaps the side through its core segment");
}
void projected_approach_projects_the_closest_point_once() {
    std::vector<Vec3> calls;
    std::optional<Vec3> answer=projected;
    const ActivationProjection project=[&](Vec3 p){calls.push_back(p);return answer;};
    const auto a=projected_activation_approach(exit_box,capsule,feet,scale,project);
    require(calls.size()==1&&same(calls[0],closest),"the nearest box point is projected exactly once");
    require(a&&same(a->point,projected,0)&&near(a->arrival_metres,.13,1e-12),"the projected endpoint becomes the approach");
    for(const std::optional<Vec3>& unusable:std::array<std::optional<Vec3>,4>{{Vec3{10700,50900,5502.43},Vec3{10700,50970,5000},std::nullopt,Vec3{not_a_number,50970,5502.43}}}) {
        calls.clear();answer=unusable;
        require(!projected_activation_approach(exit_box,capsule,feet,scale,project),"an unusable projection is no goal");
        require(calls.size()==1,"an unusable projection is never retried");
    }
    calls.clear();answer=projected;
    ActivationBox flat=exit_box;flat.half_extent.z=0;
    require(!projected_activation_approach(flat,capsule,feet,scale,project)&&
        !projected_activation_approach(exit_box,{0,100},feet,scale,project)&&
        !projected_activation_approach(exit_box,capsule,{not_a_number,50000,5302.15},scale,project)&&
        !projected_activation_approach(exit_box,capsule,feet,0,project),"invalid input is rejected");
    require(calls.empty(),"invalid input never reaches the native projection");
    require(!projected_activation_approach(exit_box,capsule,feet,scale,{}),"a missing projection adapter is rejected");
}
void centerline_approach_stays_in_the_same_box() {
    constexpr ActivationBox event{{17500,1450,232.00015},{480,72,32},90};
    constexpr Vec3 spawn{16320,-336,202.150021};
    std::vector<Vec3> calls;
    const ActivationProjection project=[&](Vec3 point)->std::optional<Vec3> {
        calls.push_back(point);
        point.z=210;
        return point;
    };
    require(projected_activation_approach(event,capsule,spawn,scale,project).has_value()&&
        same(calls.back(),{17428,970,spawn.z}),"nearest is still the first approach to the event");
    const auto middle=projected_activation_approach(event,capsule,spawn,scale,project,ActivationAnchor::Centerline);
    require(calls.size()==2&&same(calls.back(),{17500,1450,spawn.z}),"a retry projects the box centerline at the player's height");
    require(middle&&same(middle->point,{17500,1450,210})&&middle->arrival_metres>.2,
        "the centerline projection supplies an overlapping approach to the same event");
}
void lower_castle_passage_uses_capsule_height() {
    constexpr ActivationBox passage{{-32600,0,53.191484},{400,400,200},0};
    constexpr Vec3 from{-28600,-16,800};
    constexpr Vec3 floor{-32200,-16,30};
    Vec3 requested;
    const ActivationProjection project=[&](Vec3 point)->std::optional<Vec3> {
        requested=point;
        return std::abs(point.z-floor.z)<=200?std::optional<Vec3>(floor):std::nullopt;
    };
    require(!projected_activation_approach(passage,capsule,from,scale,project)&&
        near(requested.z,253.191484),"the unadjusted upper edge is beyond the native default Z extent");
    const auto approach=projected_activation_approach(passage,capsule,from,scale,project,ActivationAnchor::CapsuleHeight);
    require(same(requested,{-32200,-16,153.191484})&&approach&&same(approach->point,floor),
        "the capsule center supplies a lower query anchor without changing native extent");
}
void world_map_gate_has_an_overlapping_face_approach() {
    constexpr ActivationBox gate{{12718.990,20701.803,165.1735},{60,130,100},0};
    constexpr WalkingCapsule field_capsule{18.666666,33.333332};
    constexpr Vec3 start{16741.954229,21500.184848,-40.611432};
    constexpr Vec3 old_projection{12778.990,20856.000,139.981945};
    constexpr Vec3 face_projection{12718.990,20831.803,139.981945};
    // Recorded nearest projection and exact cooked face sample for map 1000.
    const ActivationProjection project=[=](Vec3 p)->std::optional<Vec3> {
        if(near(p.x,old_projection.x)&&near(p.y,face_projection.y))return old_projection;
        if(near(p.x,face_projection.x)&&near(p.y,face_projection.y))return face_projection;
        return {};
    };
    for(const auto anchor:{ActivationAnchor::Nearest,ActivationAnchor::CapsuleHeight,ActivationAnchor::Centerline})
        require(!projected_activation_approach(gate,field_capsule,start,scale,project,anchor),
            "the original approaches cannot provide overlapping feet");
    const auto faces=box_face_approaches(gate,start.z);
    require(faces.size()==4&&same(faces[2],{12718.990,20831.803,65.1735}),
        "the face center keeps the player's height clamped to the box");
    const auto projected=project(faces[2]);
    const auto approach=projected?activation_approach(gate,field_capsule,*projected,scale):std::nullopt;
    require(approach&&same(approach->point,face_projection)&&near(approach->arrival_metres,.09333333),
        "the face projection supplies a verified overlap for the small field capsule");
    const ActivationBox turned_gate{gate.center,gate.half_extent,37.5};
    const auto turned_faces=box_face_approaches(turned_gate,start.z);
    require(turned_faces.size()==faces.size(),"the rotated gate has four face centers");
    for(std::size_t i=0;i<faces.size();++i)
        require(same(turned_faces[i],turned(faces[i],gate.center,37.5),1e-6),"the face center rotates with the native box");
    require(box_face_approaches(gate,not_a_number).empty(),"non-finite feet cannot generate queries");
}
void a_stacked_portal_projects_at_its_own_height() {
    constexpr ActivationBox portal{{10080,11530,-60},{200,200,200},0};
    constexpr Vec3 upper_floor{9159,12586,502};
    constexpr Vec3 lower_floor{10080,11526,-90};
    const ActivationProjection project=[=](Vec3 candidate)->std::optional<Vec3> {
        if(same(candidate,portal.center))return lower_floor;
        return Vec3{candidate.x,candidate.y,510};
    };
    for(const auto anchor:{ActivationAnchor::Nearest,ActivationAnchor::CapsuleHeight,ActivationAnchor::Centerline})
        require(!projected_activation_approach(portal,capsule,upper_floor,scale,project,anchor),
            "projection to the upper floor cannot overlap the lower portal");
    const auto approach=projected_activation_approach(portal,capsule,upper_floor,scale,project,ActivationAnchor::BoxCenter);
    require(approach&&same(approach->point,lower_floor),"the portal center selects the lower approach");
}
void shrine_attendant_has_clear_talk_corners() {
    constexpr ActivationBox inn{{-4925,-255,-200},{64,64,64},0};
    constexpr double floor=-297.85;
    const auto candidates=box_corner_approaches(inn,capsule,floor);
    require(candidates.size()==8,"each talk-box corner needs two approach depths");
    for(const auto point:candidates) {
        const auto approach=activation_approach(inn,capsule,point,scale);
        require(approach&&approach->arrival_metres>.05,"the corner should retain useful native talk overlap");
        require(std::hypot(point.x-inn.center.x,point.y-inn.center.y)>2*capsule.radius+5,
            "the live Shrine capsules must fit alongside each other at the candidate");
    }
    const ActivationBox rotated{inn.center,inn.half_extent,37.5};
    const auto rotated_points=box_corner_approaches(rotated,capsule,floor);
    require(rotated_points.size()==candidates.size()&&same(rotated_points.front(),turned(candidates.front(),inn.center,37.5),1e-6),
        "candidates rotate with the owned talk box");
    require(box_corner_approaches(inn,{0,100},floor).empty(),"invalid capsules cannot generate talk approach points");
}
void an_automatic_entrance_clears_the_wall_after_the_opening() {
    constexpr ActivationBox entry{{345.,2411.711,270.749},{100.,100.,100.},0.};
    constexpr Vec3 exterior{148.184,2410.042,200.593};
    require(!reaches_activation_before_wall(entry,capsule,exterior,{148.294,exterior.y,exterior.z},scale),
        "the closed inn door blocks movement before the entry trigger");
    require(reaches_activation_before_wall(entry,capsule,exterior,{195.229,exterior.y,exterior.z},scale),
        "the opened inn door reaches the entry trigger before the exterior building shell");
    require(!reaches_activation_before_wall(entry,capsule,exterior,{exterior.x,2600.,exterior.z},scale),
        "the passage stays outside the ray through the side wall");
    require(!reaches_activation_before_wall(entry,capsule,{exterior.x,exterior.y,600.},{220.,exterior.y,600.},scale),
        "the passage belongs to a different floor");
    constexpr ActivationBox rotated{{0.,0.,100.},{12.,45.,60.},37.5};
    require(reaches_activation_before_wall(rotated,capsule,{-100.,0.,0.},{100.,0.,0.},scale),
        "a rotated trigger crossed in the middle of the probe also opens the direction");
    require(!reaches_activation_before_wall(rotated,capsule,{0.,0.,0.},{100.,0.,0.},scale),
        "starting already inside an exit does not clear an unrelated wall");
}
void spiral_route_joins_the_nearest_floor() {
    constexpr Vec3 summit{12165.0, 2145.0, 13894.1};
    for (const double height : {1260.4, 6060.4, 10860.4}) {
        const Vec3 start{14100.0, -2170.0, height};
        const auto route = chapter_eight::spiral_route(start, false);
        require(route && route->size() >= 2, "a spiral waypoint has a route at each overlapping height");
        require(same(route->front(), start), "the route keeps the actual start");
        require(same((*route)[1], {14541.1, -2101.4, height + 131.6}),
            "the next point stays on the matching turn without duplicating the start");
        require(same(route->back(), summit), "all turns lead to the exact summit point");
    }
    const auto full = chapter_eight::spiral_route({11580.0, 400.0, 52.2}, false);
    require(full && full->size() == 123, "the complete ascent includes every measured waypoint");
}

void spiral_route_keeps_the_start_and_projects_to_the_centerline() {
    constexpr Vec3 start{11638.13, 183.19, 123.9};
    constexpr Vec3 projected{11594.2, 180.35, 123.9};
    const auto route = chapter_eight::spiral_route(start, false);
    require(route && route->size() >= 3, "a point near the first ramp segment can reach the summit");
    require(same(route->front(), start), "the actual lateral approach starts the route");
    require(same((*route)[1], projected), "the first new point is the closest point on the measured segment");
    require(same((*route)[2], {11608.4, -39.3, 195.6}), "the route continues to the next measured point");
}

void spiral_route_connects_both_directions_at_the_rest_stop() {
    constexpr Vec3 landing{12600.0, 400.0, 9640.0};
    constexpr Vec3 junction{11760.0, 400.0, 9663.0};
    constexpr Vec3 above{11608.4, -39.3, 9795.6};
    const auto descent = chapter_eight::spiral_route(above, true);
    require(descent && descent->size() == 4, "the rest-stop path descends from a higher point on the ramp");
    require(same((*descent)[1], junction) && same((*descent)[2], landing),
        "the downward approach leaves the spiral at its measured junction");
    require(same(descent->back(), chapter_eight::rest_stop_approach), "the route visits the exact rest-stop approach");
    const auto ascent = chapter_eight::spiral_route(chapter_eight::rest_stop_approach, false);
    require(ascent && ascent->size() >= 4, "a player at the rest stop can reach the summit");
    require(same((*ascent)[1], landing) && same((*ascent)[2], junction) && same((*ascent)[3], above),
        "the route returns along the branch and then climbs the spiral");
    require(same(ascent->back(), {12165.0, 2145.0, 13894.1}), "the return route ends at the summit");
}

void spiral_route_keeps_steering_ahead_near_the_centerline() {
    constexpr Vec3 start{12220.24, -1333.33, 605.35};
    constexpr Vec3 next{12460.9, -1553.4, 736.4};
    const auto route = chapter_eight::spiral_route(start, true);
    require(route && route->size() > 2 && same((*route)[1], next),
        "a small ramp-height mismatch does not steer back toward the current point");
}
void spiral_route_centers_the_capsule_at_each_narrow_seam() {
    for (const double rise : {0.0, 4800.0, 9600.0}) {
        const Vec3 start{13573.19, 2902.13, 3810.8 + rise};
        const auto route = chapter_eight::spiral_route(start, false);
        require(route && route->size() > 6 && std::abs((*route)[1].x - start.x) < 1.0 &&
            std::abs((*route)[1].y - 2892.0) < 1.0 &&
            same((*route)[2], {13560.0, 2892.0, 3814.848 + rise}) &&
            same((*route)[3], {13520.0, 2895.0, 3824.400 + rise}) &&
            same((*route)[4], {13460.0, 2901.0, 3838.608 + rise}),
            "the path follows changing clearance through the two railings on the same floor");
    }
}
void spiral_route_rejects_positions_off_the_ramp() {
    require(!chapter_eight::spiral_route({14100.0, 400.0, 9700.0}, false),
        "a point far from the spiral and branch cannot join either");
    require(!chapter_eight::spiral_route({14100.0, -2170.0, 3600.0}, false),
        "the same plan position between turns cannot join a different height");
    require(!chapter_eight::spiral_route({not_a_number, 0.0, 0.0}, true),
        "non-finite positions cannot join the rest-stop route");
}
}
int main() {
    const std::array<std::pair<const char*,void(*)()>,25> tests{{
        {"observed_exit_closest_point_is_on_the_owned_box",observed_exit_closest_point_is_on_the_owned_box},
        {"feet_inside_the_box_are_returned_unmodified",feet_inside_the_box_are_returned_unmodified},
        {"observed_projection_overlaps_the_capsule",observed_projection_overlaps_the_capsule},
        {"outside_projection_is_rejected",outside_projection_is_rejected},
        {"tangent_contact_is_rejected",tangent_contact_is_rejected},
        {"other_floors_are_rejected",other_floors_are_rejected},
        {"rotated_boxes_match_the_axis_aligned_case",rotated_boxes_match_the_axis_aligned_case},
        {"corners_use_the_euclidean_capsule_radius",corners_use_the_euclidean_capsule_radius},
        {"invalid_input_is_rejected",invalid_input_is_rejected},
        {"overlap_margin_below_four_centimetres_is_rejected",overlap_margin_below_four_centimetres_is_rejected},
        {"arrival_is_half_the_clearance_capped_at_half_a_metre",arrival_is_half_the_clearance_capped_at_half_a_metre},
        {"clearance_is_measured_from_the_capsule_segment",clearance_is_measured_from_the_capsule_segment},
        {"projected_approach_projects_the_closest_point_once",projected_approach_projects_the_closest_point_once},
        {"centerline_approach_stays_in_the_same_box",centerline_approach_stays_in_the_same_box},
        {"lower_castle_passage_uses_capsule_height",lower_castle_passage_uses_capsule_height},
        {"world_map_gate_has_an_overlapping_face_approach",world_map_gate_has_an_overlapping_face_approach},
        {"a_stacked_portal_projects_at_its_own_height",a_stacked_portal_projects_at_its_own_height},
        {"shrine_attendant_has_clear_talk_corners",shrine_attendant_has_clear_talk_corners},
        {"an_automatic_entrance_clears_the_wall_after_the_opening",an_automatic_entrance_clears_the_wall_after_the_opening},
        {"spiral_route_joins_the_nearest_floor",spiral_route_joins_the_nearest_floor},
        {"spiral_route_keeps_the_start_and_projects_to_the_centerline",spiral_route_keeps_the_start_and_projects_to_the_centerline},
        {"spiral_route_connects_both_directions_at_the_rest_stop",spiral_route_connects_both_directions_at_the_rest_stop},
        {"spiral_route_keeps_steering_ahead_near_the_centerline",spiral_route_keeps_steering_ahead_near_the_centerline},
        {"spiral_route_centers_the_capsule_at_each_narrow_seam",spiral_route_centers_the_capsule_at_each_narrow_seam},
        {"spiral_route_rejects_positions_off_the_ramp",spiral_route_rejects_positions_off_the_ramp}}};
    int failures=0;
    for(const auto& [name,test]:tests) {
        std::cout<<"RUN  "<<name<<std::endl; // Flushed so a crash still names its group.
        try{test();std::cout<<"PASS "<<name<<std::endl;}
        catch(const std::exception& error){++failures;std::cout<<"FAIL "<<name<<": "<<error.what()<<std::endl;}
    }
    std::cout<<tests.size()-failures<<"/"<<tests.size()<<" activation goal groups passed\n";
    return failures?1:0;
}
