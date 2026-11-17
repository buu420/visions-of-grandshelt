#include "resonance/GroundCoverage.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace resonance {
namespace {
// Ordinary-ground tolerances retained unchanged from the 0.2.8 whole-route checker (metres).
constexpr double horizontal_tolerance=.02,vertical_tolerance=.025,adjacent_height=.025;
constexpr double link_horizontal=.01,link_vertical=.025;
// Corruption bound on the copied native point array; not a distance or density policy.
constexpr std::size_t native_point_bound=4096;
// A ray spans at most this many sample intervals of one exactly straight run.
constexpr std::size_t samples_per_ray=5;
constexpr double same_arc=1e-9;    // metres: coincident samples
constexpr double collinear=1e-9;   // sin(angle) at or below this is one straight line
constexpr double degrees_per_radian=180/3.14159265358979323846;
// Surface following: at most 0.02 m between samples, and at most 0.0225 m of walkable rise
// per spacing, so a step over 0.025 m within 1.1 spacings always exceeds the native grade.
constexpr double fine_spacing=.02,fine_rise=.0225,anchor_gap=.1;
double surface_spacing(double slope) {
    const double rise=std::tan(slope/degrees_per_radian);
    return rise>0?std::min(fine_spacing,fine_rise/rise):fine_spacing;
}
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Vec3 minus(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
double norm(Vec3 v){return std::hypot(v.x,v.y,v.z);}
bool valid_geometry(const PathSnapshot& p) {
    if(p.points.size()<2||p.points.size()>native_point_bound||!std::isfinite(p.world_to_metres)||p.world_to_metres<=0)return false;
    return std::all_of(p.points.begin(),p.points.end(),finite);
}
// arc[i]: metres along the 3D route from points[0] to points[i].
std::optional<std::vector<double>> cumulative(const PathSnapshot& p) {
    std::vector<double> arc(p.points.size());
    for(std::size_t i=1;i<p.points.size();++i) {
        arc[i]=arc[i-1]+norm(minus(p.points[i],p.points[i-1]))/p.world_to_metres;
        if(!std::isfinite(arc[i]))return std::nullopt;
    }
    return arc;
}
// Endpoint-index location of an arc position; a vertex reports the segment it ends.
RouteLocation locate(const std::vector<double>& arc,double s) {
    if(s<=0)return {1,0};
    if(s>=arc.back())return {arc.size()-1,1};
    const auto k=static_cast<std::size_t>(std::lower_bound(arc.begin()+1,arc.end(),s)-arc.begin());
    return {k,std::clamp((s-arc[k-1])/(arc[k]-arc[k-1]),0.,1.)};
}
Vec3 point_at(const PathSnapshot& p,RouteLocation at) {
    const auto& a=p.points[at.segment-1];const auto& b=p.points[at.segment];
    if(at.fraction<=0)return a;
    if(at.fraction>=1)return b;
    return {a.x+(b.x-a.x)*at.fraction,a.y+(b.y-a.y)*at.fraction,a.z+(b.z-a.z)*at.fraction};
}
// Arc positions where the route genuinely changes direction (duplicates skipped;
// exactly collinear same-direction vertices are not corners).
std::vector<double> corners(const PathSnapshot& p,const std::vector<double>& arc) {
    std::vector<double> result;
    std::optional<Vec3> incoming;
    for(std::size_t k=1;k<p.points.size();++k) {
        const auto d=minus(p.points[k],p.points[k-1]);const auto length=norm(d);
        if(length/p.world_to_metres<=same_arc)continue;
        const Vec3 u{d.x/length,d.y/length,d.z/length};
        if(incoming) {
            const auto dot=incoming->x*u.x+incoming->y*u.y+incoming->z*u.z;
            const Vec3 cross{incoming->y*u.z-incoming->z*u.y,incoming->z*u.x-incoming->x*u.z,incoming->x*u.y-incoming->y*u.x};
            if(dot<=0||norm(cross)>collinear)result.push_back(arc[k-1]);
        }
        incoming=u;
    }
    return result;
}
// corner: a ray boundary (coarse mode) or a native height anchor (surface mode).
struct Sample {double arc;bool corner;};
}
std::optional<double> route_arc_metres(const PathSnapshot& p,RouteLocation at) {
    if(!valid_geometry(p)||at.segment<1||at.segment>=p.points.size()||!std::isfinite(at.fraction)||at.fraction<0||at.fraction>1)
        return std::nullopt;
    const auto arc=cumulative(p);if(!arc)return std::nullopt;
    return (*arc)[at.segment-1]+at.fraction*((*arc)[at.segment]-(*arc)[at.segment-1]);
}
std::optional<double> horizontal_arc_metres(const PathSnapshot& path,RouteLocation at) {
    if(!std::isfinite(path.world_to_metres)||path.world_to_metres<=0||
       at.segment<1||at.segment>=path.points.size()||!std::isfinite(at.fraction)||at.fraction<0||at.fraction>1)return {};
    double metres=0;
    for(std::size_t i=1;i<=at.segment;++i) {
        const auto a=path.points[i-1],b=path.points[i];
        metres+=std::hypot(b.x-a.x,b.y-a.y)/path.world_to_metres*(i==at.segment?at.fraction:1.);
    }
    return std::isfinite(metres)?std::optional<double>{metres}:std::nullopt;
}
std::optional<GroundBudget> surface_following_budget(GroundBudget budget) {
    const auto slope=budget.walkable_slope_degrees;
    if(!slope||!std::isfinite(*slope)||*slope<0||*slope>=90||!std::isfinite(budget.horizon_metres)||budget.horizon_metres<=0||
       !std::isfinite(budget.spacing_metres)||budget.spacing_metres<=same_arc||budget.max_native_calls==0)return std::nullopt;
    budget.spacing_metres=std::min(budget.spacing_metres,surface_spacing(*slope));
    if(budget.spacing_metres<=same_arc)return std::nullopt;
    budget.follow_surface=true;
    return budget;
}
std::optional<RouteLocation> route_progress(const PathSnapshot& p,Vec3 position,std::size_t next_waypoint,bool horizontal) {
    if(!valid_geometry(p)||!finite(position)||next_waypoint<1||next_waypoint>=p.points.size())return std::nullopt;
    const auto& a=p.points[next_waypoint-1];const auto d=minus(p.points[next_waypoint],a);
    // Along the XY trace when asked, unless the segment has no horizontal extent.
    const bool xy=horizontal&&(d.x!=0||d.y!=0);
    const auto squared=d.x*d.x+d.y*d.y+(xy?0:d.z*d.z);
    if(squared==0)return RouteLocation{next_waypoint,1};
    const auto from=minus(position,a);
    const auto t=(from.x*d.x+from.y*d.y+(xy?0:from.z*d.z))/squared;
    if(!std::isfinite(t))return std::nullopt;
    return RouteLocation{next_waypoint,std::clamp(t,0.,1.)};
}
std::optional<GroundCoverage> plan_ground_coverage(const PathSnapshot& p,std::uint64_t path_id,const GroundBudget& budget,const GroundProbe& probe) {
    if(!p.valid||p.partial||!valid_geometry(p)||!probe.blocked||!probe.project)return std::nullopt;
    // A spacing within the coincidence tolerance could never yield distinct samples.
    if(!std::isfinite(budget.horizon_metres)||budget.horizon_metres<=0||!std::isfinite(budget.spacing_metres)||
       budget.spacing_metres<=same_arc||budget.max_native_calls==0)return std::nullopt;
    const auto slope=budget.walkable_slope_degrees;
    if(slope&&(!std::isfinite(*slope)||*slope<0||*slope>=90))return std::nullopt;
    // Surface following needs the verified limit and spacing fine enough for it.
    const auto swept_floor=probe.swept_floor_tolerance;
    if(swept_floor&&(!budget.follow_surface||!std::isfinite(*swept_floor)||*swept_floor<=0))return std::nullopt;
    if(budget.follow_surface&&(!slope||budget.spacing_metres>
        (swept_floor?.2:surface_spacing(*slope))*(1+1e-12)))return std::nullopt;
    const auto arc=cumulative(p);if(!arc)return std::nullopt;
    const double scale=p.world_to_metres,total=arc->back();
    // The real end is checkable only when it lies inside this update's lookahead.
    const bool full=total<=budget.horizon_metres+same_arc;
    double window=full?total:budget.horizon_metres;
    // A vertical link is never ordinary XY walking: verification stops at its start.
    bool link=false;
    for(std::size_t k=1;k<p.points.size()&&(*arc)[k-1]<window;++k) {
        const auto d=minus(p.points[k],p.points[k-1]);
        if(std::hypot(d.x,d.y)/scale<link_horizontal&&std::abs(d.z)/scale>link_vertical) {window=(*arc)[k-1];link=true;break;}
    }
    // Arc-ordered samples: the spacing grid across collinear vertices, every genuine
    // corner and the checked extent. Never more than the budget can probe.
    std::vector<Sample> samples;
    const auto turns=corners(p,*arc);
    const std::size_t cap=std::max(budget.max_native_calls,budget.max_native_calls+1); // calls+1, saturating
    if(!budget.follow_surface) {
        std::size_t grid=0,turn=0;
        const auto push=[&](double s,bool corner) {
            if(!samples.empty()&&s-samples.back().arc<=same_arc) {samples.back().corner|=corner;if(corner)samples.back().arc=s;return;}
            samples.push_back({s,corner});
        };
        while(samples.size()<cap) {
            const double g=static_cast<double>(grid)*budget.spacing_metres;
            const double t=turn<turns.size()?turns[turn]:std::numeric_limits<double>::infinity();
            const double next=std::min({g,t,window});
            if(next==t&&t<window) {push(t,true);++turn;if(g-t<=same_arc)++grid;continue;}
            if(g<window-same_arc) {push(g,false);++grid;if(t-g<=same_arc){samples.back().corner=true;samples.back().arc=t;++turn;}continue;}
            push(window,false);break;
        }
    } else {
        // The fine grid, every genuine corner and the window end. Grid points within a
        // tenth of a spacing of a corner or the window end are dropped: no interval is
        // that short (projected heights are floats) or longer than 1.1 spacings. Height
        // anchors are the start, every corner, a vertical link's start and the real end.
        const double spacing=budget.spacing_metres,gap=anchor_gap*spacing;
        samples.push_back({0,true});
        std::size_t grid=1,turn=0;
        while(samples.size()<cap) {
            const bool corner=turn<turns.size()&&turns[turn]<window;
            const double stop=corner?turns[turn]:window;
            for(double g=static_cast<double>(grid)*spacing;samples.size()<cap&&g<stop-gap;g=static_cast<double>(++grid)*spacing)
                if(g>samples.back().arc+gap)samples.push_back({g,false});
            if(samples.size()>=cap)break;
            const bool anchor=corner||link||full;
            if(stop>samples.back().arc+same_arc)samples.push_back({stop,anchor});
            else samples.back().corner|=anchor;
            if(!corner)break;
            ++turn;
        }
    }
    std::size_t calls=0;
    bool slope_relaxed=false;
    struct Verified {Vec3 route,surface;};
    std::optional<Verified> previous;
    const auto location=[&](std::size_t i){return locate(*arc,samples[i].arc);};
    const auto finish=[&](std::optional<std::size_t> good,GroundLimit limit)->GroundCoverage {
        GroundCoverage c;c.path_id=path_id;c.native_calls=calls;c.limit=limit;c.follow_surface=budget.follow_surface;
        c.requires_surface=slope_relaxed&&!budget.follow_surface;
        const bool final=good&&*good+1==samples.size()&&full&&!link&&limit==GroundLimit::None;
        c.end=!good?RouteLocation{1,0}:final?RouteLocation{p.points.size()-1,1}:location(*good);
        c.end_point=point_at(p,c.end);
        c.metres=route_arc_metres(p,c.end).value_or(0);
        c.reaches_end=final;
        if(swept_floor&&good)c.swept_ground=SweptGroundPrefix{c.end,*swept_floor};
        return c;
    };
    const auto project=[&](std::size_t i)->std::optional<GroundLimit> {
        if(calls>=budget.max_native_calls)return GroundLimit::Budget;
        ++calls;
        const auto point=point_at(p,location(i));
        const auto surface=probe.project(point);
        if(!surface)return GroundLimit::Projection;
        if(!finite(*surface))return GroundLimit::Unreadable;
        if(std::hypot(surface->x-point.x,surface->y-point.y)/scale>horizontal_tolerance||
           std::abs(surface->z-point.z)/scale>vertical_tolerance)return GroundLimit::Surface;
        if(previous) {
            // Flat rule: sampled ground height changes at most 0.025 m between samples.
            const double rise=(surface->z-previous->surface.z)/scale;
            bool ordinary=std::abs(rise)<=adjacent_height;
            if(slope) {
                const double route_rise=(point.z-previous->route.z)/scale;
                const double run=std::hypot(point.x-previous->route.x,point.y-previous->route.y)/scale;
                // A larger change is a slope only where the surface rises with the native route.
                ordinary=ordinary||std::abs(rise-route_rise)<=adjacent_height;
                // Every route grade must be within the verified native limit, except a
                // near-vertical route step no taller than the flat tolerance.
                const bool small_step=run<link_horizontal&&std::abs(route_rise)<=adjacent_height;
                if(!small_step&&std::atan2(std::abs(route_rise),run)*degrees_per_radian>*slope)ordinary=false;
            }
            if(!ordinary)return GroundLimit::Surface;
            if(slope&&std::abs(rise)>adjacent_height)slope_relaxed=true;
        }
        previous=Verified{point,*surface};return std::nullopt;
    };
    const auto outcome=[&]{
        const bool reached_window=samples.back().arc>=window-same_arc;
        return link?GroundLimit::VerticalLink:!reached_window?GroundLimit::Budget:full?GroundLimit::None:GroundLimit::Horizon;
    };
    if(budget.follow_surface) {
        // Each sample is projected at the route's XY from the last verified surface height,
        // so a native chord cutting a slope change is never checked as if it were ground.
        std::vector<double> leg_xy(p.points.size());
        for(std::size_t k=1;k<p.points.size();++k)leg_xy[k]=std::hypot(p.points[k].x-p.points[k-1].x,p.points[k].y-p.points[k-1].y)/scale;
        std::optional<Vec3> before;Vec3 route_before{};
        for(std::size_t m=0;m<samples.size();++m) {
            const auto fail=[&](GroundLimit why){return finish(m?std::optional<std::size_t>{m-1}:std::nullopt,why);};
            const auto at=location(m);const auto point=point_at(p,at);
            if(calls>=budget.max_native_calls)return fail(GroundLimit::Budget);
            ++calls;
            const Vec3 query{point.x,point.y,before?before->z:point.z};
            const auto surface=probe.project(query);
            if(!surface)return fail(GroundLimit::Projection);
            if(!finite(*surface))return fail(GroundLimit::Unreadable);
            const double anchor_tolerance=swept_floor.value_or(vertical_tolerance);
            if(std::hypot(surface->x-query.x,surface->y-query.y)/scale>horizontal_tolerance||
               (samples[m].corner&&std::abs(surface->z-point.z)/scale>anchor_tolerance))return fail(GroundLimit::Surface);
            if(before) {
                // The followed surface's own grade; a native jog without horizontal extent
                // keeps the flat height tolerance. Equality allows only rounding error.
                const double rise=std::abs(surface->z-before->z)/scale;
                const double run=std::hypot(point.x-route_before.x,point.y-route_before.y)/scale;
                const bool steep=swept_floor?rise>run*std::tan(*slope/degrees_per_radian)+.01:
                    std::atan2(rise,run)*degrees_per_radian>*slope+1e-9;
                const bool native_step=steep&&swept_floor&&rise<=*swept_floor&&probe.native_step&&probe.native_step(*before,*surface);
                if(leg_xy[at.segment]<link_horizontal?rise>adjacent_height:steep&&!native_step)return fail(GroundLimit::Surface);
                if(calls>=budget.max_native_calls)return fail(GroundLimit::Budget);
                ++calls;
                const auto blocked=probe.blocked(*before,*surface);
                if(!blocked)return fail(GroundLimit::Unreadable);
                if(*blocked)return fail(GroundLimit::Obstruction);
            }
            before=*surface;route_before=point;
        }
        return finish(samples.size()-1,outcome());
    }
    if(const auto failed=project(0))return finish(std::nullopt,*failed);
    std::size_t i=0;
    while(i+1<samples.size()) {
        // One ray per straight chunk; a corner always ends the chunk.
        std::size_t j=i+1;
        while(j+1<samples.size()&&j-i<samples_per_ray&&!samples[j].corner)++j;
        if(calls>=budget.max_native_calls)return finish(i,GroundLimit::Budget);
        ++calls;
        const auto blocked=probe.blocked(point_at(p,location(i)),point_at(p,location(j)));
        if(!blocked)return finish(i,GroundLimit::Unreadable);
        if(*blocked)return finish(i,GroundLimit::Obstruction);
        for(std::size_t m=i+1;m<=j;++m)if(const auto failed=project(m))return finish(m-1,*failed);
        i=j;
    }
    return finish(samples.size()-1,outcome());
}
}
