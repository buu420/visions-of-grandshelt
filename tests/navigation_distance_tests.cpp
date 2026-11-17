#include "resonance/Navigation.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace resonance;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string speech(const NavigationUpdate& out){std::string s;for(const auto& e:out.speech)s+=e.text;return s;}
void ground_from_live_state(NavigationFrame& f) {
    GroundBudget budget;
#ifndef RESONANCE_BASELINE
    const auto policy=walking_ground_policy(1300,750,2048,100);
    require(policy.has_value(),"the live walking policy must be readable");
    f.ground_stop_margin_metres=policy->stop_margin_metres;budget=policy->budget;
    const auto fitted=ground_window_budget(f.path,budget);require(fitted.has_value(),"nearby ground work must fit the route geometry");budget=*fitted;
#endif
    GroundProbe probe{[](Vec3,Vec3)->std::optional<bool>{return false;},[](Vec3 p)->std::optional<Vec3>{return p;}};
    f.ground=plan_ground_coverage(f.path,f.path_id,budget,probe);
}
NavigationFrame field(double metres=0,double now=0,std::uint64_t id=1) {
    NavigationFrame f;f.world="field";f.player="player";f.target="target";f.target_label="Destination";
    f.position={metres*100,0,0};f.path={{{metres*100,0,0},{100000,0,0}},{1,0,0},{0,1,0},100,true,false};
    f.sample_seconds=f.path_sample_seconds=now;f.field_eligible=f.foreground=f.walking_path=f.target_available=true;f.path_id=id;
    ground_from_live_state(f);return f;
}
void long_journey() {
    NavigationFollower nav;auto f=field();const auto original=f.path.points;
    auto out=nav.start(NavigationMode::Automatic,f);
    require(out.mode==NavigationMode::Automatic&&out.movement,"a full one-kilometre native route must allow walking along its verified prefix");
    require(f.path.points.size()==2&&f.path.points.back().x==100000&&f.ground&&f.ground->end_point.x<100000,
        "local ground verification must leave the full destination and native route intact");
    require(speech(out).find("1000 metres")!=std::string::npos,"guidance must describe the full target distance, not the ground window");
    for(int i=1;i<500;++i){f=field(i*2.,i*.25,i+1);out=nav.observe(f);require(out.mode==NavigationMode::Automatic&&out.movement,
        "rolling verification must continue through the whole long journey without a cumulative-distance cutoff");}
    f=field(1000,125,501);out=nav.observe(f);
    require(out.mode==NavigationMode::Off&&!out.movement&&speech(out).find("endpoint")!=std::string::npos,
        "arrival is the real native endpoint, never the temporary ground boundary");
}
void coverage_guards() {
    for(int test=0;test<9;++test) {
        auto f=field();
        if(test==0)f.ground.reset();
        if(test==1)f.ground->path_id=99;
        if(test==2)f.path_id=0;
        if(test==3)f.ground->end_point.x+=100;
        if(test==4)f.ground->metres+=1;
        if(test==5)f.ground->end.segment=0;
        if(test==6)f.ground->end.fraction=2;
        if(test==7)f.ground->reaches_end=true;
        if(test==8){f.sample_seconds=.7;f.path_sample_seconds=0;}
        NavigationFollower nav;const auto out=nav.start(NavigationMode::Automatic,f);
        require(out.mode==NavigationMode::Off&&!out.movement,"missing, foreign, malformed or stale coverage cannot authorize an automatic step");
    }
    for(bool beyond_prefix:{false,true}) {
        NavigationFollower nav;auto f=field();nav.start(NavigationMode::Automatic,f);
        const double position=f.ground->metres+.1-(beyond_prefix?0.:f.ground_stop_margin_metres);
        f.position={position*100,0,0};f.sample_seconds=.2;
        const auto out=nav.observe(f);
        require(out.mode==NavigationMode::Off&&!out.movement&&speech(out).find("endpoint")==std::string::npos,
            "cached proof must not be consumed past its stop margin or mistaken for arrival");
    }
    auto f=field();f.ground.reset();f.walking_path=false;
    NavigationFollower manual;const auto spoken=manual.start(NavigationMode::Manual,f);
    require(spoken.mode==NavigationMode::Manual&&!spoken.movement,"manual guidance must not require an automatic ground window");
    NavigationFollower automatic;require(!automatic.start(NavigationMode::Automatic,f).movement,
        "a cached manual route cannot authorize automatic movement without a fresh ground check");
}
void distant_link() {
    auto f=field();f.path.points={{0,0,0},{30000,0,0},{30000,0,100},{30100,0,100}};
    GroundProbe probe{[](Vec3,Vec3)->std::optional<bool>{return false;},[](Vec3 p)->std::optional<Vec3>{return p;}};
    f.ground=plan_ground_coverage(f.path,f.path_id,GroundBudget{},probe);
    NavigationFollower nav;auto out=nav.start(NavigationMode::Automatic,f);
    require(out.mode==NavigationMode::Automatic&&out.movement,"a special link300metres ahead cannot block current ordinary walking");
    f.position={29880,0,0};f.path.points.front()=f.position;f.path_id=2;f.sample_seconds=f.path_sample_seconds=10;
    f.ground=plan_ground_coverage(f.path,f.path_id,GroundBudget{},probe);out=nav.observe(f);
    require(out.mode==NavigationMode::Off&&!out.movement&&speech(out).find("different movement")!=std::string::npos,
        "the follower must stop with the real nearby link reason before entering unsupported traversal");
}
#ifndef RESONANCE_BASELINE
void dense_live_route() {
    for(double destination:{100000.,10000000.}) {
        auto f=field();f.path.points={{0,0,0}};
        for(int i=1;i<=400;++i)f.path.points.push_back({i*5.,i%2?2.5:-2.5,0});
        f.path.points.push_back({destination,0,0});ground_from_live_state(f);
        require(f.ground&&f.ground->metres+1e-6>=f.ground_stop_margin_metres+13*.25+2,
            "dense nearby corners must be funded through the live stopping margin and next normal renewal");
        NavigationFollower nav;const auto out=nav.start(NavigationMode::Automatic,f);
        require(out.mode==NavigationMode::Automatic&&out.movement,"a dense clear native prefix cannot become a stationary budget deadlock");
    }
}
void speed_policy() {
    const auto idle=walking_ground_policy(1300,0,2048,100);
    require(idle&&idle->stop_margin_metres==1.5&&idle->budget.horizon_metres>16,
        "a stationary player can use a short verified prefix while the planner looks ahead for full speed");
    const auto normal=walking_ground_policy(1300,750,2048,100);
    const auto full_speed=walking_ground_policy(1300,1300,2048,100);
    require(normal&&full_speed&&full_speed->stop_margin_metres>6&&
        normal->stop_margin_metres<full_speed->stop_margin_metres&&
        normal->budget.horizon_metres>=full_speed->stop_margin_metres+13*.6+2,
        "lookahead must support full speed while braking uses the current velocity");
    require(normal->budget.max_native_calls>160,"per-update work allowance must support the live speed, independent of journey length");
    const auto impulse=walking_ground_policy(1300,2000,2048,100);
    require(impulse&&impulse->stop_margin_metres>normal->stop_margin_metres,"current velocity above the retained speed must increase the stopping margin");
    require(!walking_ground_policy(1300,0,0,100)&&!walking_ground_policy(1300,0,2048,0),"unreadable braking or world scale cannot guess coverage");
}
void walking_bypass_at_a_baked_corner() {
    auto f=field();
    f.path.points={{-100,1800,0},{12,2208,0},{144,2369,0},{500,2700,0}};
    const auto wall=[](Vec3 from,Vec3 to)->std::optional<bool> {
        // The physical corner protrudes into the diagonal shown by the bake.
        const auto contact=[](Vec3 p){return p.x>22&&p.y>=2195&&p.y<=2244;};
        return contact(from)||contact(to);
    };
    GroundProbe probe{wall,[](Vec3 p)->std::optional<Vec3>{return p;},.45};
    const GroundBudget budget{12,.2,160,44.765,true};
    const auto blocked=plan_ground_coverage(f.path,f.path_id,budget,probe);
    require(blocked&&blocked->limit==GroundLimit::Obstruction,"the baked path really cuts the physical corner");
    const auto bypass=checked_walking_bypass(f.path,blocked->end,blocked->end_point,{-1,0,0},56,.71,probe);
    require(bypass&&bypass->path.points.size()>f.path.points.size(),"clear floor beyond the corner allows a bypass");
    require(bypass->path.points.back().x==500&&bypass->path.points.back().y==2700,"the native route keeps its destination");
    const auto repaired=plan_ground_coverage(bypass->path,f.path_id,budget,probe);
    require(repaired&&repaired->reaches_end,"every part of the bypass must pass full route verification");
    probe.project=[](Vec3 p)->std::optional<Vec3>{return p.y>2212?std::nullopt:std::optional<Vec3>{p};};
    require(!checked_walking_bypass(f.path,blocked->end,blocked->end_point,{-1,0,0},56,.71,probe),
        "a lateral retry cannot cross a gap or leave the selected walking mesh");
}
void walking_bypass_at_a_short_downhill_corner() {
    auto f=field();
    f.path.points={{282,2290,0},{44,2248,0},{12,2208,0},{-82,1837,0}};
    const auto wall=[](Vec3 from,Vec3 to)->std::optional<bool> {
        const auto contact=[](Vec3 p){return p.x>=22&&p.x<=52&&p.y<2244&&p.y>=2180;};
        return contact(from)||contact(to);
    };
    GroundProbe probe{wall,[](Vec3 p)->std::optional<Vec3>{return p;},.45};
    const GroundBudget budget{12,.2,160,44.765,true};
    const auto blocked=plan_ground_coverage(f.path,f.path_id,budget,probe);
    require(blocked&&blocked->limit==GroundLimit::Obstruction,"the downhill capsule reaches the corner face");
    const auto bypass=checked_walking_bypass(f.path,blocked->end,blocked->end_point,{0,1,0},56,.71,probe);
    require(bypass.has_value(),"a short obstructed leg can rejoin past the next native corner");
    require(bypass->path.points[bypass->rejoin].y<2208,"the marked rejoin is beyond the obstructed corner");
    const auto repaired=plan_ground_coverage(bypass->path,f.path_id,budget,probe);
    require(repaired&&repaired->reaches_end,"the downhill detour and unchanged destination pass the capsule check");
    f.path.points.insert(f.path.points.begin()+3,{12,2208,250});
    require(!checked_walking_bypass(f.path,blocked->end,blocked->end_point,{0,1,0},56,.71,probe),
        "a detour cannot skip a vertical native link while finding a rejoin point");
}
void walking_bypass_at_a_floor_edge() {
    auto f=field();
    f.path.points={{-200,0,0},{0,0,0},{180,60,0},{500,60,0}};
    const auto overlap=[](Vec3 p) {return p.x>0&&p.x<100&&p.y>=-8;};
    const GroundBudget budget{12,.2,160,44.765,true};
    for(const bool reports_contact:{true,false}) {
        const GroundProbe probe{
            [&](Vec3 a,Vec3 b)->std::optional<bool>{return reports_contact&&(overlap(a)||overlap(b));},
            [&](Vec3 p)->std::optional<Vec3>{
                if(!reports_contact&&overlap(p))return {};
                return p;
            },.45};
        const auto blocked=plan_ground_coverage(f.path,f.path_id,budget,probe);
        require(blocked&&blocked->limit==(reports_contact?GroundLimit::Obstruction:GroundLimit::Projection),
            "the baked chord enters the same unusable corner with or without a capsule contact normal");
        const auto normal=reports_contact?Vec3{0,-.423583,.905857}:Vec3{};
        const auto bypass=checked_walking_bypass(f.path,blocked->end,blocked->end_point,normal,56,.71,probe);
        require(bypass.has_value(),"a checked lateral route must avoid the unusable floor edge");
        const auto repaired=plan_ground_coverage(bypass->path,f.path_id,budget,probe);
        require(repaired&&repaired->reaches_end,"the whole repaired route clears the floor edge");
    }
}
void walking_bypass_at_successive_walls() {
    auto f=field();
    f.path.points={{-200,0,0},{0,0,0},{100,0,0},{400,0,0},{700,0,0},{1000,0,0}};
    const auto obstructed=[](Vec3 p) {
        // These strips represent capsule-centre clearance next to two walls.
        const auto wall=[&](double start,double end) {
            return p.x>start&&p.x<end&&p.y>=-.04*std::min(p.x-start,end-p.x);
        };
        return wall(0,39)||wall(55,420);
    };
    const auto project=[](Vec3 p)->std::optional<Vec3>{return p;};
    const GroundProbe probe{
        [&](Vec3 a,Vec3 b)->std::optional<bool>{return obstructed(a)||obstructed(b);},project,.45};
    const GroundBudget budget{15,.2,190,44.765,true};
    const auto prefix=plan_ground_coverage(f.path,f.path_id,budget,probe);
    require(prefix&&prefix->limit==GroundLimit::Obstruction,"the native route hits the first wall");
    const auto first=checked_walking_bypass(f.path,prefix->end,prefix->end_point,{0,-1,0},20,.71,probe);
    require(first&&first->path.points[first->rejoin].x==40,"a nearby lane clears the first wall");
    const auto held=plan_ground_coverage(first->path,f.path_id,budget,probe);
    require(held&&held->limit==GroundLimit::Obstruction&&
        (held->end.segment>first->rejoin||(held->end.segment==first->rejoin&&held->end.fraction==1.)),
        "the first rejoin stays clear while the next wall blocks the retained route");
    require(!checked_walking_bypass(first->path,held->end,held->end_point,{0,-1,0},20,.71,probe),
        "the nearby search cannot rejoin inside a longer wall");
    const auto next=checked_walking_bypass(first->path,held->end,held->end_point,{0,-1,0},20,.71,probe,8.);
    require(next&&next->rejoin>first->rejoin&&next->path.points[first->rejoin].x==40&&
        next->path.points[next->rejoin].x>=420,"the next checked lane retains the first bypass and clears the later wall");
    const auto repaired=plan_ground_coverage(next->path,f.path_id,budget,probe);
    require(repaired&&repaired->reaches_end&&next->path.points.back().x==1000,
        "the assembled route clears both walls to the original destination");

    auto gap=probe;
    gap.project=[&](Vec3 p)->std::optional<Vec3>{return p.x>=135&&p.x<=155?std::nullopt:project(p);};
    require(!checked_walking_bypass(first->path,held->end,held->end_point,{0,-1,0},20,.71,gap,8.),
        "a farther rejoin cannot skip an unsupported strip of floor");
    require(!checked_walking_bypass(first->path,held->end,held->end_point,{0,-1,0},20,.71,probe,2.),
        "a farther search stays inside the remaining ground-check window");
}
void walking_bypass_through_a_curved_corridor() {
    auto f=field();
    f.path.points={{-100,0,0},{0,0,0},{120,0,0},{360,0,0},{620,0,0}};
    const std::vector<Vec3> centerline{{-100,0,0},{0,0,0},{40,40,0},{100,40,0},{140,80,0},
        {200,80,0},{240,40,0},{290,40,0},{330,0,0},{620,0,0}};
    const auto supported=[&](Vec3 p) {
        const auto end=std::lower_bound(centerline.begin()+1,centerline.end(),p.x,
            [](Vec3 point,double x){return point.x<x;});
        if(end==centerline.end()||p.x<centerline.front().x)return false;
        const auto start=*(end-1);
        const auto y=start.y+(end->y-start.y)*(p.x-start.x)/(end->x-start.x);
        return std::abs(p.y-y)<=18.;
    };
    const GroundProbe probe{
        [&](Vec3 a,Vec3 b)->std::optional<bool>{
            const auto samples=std::max(1,static_cast<int>(std::ceil(std::hypot(a.x-b.x,a.y-b.y)/4.)));
            for(int i=0;i<=samples;++i) {
                const auto t=static_cast<double>(i)/samples;
                if(!supported({a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),0}))return true;
            }
            return false;
        },
        [&](Vec3 p)->std::optional<Vec3>{return supported(p)?std::optional<Vec3>{p}:std::nullopt;},.45};
    const GroundBudget budget{12,.2,180,44.765,true};
    const auto prefix=plan_ground_coverage(f.path,f.path_id,budget,probe);
    require(prefix&&prefix->limit==GroundLimit::Projection&&prefix->end_point.x==0,
        "the narrow capsule lane immediately leaves the native chord");
    require(!checked_walking_bypass(f.path,prefix->end,prefix->end_point,{0,1,0},20,.71,probe,8.),
        "none of the one-pivot detours can follow both bends");
    std::size_t probes{};
    const GroundProbe counted{
        [&](Vec3 a,Vec3 b){++probes;return probe.blocked(a,b);},
        [&](Vec3 p){++probes;return probe.project(p);},.45};
    const auto lane=checked_walking_corridor(f.path,prefix->end,prefix->end_point,{0,1,0},20,.71,counted,8.);
    require(lane&&lane->path.points[lane->rejoin].x>=310&&lane->path.points.back().x==620&&probes<1024,
        "the bounded multi-pivot search returns to the same complete route after the obstruction");
    const auto repaired=plan_ground_coverage(lane->path,f.path_id,budget,probe);
    require(repaired&&repaired->reaches_end,"every graph leg must still pass independent rolling verification");

    auto gap=probe;
    gap.project=[&](Vec3 p)->std::optional<Vec3>{return p.x>130&&p.x<156?std::nullopt:probe.project(p);};
    require(!checked_walking_corridor(f.path,prefix->end,prefix->end_point,{0,1,0},20,.71,gap,8.),
        "the graph must not bridge a floor gap");
    f.path.points.insert(f.path.points.begin()+3,{120,0,400});
    require(!checked_walking_corridor(f.path,prefix->end,prefix->end_point,{0,1,0},20,.71,probe,8.),
        "the graph must not bypass a native vertical link");
}
void walking_bypass_starts_before_a_trapped_pivot() {
    auto f=field();
    f.path.points={{0,-200,0},{0,-70,0},{0,0,8},{0,180,0},{0,400,0}};
    const auto supported=[](Vec3 p){return !(p.y>=-5&&p.y<=0&&p.x>5&&p.x<30);};
    const auto overlap=[&](Vec3 p){return !supported(p)||(p.y>0&&p.y<=150&&p.x<30);};
    const GroundProbe probe{
        [&](Vec3 a,Vec3 b)->std::optional<bool>{
            const auto count=std::max(1,static_cast<int>(std::ceil(std::hypot(b.x-a.x,b.y-a.y)/2.)));
            for(int i=0;i<=count;++i) {
                const auto t=static_cast<double>(i)/count;
                if(overlap({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0}))return true;
            }
            return false;
        },
        [&](Vec3 p)->std::optional<Vec3>{
            return supported(p)?std::optional<Vec3>{Vec3{p.x,p.y,0}}:std::nullopt;
        },.45};
    const GroundBudget budget{12,.2,180,44.765,true};
    const auto prefix=plan_ground_coverage(f.path,f.path_id,budget,probe);
    require(prefix&&prefix->limit==GroundLimit::Obstruction&&prefix->end.segment==2&&prefix->end.fraction==1.,
        "the baked pivot itself remains reachable before the wall");
    require(!checked_walking_corridor(f.path,prefix->end,{0,0,0},{1,0,0},20,.71,probe,4.),
        "from the last clear baked pivot all outgoing chords are trapped");
    const auto entry=walking_detour_entry(f.path,prefix->end,20,1,probe);
    require(entry&&entry->location.segment==2&&std::abs(entry->feet.y+20)<1e-6&&entry->feet.z==0,
        "the upstream anchor uses the actual floor one capsule radius before contact");
    require(!walking_detour_entry(f.path,prefix->end,20,2,probe),
        "an upstream detour must preserve the earlier verified rejoin");
    const auto lane=checked_walking_corridor(f.path,entry->location,entry->feet,{1,0,0},20,.71,probe,4.2,.4);
    require(lane&&lane->path.points[1].y==-70&&lane->path.points[lane->rejoin].y>150&&lane->path.points.back().y==400,
        "the new entry removes the trapped pivot and rejoins beyond the wall");
    require(std::none_of(lane->path.points.begin()+2,lane->path.points.end(),
        [](Vec3 p){return p.x==0&&p.y==0;}),"the full splice does not still force the old pivot");
    const auto checked=plan_ground_coverage(lane->path,f.path_id,budget,probe);
    require(checked&&checked->reaches_end,"the upstream approach and new rejoin pass the complete physical check");

    const GroundProbe clear{[](Vec3,Vec3)->std::optional<bool>{return false;},probe.project,.45};
    require(!checked_walking_corridor(f.path,entry->location,entry->feet,{1,0,0},20,.71,clear,.35,.4),
        "the search cannot rejoin before the failed sample's forward position");
}
void detour_rejoins_the_native_vertex() {
    auto f=field();
    constexpr Vec3 entry{16633.989247,22097.769304,-24.179394};
    constexpr Vec3 native_join{16638,22434,79.981945};
    constexpr double radius=18.666666;
    f.path.points={entry,{16638,22116,-10.018055},native_join,{16620,22482,89.981945},{16206,22896,89.981945}};
    const GroundProbe probe{
        [](Vec3,Vec3)->std::optional<bool>{return false;},
        [&](Vec3 p)->std::optional<Vec3>{
            if(std::abs(p.x-native_join.x)<.01&&p.y>22116&&p.y<22432.5)return {};
            // The cooked and supported heights can differ by a few cm at a join.
            p.z=-18.128364+(std::min(p.y,22482.)-22116)*(77.289923+18.128364)/(22433.333323-22116.);
            return p;
        },.45};
    const GroundBudget budget{15,.2,200,44.765,true};
    const auto bypass=checked_walking_corridor(f.path,{1,0.},entry,{1,0,0},radius,.71,probe,8.);
    require(bypass.has_value(),"the world detour finds its live rejoin");
    const auto ground=plan_ground_coverage(bypass->path,f.path_id,budget,probe);
    require(ground&&ground->limit!=GroundLimit::VerticalLink,
        "the observed 0.667 cm join at mixed floor heights must not create a traversal link");
    const auto join=bypass->path.points[bypass->rejoin];
    require(join.x==native_join.x&&join.y==native_join.y&&join.z==native_join.z,
        "the checked splice rejoins the original cooked vertex exactly");

    f.path.points.insert(f.path.points.begin()+3,{native_join.x,native_join.y,native_join.z+30});
    const auto link=checked_walking_corridor(f.path,{1,0.},entry,{1,0,0},radius,.71,probe,8.);
    require(link&&link->path.points[link->rejoin+1].z==native_join.z+30,
        "the splice preserves the genuine native link after the join");
    const auto blocked=plan_ground_coverage(link->path,f.path_id,budget,probe);
    require(blocked&&blocked->limit==GroundLimit::VerticalLink&&blocked->end.segment==link->rejoin,
        "a genuine native link still stops the walking proof");

    f.path.points={{0,0,0},{37.8,0,4},{200,0,4}};
    const GroundProbe level{[](Vec3,Vec3)->std::optional<bool>{return false;},
        [](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,0};},.45};
    const auto short_bypass=checked_walking_bypass(f.path,{1,0.},{0,0,0},{0,1,0},radius,.71,level);
    require(short_bypass&&short_bypass->path.points[short_bypass->rejoin].x==37.8,
        "the single-pivot splice also avoids a synthetic vertex just before the native join");
}
void refresh_remaining_bypass() {
    auto f=field();
    f.path.points={{0,0,0},{150,0,0},{150,100,0},{250,100,0},{500,100,0}};
    ground_from_live_state(f);
    NavigationFollower nav;
    require(nav.start(NavigationMode::Automatic,f).movement.has_value(),"the checked detour starts walking");
    f.position={150,0,0};f.sample_seconds=.1;
    require(nav.observe(f).movement.has_value(),"the follower tracks the first turn");
    const Vec3 feet{150,42,0};
    const auto remaining=nav.remaining_bypass(f.path,f.path_id,feet,3);
    require(remaining&&remaining->rejoin==2&&remaining->path.points.size()==4&&
        remaining->path.points[0].y==42&&remaining->path.points[1].y==100&&remaining->path.points.back().x==500,
        "the current feet replace the completed prefix and the next bend, rejoin and destination are retained");
    require(!nav.remaining_bypass(f.path,99,feet,3)&&!nav.remaining_bypass(f.path,f.path_id,{190,42,0},3),
        "a foreign path or off-route position must obtain a fresh native route");
    f.position={150,100,0};f.sample_seconds=.2;
    nav.observe(f);
    require(!nav.remaining_bypass(f.path,f.path_id,{250,100,0},3),
        "once the rejoin is reached, the next refresh uses the current native route");

    f.position={0,0,0};f.sample_seconds=0;f.bypass_rejoin=3;
    NavigationFollower guarded_turn;
    guarded_turn.start(NavigationMode::Automatic,f);
    f.position={136,0,0};f.sample_seconds=.1;
    const auto towards_pivot=guarded_turn.observe(f);
    require(towards_pivot.movement_waypoint&&towards_pivot.movement_waypoint->y==0,
        "approaching a detour pivot cannot switch to an unchecked chord");
    const auto proposed=guarded_turn.remaining_bypass(f.path,f.path_id,f.position,3);
    const auto approach=guarded_turn.remaining_bypass(f.path,f.path_id,f.position,3,false);
    require(proposed&&approach&&proposed->path.points[1].y==100&&approach->path.points[1].y==0,
        "refresh can check the outgoing chord and fall back to the remaining approach if blocked");
    f.position={150,0,0};f.sample_seconds=.2;
    const auto at_pivot=guarded_turn.observe(f);
    require(at_pivot.mode==NavigationMode::Automatic&&!at_pivot.movement,
        "a reached detour pivot waits for refresh without issuing unsupported movement");

    f.position={0,0,0};f.sample_seconds=0;
    f.path.points.insert(f.path.points.end()-1,{{250,200,0},{350,200,0}});
    f.bypass_rejoin=5;f.check_route_turns=true;
    ground_from_live_state(f);
    NavigationFollower composed;
    composed.start(NavigationMode::Automatic,f);
    f.position={136,0,0};f.sample_seconds=.1;
    const auto needs_check=composed.observe(f);
    require(needs_check.floor_turn_waypoint==2&&!needs_check.movement,
        "an earlier turn in a composed detour asks for a new proof from live feet");
    f.hold_floor_turn=true;
    const auto hold=composed.observe(f);
    require(hold.movement_waypoint&&hold.movement_waypoint->x==150&&hold.movement_waypoint->y==0,
        "when the live turn is blocked the follower keeps the approach to its first pivot");
    f.position={0,0,0};f.sample_seconds=0;f.bypass_rejoin=0;f.hold_floor_turn=false;
    ground_from_live_state(f);
    NavigationFollower world;
    world.start(NavigationMode::Automatic,f);
    f.position={136,0,0};f.sample_seconds=.1;
    require(world.observe(f).floor_turn_waypoint==2,
        "the next world turn still requires proof after the last detour rejoins");
}
void floor_checked_waypoint_height() {
    auto f=field();
    const GroundProbe probe{
        [](Vec3,Vec3)->std::optional<bool>{return false;},
        [](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,0};},.45};
    const GroundBudget budget{12,.2,160,44.765,true};
    for(double height:{-18.,-25.}) {
        f.path.points={{0,0,0},{3,0,height},{1000,0,height}};
        f.ground=plan_ground_coverage(f.path,f.path_id,budget,probe);
        require(f.ground&&f.ground->reaches_end,"the actual walking floor is continuous through the baked corner");
        NavigationFollower nav;
        const auto out=nav.start(NavigationMode::Automatic,f);
        require(out.floor_turn_waypoint==2&&!out.movement,
            "a nearby baked corner on checked floor requests a recheck from the live feet");
        auto refreshed=f;
        refreshed.path.points={f.position,f.path.points.back()};
        refreshed.sample_seconds=refreshed.path_sample_seconds=.1;
        ++refreshed.path_id;
        refreshed.ground=plan_ground_coverage(refreshed.path,refreshed.path_id,budget,probe);
        const auto forward=nav.observe(refreshed);
        require(forward.movement&&forward.movement_waypoint&&forward.movement_waypoint->x==1000,
            "the verified outgoing leg can proceed despite the baked height offset");
    }
    f.ground->swept_ground->end={1,.5};
    NavigationFollower unchecked;
    require(!unchecked.start(NavigationMode::Automatic,f).movement,
        "the longer mesh proof cannot substitute for capsule floor coverage through the corner");

    f.path.points={{0,0,0},{3,0,-25},{100,0,-25},{1000,0,-25}};
    f.ground=plan_ground_coverage(f.path,f.path_id,budget,probe);
    f.bypass_rejoin=2;
    NavigationFollower detour;
    const auto held=detour.start(NavigationMode::Automatic,f);
    require(held.mode==NavigationMode::Automatic&&held.movement_waypoint&&held.movement_waypoint->x==3,
        "the height offset must not stop the approach to a held detour turn");
    const auto next=detour.remaining_bypass(f.path,f.path_id,f.position,f.bypass_rejoin,true,&*f.ground);
    require(next&&next->path.points[1].x==100,
        "the next ground check can verify the outgoing leg from live feet");
}
void floor_turn_requires_current_clearance() {
    auto f=field();
    const GroundProbe probe{
        [](Vec3 a,Vec3 b)->std::optional<bool>{
            constexpr Vec3 obstacle{-56.3,56.3,0};
            const auto dx=b.x-a.x,dy=b.y-a.y;
            const auto length=dx*dx+dy*dy;
            const auto t=length?std::clamp(((obstacle.x-a.x)*dx+(obstacle.y-a.y)*dy)/length,0.,1.):0.;
            return std::hypot(a.x+t*dx-obstacle.x,a.y+t*dy-obstacle.y)<56.;
        },
        [](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,0};},.45};
    const GroundBudget budget{12,.2,160,44.765,true};
    f.position={-4.7,0,0};
    f.path.points={{-100,0,0},{0,0,-25},{0,100,-25}};
    f.ground=plan_ground_coverage(f.path,f.path_id,budget,probe);
    require(f.ground&&f.ground->reaches_end,"both original route legs clear the obstacle");
    auto chord=f.path;
    chord.points={f.position,f.path.points.back()};
    const auto checked=plan_ground_coverage(chord,f.path_id+1,budget,probe);
    require(checked&&checked->limit==GroundLimit::Obstruction,"the early turn clips the obstacle");
    NavigationFollower nav;
    const auto out=nav.start(NavigationMode::Automatic,f);
    require(out.floor_turn_waypoint==2&&!out.movement,"the floor-checked corner cannot turn onto an unchecked chord");
    f.hold_floor_turn=true;
    f.sample_seconds=.1;
    const auto held=nav.observe(f);
    require(held.movement_waypoint&&held.movement_waypoint->x==0&&held.movement_waypoint->y==0,
        "a blocked outgoing chord holds the supported approach to the pivot");

    f.path.points.insert(f.path.points.end(),{{25,150,-25},{50,175,-25},{300,175,-25}});
    f.ground=plan_ground_coverage(f.path,f.path_id,budget,probe);
    f.bypass_rejoin=4;
    f.hold_floor_turn=false;
    NavigationFollower with_detour;
    const auto prior_to_detour=with_detour.start(NavigationMode::Automatic,f);
    require(prior_to_detour.floor_turn_waypoint==2&&!prior_to_detour.movement,
        "a later detour cannot exempt a preceding turn from current clearance");
}
#endif
}
int main(){try{long_journey();coverage_guards();distant_link();
#ifndef RESONANCE_BASELINE
 dense_live_route();speed_policy();walking_bypass_at_a_baked_corner();walking_bypass_at_a_short_downhill_corner();
 walking_bypass_at_a_floor_edge();walking_bypass_at_successive_walls();walking_bypass_through_a_curved_corridor();
 walking_bypass_starts_before_a_trapped_pivot();detour_rejoins_the_native_vertex();
 refresh_remaining_bypass();
 floor_checked_waypoint_height();
 floor_turn_requires_current_clearance();
#endif
 std::cout<<"Long-distance route and verified-ground follower checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
