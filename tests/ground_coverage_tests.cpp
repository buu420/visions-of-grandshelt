#include "resonance/GroundCoverage.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace resonance;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
constexpr double scale=100; // Live field WorldToMeters.
PathSnapshot path(std::vector<Vec3> points){return {std::move(points),{0,1,0},{1,0,0},scale,true,false};}
PathSnapshot straight(double metres){return path({{0,0,0},{metres*scale,0,0}});}
constexpr double pi=3.14159265358979323846;
// A flat lead of `lead` metres, then a straight ramp rising (falling, for a negative
// grade) at `grade` degrees over `run` horizontal metres, all along `heading` degrees.
PathSnapshot ramp(double heading,double grade,double lead=3,double run=40) {
    const double h=heading*pi/180;const Vec3 d{std::cos(h),std::sin(h),0};
    const Vec3 foot{d.x*lead*scale,d.y*lead*scale,0};
    return path({{0,0,0},foot,{foot.x+d.x*run*scale,foot.y+d.y*run*scale,std::tan(grade*pi/180)*run*scale}});
}
GroundBudget walkable(double degrees){GroundBudget b;b.walkable_slope_degrees=degrees;return b;}
// Recording fake native probes; positions in world units (scale 100 per metre).
struct Probe {
    std::vector<std::pair<Vec3,Vec3>> rays;std::vector<Vec3> projections;
    std::function<std::optional<bool>(Vec3,Vec3)> ray=[](Vec3,Vec3)->std::optional<bool>{return false;};
    std::function<std::optional<Vec3>(Vec3)> surface=[](Vec3 p)->std::optional<Vec3>{return p;};
    GroundProbe probe() {
        return {[this](Vec3 a,Vec3 b){rays.push_back({a,b});return ray(a,b);},
                [this](Vec3 p){projections.push_back(p);return surface(p);}};
    }
    double max_x() const {
        double result=-std::numeric_limits<double>::infinity();
        for(const auto& [a,b]:rays)result=std::max({result,a.x,b.x});
        for(const auto& p:projections)result=std::max(result,p.x);
        return result;
    }
};
bool near(double a,double b,double tolerance=1e-9){return std::abs(a-b)<=tolerance;}
bool same(Vec3 a,Vec3 b,double tolerance=1e-9){return near(a.x,b.x,tolerance)&&near(a.y,b.y,tolerance)&&near(a.z,b.z,tolerance);}
// The follower validates these; every returned coverage must satisfy them.
void invariants(const PathSnapshot& p,const GroundCoverage& c,std::uint64_t id) {
    require(c.path_id==id,"coverage is bound to the planned path identity");
    const auto arc=route_arc_metres(p,c.end);
    require(arc&&near(*arc,c.metres,1e-9*std::max(1.,c.metres)),"metres equals the arc length of the reported end");
    const auto& a=p.points[c.end.segment-1];const auto& b=p.points[c.end.segment];
    const Vec3 at{a.x+(b.x-a.x)*c.end.fraction,a.y+(b.y-a.y)*c.end.fraction,a.z+(b.z-a.z)*c.end.fraction};
    require(same(at,c.end_point,1e-6),"end_point is the route point at the reported end");
    const bool final=c.end.segment==p.points.size()-1&&c.end.fraction==1;
    require(c.reaches_end==final,"reaches_end exactly when the final route point is the verified end");
    if(c.reaches_end)require(same(c.end_point,p.points.back(),0)&&c.limit==GroundLimit::None,"a verified real end has no limit");
}
GroundCoverage plan(const PathSnapshot& p,Probe& probe,GroundBudget budget={},std::uint64_t id=7) {
    const auto c=plan_ground_coverage(p,id,budget,probe.probe());
    require(c.has_value(),"valid input yields coverage");
    invariants(p,*c,id);
    return *c;
}
void long_paths_have_equal_near_prefix_cost() {
    std::vector<std::size_t> calls;std::vector<std::size_t> projections;
    for(double metres:{49.,1000.,100000.}) {
        Probe probe;const auto p=straight(metres);const auto c=plan(p,probe);
        require(c.limit==GroundLimit::Horizon&&!c.reaches_end,"a long full route is truncated by the lookahead, never rejected or falsely arrived");
        require(near(c.metres,12,1e-9)&&same(c.end_point,{1200,0,0},1e-6),"coverage ends exactly at the 12 m horizon");
        require(probe.max_x()<=1200+1e-6,"no native probe lies beyond the checked extent");
        require(p.points.back().x==metres*scale,"the full native path is not clipped");
        calls.push_back(c.native_calls);projections.push_back(probe.projections.size());
        require(c.native_calls==probe.rays.size()+probe.projections.size(),"native_calls counts every ray and projection");
    }
    require(calls[0]==calls[1]&&calls[1]==calls[2]&&projections[0]==61,"49 m, 1 km and 100 km routes cost the same near-prefix work");
}
void short_path_reaches_true_end() {
    Probe probe;const auto p=straight(10);const auto c=plan(p,probe);
    require(c.reaches_end&&c.end.segment==1&&c.end.fraction==1&&near(c.metres,10),"a route inside the horizon is verified to its real end");
    Probe exact;const auto twelve=straight(12);const auto e=plan(twelve,exact);
    require(e.reaches_end,"a route ending exactly at the horizon is verified to its end");
}
void dense_collinear_vertices_are_resampled_along_the_arc() {
    std::vector<Vec3> dense;for(int i=0;i<=80;++i)dense.push_back({i*12.5,0,0});
    Probe a,b;const auto dense_path=path(dense);const auto c=plan(dense_path,a);const auto d=plan(straight(10),b);
    require(c.reaches_end&&dense.size()>64,"81 exactly collinear vertices are not a route rejection");
    require(a.projections.size()==b.projections.size()&&a.rays.size()==b.rays.size()&&c.native_calls==d.native_calls,
        "exactly collinear vertices cost the same as one straight segment");
    std::vector<Vec3> many;for(int i=0;i<=4000;++i)many.push_back({i*10.,0,0}); // 400 m, 4001 points (<4096).
    Probe m,s;const auto big=plan(path(many),m);const auto ref=plan(straight(400),s);
    require(big.limit==GroundLimit::Horizon&&big.native_calls==ref.native_calls,"4001 collinear points over 400 m keep the near-prefix cost");
}
void corners_are_preserved() {
    const auto boundary=[](const Probe& probe,Vec3 corner) {
        return std::any_of(probe.rays.begin(),probe.rays.end(),[&](const auto& ray){return same(ray.second,corner,0);})&&
            std::any_of(probe.rays.begin(),probe.rays.end(),[&](const auto& ray){return same(ray.first,corner,0);});
    };
    // 5 m lies on the 1 m ray-chunk and 0.2 m sample grids; 2.3 m lies on neither.
    for(const double corner:{500.,230.}) {
        Probe probe;const auto l=path({{0,0,0},{corner,0,0},{corner,500,0}});const auto c=plan(l,probe);
        require(c.reaches_end,"an L route inside the horizon reaches its end");
        for(const auto& [a,b]:probe.rays)
            require((a.y==0&&b.y==0)||(a.x==corner&&b.x==corner),"no ray shortcuts a genuine corner");
        require(std::any_of(probe.projections.begin(),probe.projections.end(),[&](Vec3 p){return p.x==corner&&p.y==0;}),"the corner itself is sampled");
        require(boundary(probe,{corner,0,0}),"a genuine corner is an exact ray boundary");
        Probe bend;const auto slight=path({{0,0,0},{corner,0,0},{corner+500,.5,0}});plan(slight,bend);
        for(const auto& [a,b]:bend.rays)require(!(a.x<corner&&b.x>corner),"a 0.001 rad native bend is a corner, not a 0.02 m shortcut");
        require(boundary(bend,{corner,0,0}),"a slight bend is an exact ray boundary");
    }
    Probe back;const auto reverse=path({{0,0,0},{500,0,0},{100,0,0}});const auto r=plan(reverse,back);
    require(boundary(back,{500,0,0})&&r.reaches_end,"a reversal is a ray boundary and the route is checked to its end");
}
PathSnapshot suffix(const PathSnapshot& p,const GroundCoverage& c) {
    std::vector<Vec3> points{c.end_point};
    for(std::size_t i=c.end.segment;i<p.points.size();++i)if(i!=c.end.segment||c.end.fraction<1)points.push_back(p.points[i]);
    if(points.size()<2)points.push_back(c.end_point);
    return path(std::move(points));
}
void budget_progression_never_rejects_dense_corners() {
    std::vector<Vec3> zig;for(int i=0;i<=400;++i)zig.push_back({i*25.,(i%2)*25.,0}); // 400 genuine corners.
    double total=0;for(std::size_t i=1;i<zig.size();++i)total+=std::hypot(zig[i].x-zig[i-1].x,zig[i].y-zig[i-1].y)/scale;
    // Three calls exhaust the budget both before a ray and before a projection.
    for(const std::size_t calls:{std::size_t{3},std::size_t{7},std::size_t{30}}) {
        auto current=path(zig);double covered=0;int plans=0;
        const GroundBudget small{12,.2,calls};
        while(true) {
            Probe probe;const auto c=plan(current,probe,small);++plans;
            require(c.native_calls<=calls&&c.native_calls==probe.rays.size()+probe.projections.size(),
                "a plan never exceeds its per-update native budget");
            if(c.reaches_end){covered+=c.metres;break;}
            require(c.limit==GroundLimit::Budget&&c.metres>0,"budget exhaustion shortens coverage and still progresses");
            covered+=c.metres;current=suffix(current,c);
            require(plans<2000,"dense corners cannot stall progression");
        }
        require(near(covered,total,1e-6)&&plans>1,"successive budgeted plans cover the entire dense journey");
    }
}
void near_and_far_failures_end_at_the_last_good_sample() {
    {Probe probe;probe.ray=[](Vec3 a,Vec3 b)->std::optional<bool>{return a.x<290&&b.x>=290;};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Obstruction&&near(c.metres,2)&&!c.reaches_end,"a near obstruction ends coverage at the chunk start");
     for(const auto& p:probe.projections)require(p.x<=200,"no projection beyond a blocked ray");}
    {Probe probe;probe.ray=[](Vec3,Vec3 b)->std::optional<bool>{return b.x>=10000;};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Horizon&&near(c.metres,12)&&probe.max_x()<10000,"a 100 m obstruction is not probed or rejected yet");}
    {Probe probe;probe.surface=[](Vec3 p)->std::optional<Vec3>{if(p.x>=500)return std::nullopt;return p;};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Projection&&near(c.metres,4.8),"a failed projection ends at the previous sample");}
    {Probe probe;probe.surface=[](Vec3 p)->std::optional<Vec3>{return p.x>=600?Vec3{p.x+3,p.y,p.z}:p;};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Surface&&near(c.metres,5.8),"a horizontal projection error over 0.02 m is unsupported surface");}
    {Probe probe;probe.surface=[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,p.z+(p.x>=700?2.6:0)};};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Surface&&near(c.metres,6.8),"a vertical projection error over 0.025 m is unsupported surface");}
    {Probe probe;probe.surface=[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,p.z+(p.x>=400?2.:-2.)};};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Surface&&near(c.metres,3.8),"an adjacent sampled height step over 0.025 m is unsupported surface");}
    {Probe probe;const auto link=path({{0,0,0},{800,0,0},{800,0,50},{2000,0,50}});const auto c=plan(link,probe);
     require(c.limit==GroundLimit::VerticalLink&&near(c.metres,8)&&c.end.segment==1&&c.end.fraction==1,"a vertical link stops coverage at its start");
     for(const auto& p:probe.projections)require(p.z==0,"the vertical link itself is never sampled as walkable");}
    {Probe probe;probe.surface=[](Vec3)->std::optional<Vec3>{return std::nullopt;};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Projection&&c.metres==0&&c.end.segment==1&&c.end.fraction==0,"an unverified start is zero coverage, not invalid input");}
    {Probe probe;const auto c=plan(straight(1000),probe,{12,.2,1});
     require(c.limit==GroundLimit::Budget&&c.metres==0,"a one-call budget verifies only the start");}
}
void unknowns_partial_and_nonfinite() {
    {Probe probe;probe.ray=[](Vec3 a,Vec3)->std::optional<bool>{if(a.x>=200)return std::nullopt;return false;};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Unreadable&&near(c.metres,2),"an unreadable ray ends coverage at its start");}
    {Probe probe;probe.surface=[](Vec3 p)->std::optional<Vec3>{if(p.x>=300)return Vec3{std::nan(""),p.y,p.z};return p;};
     const auto c=plan(straight(1000),probe);
     require(c.limit==GroundLimit::Unreadable&&near(c.metres,2.8),"a non-finite projection is unreadable");}
    Probe probe;const auto good=straight(10);const auto any=probe.probe();
    auto partial=good;partial.partial=true;
    auto invalid=good;invalid.valid=false;
    auto single=good;single.points.resize(1);
    auto nan=good;nan.points[1].y=std::nan("");
    auto unscaled=good;unscaled.world_to_metres=0;
    auto huge=path(std::vector<Vec3>(4097,Vec3{}));
    for(const auto* p:{&partial,&invalid,&single,&nan,&unscaled,&huge})
        require(!plan_ground_coverage(*p,1,{},any),"invalid, partial, non-finite, unscaled or corrupt (>4096) paths return null");
    for(const auto& budget:{GroundBudget{0,.2,160},GroundBudget{std::nan(""),.2,160},GroundBudget{12,0,160},GroundBudget{12,std::nan(""),160},GroundBudget{12,.2,0}})
        require(!plan_ground_coverage(good,1,budget,any),"an invalid budget returns null");
    require(!plan_ground_coverage(good,1,{},GroundProbe{nullptr,any.project})&&!plan_ground_coverage(good,1,{},GroundProbe{any.blocked,nullptr}),
        "missing native probes return null");
}
void exact_endpoint_and_duplicates() {
    Probe probe;const auto duplicate=path({{0,0,0},{500,0,0},{500,0,0},{500,0,0}});const auto c=plan(duplicate,probe,{},42);
    require(c.reaches_end&&c.end.segment==3&&c.end.fraction==1&&same(c.end_point,{500,0,0},0)&&near(c.metres,5)&&c.path_id==42,
        "duplicate endpoints report the real final index once the endpoint is verified");
    Probe zero;const auto still=path({{100,0,0},{100,0,0}});const auto z=plan(still,zero);
    require(z.reaches_end&&z.metres==0,"a zero-length route verifies its single point");
    Probe fail;fail.surface=[](Vec3 p)->std::optional<Vec3>{if(p.x>=1000)return std::nullopt;return p;};
    const auto ten=straight(10);const auto f=plan(ten,fail);
    require(!f.reaches_end&&f.limit==GroundLimit::Projection&&near(f.metres,9.8),"a failed endpoint check is not arrival");
}
void progress_fractions_arc_and_wrong_index() {
    const auto l=path({{0,0,0},{500,0,0},{500,500,0}});
    require(near(*route_arc_metres(l,{1,0}),0)&&near(*route_arc_metres(l,{1,.5}),2.5)&&near(*route_arc_metres(l,{1,1}),5)&&
        near(*route_arc_metres(l,{2,0}),5)&&near(*route_arc_metres(l,{2,1}),10),"arc lengths follow endpoint-index segments");
    for(const RouteLocation bad:{RouteLocation{0,0},RouteLocation{3,0},RouteLocation{1,-.1},RouteLocation{1,1.1},RouteLocation{1,std::nan("")}})
        require(!route_arc_metres(l,bad),"out-of-range segment or fraction is invalid");
    const auto a=route_progress(l,{250,30,0},1);require(a&&a->segment==1&&near(a->fraction,.5),"progress projects onto the current segment");
    const auto b=route_progress(l,{600,0,0},1);require(b&&near(b->fraction,1),"progress clamps past the segment end");
    const auto c=route_progress(l,{-100,0,0},1);require(c&&near(c->fraction,0),"progress clamps before the segment start");
    const auto d=route_progress(l,{500,250,0},2);require(d&&d->segment==2&&near(d->fraction,.5),"progress uses the requested next waypoint");
    require(!route_progress(l,{0,0,0},0)&&!route_progress(l,{0,0,0},3)&&!route_progress(l,{std::nan(""),0,0},1),"wrong index or non-finite position is invalid");
    const auto dup=path({{0,0,0},{0,0,0},{500,0,0}});const auto e=route_progress(dup,{0,0,0},1);
    require(e&&e->fraction==1,"a zero-length segment is already traversed");
    auto bad=l;bad.points[0].x=std::nan("");require(!route_arc_metres(bad,{1,0})&&!route_progress(bad,{0,0,0},1),"non-finite paths are invalid");
    const auto ramp=path({{0,0,0},{300,0,400}});const auto half=route_progress(ramp,{150,0,200},1);
    require(near(*route_arc_metres(ramp,{1,1}),5)&&half&&near(*route_arc_metres(ramp,*half),2.5),"arc and progress are three-dimensional");
}
void horizon_is_measured_along_the_3d_arc() {
    // A 5.7 degree ramp: 0.2 m arc samples rise 0.0199 m, inside the adjacent-height rule.
    Probe probe;const auto ramp=path({{0,0,0},{2000,0,200}});const auto c=plan(ramp,probe);
    const double length=std::hypot(2000.,200.);
    require(c.limit==GroundLimit::Horizon&&near(c.metres,12)&&same(c.end_point,{2000*1200/length,0,200*1200/length},1e-6),
        "the 12 m horizon is measured along the sloped 3D route");
}
void native_tolerances_are_unchanged() {
    const auto offset=[](double dx,double dz){return [=](Vec3 p)->std::optional<Vec3>{return Vec3{p.x+dx,p.y,p.z+dz};};};
    const auto rejected_at_start=[](const GroundCoverage& c){return c.limit==GroundLimit::Surface&&c.metres==0;};
    {Probe ok;ok.surface=offset(1.9,0);require(plan(straight(10),ok).reaches_end,"a 0.019 m horizontal projection error is ordinary ground");}
    {Probe bad;bad.surface=offset(2.1,0);require(rejected_at_start(plan(straight(10),bad)),"a 0.021 m horizontal projection error is unsupported");}
    {Probe ok;ok.surface=offset(0,2.4);require(plan(straight(10),ok).reaches_end,"a 0.024 m vertical projection error is ordinary ground");}
    {Probe bad;bad.surface=offset(0,2.6);require(rejected_at_start(plan(straight(10),bad)),"a 0.026 m vertical projection error is unsupported");}
    // Alternating sampled heights: each within the projection tolerance, steps of 0.024 or 0.026 m.
    const auto alternate=[](double h){return [=](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,p.z+(std::llround(p.x/20)%2!=0?h:-h)};};};
    {Probe ok;ok.surface=alternate(1.2);require(plan(straight(10),ok).reaches_end,"a 0.024 m adjacent sampled height step is ordinary ground");}
    {Probe bad;bad.surface=alternate(1.3);require(rejected_at_start(plan(straight(10),bad)),"a 0.026 m adjacent sampled height step is unsupported");}
    // Vertical link: horizontal < 0.01 m and |dz| > 0.025 m; otherwise the leg is sampled ground.
    const auto rise=[](double horizontal,double dz){return path({{0,0,0},{300,0,0},{300+horizontal,0,dz},{1300+horizontal,0,dz}});};
    {Probe p;const auto c=plan(rise(.9,50),p);require(c.limit==GroundLimit::VerticalLink&&near(c.metres,3),"a 0.009 m horizontal rise is a vertical link");}
    {Probe p;const auto c=plan(rise(1.5,50),p);require(c.limit==GroundLimit::Surface&&near(c.metres,3),"a 0.015 m horizontal rise is sampled ground, not a vertical link");}
    {Probe p;const auto c=plan(rise(0,2.6),p);require(c.limit==GroundLimit::VerticalLink&&near(c.metres,3),"a 0.026 m vertical leg is a vertical link");}
    {Probe p;const auto c=plan(rise(0,2.4),p);require(c.limit==GroundLimit::Horizon&&near(c.metres,12),"a 0.024 m vertical leg is ordinary sampled ground");}
}
void walkable_ramps_follow_the_native_limit() {
    // Up, down and rotated ramps within a 30 degree limit are ordinary ground to the horizon.
    for(const auto& [heading,grade]:{std::pair{0.,20.},std::pair{0.,-20.},std::pair{90.,20.},std::pair{217.,-20.},std::pair{0.,29.9}}) {
        Probe probe;const auto c=plan(ramp(heading,grade),probe,walkable(30));
        require(c.limit==GroundLimit::Horizon&&near(c.metres,12),"a ramp within the verified walkable limit is ordinary ground");
    }
    // Without a verified limit, or beyond it, a ramp stops coverage at its foot.
    for(const auto& [heading,grade]:{std::pair{0.,20.},std::pair{217.,-20.}}) {
        Probe flat;const auto c=plan(ramp(heading,grade),flat);
        require(c.limit==GroundLimit::Surface&&near(c.metres,3),"without a verified limit a ramp stays unsupported");
    }
    for(const auto& [heading,grade]:{std::pair{0.,30.1},std::pair{0.,-30.1},std::pair{90.,35.}}) {
        Probe steep;const auto c=plan(ramp(heading,grade),steep,walkable(30));
        require(c.limit==GroundLimit::Surface&&near(c.metres,3),"a ramp steeper than the verified limit stops at its foot");
    }
    // Even a gentle grade, rising under 0.025 m per sample, exceeds a low native limit.
    for(const auto& [grade,limit]:{std::pair{2.,0.},std::pair{6.,3.},std::pair{-6.,3.}}) {
        Probe low;const auto c=plan(ramp(0,grade),low,walkable(limit));
        require(c.limit==GroundLimit::Surface&&near(c.metres,3),"any grade above the verified limit stops at the ramp foot");
    }
    Probe top;const auto c=plan(ramp(0,20,3,5),top,walkable(30));
    require(c.reaches_end&&near(c.metres,3+5/std::cos(20*pi/180)),"a short walkable ramp is verified to its real end");
}
void unknown_or_wrong_slope_limits_never_authorize_a_ramp() {
    // A zero limit is the native "flat floors only" setting: no ramp, flat ground unchanged.
    {Probe probe;const auto c=plan(ramp(0,10),probe,walkable(0));
     require(c.limit==GroundLimit::Surface&&near(c.metres,3),"a zero walkable limit authorizes no ramp");}
    {Probe probe;require(plan(straight(1000),probe,walkable(0)).limit==GroundLimit::Horizon,"a zero walkable limit keeps flat ground ordinary");}
    // An unreadable limit is absent (the flat rule above); a malformed one is an invalid budget.
    for(const double bad:{std::nan(""),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),-1.,90.,120.}) {
        Probe probe;const auto any=probe.probe();
        require(!plan_ground_coverage(ramp(0,10),1,walkable(bad),any)&&probe.rays.empty()&&probe.projections.empty(),
            "a malformed walkable limit is an invalid budget and probes nothing");
    }
}
void slope_support_keeps_steps_gaps_drops_and_links() {
    // On a 20 degree ramp from 3 m, samples at 6.0 m (x=581.9) and 6.2 m (x=600.7) straddle
    // x=600. Each surface lies within 0.02 m of the route; the change between them is 0.04 m.
    const auto shifted=[](double before,double after){return [=](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,p.z+(p.x>=600?after:before)};};};
    {Probe step;step.surface=shifted(-2,2);const auto c=plan(ramp(0,20),step,walkable(30));
     require(c.limit==GroundLimit::Surface&&near(c.metres,6),"a step on a walkable ramp is unsupported surface");}
    {Probe drop;drop.surface=shifted(2,-2);const auto c=plan(ramp(0,20),drop,walkable(30));
     require(c.limit==GroundLimit::Surface&&near(c.metres,6),"a drop on a walkable ramp is unsupported surface");}
    {Probe gap;gap.surface=[](Vec3 p)->std::optional<Vec3>{if(p.x>=600&&p.x<700)return std::nullopt;return p;};
     const auto c=plan(ramp(0,20),gap,walkable(30));
     require(c.limit==GroundLimit::Projection&&near(c.metres,6),"a gap in a walkable ramp ends coverage");}
    {Probe flat;flat.surface=[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,0};};
     const auto c=plan(ramp(0,20),flat,walkable(30));
     require(c.limit==GroundLimit::Surface&&near(c.metres,3),"a route rising above flat ground is not a ramp");}
    const double rise=std::tan(20*pi/180)*400;
    {Probe link;const auto c=plan(path({{0,0,0},{300,0,0},{700,0,rise},{700,0,rise+50},{1100,0,2*rise+50}}),link,walkable(30));
     require(c.limit==GroundLimit::VerticalLink&&near(c.metres,3+4/std::cos(20*pi/180)),"a vertical link on a ramp still stops coverage at its start");}
    {Probe steep;const auto c=plan(path({{0,0,0},{300,0,0},{301.5,0,50},{1300,0,50}}),steep,walkable(30));
     require(c.limit==GroundLimit::Surface&&near(c.metres,3),"a near-vertical rise is not a walkable slope");}
}
void flat_routes_are_judged_identically_with_a_walkable_limit() {
    const auto alternate=[](double h){return [=](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,p.z+(std::llround(p.x/20)%2!=0?h:-h)};};};
    {Probe ok;ok.surface=alternate(1.2);require(plan(straight(10),ok,walkable(30)).reaches_end,"a 0.024 m flat step stays ordinary with a limit");}
    {Probe bad;bad.surface=alternate(1.3);const auto c=plan(straight(10),bad,walkable(30));
     require(c.limit==GroundLimit::Surface&&c.metres==0,"a 0.026 m flat step stays unsupported with a limit");}
    {Probe probe;probe.surface=[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,p.z+(p.x>=400?2.:-2.)};};
     const auto c=plan(straight(1000),probe,walkable(30));
     require(c.limit==GroundLimit::Surface&&near(c.metres,3.8),"a 0.04 m flat height step stays unsupported with a limit");}
    const auto rise=[](double horizontal,double dz){return path({{0,0,0},{300,0,0},{300+horizontal,0,dz},{1300+horizontal,0,dz}});};
    {Probe p;const auto c=plan(rise(0,2.6),p,walkable(30));require(c.limit==GroundLimit::VerticalLink&&near(c.metres,3),"a vertical link stays a link with a limit");}
    {Probe p;const auto c=plan(rise(0,2.4),p,walkable(30));require(c.limit==GroundLimit::Horizon&&near(c.metres,12),"a 0.024 m vertical leg stays ordinary with a limit");}
}
void long_ramp_renewal_reaches_the_top() {
    // One 200 m horizontal, 15 degree native ramp: every rolling update covers its full
    // horizon until the last verifies the real top (207.06 m = 17 x 12 m + 3.06 m).
    auto current=path({{0,0,0},{20000,0,std::tan(15*pi/180)*20000}});
    const double total=200/std::cos(15*pi/180);double covered=0;int plans=0;
    while(true) {
        Probe probe;const auto c=plan(current,probe,walkable(30));++plans;
        if(c.reaches_end){covered+=c.metres;break;}
        require(c.limit==GroundLimit::Horizon&&near(c.metres,12),"each update on a long walkable ramp covers its full horizon");
        covered+=c.metres;current=suffix(current,c);
        require(plans<100,"a long ramp cannot stall");
    }
    require(near(covered,total,1e-6)&&plans==18,"successive updates verify the entire ramp to its top");
}
// Surface-following fixtures. ground() projects like root's adapter: terrain is found
// only within 0.10 m (10 units) of the query height, at the query XY.
std::function<std::optional<Vec3>(Vec3)> ground(std::function<double(double,double)> height) {
    return [height](Vec3 q)->std::optional<Vec3>{const double z=height(q.x,q.y);if(std::abs(z-q.z)>10)return std::nullopt;return Vec3{q.x,q.y,z};};
}
// Flat to x=5 m, a 21.8 degree ramp rising 2 m to x=10 m, then flat (world units).
double terrain_break(double x,double){return x<500?0:std::min(x-500,500.)*.4;}
GroundBudget fine(double degrees,std::size_t calls=1300) {
    auto b=surface_following_budget(walkable(degrees));require(b.has_value(),"a verified limit derives a surface-following budget");
    b->max_native_calls=calls;return *b;
}
void surface_following_crosses_mid_segment_terrain_breaks() {
    // Characterization B: one native segment from flat ground to a ramp top. Coarse checks
    // follow the chord and stop at once; surface following verifies the real native end.
    const auto b=path({{0,0,0},{1000,0,200}});
    {Probe coarse;coarse.surface=ground(terrain_break);const auto c=plan(b,coarse,walkable(30));
     require(c.limit==GroundLimit::Surface&&c.metres==0&&!c.follow_surface,"coarse checks stay on the chord");}
    Probe probe;probe.surface=ground(terrain_break);const auto c=plan(b,probe,fine(30));
    require(c.reaches_end&&c.follow_surface&&near(c.metres,std::hypot(10.,2.))&&same(c.end_point,{1000,0,200},0),
        "surface following verifies a ramp entered mid-segment up to the real native end");
    require(probe.rays.size()+1==probe.projections.size()&&c.native_calls==2*probe.projections.size()-1,"one ray per verified interval");
    for(std::size_t i=0;i<probe.rays.size();++i) {
        const auto& [from,to]=probe.rays[i];
        require(from.z==terrain_break(from.x,from.y)&&to.z==terrain_break(to.x,to.y)&&(i==0||same(from,probe.rays[i-1].second,0)),
            "rays run between successive verified surfaces, never along the chord");
    }
    // Characterization C: the horizon edge lies 0.41 m below the chord and is not an anchor.
    Probe beyond;beyond.surface=ground(terrain_break);const auto h=plan(path({{0,0,0},{1500,0,200}}),beyond,fine(30));
    const double length=std::hypot(1500.,200.);
    require(h.limit==GroundLimit::Horizon&&!h.reaches_end&&near(h.metres,12)&&same(h.end_point,{1500*1200/length,0,200*1200/length},1e-6),
        "the horizon edge is the literal native chord point, not an endpoint");
}
void surface_following_descends_and_rotates() {
    Probe down;down.surface=ground([](double x,double y){return 200-terrain_break(x,y);});
    require(plan(path({{0,0,200},{1000,0,0}}),down,fine(30)).reaches_end,"a descending mid-segment break is followed");
    const double h=217*pi/180,cx=std::cos(h),cy=std::sin(h);
    Probe turned;turned.surface=ground([=](double x,double y){return terrain_break(x*cx+y*cy,0);});
    require(plan(path({{0,0,0},{1000*cx,1000*cy,200}}),turned,fine(30)).reaches_end,"a rotated mid-segment break is followed");
}
void surface_anchors_reject_wrong_heights_and_floors() {
    const auto check=[](PathSnapshot p,std::function<double(double,double)> height){Probe probe;probe.surface=ground(height);return plan(p,probe,fine(30));};
    // A start, corner or end 0.05 m from the surface under it is not that native route.
    {const auto c=check(path({{0,0,5},{1000,0,200}}),terrain_break);
     require(c.limit==GroundLimit::Surface&&c.metres==0,"a start off the followed surface verifies nothing");}
    {const auto c=check(path({{0,0,0},{500,0,5},{1000,0,200}}),terrain_break);
     require(c.limit==GroundLimit::Surface&&near(c.metres,4.98),"a corner off the followed surface ends coverage before it");}
    {const auto c=check(path({{0,0,0},{1000,0,205}}),terrain_break);
     require(c.limit==GroundLimit::Surface&&!c.reaches_end,"an end off the followed surface is not arrival");}
    // A native route across a deck that ends cannot drop to the floor 3 m below.
    {const auto c=check(path({{0,0,300},{1000,0,300}}),[](double x,double){return x<400?300.:0.;});
     require(c.limit==GroundLimit::Projection&&near(c.metres,3.98),"surface following never changes floors");}
}
void surface_mode_rejects_steps_drops_gaps_and_links() {
    // Flat native route; the terrain changes at x=6 m, between samples at 5.98 m and 6 m.
    for(const double dz:{2.6,4.,-2.6,-4.,2.4}) {
        Probe probe;probe.surface=ground([=](double x,double){return x<600?0.:dz;});const auto c=plan(straight(10),probe,fine(30));
        require(c.limit==GroundLimit::Surface&&near(c.metres,5.98),"a step or drop between fine samples exceeds the native grade");
    }
    {Probe gap;gap.surface=[](Vec3 q)->std::optional<Vec3>{if(q.x>=600&&q.x<700)return std::nullopt;return Vec3{q.x,q.y,0};};
     const auto c=plan(straight(10),gap,fine(30));
     require(c.limit==GroundLimit::Projection&&near(c.metres,5.98),"a gap ends surface following");}
    {Probe link;link.surface=ground([](double x,double){return x<=500?0.:50.;});
     const auto c=plan(path({{0,0,0},{500,0,0},{500,0,50},{1500,0,50}}),link,fine(30));
     require(c.limit==GroundLimit::VerticalLink&&near(c.metres,5),"a vertical link still stops at its start");}
    {Probe jog;jog.surface=ground([](double x,double){return std::clamp((x-490)*.12,0.,2.4);});
     const auto c=plan(path({{0,0,0},{500,0,0},{500,0,2.4},{1500,0,2.4}}),jog,fine(30));
     require(c.limit==GroundLimit::Horizon&&near(c.metres,12),"a 0.024 m zero-XY native jog over continuous ground is ordinary");}
    {Probe blocked;blocked.surface=ground([](double,double){return 0.;});
     blocked.ray=[](Vec3 a,Vec3 b)->std::optional<bool>{return a.x<300&&b.x>=300;};
     const auto c=plan(straight(10),blocked,fine(30));
     require(c.limit==GroundLimit::Obstruction&&near(c.metres,2.98),"an obstruction ends coverage at the previous verified surface");}
}
void surface_following_needs_a_verified_limit_and_fine_spacing() {
    require(!surface_following_budget(GroundBudget{})&&!surface_following_budget(walkable(std::nan("")))&&!surface_following_budget(walkable(90)),
        "an unknown or malformed limit derives no surface-following budget");
    const auto zero=surface_following_budget(walkable(0));const auto steep=surface_following_budget(walkable(60));
    auto finer=walkable(30);finer.spacing_metres=.01;const auto kept=surface_following_budget(finer);
    require(zero&&zero->follow_surface&&zero->spacing_metres==.02&&steep&&near(steep->spacing_metres,.0225/std::sqrt(3.))&&
        kept&&kept->spacing_metres==.01,"spacing is min(budget, 0.02, 0.0225/tan(limit))");
    Probe probe;const auto any=probe.probe();
    GroundBudget unknown;unknown.follow_surface=true;unknown.spacing_metres=.02;
    auto coarse=*surface_following_budget(walkable(30));coarse.spacing_metres=.05;
    require(!plan_ground_coverage(straight(10),1,unknown,any)&&!plan_ground_coverage(straight(10),1,coarse,any)&&probe.projections.empty(),
        "surface following without a limit or with coarse spacing is an invalid budget");
    // A flat native chord over ground ramping from x=3 m: the followed surface's own grade counts.
    for(const auto& [grade,ok]:{std::pair{29.,true},std::pair{31.,false}}) {
        Probe r;r.surface=ground([=](double x,double){return std::max(0.,x-300)*std::tan(grade*pi/180);});
        const auto c=plan(straight(20),r,fine(30));
        require(ok?(c.limit==GroundLimit::Horizon&&near(c.metres,12)):(c.limit==GroundLimit::Surface&&near(c.metres,3)),
            "the followed surface's grade is held to the native limit");
    }
}
void surface_rolling_progress_and_call_bounds() {
    // A 60 m native segment over flat ground, a 16.7 degree ramp rising 6 m, then flat;
    // each update replans from the ground under the previous coverage end.
    const auto height=[](double x,double){return std::clamp((x-2000)*.3,0.,600.);};
    auto current=path({{0,0,0},{6000,0,600}});int plans=0;double last_x=0;
    while(true) {
        Probe probe;probe.surface=ground(height);const auto c=plan(current,probe,fine(30));++plans;
        require(c.follow_surface&&c.native_calls<=1201&&c.native_calls==2*probe.projections.size()-1,"each update stays within its funded calls");
        if(c.reaches_end)break;
        require(c.limit==GroundLimit::Horizon&&near(c.metres,12)&&c.end_point.x>last_x,"each update covers its full horizon");
        last_x=c.end_point.x;current=path({{c.end_point.x,c.end_point.y,height(c.end_point.x,0)},{6000,0,600}});
        require(plans<20,"surface following cannot stall");
    }
    require(plans>=5,"the journey needed several rolling updates");
}
void horizontal_progress_follows_the_xy_trace() {
    // On the ground halfway under a chord rising 5 m over 10 m: XY says 0.5, 3D says 0.4.
    const auto chord=path({{0,0,0},{1000,0,500}});
    const auto xy=route_progress(chord,{500,0,0},1,true);const auto d3=route_progress(chord,{500,0,0},1);
    require(xy&&near(xy->fraction,.5)&&d3&&near(d3->fraction,.4),"horizontal progress ignores the height between chord and ground");
    const auto rise=path({{0,0,0},{0,0,10},{100,0,10}});const auto v=route_progress(rise,{0,0,5},1,true);
    require(v&&near(v->fraction,.5),"a segment without XY extent keeps 3D progress");
    require(!route_progress(chord,{500,0,0},0,true)&&!route_progress(chord,{std::nan(""),0,0},1,true),"invalid input stays invalid");
}
void coincident_spacing_is_an_invalid_budget() {
    // Samples closer than the 1e-9 m coincidence tolerance cannot be distinct; with a
    // 12 m horizon such a grid would never terminate, so it is rejected up front.
    Probe probe;const auto any=probe.probe();
    for(const auto& budget:{GroundBudget{1e-6,1e-12,160},GroundBudget{12,1e-9,160}})
        require(!plan_ground_coverage(straight(1000),1,budget,any)&&probe.rays.empty()&&probe.projections.empty(),
            "a spacing at or below the sample-coincidence tolerance is an invalid budget");
}
void unlimited_call_budget_still_stops_at_the_horizon() {
    Probe unlimited,standard;
    const auto u=plan(straight(1000),unlimited,{12,.2,std::numeric_limits<std::size_t>::max()});
    const auto s=plan(straight(1000),standard);
    require(u.limit==GroundLimit::Horizon&&near(u.metres,12)&&u.native_calls==s.native_calls,
        "a maximal call budget is bounded by the horizon, not overflowed");
}
}
void aligned_terraces_cannot_bypass_surface_verification() {
    const auto stairs=path({{0,0,0},{2000,0,200}});
    Probe coarse;coarse.surface=[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,std::round(p.x*.1/4)*4};};
    const auto c=plan(stairs,coarse,walkable(45));
    require(c.limit==GroundLimit::Horizon&&c.requires_surface,"a successful coarse slope check cannot certify hidden four-centimetre risers");
    auto fine=*surface_following_budget(walkable(45));fine.max_native_calls=2000;
    Probe detailed;detailed.surface=coarse.surface;
    const auto d=plan(stairs,detailed,fine);
    require(d.limit==GroundLimit::Surface&&!d.requires_surface&&!d.reaches_end,"fine sampling must reject the aligned staircase before authorizing its riser");
}
void capsule_floor_uses_live_support_without_skipping_obstacles() {
    const auto p=straight(6);
    auto budget=walkable(45);
    budget.follow_surface=true;
    Probe shape;
    shape.surface=[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,-8};};
    auto probe=shape.probe();
    require(!plan_ground_coverage(p,7,budget,probe),
        "coarse surface samples require continuous capsule sweeps");
    probe.swept_floor_tolerance=.45;
    const auto clear=plan_ground_coverage(p,7,budget,probe);
    require(clear&&clear->reaches_end&&clear->follow_surface&&shape.projections.size()<40,
        "a swept floor may sit below the baked mesh while supporting the full route");
    shape.ray=[](Vec3 a,Vec3 b)->std::optional<bool>{return a.x<250&&b.x>=250;};
    const auto wall=plan_ground_coverage(p,7,budget,probe);
    require(wall&&wall->limit==GroundLimit::Obstruction&&!wall->reaches_end&&wall->end_point.x<250,
        "the complete native route cannot override a blocking capsule sweep");
    shape.ray=[](Vec3,Vec3)->std::optional<bool>{return false;};
    shape.surface=[](Vec3 p)->std::optional<Vec3>{
        return p.x>=250?std::nullopt:std::optional<Vec3>{{p.x,p.y,-8}};
    };
    const auto gap=plan_ground_coverage(p,7,budget,probe);
    require(gap&&gap->limit==GroundLimit::Projection&&gap->end_point.x<250,
        "capsule coverage stops where the floor disappears");
    shape.surface=[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,-200};};
    const auto lower_floor=plan_ground_coverage(p,7,budget,probe);
    require(lower_floor&&lower_floor->limit==GroundLimit::Surface&&lower_floor->metres==0,
        "a different floor cannot verify the native route");
}
int main() {
    const std::array<std::pair<const char*,void(*)()>,27> tests{{
        {"long_paths_have_equal_near_prefix_cost",long_paths_have_equal_near_prefix_cost},
        {"short_path_reaches_true_end",short_path_reaches_true_end},
        {"dense_collinear_vertices_are_resampled_along_the_arc",dense_collinear_vertices_are_resampled_along_the_arc},
        {"corners_are_preserved",corners_are_preserved},
        {"budget_progression_never_rejects_dense_corners",budget_progression_never_rejects_dense_corners},
        {"near_and_far_failures_end_at_the_last_good_sample",near_and_far_failures_end_at_the_last_good_sample},
        {"unknowns_partial_and_nonfinite",unknowns_partial_and_nonfinite},
        {"exact_endpoint_and_duplicates",exact_endpoint_and_duplicates},
        {"progress_fractions_arc_and_wrong_index",progress_fractions_arc_and_wrong_index},
        {"horizon_is_measured_along_the_3d_arc",horizon_is_measured_along_the_3d_arc},
        {"native_tolerances_are_unchanged",native_tolerances_are_unchanged},
        {"walkable_ramps_follow_the_native_limit",walkable_ramps_follow_the_native_limit},
        {"unknown_or_wrong_slope_limits_never_authorize_a_ramp",unknown_or_wrong_slope_limits_never_authorize_a_ramp},
        {"slope_support_keeps_steps_gaps_drops_and_links",slope_support_keeps_steps_gaps_drops_and_links},
        {"flat_routes_are_judged_identically_with_a_walkable_limit",flat_routes_are_judged_identically_with_a_walkable_limit},
        {"long_ramp_renewal_reaches_the_top",long_ramp_renewal_reaches_the_top},
        {"surface_following_crosses_mid_segment_terrain_breaks",surface_following_crosses_mid_segment_terrain_breaks},
        {"surface_following_descends_and_rotates",surface_following_descends_and_rotates},
        {"surface_anchors_reject_wrong_heights_and_floors",surface_anchors_reject_wrong_heights_and_floors},
        {"surface_mode_rejects_steps_drops_gaps_and_links",surface_mode_rejects_steps_drops_gaps_and_links},
        {"surface_following_needs_a_verified_limit_and_fine_spacing",surface_following_needs_a_verified_limit_and_fine_spacing},
        {"surface_rolling_progress_and_call_bounds",surface_rolling_progress_and_call_bounds},
        {"horizontal_progress_follows_the_xy_trace",horizontal_progress_follows_the_xy_trace},
        {"aligned_terraces_cannot_bypass_surface_verification",aligned_terraces_cannot_bypass_surface_verification},
        {"capsule_floor_uses_live_support_without_skipping_obstacles",capsule_floor_uses_live_support_without_skipping_obstacles},
        {"coincident_spacing_is_an_invalid_budget",coincident_spacing_is_an_invalid_budget},
        {"unlimited_call_budget_still_stops_at_the_horizon",unlimited_call_budget_still_stops_at_the_horizon}}};
    int failures=0;
    for(const auto& [name,test]:tests) {
        std::cout<<"RUN  "<<name<<std::endl; // Flushed so a crash still names its group.
        try{test();std::cout<<"PASS "<<name<<std::endl;}
        catch(const std::exception& error){++failures;std::cout<<"FAIL "<<name<<": "<<error.what()<<std::endl;}
    }
    std::cout<<tests.size()-failures<<"/"<<tests.size()<<" ground coverage groups passed\n";
    return failures?1:0;
}
