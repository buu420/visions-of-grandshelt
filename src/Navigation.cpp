#include "resonance/Navigation.hpp"
#include "resonance/Localization.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <string_view>
#include <tuple>
#include <utility>
namespace resonance {
std::optional<WalkingYawSource> walking_yaw_source(const WalkingBasisFlags& flags) {
    if(!flags.internal||!flags.external||!flags.force_internal||!flags.vertical||*flags.vertical)return {};
    if(*flags.internal||*flags.force_internal)return WalkingYawSource::UnitCamera;
    return *flags.external?WalkingYawSource::ViewTarget:WalkingYawSource::Control;
}
std::optional<WalkingAxes> walking_axes(double yaw_degrees) {
    if(!std::isfinite(yaw_degrees))return {};
    // Reduce before converting degrees, so even a finite large angle cannot
    // overflow and an equivalent native heading retains the same basis.
    const double yaw=std::remainder(yaw_degrees,360.)*(3.14159265358979323846/180.);
    const double s=std::sin(yaw),c=std::cos(yaw);
    return WalkingAxes{{-s,c,0},{c,s,0}};
}
std::optional<Vec3> walking_stick(Vec3 direction,const WalkingAxes& axes) {
    if(!std::isfinite(direction.z)||std::abs(direction.z)>1e-8)return {};
    const auto right=direction.x*axes.right.x+direction.y*axes.right.y;
    const auto up=direction.x*axes.up.x+direction.y*axes.up.y;
    const auto magnitude=std::hypot(right,up);
    if(!std::isfinite(magnitude)||std::abs(magnitude-1.)>1e-5)return {};
    return Vec3{right/magnitude,up/magnitude,0.};
}
std::optional<WalkingGroundPolicy> walking_ground_policy(double speed_world,double velocity_world,
        double braking_world,double scale) {
    if(!std::isfinite(speed_world)||speed_world<=0||!std::isfinite(velocity_world)||velocity_world<0||
       !std::isfinite(braking_world)||braking_world<=0||!std::isfinite(scale)||scale<=0)return {};
    const double speed=velocity_world/scale,maximum=std::max(speed_world,velocity_world)/scale;
    const double braking=braking_world/scale;
    // Ignore positive friction: deceleration alone gives a conservative stopping
    // distance. Include reaction and position margins without changing physics.
    const auto stopping_margin=[&](double pace){return 1.5+pace*.1+(pace/braking)*pace*.5;};
    const double margin=stopping_margin(speed);
    // Plan for full speed. The native walking pulse limits acceleration to
    // the verified distance, so stopping the current walk uses live speed.
    const double horizon=std::max(12.,maximum*navigation_path_max_age+2.+stopping_margin(maximum));
    const double calls=std::ceil(horizon/.2)*2.+32.;
    if(!std::isfinite(margin)||!std::isfinite(horizon)||!std::isfinite(calls)||
       calls>=static_cast<double>(std::numeric_limits<std::size_t>::max()))return {};
    return WalkingGroundPolicy{GroundBudget{horizon,.2,std::max<std::size_t>(160,static_cast<std::size_t>(calls))},margin};
}
std::optional<GroundBudget> ground_window_budget(const PathSnapshot& path,GroundBudget budget) {
    if(!path.valid||path.partial||path.points.size()<2||path.points.size()>4096||
       !std::isfinite(path.world_to_metres)||path.world_to_metres<=0||
       !std::isfinite(budget.horizon_metres)||budget.horizon_metres<=0||
       !std::isfinite(budget.spacing_metres)||budget.spacing_metres<=0||!budget.max_native_calls)return {};
    std::size_t edges=0;double arc=0;
    for(std::size_t i=1;i<path.points.size()&&arc<budget.horizon_metres;++i) {
        const auto a=path.points[i-1],b=path.points[i];
        for(double v:{a.x,a.y,a.z,b.x,b.y,b.z})if(!std::isfinite(v))return {};
        arc+=std::hypot(b.x-a.x,b.y-a.y,b.z-a.z)/path.world_to_metres;
        if(!std::isfinite(arc))return {};
        ++edges;
    }
    // At worst every local vertex is a genuine corner and every sample needs
    // both a ray and a projection. Distant vertices never fund or limit this
    // window. Dense nearby geometry cannot leave a stationary budget deadlock.
    const double calls=2.*(std::ceil(budget.horizon_metres/budget.spacing_metres)+static_cast<double>(edges)+4.);
    if(!std::isfinite(calls)||calls>=static_cast<double>(std::numeric_limits<std::size_t>::max()))return {};
    budget.max_native_calls=std::max(budget.max_native_calls,static_cast<std::size_t>(calls));
    return budget;
}
std::optional<GroundBudget> surface_window_budget(const PathSnapshot& path,GroundBudget budget) {
    if(!budget.follow_surface||!ground_window_budget(path,budget))return {};
    double remaining=budget.horizon_metres,native_arc=0;
    for(std::size_t i=1;i<path.points.size()&&remaining>0;++i) {
        const auto a=path.points[i-1],b=path.points[i];
        const double xy=std::hypot(b.x-a.x,b.y-a.y)/path.world_to_metres;
        const double arc=std::hypot(b.x-a.x,b.y-a.y,b.z-a.z)/path.world_to_metres;
        if(!std::isfinite(xy)||!std::isfinite(arc))return {};
        if(xy>0&&xy>=remaining){native_arc+=arc*(remaining/xy);remaining=0;}
        else {native_arc+=arc;remaining-=xy;}
        if(!std::isfinite(native_arc))return {};
    }
    budget.horizon_metres=std::max(budget.horizon_metres,native_arc);
    return ground_window_budget(path,budget);
}
std::optional<float> capsule_input_scale(double clearance,double speed,double acceleration,double braking,double tick,
        std::optional<WalkingAnalogSpeed> analog,float native_gain) {
    for(double value:{clearance,acceleration,braking,tick})
        if(!std::isfinite(value)||value<=0)return {};
    if(!std::isfinite(speed)||speed<0||!std::isfinite(native_gain)||native_gain<=0||native_gain>1.f)return {};
    const auto bt=braking*tick;
    const auto limit=std::sqrt(bt*bt+2.*braking*clearance)-bt;
    if(!std::isfinite(limit))return {};
    auto scale=(limit-speed)/(acceleration*tick);
    // With no native overrides, the character component limits accelerated
    // speed to MaxSpeed times input magnitude or the live analog minimum.
    // Either that cap or the acceleration bound can fit the verified runway.
    if(analog&&speed<=limit&&std::isfinite(analog->maximum)&&analog->maximum>0&&
       std::isfinite(analog->minimum)&&analog->minimum>=0&&analog->minimum<=limit)
        scale=std::max(scale,limit/analog->maximum);
    return static_cast<float>(std::clamp(scale,0.,static_cast<double>(native_gain)));
}
bool navmesh_supports_capsule(double config_radius,double config_height,double radius,double height,
        double capsule_radius,double half_height) {
    for(double value:{config_radius,config_height,radius,height,capsule_radius,half_height})
        if(!std::isfinite(value)||value<=0)return false;
    const auto tolerance=[](double value){return std::max(.001,std::abs(value)*.00001);};
    return std::abs(config_radius-radius)<=tolerance(radius)&&std::abs(config_height-height)<=tolerance(height)&&
        capsule_radius<=radius+tolerance(radius)&&2*half_height<=height+tolerance(height);
}
namespace {
constexpr double max_path_age=navigation_path_max_age;
constexpr double corner_radius=.15;
constexpr double floor_anchor_radius=.05;
constexpr double passed_corridor=.3;
constexpr double arrival_radius=.5;
constexpr double origin_radius=2;
constexpr double progress_distance=.1;
constexpr double blocked_seconds=1.5;
constexpr double corner_tolerance=.3;      // Same next route point across replans, metres.
constexpr double endpoint_tolerance=1;     // Endpoint jitter or slow target drift, metres.
constexpr double rejoin_vertex_tolerance=.01; // Avoid a synthetic edge beside the original native vertex.
constexpr double orientation_degrees=30;   // Control-relative change worth a quiet update.
constexpr double orientation_clearance=1;  // No orientation updates this close to the next point.
constexpr double quiet_interval=1.5;       // Minimum spacing of quiet updates, seconds.
constexpr double pi=3.14159265358979323846;
bool finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
std::optional<Vec3> checked_walking_leg(Vec3 from,Vec3 to,double scale,double floor_z,const GroundProbe& probe) {
    if(!finite(from)||!finite(to)||!std::isfinite(scale)||scale<=0||
       !std::isfinite(floor_z)||floor_z<=0||floor_z>1||!probe.project||!probe.blocked)return {};
    const auto run=std::hypot(to.x-from.x,to.y-from.y);
    const auto count=std::ceil(run/(.2*scale));
    if(!std::isfinite(count)||count>=static_cast<double>(std::numeric_limits<std::size_t>::max()))return {};
    if(count==0)return from;
    const auto samples=static_cast<std::size_t>(count);
    const auto grade=std::sqrt(1-floor_z*floor_z)/floor_z;
    const auto rise=grade*(run/count)+scale*.01; // Allow contact/float noise, not a step across a gap.
    Vec3 previous=from;
    for(std::size_t i=1;i<=samples;++i) {
        const auto t=static_cast<double>(i)/count;
        const Vec3 sample{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,previous.z};
        const auto floor=probe.project(sample);
        if(!floor||!finite(*floor)||std::hypot(floor->x-sample.x,floor->y-sample.y)>scale*.01)return {};
        const auto height=std::abs(floor->z-previous.z);
        if(height>rise&&(!probe.swept_floor_tolerance||height>*probe.swept_floor_tolerance*scale||
           !probe.native_step||!probe.native_step(previous,*floor)))return {};
        const auto blocked=probe.blocked(previous,*floor);
        if(!blocked||*blocked)return {};
        previous=*floor;
    }
    return previous;
}
std::optional<std::vector<Vec3>> checked_walking_path(Vec3 start,std::span<const Vec3> approaches,
        double stride,std::size_t expansion_limit,const std::function<std::optional<Vec3>(Vec3,Vec3)>& leg,
        const std::function<bool(Vec3)>& arrived) {
    if(!finite(start)||!std::isfinite(stride)||stride<=0||approaches.empty()||!leg||!arrived||
       !std::all_of(approaches.begin(),approaches.end(),finite))return {};
    const auto distance=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);};
    const auto distance_to_target=[&](Vec3 feet) {
        double best=std::numeric_limits<double>::infinity();
        for(const auto goal:approaches)best=std::min(best,distance(feet,goal));
        return best;
    };
    // Include height so a traversable upper level can reuse an XY cell.
    using Cell=std::tuple<int,int,double>;
    using Pending=std::pair<double,std::size_t>;
    struct Node {Vec3 feet;Cell cell;double cost;std::size_t parent;};
    std::vector<Node> nodes{{start,{0,0,0},0,0}};
    std::map<Cell,std::size_t> best{{nodes.front().cell,0}};
    std::priority_queue<Pending,std::vector<Pending>,std::greater<>> pending;
    pending.emplace(distance_to_target(start),0);
    const auto finish=[&](std::size_t index,Vec3 feet) {
        std::vector<Vec3> path{feet};
        if(distance(feet,nodes[index].feet)>0.)path.push_back(nodes[index].feet);
        while(index!=0) {
            index=nodes[index].parent;
            path.push_back(nodes[index].feet);
        }
        std::reverse(path.begin(),path.end());
        return path;
    };
    constexpr std::array offsets{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1},
        std::pair{1,1},std::pair{1,-1},std::pair{-1,1},std::pair{-1,-1}};
    while(!pending.empty()&&expansion_limit) {
        const auto index=pending.top().second;
        pending.pop();
        const auto current=nodes[index];
        if(best.at(current.cell)!=index)continue;
        --expansion_limit;
        if(index&&arrived(current.feet))return finish(index,current.feet);
        for(const auto goal:approaches) {
            const auto gap=distance(current.feet,goal);
            if(gap<=stride*.001||gap>2.*stride)continue;
            const auto feet=leg(current.feet,goal);
            if(feet&&finite(*feet)&&arrived(*feet))return finish(index,*feet);
        }
        const auto [column,row,height]=current.cell;
        for(const auto [dx,dy]:offsets) {
            const auto x=column+dx,y=row+dy;
            const Vec3 sample{start.x+x*stride,start.y+y*stride,current.feet.z};
            const auto feet=leg(current.feet,sample);
            if(!feet||!finite(*feet))continue;
            const Cell cell{x,y,std::round((feet->z-start.z)/(.5*stride))};
            const auto cost=current.cost+distance(current.feet,*feet);
            const auto old=best.find(cell);
            if(old!=best.end()&&nodes[old->second].cost<=cost)continue;
            const auto next=nodes.size();
            nodes.push_back({*feet,cell,cost,index});
            best.insert_or_assign(cell,next);
            pending.emplace(cost+distance_to_target(*feet),next);
        }
    }
    return {};
}
std::optional<WalkingBypass> checked_walking_bypass(const PathSnapshot& route,RouteLocation at,
        Vec3 from,Vec3 normal,double radius,double floor_z,const GroundProbe& probe,double rejoin_lookahead_metres) {
    const auto scale=route.world_to_metres;
    if(!route_arc_metres(route,at)||!finite(from)||!finite(normal)||
       !std::isfinite(radius)||radius<=0||!probe.swept_floor_tolerance)return {};
    const auto a=route.points[at.segment-1],b=route.points[at.segment];
    const auto t=at.fraction;
    const Vec3 origin{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
    const auto segment=at.segment+(t==1.);
    if(segment>=route.points.size())return {};
    const auto normal_length=std::hypot(normal.x,normal.y);
    const Vec3 away=normal_length>=.1?Vec3{normal.x/normal_length,normal.y/normal_length,0}:Vec3{};
    const auto leg_run=std::hypot(route.points[segment].x-origin.x,route.points[segment].y-origin.y);
    const auto lookahead=std::isfinite(rejoin_lookahead_metres)?std::max(0.,rejoin_lookahead_metres)*scale:0.;
    double previous_extended=6.*radius;
    for(const auto [advance,across_corner]:{std::pair{2.,false},{4.,false},{6.,false},{2.,true},{4.,true},{6.,true},
                                           {10.,true},{16.,true},{24.,true},{32.,true},{48.,true}}) {
        if(!across_corner&&leg_run<radius)continue;
        const bool extended=advance>6.;
        double remaining=advance*radius;
        if(extended) {
            remaining=std::min(remaining,lookahead);
            if(remaining<=previous_extended)break;
            previous_extended=remaining;
        }
        // Follow the baked legs to a rejoin point. Classify vertical links
        // from the route geometry, independently of the live floor height.
        double fraction=0;
        auto join_segment=segment;
        auto begin=origin;
        for(;join_segment<route.points.size();++join_segment) {
            const auto end=route.points[join_segment];
            const auto run=std::hypot(end.x-begin.x,end.y-begin.y);
            if(std::abs(end.z-begin.z)>std::max(run,scale*.025))return {};
            if(run>0&&(!across_corner||remaining<=run||join_segment+1==route.points.size())) {
                fraction=std::min(1.,remaining/run);
                break;
            }
            remaining-=run;
            begin=end;
        }
        if(join_segment>=route.points.size()||(!extended&&across_corner&&join_segment==segment&&leg_run>=radius))continue;
        const auto end=route.points[join_segment];
        if((1.-fraction)*std::hypot(end.x-begin.x,end.y-begin.y)<rejoin_vertex_tolerance*scale)fraction=1.;
        const Vec3 join{begin.x+(end.x-begin.x)*fraction,begin.y+(end.y-begin.y)*fraction,
            begin.z+(end.z-begin.z)*fraction};
        if(lookahead>0.) {
            // Reject joins that are already occupied before testing every pivot.
            // A successful lane still needs both moving sweeps below.
            const auto support=probe.project(join);
            if(!support||probe.blocked(*support,*support)==true)continue;
        }
        const auto dx=join.x-from.x,dy=join.y-from.y,run=std::hypot(dx,dy);
        if(run<scale*.01)continue;
        const Vec3 side{-dy/run,dx/run,0};
        const std::array directions{away,side,Vec3{-side.x,-side.y,0}};
        for(std::size_t i=0;i<directions.size();++i) {
            const auto nx=directions[i].x,ny=directions[i].y;
            if((nx==0.&&ny==0.)||(i&&nx*away.x+ny*away.y>.95))continue;
            // The outgoing chord may continue towards the obstruction.
            // Place the pivot on the clear side of the supported pose.
            const auto retreat=std::max(0.,-dx*nx-dy*ny);
            for(const auto offset:{.5,1.,1.5}) {
                const auto shift=radius*offset;
                // A corner can block the first diagonal. Try stepping
                // sideways on the current floor before heading around it.
                const std::array pivots{Vec3{join.x+nx*(shift+retreat),join.y+ny*(shift+retreat),join.z},
                    Vec3{from.x+nx*shift,from.y+ny*shift,from.z}};
                for(const auto candidate:pivots) {
                    const auto pivot=checked_walking_leg(from,candidate,scale,floor_z,probe);
                    if(!pivot)continue;
                    const auto tail=checked_walking_leg(*pivot,join,scale,floor_z,probe);
                    if(!tail||std::abs(tail->z-join.z)>*probe.swept_floor_tolerance*scale)continue;
                    PathSnapshot bypass=route;
                    auto& points=bypass.points;
                    points.resize(segment);
                    if(std::hypot(from.x-points.back().x,from.y-points.back().y)>scale*.05)points.push_back(from);
                    points.push_back(*pivot);
                    const auto rejoin=points.size();
                    if(fraction<1.)points.push_back(*tail);
                    points.insert(points.end(),route.points.begin()+join_segment,route.points.end());
                    return WalkingBypass{std::move(bypass),rejoin};
                }
            }
        }
    }
    return {};
}
std::optional<WalkingDetourEntry> walking_detour_entry(const PathSnapshot& route,RouteLocation at,
        double backtrack,std::size_t preserve_through,const GroundProbe& probe) {
    if(!route_arc_metres(route,at)||!std::isfinite(backtrack)||backtrack<=0||
       !probe.project||!probe.swept_floor_tolerance)return {};
    const auto scale=route.world_to_metres;
    for(auto segment=at.segment;segment>preserve_through;--segment) {
        const auto a=route.points[segment-1],b=route.points[segment];
        const auto fraction=segment==at.segment?at.fraction:1.;
        const auto run=std::hypot(b.x-a.x,b.y-a.y);
        const auto length=run*fraction;
        if(std::abs(b.z-a.z)*fraction>std::max(length,scale*.025))return {};
        if(backtrack<=length) {
            const auto t=fraction-backtrack/run;
            const Vec3 anchor{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
            const auto feet=probe.project(anchor);
            if(!feet||!finite(*feet)||std::hypot(feet->x-anchor.x,feet->y-anchor.y)>scale*.01||
               std::abs(feet->z-anchor.z)>*probe.swept_floor_tolerance*scale)return {};
            return WalkingDetourEntry{{segment,t},*feet};
        }
        backtrack-=length;
    }
    return {};
}
std::optional<WalkingBypass> checked_walking_corridor(const PathSnapshot& route,RouteLocation at,
        Vec3 from,Vec3 normal,double radius,double floor_z,const GroundProbe& probe,
        double lookahead_metres,double rejoin_after_metres) {
    const auto scale=route.world_to_metres;
    const auto lookahead=lookahead_metres*scale;
    const auto rejoin_after=rejoin_after_metres*scale;
    if(!route_arc_metres(route,at)||!finite(from)||!finite(normal)||
       !std::isfinite(radius)||radius<=0||!std::isfinite(lookahead)||
       !std::isfinite(rejoin_after)||rejoin_after<0||lookahead<=rejoin_after||
       !probe.swept_floor_tolerance)return {};
    const auto start_segment=at.segment+(at.fraction==1.);
    if(start_segment>=route.points.size())return {};
    const auto a=route.points[at.segment-1],b=route.points[at.segment];
    const auto mix=[](Vec3 a,Vec3 b,double t) {
        return Vec3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
    };
    const auto horizontal=[](Vec3 a,Vec3 b){return std::hypot(b.x-a.x,b.y-a.y);};
    auto start=mix(a,b,at.fraction);
    const auto stride=std::clamp(2.*radius,.15*scale,.6*scale);
    const auto max_edge=std::max(4.*radius,2.*stride);
    struct Node { Vec3 feet; double cost; std::size_t previous; };
    std::vector<Node> nodes{{from,0,0}};
    std::vector<std::size_t> frontier{0};
    const auto connect=[&](Vec3 target)->std::optional<std::size_t> {
        std::vector<std::pair<double,std::size_t>> choices;
        for(const auto i:frontier) {
            const auto run=horizontal(nodes[i].feet,target);
            if(run<=max_edge)choices.emplace_back(nodes[i].cost+run,i);
        }
        std::sort(choices.begin(),choices.end());
        for(const auto [cost,i]:choices) {
            const auto feet=checked_walking_leg(nodes[i].feet,target,scale,floor_z,probe);
            if(feet) {
                nodes.push_back({*feet,cost,i});
                return nodes.size()-1;
            }
        }
        return {};
    };
    const auto lanes=[&](Vec3 center,Vec3 side) {
        std::vector<std::size_t> result;
        for(const double width:{.75,-.75,1.5,-1.5,2.25,-2.25,3.,-3.,4.5,-4.5,6.,-6.}) {
            const auto offset=width*radius;
            const auto lane=connect({center.x+side.x*offset,center.y+side.y*offset,center.z});
            if(lane)result.push_back(*lane);
        }
        return result;
    };
    const auto finish=[&](std::size_t join,std::size_t segment,double fraction) {
        std::vector<Vec3> chain{nodes[join].feet};
        for(auto i=nodes[join].previous;i;i=nodes[i].previous)chain.push_back(nodes[i].feet);
        chain.push_back(from);
        std::reverse(chain.begin(),chain.end());
        auto bypass=route;
        auto& points=bypass.points;
        points.resize(start_segment);
        if(horizontal(from,points.back())>scale*.05)points.push_back(from);
        for(std::size_t i=0;i+1<chain.size();) {
            auto next=std::min(i+4,chain.size()-1);
            // Reduce short turns only when the longer chord has its own proof.
            for(;next>i+1;--next) {
                const auto shortcut=checked_walking_leg(chain[i],chain[next],scale,floor_z,probe);
                if(shortcut&&std::abs(shortcut->z-chain[next].z)<=scale*.025)break;
            }
            points.push_back(chain[next]);
            i=next;
        }
        const auto rejoin=points.size()-1;
        if(fraction==1.)points.back()=route.points[segment];
        points.insert(points.end(),route.points.begin()+segment+(fraction==1.),route.points.end());
        return WalkingBypass{std::move(bypass),rejoin};
    };
    double before=0,next=std::min(stride,lookahead),side_sign=0;
    for(auto segment=start_segment;segment<route.points.size()&&!frontier.empty();++segment) {
        const auto end=route.points[segment];
        const auto run=horizontal(start,end);
        if(std::abs(end.z-start.z)>std::max(run,scale*.025))break;
        if(run>0) {
            const Vec3 left{(start.y-end.y)/run,(end.x-start.x)/run,0};
            if(side_sign==0) {
                side_sign=left.x*normal.x+left.y*normal.y>=0?1.:-1.;
                // Some obstructions require a sidestep before any progress.
                auto sidesteps=lanes(from,{left.x*side_sign,left.y*side_sign,0});
                frontier.insert(frontier.end(),sidesteps.begin(),sidesteps.end());
            }
            const auto end_arc=before+run;
            const Vec3 side{left.x*side_sign,left.y*side_sign,0};
            while(next<=end_arc||segment+1==route.points.size()) {
                auto fraction=std::min(1.,(next-before)/run);
                if(fraction<1.&&(1.-fraction)*run<rejoin_vertex_tolerance*scale) {
                    // Check the leg to the native vertex itself. Appending it
                    // after a nearly coincident live-height sample can look
                    // like a vertical traversal even on continuous floor.
                    if(end_arc>lookahead)return {};
                    fraction=1.;
                }
                const auto center=mix(start,end,fraction);
                const auto center_node=connect(center);
                if(before+fraction*run>rejoin_after&&center_node&&
                   std::abs(nodes[*center_node].feet.z-center.z)<=*probe.swept_floor_tolerance*scale)
                    return finish(*center_node,segment,fraction);
                if(next>=lookahead||fraction==1.&&segment+1==route.points.size())return {};
                frontier=lanes(center,side);
                if(center_node)frontier.push_back(*center_node);
                if(frontier.empty())return {};
                next=std::min(next+stride,lookahead);
            }
            before=end_arc;
        }
        start=end;
    }
    return {};
}
namespace {
Vec3 delta(Vec3 a,Vec3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
double length(Vec3 p) {return std::hypot(p.x,p.y,p.z);}
double distance(Vec3 a,Vec3 b) {return length(delta(a,b));}
bool same_points(const std::vector<Vec3>& a,const std::vector<Vec3>& b) {
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](Vec3 x,Vec3 y) {
        return x.x==y.x&&x.y==y.y&&x.z==y.z;
    });
}
// Full route geometry is independent of the automatic ground prefix. A future
// vertical link must not prevent movement along verified ordinary ground now.
bool geometry(const NavigationFrame& f) {
    const auto& p=f.path;
    if(!finite(f.position)||!p.valid||p.partial||p.points.size()<2||p.points.size()>4096||
       !std::isfinite(p.world_to_metres)||p.world_to_metres<=0||!finite(p.right)||!finite(p.up)) return false;
    const auto r=std::hypot(p.right.x,p.right.y),u=std::hypot(p.up.x,p.up.y);
    if(!std::isfinite(r)||!std::isfinite(u)||r<.01||u<.01||
       std::abs((p.right.x/r)*(p.up.x/u)+(p.right.y/r)*(p.up.y/u))>.1) return false;
    for(const auto point:p.points) if(!finite(point)) return false;
    for(std::size_t i=1;i<p.points.size();++i) {
        const auto d=delta(p.points[i],p.points[i-1]);
        const auto metres=length(d)/p.world_to_metres;
        if(!finite(d)||!std::isfinite(metres)) return false;
    }
    return true;
}
bool ground_matches(const NavigationFrame& f) {
    if(!f.ground||!f.path_id||f.ground->path_id!=f.path_id||
       !std::isfinite(f.ground_stop_margin_metres)||f.ground_stop_margin_metres<0)return false;
    const auto& g=*f.ground;const auto& points=f.path.points;
    if(g.end.segment<1||g.end.segment>=points.size()||!std::isfinite(g.end.fraction)||g.end.fraction<0||g.end.fraction>1)return false;
    if(g.swept_ground) {
        const auto& swept=*g.swept_ground;
        if(!g.follow_surface||swept.end.segment<1||swept.end.segment>g.end.segment||
           !std::isfinite(swept.end.fraction)||swept.end.fraction<0||swept.end.fraction>1||
           (swept.end.segment==g.end.segment&&swept.end.fraction>g.end.fraction)||
           !std::isfinite(swept.max_anchor_offset_metres)||swept.max_anchor_offset_metres<=0)return false;
    }
    const auto arc=route_arc_metres(f.path,g.end);
    if(!arc||!finite(g.end_point)||!std::isfinite(g.metres)||g.metres<0||std::abs(*arc-g.metres)>.001)return false;
    const auto a=points[g.end.segment-1],b=points[g.end.segment];
    const auto t=g.end.fraction;
    const Vec3 expected{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
    if(std::hypot(g.end_point.x-expected.x,g.end_point.y-expected.y,g.end_point.z-expected.z)/f.path.world_to_metres>.001)return false;
    const bool endpoint=g.end.segment+1==points.size()&&g.end.fraction==1.;
    return g.reaches_end==endpoint&&(!g.reaches_end||g.limit==GroundLimit::None);
}
std::string ground_stop_reason(GroundLimit limit) {
    switch(limit) {
    case GroundLimit::Obstruction:return tr("Automatic walking stopped. The ground ahead is blocked.");
    case GroundLimit::VerticalLink:return tr("Automatic walking stopped. The next section requires a different movement action. Spoken guidance is available.");
    case GroundLimit::Surface:return tr("Automatic walking stopped. The ground ahead requires manual movement.");
    case GroundLimit::Projection:case GroundLimit::Unreadable:case GroundLimit::Mesh:
        return tr("Automatic walking stopped. The ground ahead could not be verified.");
    default:return tr("Automatic walking stopped. The ground check needs refreshing.");
    }
}
double segment_distance(Vec3 position,Vec3 a,Vec3 b,bool horizontal=false) {
    if(horizontal)position.z=a.z=b.z=0;
    const auto d=delta(b,a);const auto n=length(d);
    if(n==0) return distance(position,a);
    const Vec3 unit{d.x/n,d.y/n,d.z/n};const auto from=delta(position,a);
    const auto along=std::clamp(from.x*unit.x+from.y*unit.y+from.z*unit.z,0.,n);
    return distance(position,{a.x+unit.x*along,a.y+unit.y*along,a.z+unit.z*along});
}
bool passed(Vec3 position,Vec3 a,Vec3 b,double scale) {
    const auto d=delta(b,a);const auto n=std::hypot(d.x,d.y);
    if(n<1e-8*scale) return false;
    const auto from=delta(position,a);
    const auto along=from.x*(d.x/n)+from.y*(d.y/n);
    const auto side=std::abs(from.x*(d.y/n)-from.y*(d.x/n));
    return along>=n&&side/scale<=passed_corridor&&std::abs(position.z-b.z)/scale<=arrival_radius;
}
bool at_swept_floor_anchor(Vec3 position,Vec3 point,std::size_t waypoint,double scale,const GroundCoverage* ground) {
    if(!ground||!ground->swept_ground)return false;
    const auto& swept=*ground->swept_ground;
    const bool covered=swept.end.segment>waypoint||(swept.end.segment==waypoint&&swept.end.fraction==1.);
    return covered&&std::hypot(point.x-position.x,point.y-position.y)/scale<=floor_anchor_radius&&
        std::abs(point.z-position.z)/scale<=swept.max_anchor_offset_metres;
}
// Camera-relative bearing in degrees of an XY leg, as describe_route projects it.
std::optional<double> bearing(Vec3 d,const PathSnapshot& p) {
    const auto horizontal=std::hypot(d.x,d.y);
    if(horizontal/p.world_to_metres<.05||horizontal<.1*std::abs(d.z)) return std::nullopt;
    const auto r=std::hypot(p.right.x,p.right.y),u=std::hypot(p.up.x,p.up.y);
    const auto x=d.x*(p.right.x/r)+d.y*(p.right.y/r);
    const auto y=d.x*(p.up.x/u)+d.y*(p.up.y/u);
    return std::atan2(y,x)*180/pi;
}
int octant(double degrees) {return static_cast<int>(std::llround(degrees/45)+8)%8;}
double angular_difference(double a,double b) {
    auto d=std::fmod(std::abs(a-b),360.);return d>180?360-d:d;
}
std::string concise_guidance(const PathSnapshot& p) {
    return describe_route(p,2).value_or("");
}
}
unsigned route_beacon_interval_ms(double remaining_metres) {
    if(!std::isfinite(remaining_metres))return 1200;
    const auto metres=std::max(0.,remaining_metres);
    return 220+static_cast<unsigned>(std::lround(980*(metres/(metres+6))));
}
NavigationUpdate NavigationFollower::start(NavigationMode requested,const NavigationFrame& f) {
    if(requested==NavigationMode::Off) return stop();
    const bool previous_auto=mode_==NavigationMode::Automatic;
    stop("");
    mode_=requested;world_=f.world;player_=f.player;target_=f.target;
    progress_position_=f.position;progress_sample_=f.sample_seconds;
    auto out=observe(f);
    // The newly requested mode has not supplied input yet if activation failed.
    out.stopped_auto=previous_auto;
    if(out.mode!=NavigationMode::Off) {
        const auto message=f.target_label.empty()?
            tr(requested==NavigationMode::Automatic?"Automatic walking on.":
                out.beacon?"Navigation beacon on.":"Spoken guidance on."):
            trf(requested==NavigationMode::Automatic?"Automatic walking on. {0}.":
                out.beacon?"Navigation beacon on. {0}.":"Spoken guidance on. {0}.",{f.target_label});
        if(!out.speech.empty())out.speech.front().text=message+" "+out.speech.front().text;
        else if(out.beacon||out.floor_turn_waypoint)out.speech.push_back({message,true});
    }
    return out;
}
NavigationUpdate NavigationFollower::toggle(NavigationMode requested,const NavigationFrame& f) {
    if(requested==mode_) return stop(tr(requested==NavigationMode::Automatic?"Automatic walking off.":"Guidance off."));
    return start(requested,f);
}
std::vector<SpeechEvent> navigation_speech_for_output(const NavigationUpdate& update,bool passive) {
    auto events=update.speech;
    if(passive&&update.mode==NavigationMode::Off)
        for(auto& event:events)event.interrupt=false;
    return events;
}
NavigationUpdate NavigationFollower::stop(std::string reason) {
    NavigationUpdate out;out.stopped_auto=mode_==NavigationMode::Automatic;
    if(mode_!=NavigationMode::Off&&!reason.empty()) out.speech.push_back({std::move(reason),true});
    mode_=NavigationMode::Off;world_.clear();player_.clear();target_.clear();
    announced_=false;announced_points_.clear();announced_angle_.reset();announced_sample_=0;
    beacon_audio_=false;
    waiting_for_route_=false;retried_blockage_=false;
    points_.clear();waypoint_=1;path_sample_=-1;last_sample_=-1;progress_sample_=0;progress_position_={};path_id_=0;
    return out;
}
void NavigationFollower::pause() {
    waiting_for_route_=mode_!=NavigationMode::Off;
    retried_blockage_=false;
}
NavigationUpdate NavigationFollower::wait_for_route(const std::string& reason) {
    NavigationUpdate out;out.mode=mode_;
    if(!waiting_for_route_)out.speech.push_back({trf("Waiting for a walking route. {0}",{reason}),false});
    waiting_for_route_=true;
    return out;
}
NavigationUpdate NavigationFollower::arrive(std::string reason) {
    auto update=stop(std::move(reason));
    update.reached_target=true;
    return update;
}
std::optional<std::pair<Vec3,Vec3>> NavigationFollower::leg() const {
    if(waiting_for_route_||waypoint_==0||waypoint_>=points_.size())return {};
    return std::pair{points_[waypoint_-1],points_[waypoint_]};
}
std::size_t NavigationFollower::advance_waypoint(Vec3 position,double scale,const GroundCoverage* ground) const {
    auto next=waypoint_;
    while(next+1<points_.size()) {
        if(distance(points_[next],points_[next-1])/scale>=.001&&
           distance(position,points_[next])/scale>corner_radius&&
           !at_swept_floor_anchor(position,points_[next],next,scale,ground)&&
           !passed(position,points_[next-1],points_[next],scale))break;
        ++next;
    }
    return next;
}
std::optional<WalkingBypass> NavigationFollower::remaining_bypass(const PathSnapshot& path,std::uint64_t path_id,
        Vec3 position,std::size_t rejoin,bool advance,const GroundCoverage* ground) const {
    const auto scale=path.world_to_metres;
    if(mode_!=NavigationMode::Automatic||waiting_for_route_||!path_id||path_id!=path_id_||
       !leg()||rejoin>=points_.size()||!finite(position)||!std::isfinite(scale)||scale<=0||
       !same_points(points_,path.points))return {};
    const auto next=advance?advance_waypoint(position,scale,ground&&ground->path_id==path_id?ground:nullptr):waypoint_;
    if(next>rejoin||segment_distance(position,points_[next-1],points_[next],true)>scale*.35)return {};
    WalkingBypass remaining{path,rejoin-next+1};
    auto& points=remaining.path.points;
    points.erase(points.begin(),points.begin()+(next-1));
    points.front()=position;
    return remaining;
}
NavigationUpdate NavigationFollower::observe(const NavigationFrame& f,bool explicit_status) {
    if(mode_==NavigationMode::Off) {
        NavigationUpdate out;if(explicit_status) out.speech.push_back({tr("Navigation is off."),true});return out;
    }
    if(f.world!=world_||f.player!=player_) return stop(tr("Navigation stopped. Area or player changed."));
    if(f.target!=target_) return stop(tr("Navigation stopped. Target changed."));
    if(f.world.empty()||f.player.empty()||f.target.empty()) return stop(tr("Navigation unavailable. Select a target in the field."));
    if(!f.foreground) return stop(tr("Navigation stopped. Game focus lost."));
    if(!f.field_eligible) return stop(tr("Navigation stopped. Field controls unavailable."));
    if(!std::isfinite(f.arrival_metres)||f.arrival_metres<.02||f.arrival_metres>arrival_radius)
        return stop(tr("Navigation stopped. The destination arrival distance could not be verified."));
    if(!f.target_available)return stop(f.target_unavailable_reason.empty()?tr("Navigation stopped. Target is no longer available."):
        trf("Navigation stopped. {0}",{tr(f.target_unavailable_reason)}));
    if(mode_==NavigationMode::Automatic&&f.manual_input) return stop(tr("Automatic walking cancelled by movement."));
    if(f.activation_reached)return arrive(f.arrival_message.empty()?tr("At the target's activation area."):f.arrival_message);
    if(f.interaction_available)
        return arrive(f.target_label.empty()?tr("At target. Interaction available."):
            trf("At {0}. Interaction available.",{f.target_label}));
    if(!f.path.valid&&!f.route_unavailable_reason.empty()) {
        const auto reason=tr(f.route_unavailable_reason);
        return points_.empty()?stop(trf("Navigation unavailable. {0}",{reason})):wait_for_route(reason);
    }
    const bool automatic=mode_==NavigationMode::Automatic;
    if(automatic&&(!f.walking_path||!f.ground))return stop(f.ground_unavailable_reason.empty()?
        tr("Navigation stopped. Verified ground ahead is unavailable."):trf("Navigation stopped. {0}",{tr(f.ground_unavailable_reason)}));
    if(automatic&&f.ground->requires_surface)return stop(tr("Automatic walking stopped. The slope needs further surface verification."));
    if(!std::isfinite(f.sample_seconds)||!std::isfinite(f.path_sample_seconds)||f.sample_seconds<0||
       f.path_sample_seconds<0||f.path_sample_seconds>f.sample_seconds+1e-6||
       f.sample_seconds-f.path_sample_seconds>max_path_age||f.sample_seconds<last_sample_||
       f.path_sample_seconds<path_sample_||!geometry(f))
        return stop(tr("Navigation stopped. A fresh complete walking route is unavailable."));
    if(automatic&&!ground_matches(f))return stop(tr("Navigation stopped. The ground check does not match the current route."));
    const double scale=f.path.world_to_metres;
    const bool new_path=waiting_for_route_||f.path_id!=path_id_||!same_points(points_,f.path.points);
    if(new_path) {
        const auto origin_distance=distance(f.position,f.path.points.front())/scale;
        if(!std::isfinite(origin_distance)||origin_distance>origin_radius)
            return stop(tr("Navigation stopped. Walking route is too far from the player."));
        points_=f.path.points;waypoint_=1;path_id_=f.path_id;
    } else {
        double closest=std::numeric_limits<double>::infinity();
        for(std::size_t i=std::max<std::size_t>(1,waypoint_);i<points_.size();++i)
            closest=std::min(closest,segment_distance(f.position,points_[i-1],points_[i],automatic&&f.ground->follow_surface)/scale);
        if(!std::isfinite(closest)||closest>origin_radius)
            return {.mode=mode_,.replan=NavigationReplan::OffRoute};
    }
    const bool recovered=waiting_for_route_;
    if(recovered) {
        waiting_for_route_=false;
        announced_=false;
    }
    path_sample_=f.path_sample_seconds;
    last_sample_=f.sample_seconds;
    const auto previous_waypoint=waypoint_;
    const auto* ground=automatic?&*f.ground:nullptr;
    const auto ordinary_next=advance_waypoint(f.position,scale,nullptr);
    const auto next=ground&&ground->swept_ground?advance_waypoint(f.position,scale,ground):ordinary_next;
    auto turn_limit=points_.size()-1;
    if(automatic&&f.check_route_turns) {
        if(next>waypoint_&&!f.hold_floor_turn)return {.mode=mode_,.floor_turn_waypoint=waypoint_+1};
        turn_limit=waypoint_;
    } else if(automatic&&f.bypass_rejoin&&waypoint_<=f.bypass_rejoin) {
        // Turning early can cut a detour corner, even with clearance along
        // both legs. The rolling ground check must prove the new heading
        // from the actual feet before advancing past a detour waypoint.
        const auto first_detour_point=f.bypass_rejoin>2?f.bypass_rejoin-2:1;
        turn_limit=std::max(waypoint_,first_detour_point);
    }
    const auto allowed_turn=std::min(next,turn_limit);
    const auto ordinary_turn=std::min(ordinary_next,turn_limit);
    if(allowed_turn>ordinary_turn&&!f.hold_floor_turn)return {.mode=mode_,.floor_turn_waypoint=allowed_turn};
    waypoint_=ordinary_turn;
    PathSnapshot remaining=f.path;remaining.points.clear();remaining.points.push_back(f.position);
    remaining.points.insert(remaining.points.end(),points_.begin()+waypoint_,points_.end());
    double remaining_metres=0;
    for(std::size_t i=1;i<remaining.points.size();++i) remaining_metres+=distance(remaining.points[i],remaining.points[i-1])/scale;
    if(!std::isfinite(remaining_metres)) return stop(tr("Navigation stopped. Walking route unavailable."));
    if(remaining_metres<=f.arrival_metres)
        return arrive(f.arrival_message.empty()?tr("Walking route endpoint reached."):f.arrival_message);
    std::optional<double> movement_ground_metres;
    if(automatic&&!f.ground->reaches_end) {
        const auto location=route_progress(f.path,f.position,waypoint_,f.ground->follow_surface);
        const auto progress=location?(f.ground->follow_surface?horizontal_arc_metres(f.path,*location):route_arc_metres(f.path,*location)):std::nullopt;
        const auto end=f.ground->follow_surface?horizontal_arc_metres(f.path,f.ground->end):route_arc_metres(f.path,f.ground->end);
        if(!progress||!end||*end-*progress<=f.ground_stop_margin_metres) {
            auto out=stop(ground_stop_reason(f.ground->limit));
            out.stopped_for_projection=f.ground->limit==GroundLimit::Projection;
            return out;
        }
        movement_ground_metres=*end-*progress;
    }
    if(mode_==NavigationMode::Automatic) {
        auto moved=distance(f.position,progress_position_)/scale;
        auto minimum_progress=std::min(progress_distance,f.arrival_metres);
        const auto waypoint=points_[waypoint_];
        const auto remaining_to_turn=[&](Vec3 position) {
            const auto d=delta(position,waypoint);
            return ground->follow_surface?std::hypot(d.x,d.y):length(d);
        };
        if(remaining_to_turn(f.position)/scale<=arrival_radius) {
            // Near a turn or interaction, even a small step toward the
            // waypoint is useful. Sideways movement cannot reset the timer.
            moved=(remaining_to_turn(progress_position_)-remaining_to_turn(f.position))/scale;
            minimum_progress=.002;
        }
        if(moved>=minimum_progress||(!new_path&&waypoint_!=previous_waypoint)) {
            retried_blockage_=false;
            progress_position_=f.position;progress_sample_=f.sample_seconds;
        } else if(f.beginning_movement||recovered) {
            progress_position_=f.position;progress_sample_=f.sample_seconds;
        } else if(f.sample_seconds-progress_sample_>=blocked_seconds) {
            if(retried_blockage_)return stop(tr("Automatic walking stopped. Route is blocked."));
            retried_blockage_=true;
            progress_position_=f.position;progress_sample_=f.sample_seconds;
            return {.mode=mode_,.replan=NavigationReplan::Blocked};
        }
    }
    NavigationUpdate out;out.mode=mode_;
    const auto angle=bearing(delta(points_[waypoint_],f.position),f.path);
    if(f.use_beacon&&angle&&std::isfinite(*angle))
        out.beacon=RouteBeaconCue{std::remainder(90.-*angle,360.),remaining_metres};
    if(beacon_audio_&&!f.use_beacon)announced_=false;
    beacon_audio_=f.use_beacon;
    if(!out.beacon||explicit_status) {
        // Next two route points ahead of the player, independent of replan identity.
        std::vector<RoutePoint> ahead;
        for(std::size_t i=waypoint_;i<points_.size()&&ahead.size()<2;++i) ahead.push_back({points_[i],i+1==points_.size()});
        const auto same=[&](const RoutePoint& a,const RoutePoint& b) {
            if(a.endpoint!=b.endpoint) return false;
            return distance(a.point,b.point)/scale<=(a.endpoint?endpoint_tolerance:corner_tolerance);
        };
        bool left_spoken_route=false;
        if(!automatic&&new_path&&announced_&&!announced_points_.empty()) {
            auto from=announced_origin_;
            double closest=std::numeric_limits<double>::infinity();
            for(const auto& point:announced_points_) {
                closest=std::min(closest,segment_distance(f.position,from,point.point,true));
                from=point.point;
            }
            left_spoken_route=closest/scale>origin_radius;
        }
        enum class Change { None, Turn, Quiet } change=Change::None;
        if(explicit_status||!announced_||left_spoken_route) change=Change::Turn;
        else if(announced_points_.empty()||!same(announced_points_.front(),ahead.front())) {
            // A changed next corner is a real turn; an endpoint that merely moved is not.
            change=announced_points_.empty()||!announced_points_.front().endpoint||!ahead.front().endpoint?Change::Turn:Change::Quiet;
        } else if(announced_points_.size()!=ahead.size()||(ahead.size()>1&&!same(announced_points_[1],ahead[1]))) {
            change=Change::Quiet;
        } else if(angle&&announced_angle_&&distance(f.position,ahead.front().point)/scale>=orientation_clearance&&
                  angular_difference(*angle,*announced_angle_)>=orientation_degrees&&octant(*angle)!=octant(*announced_angle_)) {
            change=Change::Quiet;
        }
        if(change==Change::Quiet&&f.sample_seconds-announced_sample_<quiet_interval) change=Change::None;
        if(change!=Change::None) {
            auto text=concise_guidance(remaining);
            if(text.empty()) return stop(tr("Navigation stopped. Route guidance unavailable."));
            out.speech.push_back({std::move(text),change==Change::Turn});
            announced_=true;announced_origin_=f.position;announced_points_=std::move(ahead);announced_angle_=angle;announced_sample_=f.sample_seconds;
        }
    }
    if(mode_==NavigationMode::Automatic) {
        const auto d=delta(points_[waypoint_],f.position);const auto horizontal=std::hypot(d.x,d.y);
        const bool held_floor_turn=waypoint_+1<points_.size()&&
            at_swept_floor_anchor(f.position,points_[waypoint_],waypoint_,scale,ground);
        // At the held pivot, let the next ground refresh prove the turn.
        if(horizontal/scale<1e-8&&(held_floor_turn||(next>waypoint_&&std::abs(d.z)/scale<=corner_radius)))return out;
        if(!std::isfinite(horizontal)||horizontal/scale<1e-8||
           (!held_floor_turn&&horizontal/scale<floor_anchor_radius&&std::abs(d.z)/scale>.2))
            return stop(tr("Navigation stopped. This route requires unsupported movement."));
        out.movement=Vec3{d.x/horizontal,d.y/horizontal,0};
        out.movement_waypoint=points_[waypoint_];
        out.movement_ground_metres=movement_ground_metres;
    }
    return out;
}
}
