#include "resonance/Navigation.hpp"
#include "../src/ChapterThreeBoats.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace resonance;
namespace {
void require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
using Mode=NavigationMode;
void checked_ground(NavigationFrame& f) {
    f.path_id=1;
    GroundProbe probe{[](Vec3,Vec3)->std::optional<bool>{return false;},[](Vec3 p)->std::optional<Vec3>{return p;}};
    f.ground=plan_ground_coverage(f.path,f.path_id,GroundBudget{},probe);
}
NavigationFrame frame(double now=0,Vec3 position={0,0,0}) {
    NavigationFrame f{"field-1","player-1","target-1","Door",position,
        {{{0,0,0},{-200,0,0},{-200,400,0}},{1,0,0},{0,1,0},100,true,false},
        now,now,true,true,false,false,true};
    checked_ground(f);return f;
}
std::string speech(const NavigationUpdate& out) {
    std::string text;for(const auto& s:out.speech) {if(!text.empty())text+=' ';text+=s.text;}return text;
}
bool near(double a,double b) {return std::abs(a-b)<1e-6;}
void rowing_around_obstacles() {
    using namespace resonance::chapter_three;
    const Vec3 start{40,80,12},goal{440,80,12};
    const auto clear=[](Vec3 a,Vec3 b) {
        if(a.z!=12||b.z!=12)return false;
        for(int i=0;i<=100;++i) {
            const auto fraction=i/100.;
            const auto x=a.x+(b.x-a.x)*fraction,y=a.y+(b.y-a.y)*fraction;
            if(x>=180&&x<=280&&y<=190)return false;
        }
        return true;
    };
    const Crossing pool{3080,11,-1,L"Dock","Landing",goal,{0,480,0,290},{}};
    const auto route=rowing_path(start,goal,pool,clear);
    require(route.size()>2&&near(route.front().x,start.x)&&near(route.back().x,goal.x),
        "a rowing route must get around the wall and end at the same landing");
    for(std::size_t i=1;i<route.size();++i)
        require(pool.water.contains(route[i])&&clear(route[i-1],route[i]),
            "every rowing edge needs verified clearance inside the active pool");
    auto narrow=pool;narrow.water.max_y=180;
    require(rowing_path(start,goal,narrow,clear).empty(),"a disconnected landing must not get a shortcut");
    require(rowing_path({-40,80,12},goal,pool,clear).empty(),"the route cannot start outside this pool");
}
void native_walking_basis() {
    WalkingBasisFlags flags{false,false,false,false};
    require(walking_yaw_source(flags)==WalkingYawSource::Control,"ordinary input must use native control rotation");
    flags.external=true;
    require(walking_yaw_source(flags)==WalkingYawSource::ViewTarget,"the external flag must select the view target");
    flags.internal=true;
    require(walking_yaw_source(flags)==WalkingYawSource::UnitCamera,"internal rotation takes priority over external rotation");
    flags.internal=false;flags.force_internal=true;
    require(walking_yaw_source(flags)==WalkingYawSource::UnitCamera,"the native override byte must select the unit camera");
    flags.vertical=true;
    require(!walking_yaw_source(flags),"vertical input must not be described as ordinary ground walking");
    for(int missing=0;missing<4;++missing) {
        WalkingBasisFlags unknown{false,false,false,false};
        if(missing==0)unknown.internal.reset();
        if(missing==1)unknown.external.reset();
        if(missing==2)unknown.force_internal.reset();
        if(missing==3)unknown.vertical.reset();
        require(!walking_yaw_source(unknown),"an unreadable native mode flag cannot guess an input frame");
    }
    for(double yaw:{0.,90.,-90.,180.,450.,-450.}) {
        const auto axes=walking_axes(yaw);require(axes.has_value(),"finite native headings must resolve");
        require(near(std::hypot(axes->right.x,axes->right.y),1)&&near(std::hypot(axes->up.x,axes->up.y),1)&&
            near(axes->right.x*axes->up.x+axes->right.y*axes->up.y,0)&&axes->right.z==0&&axes->up.z==0,
            "native horizontal input axes must be unit length and perpendicular");
        PathSnapshot p{{{0,0,0},{axes->up.x*200,axes->up.y*200,0},{axes->up.x*200+axes->right.x*400,axes->up.y*200+axes->right.y*400,0}},
            axes->right,axes->up,100,true,false};
        require(describe_route(p)=="up 2 metres, then right 4 metres","both native positive input axes must match spoken controls at every heading");
    }
    const auto zero=walking_axes(0),quarter=walking_axes(90);
    require(zero&&near(zero->right.y,1)&&near(zero->up.x,1)&&quarter&&near(quarter->right.x,-1)&&near(quarter->up.y,1),
        "the exact executable's +Y right / +X up constants must set the handedness");
    PathSnapshot logged{{{-85.486352,50000,5102.149999},{4777.000862,49996.999836,5132.983201}},quarter->right,quarter->up,100,true,false};
    require(describe_route(logged)=="left 49 metres","the real native result must be speakable at a 90-degree input heading");
    logged.right=zero->right;logged.up=zero->up;
    require(describe_route(logged)=="up 49 metres","the same native result must follow the actual control heading");
    require(!walking_axes(std::numeric_limits<double>::quiet_NaN())&&!walking_axes(std::numeric_limits<double>::infinity()),
        "nonfinite native headings must remain unavailable");
}
void activation_endpoint_arrival() {
    auto f=frame(0,{60,0,0});f.path.points={{0,0,0},{100,0,0}};f.arrival_metres=.05;
    checked_ground(f);NavigationFollower precise;
    require(precise.start(Mode::Manual,f).mode==Mode::Manual,"a narrow exit approach must not stop at the ordinary half-metre distance");
    f.sample_seconds=.1;f.position={98,0,0};
    require(precise.observe(f).mode==Mode::Off,"the approach stops inside its verified smaller arrival radius");
    f=frame(0,{60,0,0});f.path.points={{0,0,0},{100,0,0}};checked_ground(f);NavigationFollower normal;
    require(normal.start(Mode::Manual,f).mode==Mode::Off,"ordinary point targets keep their existing arrival distance");
    for(double invalid:{0.,.001,.51,std::numeric_limits<double>::quiet_NaN()}) {
        f=frame();f.arrival_metres=invalid;NavigationFollower guarded;
        require(guarded.start(Mode::Manual,f).mode==Mode::Off,"an unreadable or unsupported arrival radius cannot start navigation");
    }
    f=frame();f.path={};f.ground.reset();f.activation_reached=true;
    NavigationFollower overlapping;
    const auto arrived=overlapping.start(Mode::Automatic,f);
    require(arrived.mode==Mode::Off&&!arrived.movement&&speech(arrived).find("At the target's activation area")!=std::string::npos,
        "live activation overlap stops without a path or movement");
    f.arrival_message="At the door to the hall. Walk through the door.";
    const auto doorway=overlapping.start(Mode::Automatic,f);
    require(doorway.mode==Mode::Off&&!doorway.movement&&speech(doorway)==f.arrival_message,
        "a door leg stops and gives the user the crossing instruction");
    f.target_available=false;
    require(speech(overlapping.start(Mode::Automatic,f)).find("activation area")==std::string::npos,
        "stale target geometry cannot report arrival");
}
void native_agent_and_speed_policy() {
    require(navmesh_supports_capsule(20,66.666664,20,66.666664,18.666666,33.333332),
        "the actual field capsule fits the default mesh despite the separate LocalMap mesh");
    require(!navmesh_supports_capsule(20,66.666664,20,66.666664,25,33)&&
        !navmesh_supports_capsule(20,66.666664,20,66.666664,18,40),"a mesh for a smaller or shorter agent must not authorize automatic walking");
    require(!navmesh_supports_capsule(56,200,20,66.666664,18,33)&&
        !navmesh_supports_capsule(20,66,20,66,0,33),"mismatched native/config values and invalid capsule dimensions must fail closed");
}
void capsule_speed_bound() {
    const auto conservative=capsule_input_scale(196,0,15000,2600,.4);
    const auto analog=capsule_input_scale(196,0,15000,2600,.4,WalkingAnalogSpeed{1300,0});
    require(conservative&&*conservative>0&&*conservative<.08f,
        "the acceleration-only limit reproduces the old Shrine creep");
    require(analog&&*analog>.30f&&*analog<.33f,
        "the exact native analog cap permits a useful pulse within the same braking distance");
    require(capsule_input_scale(196,450,15000,2600,.4,WalkingAnalogSpeed{1300,0})==0.f,
        "existing velocity above the limit must brake");
    require(capsule_input_scale(196,0,15000,2600,.4,WalkingAnalogSpeed{1300,500})==conservative,
        "an analog floor above the verified bound cannot authorize faster input");
    const auto close=capsule_input_scale(2,0,15000,2600,.4,WalkingAnalogSpeed{1300,0});
    require(close&&*close>0&&*close<.004f,"a close approach must still slow down");
    require(!capsule_input_scale(196,0,0,2600,.4),"invalid kinematics must fail closed");
    const auto full_walk=capsule_input_scale(800,0,15000,2600,.4,WalkingAnalogSpeed{750,0},.8f);
    const auto full_dash=capsule_input_scale(800,0,15000,2600,.4,WalkingAnalogSpeed{1000,0},1.f);
    require(full_walk==.8f&&full_dash==1.f,"verified runway must allow the game's normal pace and Dash gain");
    const auto braking_walk=capsule_input_scale(80,450,15000,2600,.4,WalkingAnalogSpeed{750,0},.8f);
    require(braking_walk==0.f,"native walking must still coast down near a waypoint");
    const auto axes=walking_axes(90);
    const auto stick=axes?walking_stick({0,1,0},*axes):std::nullopt;
    require(stick&&near(stick->x,0)&&near(stick->y,1),"native stick axes must invert the verified world heading");
}
void manual_and_auto() {
    NavigationFollower manual;
    auto f=frame();
    auto out=manual.toggle(Mode::Manual,f);
    require(out.mode==Mode::Manual&&!out.movement&&!out.stopped_auto,"manual guidance must start without generating movement");
    require(speech(out).find("left 2 metres, then up 4 metres")!=std::string::npos,"initial guidance must use native route legs and metre scale");
    require(speech(out).find("Spoken guidance on")!=std::string::npos,"manual start must announce its mode");
    f.sample_seconds=.1;f.path_sample_seconds=0;
    require(manual.observe(f).speech.empty(),"unchanged route must not chatter every tick");
    require(!manual.observe(f).movement,"manual observe must never generate movement");
    f=frame(10);f.manual_input=true;
    out=manual.observe(f);
    require(out.mode==Mode::Manual&&out.speech.empty(),"manual mode must allow standing and physical input without a block timeout");
    out=manual.observe(f,true);
    require(speech(out).find("left 2 metres")!=std::string::npos,"explicit status must repeat current route guidance");
    out=manual.toggle(Mode::Manual,f);
    require(out.mode==Mode::Off&&!out.stopped_auto&&!out.speech.empty(),"manual toggle off must not release physical player input");

    NavigationFollower automatic;
    out=automatic.toggle(Mode::Automatic,frame());
    require(out.mode==Mode::Automatic&&out.movement,"deliberate automatic start must supply a movement direction");
    require(near(out.movement->x,-1)&&near(out.movement->y,0)&&near(out.movement->z,0),"automatic movement must follow the first actual path segment in world space");
    require(speech(out).find("Automatic walking on")!=std::string::npos,"automatic start must announce its mode");
    out=automatic.toggle(Mode::Automatic,frame(.1));
    require(out.mode==Mode::Off&&out.stopped_auto&&!out.movement,"automatic toggle off must release generated movement");
    require(automatic.observe(frame(.2)).mode==Mode::Off&&!automatic.observe(frame(.2)).movement,"observe must never restart an inactive follower");
}
void corners_and_replanning() {
    NavigationFollower s;s.start(Mode::Automatic,frame());
    auto f=frame(.1,{-100,0,0});f.path_sample_seconds=0;
    auto out=s.observe(f);
    require(out.movement&&near(out.movement->x,-1)&&out.speech.empty(),"moving along one leg must keep direction without distance chatter");
    f=frame(.2,{-195,0,0});f.path_sample_seconds=0;
    out=s.observe(f);
    require(out.movement&&out.movement->y>.99,"near a native bend must follow the next path leg");
    require(speech(out).find("up 4 metres")!=std::string::npos,"reaching a bend must announce the next sequential leg");
    f=frame(.3,{-200,100,0});f.path_sample_seconds=0;
    out=s.observe(f);
    require(out.movement&&near(out.movement->x,0)&&near(out.movement->y,1)&&out.speech.empty(),"a consumed waypoint must stay consumed until the next fresh path");
    f.path_sample_seconds=.3;f.path.points={{-200,100,0},{-200,400,0}};
    checked_ground(f);
    out=s.observe(f);
    require(out.movement&&near(out.movement->y,1)&&out.speech.empty(),"fresh replanning must preserve useful direction without repeating it");

    NavigationFollower overshoot;overshoot.start(Mode::Automatic,frame());
    f=frame(.1,{-220,0,0});f.path_sample_seconds=0;
    out=overshoot.observe(f);
    require(out.movement&&out.movement->y>.99,"passing a waypoint inside its corridor must not turn back toward the old corner");

    NavigationFollower rotated;
    f=frame();f.path.right={0,1,0};f.path.up={-1,0,0};
    out=rotated.start(Mode::Automatic,f);
    require(out.movement&&near(out.movement->x,-1)&&near(out.movement->y,0),"camera orientation must not rotate native world movement");
    require(speech(out).find("up 2 metres, then right 4 metres")!=std::string::npos,"spoken guidance must follow the current camera axes");
}
void cancellation_guards() {
    for(int guard=0;guard<13;++guard) {
        NavigationFollower s;s.start(Mode::Automatic,frame());
        auto f=frame(.1);
        if(guard==0)f.manual_input=true;
        if(guard==1)f.foreground=false;
        if(guard==2)f.field_eligible=false;
        if(guard==3)f.path.valid=false;
        if(guard==4)f.path.partial=true;
        if(guard==5)f.walking_path=false;
        if(guard==6)f.target="target-2";
        if(guard==7)f.world="field-2";
        if(guard==8)f.player="player-2";
        if(guard==9)f.path.world_to_metres=0;
        if(guard==10)f.position.x=std::numeric_limits<double>::quiet_NaN();
        if(guard==11)f.path.points[1].y=std::numeric_limits<double>::infinity();
        if(guard==12)f.path.right={0,0,0};
        const auto out=s.observe(f);
        require(out.mode==Mode::Off&&out.stopped_auto&&!out.movement&&!out.speech.empty(),"any physical/control/context/path guard must cancel automatic movement with feedback");
        require(s.observe(frame(.2)).mode==Mode::Off&&!s.observe(frame(.2)).movement,"guard cancellation must not automatically restart the prior target");
    }
    NavigationFollower manual;manual.start(Mode::Manual,frame());
    auto f=frame(.1);f.field_eligible=false;
    auto out=manual.observe(f);
    require(out.mode==Mode::Off&&!out.stopped_auto,"losing native field control must stop manual guidance without touching physical input");
    NavigationFollower target;target.start(Mode::Manual,frame());
    f=frame(.1);f.target="new-target";
    require(target.observe(f).mode==Mode::Off,"selection change must stop guidance instead of silently following a new target");
    require(target.observe(f).speech.empty(),"target cancellation must be deduplicated");
}
void freshness_blocking_and_geometry() {
    NavigationFollower stale;stale.start(Mode::Automatic,frame());
    auto f=frame(.61);f.path_sample_seconds=0;
    auto out=stale.observe(f);
    require(out.mode==Mode::Off&&out.stopped_auto,"paths older than .6 seconds must stop automatic movement");
    for(int guard=0;guard<6;++guard) {
        NavigationFollower s;
        f=frame();
        if(guard==0)f.path_sample_seconds=1;
        if(guard==1)f.sample_seconds=std::numeric_limits<double>::quiet_NaN();
        if(guard==2)f.path.points={{0,0,0},{0,0,200}};
        if(guard==3)f.path.points={{1000,0,0},{1200,0,0}};
        if(guard==4)f.path.points.resize(1);
        if(guard==5)f.path.up={1,0,0};
        out=s.start(Mode::Automatic,f);
        require(out.mode==Mode::Off&&!out.movement&&!out.speech.empty(),"future timestamps, nonfinite clocks and unsupported geometry must refuse activation");
    }
    NavigationFollower blocked;blocked.start(Mode::Automatic,frame());
    for(double time:{.25,.5,.75,1.0,1.25}) {
        out=blocked.observe(frame(time));
        require(out.mode==Mode::Automatic&&out.movement,"fresh replans must not cause premature blockage");
    }
    out=blocked.observe(frame(1.51));
    require(out.mode==Mode::Automatic&&out.replan==NavigationReplan::Blocked&&!out.movement,
        "stalled automatic walking asks for a route from the actual position before giving up");
    f=frame(1.6);f.path_id=2;f.ground->path_id=2;
    require(blocked.observe(f).movement.has_value(),"a replacement route may make a fresh attempt");
    out=blocked.observe(frame(3.02));
    require(out.mode==Mode::Off&&out.stopped_auto&&speech(out).find("blocked")!=std::string::npos,
        "replanning the same blocked approach cannot reset the retry indefinitely");
    NavigationFollower startup;startup.start(Mode::Automatic,frame());
    for(double time:{1.6,3.2}) {
        f=frame(time);f.beginning_movement=true;
        out=startup.observe(f);
        require(out.mode==Mode::Automatic&&out.movement&&out.replan==NavigationReplan::None,
            "waiting for the first native walking input must not report a blocked route");
    }
    out=startup.observe(frame(4.71));
    require(out.replan==NavigationReplan::Blocked&&out.mode==Mode::Automatic,
        "the blockage timer must take effect after the first input startup ends");
    NavigationFollower progress;progress.start(Mode::Automatic,frame());
    f=frame(1,{-20,0,0});
    out=progress.observe(f);
    require(out.mode==Mode::Automatic,"actual forward travel must reset the blocked timer");
    f=frame(2,{-40,0,0});
    require(progress.observe(f).mode==Mode::Automatic,"continued native movement must remain active beyond 1.5 total seconds");
    NavigationFollower reverse;reverse.start(Mode::Automatic,frame(1));
    f=frame(.9);
    require(reverse.observe(f).mode==Mode::Off,"backward monotonic time must cancel navigation");
}
void arrival_and_explicit_mode_switch() {
    NavigationFollower endpoint;
    auto f=frame();f.path.points={{0,0,0},{40,0,0}};
    checked_ground(f);
    auto out=endpoint.start(Mode::Automatic,f);
    require(out.mode==Mode::Off&&!out.movement&&speech(out).find("endpoint")!=std::string::npos,"remaining route within .5 metre must finish without movement or Confirm");
    NavigationFollower interaction;
    f=frame();f.interaction_available=true;
    out=interaction.start(Mode::Automatic,f);
    require(out.mode==Mode::Off&&!out.movement&&speech(out).find("Interaction available")!=std::string::npos,"native interaction availability must stop before attempting Confirm");
    NavigationFollower wall;
    f=frame();f.path.points={{0,0,0},{200,0,0},{200,200,0},{0,200,0},{0,40,0}};
    checked_ground(f);
    out=wall.start(Mode::Automatic,f);
    require(out.mode==Mode::Automatic&&out.movement,"endpoint proximity through a long path must not claim arrival through a wall");
    NavigationFollower switched;switched.start(Mode::Automatic,frame());
    out=switched.start(Mode::Manual,frame(.1));
    require(out.mode==Mode::Manual&&out.stopped_auto&&!out.movement,"explicitly switching to manual must release automatic input");
    require(switched.stop().mode==Mode::Off&&!switched.stop().stopped_auto,"stopping manual or inactive mode must not touch native movement");
}
NavigationFrame at(double now,Vec3 position,std::vector<Vec3> points) {
    auto f=frame(now,position);f.path.points=std::move(points);checked_ground(f);return f;
}
void surface_following_corridor_and_progress() {
    auto pending=frame();pending.ground->requires_surface=true;NavigationFollower unfinished;
    require(unfinished.start(Mode::Automatic,pending).mode==Mode::Off,"coarse slope relaxation is not sufficient proof for automatic movement");
    // A native straight segment can cut above flat terrain before the ramp's
    // foot. Supply a verified surface-mode prefix to test its consumer; the
    // planner's terrain/anchor probes are covered by ground_coverage_tests.
    const auto surface=[](double rise,double metres,bool complete=false){
        auto f=at(0,{0,0,0},{{0,0,0},{4000,0,rise}});
        const auto total=std::hypot(4000.,rise)/100;
        const double t=complete?1.:metres/total;
        f.ground=GroundCoverage{1,{1,t},{4000*t,0,rise*t},complete?total:metres,complete,
            complete?GroundLimit::None:GroundLimit::Horizon,100,true};
        return f;
    };
    NavigationFollower corridor;auto f=surface(3700,12);
    require(corridor.start(Mode::Automatic,f).mode==Mode::Automatic,"surface coverage must authorize the ordinary walking start");
    f.position={300,0,0};f.sample_seconds=.24;
    const auto walking=corridor.observe(f);
    require(walking.mode==Mode::Automatic&&walking.movement,"ground below the native chord must remain inside its XY walking corridor");
    f.position.y=250;f.sample_seconds=.25;
    const auto off_route=corridor.observe(f);
    require(off_route.replan==NavigationReplan::OffRoute&&!off_route.movement,
        "leaving the horizontal corridor requests a new path before any further movement");

    NavigationFollower progress;f=surface(2000,4.5);
    f.ground->limit=GroundLimit::Projection;
    require(progress.start(Mode::Automatic,f).mode==Mode::Automatic,"the prefix initially exceeds the stopping margin");
    f.position={300,0,0};f.sample_seconds=.24;
    const auto stopped=progress.observe(f);
    require(stopped.mode==Mode::Off&&!stopped.movement,"surface progress follows XY so lower ground cannot reuse an already consumed prefix");
    require(stopped.stopped_for_projection,"a consumed projected prefix can be planned again from the current feet");

    NavigationFollower override;f=surface(2000,4.5);
    f.ground->limit=GroundLimit::Projection;
    require(override.start(Mode::Automatic,f).mode==Mode::Automatic,"movement override starts on a checked prefix");
    f.position={300,0,0};f.sample_seconds=.24;f.manual_input=true;
    const auto cancelled=override.observe(f);
    require(cancelled.stopped_auto&&!cancelled.stopped_for_projection,"player movement cancels without requesting a ground recheck");

    NavigationFollower braking;f=surface(2000,12);f.ground_stop_margin_metres=6;
    require(braking.start(Mode::Automatic,f).mode==Mode::Automatic,"the surface prefix initially exceeds the physical braking margin");
    f.position={500,0,0};f.sample_seconds=.4;
    require(braking.observe(f).mode==Mode::Off,"a rising native chord cannot overstate physical verified ground remaining before braking");

    NavigationFollower floor;f=surface(2000,0,true);floor.start(Mode::Automatic,f);
    f.position={4000,0,0};f.sample_seconds=.24;
    const auto wrong_floor=floor.observe(f);
    require(speech(wrong_floor).find("endpoint reached")==std::string::npos,"matching endpoint XY on a different floor is not arrival");

    const auto policy=walking_ground_policy(1300,1300,2600,100);
    require(policy.has_value(),"the retained live walking/braking values need a surface lookahead");
    auto budget=policy->budget;budget.walkable_slope_degrees=44.765083;
    const auto fine=surface_following_budget(budget);
    require(fine.has_value(),"known native limits must produce a fine surface policy");
    auto long_path=at(0,{0,0,0},{{0,0,0},{100000,0,50000}}).path;
    const auto funded=surface_window_budget(long_path,*fine);
    GroundProbe terrain{[](Vec3,Vec3)->std::optional<bool>{return false;},[](Vec3 p)->std::optional<Vec3>{return Vec3{p.x,p.y,0};}};
    const auto checked=funded?plan_ground_coverage(long_path,7,*funded,terrain):std::nullopt;
    require(checked&&checked->limit==GroundLimit::Horizon&&checked->follow_surface&&
        checked->end_point.x/100>=budget.horizon_metres-1e-6,
        "fine lookahead must cover the physical XY horizon even when the native chord rises over flat terrain");
}
bool interrupting(const NavigationUpdate& out) {return !out.speech.empty()&&out.speech.front().interrupt;}
void chatter_and_turns() {
    constexpr double pi=3.14159265358979323846;
    // Review probe P1: straight route at an octant boundary with +-3 cm sway,
    // replanned from the player every .25 s for 10 s.
    const double a=22.5*pi/180;
    const Vec3 corner{1000*std::cos(a),1000*std::sin(a),0},end{corner.x,corner.y+800,0};
    NavigationFollower sway;
    auto out=sway.start(Mode::Manual,at(0,{0,0,0},{{0,0,0},corner,end}));
    require(out.mode==Mode::Manual&&!out.speech.empty(),"sway probe must start guidance");
    int announcements=0;
    for(int i=1;i<=40;++i) {
        const double drift=i%2?3.:-3.,along=i*20.;
        const Vec3 p{along*std::cos(a)-drift*std::sin(a),along*std::sin(a)+drift*std::cos(a),0};
        out=sway.observe(at(i*.25,p,{p,corner,end}));
        require(out.mode==Mode::Manual,"sway probe must keep guidance active");
        if(!out.speech.empty())++announcements;
    }
    require(announcements==0,"fresh replans and lateral sway must not re-announce an unchanged route");

    // A true 90-degree corner is announced in the frame it is reached, interrupting.
    NavigationFollower turn;
    turn.start(Mode::Manual,at(0,{0,0,0},{{0,0,0},{300,0,0},{300,300,0}}));
    out=turn.observe(at(.25,{250,0,0},{{250,0,0},{300,0,0},{300,300,0}}));
    require(out.speech.empty(),"approaching a corner must not announce distance updates");
    out=turn.observe(at(.5,{302,4,0},{{302,4,0},{300,300,0}}));
    require(speech(out).find("up")!=std::string::npos&&interrupting(out),"passing a real corner must announce the next leg immediately");

    // Short legs: every corner still produces its own immediate turn.
    NavigationFollower zigzag;
    const std::vector<Vec3> zig={{0,0,0},{60,0,0},{60,60,0},{120,60,0},{120,120,0}};
    zigzag.start(Mode::Manual,at(0,{0,0,0},zig));
    int turns=0;
    for(std::size_t i=1;i+1<zig.size();++i) {
        const Vec3 p{zig[i].x+(zig[i+1].x-zig[i].x)*.1,zig[i].y+(zig[i+1].y-zig[i].y)*.1,0};
        std::vector<Vec3> remaining={p};remaining.insert(remaining.end(),zig.begin()+static_cast<long long>(i)+1,zig.end());
        out=zigzag.observe(at(.1*static_cast<double>(i),p,remaining));
        if(interrupting(out))++turns;
    }
    require(turns==3,"short consecutive legs must each announce their turn without a cooldown");

    // Camera orientation: one quiet update after a clear 90-degree change.
    NavigationFollower camera;
    camera.start(Mode::Manual,at(0,{0,0,0},{{0,0,0},{2000,0,0}}));
    int updates=0;bool quiet=true;
    for(int i=1;i<=20;++i) {
        auto f=at(i*.25,{i*5.,0,0},{{i*5.,0,0},{2000,0,0}});
        if(i>=4){f.path.right={0,1,0};f.path.up={-1,0,0};}
        out=camera.observe(f);
        if(!out.speech.empty()){++updates;quiet=quiet&&!out.speech.front().interrupt;}
    }
    require(updates==1&&quiet,"a clear camera orientation change must update guidance once, without interrupting");

    // Small endpoint movement is not a new instruction; explicit status still repeats.
    NavigationFollower endpoint;
    endpoint.start(Mode::Manual,at(0,{0,0,0},{{0,0,0},{2000,0,0}}));
    out=endpoint.observe(at(.25,{5,0,0},{{5,0,0},{2030,20,0}}));
    require(out.speech.empty(),"sub-metre endpoint jitter must not re-announce");
    out=endpoint.observe(at(.5,{10,0,0},{{10,0,0},{2030,20,0}}),true);
    require(speech(out).find("right")!=std::string::npos&&interrupting(out),"explicit status must always repeat guidance");
}
void manual_without_ground_proof() {
    // Stairs, a vertical leg and a 60 m route: valid for manual guidance only.
    const std::vector<Vec3> stairs={{0,0,0},{100,0,0},{100,0,60},{100,3000,60},{100,6000,400}};
    auto f=at(0,{0,0,0},stairs);f.walking_path=false;
    NavigationFollower manual;
    auto out=manual.start(Mode::Manual,f);
    require(out.mode==Mode::Manual&&!out.movement,"manual guidance must not require the automatic ground proof");
    f=at(.25,{50,0,0},{{50,0,0},{100,0,0},{100,0,60},{100,3000,60},{100,6000,400}});f.walking_path=false;
    require(manual.observe(f).mode==Mode::Manual,"manual guidance must continue on a fresh complete path without ground proof");
    f=at(.5,{100,1,0},{{100,1,0},{100,0,60},{100,3000,60},{100,6000,400}});f.walking_path=false;
    out=manual.observe(f);
    require(out.mode==Mode::Manual&&speech(out).find("higher ground")!=std::string::npos,"manual guidance must speak a vertical leg as higher ground");

    NavigationFollower automatic;
    f=at(0,{0,0,0},stairs);f.walking_path=true;
    require(automatic.start(Mode::Automatic,f).mode==Mode::Off,"automatic walking must still reject vertical legs");
    f.walking_path=false;f.path.points={{0,0,0},{300,0,0}};
    require(automatic.start(Mode::Automatic,f).mode==Mode::Off,"automatic walking must still require the ground proof");

    for(int guard=0;guard<6;++guard) {
        NavigationFollower s;
        f=at(0,{0,0,0},stairs);f.walking_path=false;
        if(guard==0)f.path.valid=false;
        if(guard==1)f.path.partial=true;
        if(guard==2)f.path.points[2].z=std::numeric_limits<double>::quiet_NaN();
        if(guard==3)f.path_sample_seconds=1;
        if(guard==4)f.path.points={{900,0,0},{1200,0,0}};
        if(guard==5)f.world.clear();
        require(s.start(Mode::Manual,f).mode==Mode::Off,"manual guidance must still reject invalid, partial, nonfinite, stale, distant or contextless paths");
    }
    NavigationFollower stale;f=at(0,{0,0,0},stairs);f.walking_path=false;stale.start(Mode::Manual,f);
    f=at(.7,{0,0,0},stairs);f.path_sample_seconds=0;f.walking_path=false;
    require(stale.observe(f).mode==Mode::Off,"manual guidance must stop on a stale path");
}
void active_target_following() {
    NavigationFollower unavailable;
    auto f=frame();f.target_available=false;
    auto out=unavailable.start(Mode::Manual,f);
    require(out.mode==Mode::Off&&speech(out).find("Target is no longer available")!=std::string::npos,
        "an inactive or unloaded target must revoke its old route instead of following a frozen point");
    NavigationFollower moving;
    require(moving.start(Mode::Manual,frame()).mode==Mode::Manual,"an available area target must start guidance");
    f=at(.2,{0,0,0},{{0,0,0},{0,800,0}});
    out=moving.observe(f);
    require(out.mode==Mode::Manual,"a live target's changed position must remain followable without a visibility transition");
    f=at(.3,{0,0,0},{{0,0,0},{0,800,0}});f.target_available=false;f.interaction_available=true;
    out=moving.observe(f);
    require(out.mode==Mode::Off&&speech(out).find("Interaction available")==std::string::npos,
        "old interaction data must not keep an unavailable target alive");
    NavigationFollower prompt;f=frame();f.interaction_available=true;
    out=prompt.start(Mode::Manual,f);
    require(out.mode==Mode::Off&&speech(out).find("Interaction available")!=std::string::npos,
        "a native nearby prompt on an available area target must be respected");
    NavigationFollower moved;moved.start(Mode::Manual,frame());f=frame(.1);f.world="field-2";
    require(moved.observe(f).mode==Mode::Off,"an area target from another world must not be followed");
    NavigationFollower retarget;retarget.start(Mode::Automatic,frame());f=frame(.1);f.target="target-2";
    require(retarget.observe(f).mode==Mode::Off,"a different selection must not reuse the previous route");
}
void native_route_failure_feedback() {
    NavigationFollower nav;auto f=frame();f.path.valid=false;
    f.route_unavailable_reason="The game returned no complete walking path to this target.";
    const auto out=nav.start(Mode::Manual,f);
    require(out.mode==Mode::Off&&speech(out).find(f.route_unavailable_reason)!=std::string::npos,
        "a captured native path failure must be spoken instead of collapsing every cause to no path");
}
void verified_capsule_routes() {
    GroundProbe physical;
    physical.project=[](Vec3 sample)->std::optional<Vec3> {
        // A 1:2 ramp and an unwalkable drop farther ahead.
        const auto height=sample.x>=800?-300.:std::clamp((sample.x-200)*.5,0.,100.);
        return Vec3{sample.x,sample.y,height};
    };
    physical.blocked=[](Vec3 a,Vec3 b)->std::optional<bool> {
        return a.x<480&&b.x>320&&std::max(a.y,b.y)>120.;
    };
    require(!checked_walking_leg({0,0,0},{700,250,100},100,.71,physical),
        "a walkable bake must not excuse a swept capsule hitting a wall");
    const auto first=checked_walking_leg({0,0,0},{520,0,100},100,.71,physical);
    const auto corner=first?checked_walking_leg(*first,{700,250,100},100,.71,physical):std::nullopt;
    require(corner&&corner->x==700&&corner->y==250&&corner->z==100,
        "two clear capsule legs must take the ramp around the wall");
    require(!checked_walking_leg(*corner,{850,250,100},100,.71,physical),
        "a floor query must not bridge a drop");
    GroundProbe stair{
        [](Vec3,Vec3)->std::optional<bool>{return false;},
        [](Vec3 p)->std::optional<Vec3>{p.z=p.x>=40?35:0;return p;},.45};
    require(!checked_walking_leg({0,0,0},{80,0,35},100,.71,stair),"a steep step needs an identified native stair");
    stair.native_step=[](Vec3 a,Vec3 b){return a.x<40&&b.x>=40;};
    require(checked_walking_leg({0,0,0},{80,0,35},100,.71,stair).has_value(),
        "the audited stair can use the verified character step height");
    stair.blocked=[](Vec3 a,Vec3 b)->std::optional<bool>{return a.x<40&&b.x>=40;};
    require(!checked_walking_leg({0,0,0},{80,0,35},100,.71,stair),"the stair still requires a clear capsule sweep");
    physical.project=[](Vec3 p)->std::optional<Vec3> {p.y+=3;return p;};
    require(!checked_walking_leg({0,0,0},{40,0,0},100,.71,physical),
        "the floor proof must match the requested XY trace");
}
void nearby_target_does_not_require_a_route() {
    auto f=frame();
    f.interaction_available=true;f.path.valid=false;f.ground.reset();
    f.route_unavailable_reason="The player's walking surface is unavailable.";
    for(const auto mode:{Mode::Manual,Mode::Automatic}) {
        NavigationFollower nav;
        const auto out=nav.start(mode,f);
        require(!out.movement&&out.mode==Mode::Off&&speech(out).find("Interaction available")!=std::string::npos,
            "native interaction must end navigation even when the target centre cannot be routed");
    }
}
void walking_path_around_a_town_building() {
    const Vec3 entrance{100,200,0},door{1050,200,0};
    const std::array approaches{door};
    bool closed=false;
    const GroundProbe floor{
        [&](Vec3 a,Vec3 b)->std::optional<bool> {
            return std::min(a.x,b.x)<760&&std::max(a.x,b.x)>350&&
                (closed||std::min(a.y,b.y)<480);
        },
        [](Vec3 p)->std::optional<Vec3> {
            return p.x>=0&&p.x<=1200&&p.y>=0&&p.y<=800?std::optional{p}:std::nullopt;
        }};
    const auto walk=[&](Vec3 from,Vec3 to){return checked_walking_leg(from,to,100.,.71,floor);};
    const auto at_door=[&](Vec3 feet){return std::hypot(feet.x-door.x,feet.y-door.y)<=1.;};
    require(!walk(entrance,door),"the direct approach crosses the building");
    const auto route=checked_walking_path(entrance,approaches,80.,160,walk,at_door);
    require(route&&route->size()>2&&at_door(route->back()),
        "the buffer must find the door around the building from the entrance");
    for(std::size_t i=1;i<route->size();++i)
        require(walk((*route)[i-1],(*route)[i]).has_value(),"every leg needs its own walking proof");
    closed=true;
    require(!checked_walking_path(entrance,approaches,80.,160,walk,at_door),
        "a door beyond a solid wall has no walking approach");
}
void capsule_route_renewal_keeps_the_turn() {
    const std::vector<Vec3> points{{0,0,0},{300,0,0},{300,600,0}};
    NavigationFollower nav;
    require(nav.start(Mode::Manual,at(0,{0,0,0},points)).mode==Mode::Manual,
        "the original route must be accepted");
    require(nav.observe(at(.3,{299,0,0},points)).mode==Mode::Manual,
        "the player can advance past the turn when only the proof time changes");
    const auto next=nav.leg();
    require(next&&next->second.y==600,"the next waypoint must be the destination after the turn");
    require(nav.observe(at(.6,{300,200,0},points)).mode==Mode::Manual,
        "a renewed route must remain valid after leaving its original start");
}
void slow_progress_into_a_narrow_interaction() {
    const std::vector<Vec3> path{{285,0,0},{300,0,0}};
    auto f=at(0,{285,0,0},path);f.arrival_metres=.03;
    NavigationFollower nav;
    require(nav.start(Mode::Automatic,f).mode==Mode::Automatic,
        "the short verified approach must start");
    f=at(1.6,{285.4,0,0},path);f.arrival_metres=.03;
    require(nav.observe(f).mode==Mode::Automatic,
        "sub-centimetre progress toward a close native interaction must continue");
    f=at(3.2,{285.4,1,0},path);f.arrival_metres=.03;
    const auto stalled=nav.observe(f);
    require(stalled.mode==Mode::Automatic&&stalled.replan==NavigationReplan::Blocked&&!stalled.movement,
        "sideways movement alone cannot reset the close-approach stall timer");
}
void reroute_from_live_position() {
    NavigationFollower manual;
    manual.start(Mode::Manual,frame());
    auto f=frame(.1,{75,320,0});
    auto update=manual.observe(f);
    require(update.mode==Mode::Manual&&update.replan==NavigationReplan::OffRoute&&!update.movement,
        "manual deviation must keep the target and ask for an immediate fresh route");
    require(manual.target()==f.target,"rerouting must retain the chosen target");
    f.path.valid=false;
    f.route_unavailable_reason="A clear walking approach for the player could not be verified.";
    update=manual.observe(f);
    require(update.mode==Mode::Manual&&!update.movement&&speech(update).find("Waiting for a walking route")!=std::string::npos,
        "a temporary route query failure holds guidance with feedback");
    f.sample_seconds=.2;
    require(manual.observe(f).speech.empty(),"a waiting route must not repeat its message each tick");
    f=at(.3,{0,370,0},{{0,370,0},{-200,400,0}});
    f.path_id=2;
    update=manual.observe(f);
    require(update.mode==Mode::Manual&&!update.movement&&speech(update).find("left 2 metres")!=std::string::npos,
        "guidance resumes from the new live position");
    f.field_eligible=false;
    require(manual.observe(f).mode==Mode::Off,"an ineligible field frame cannot drive guidance");

    NavigationFollower suspended;
    f=frame();f.use_beacon=true;
    suspended.start(Mode::Manual,f);
    suspended.pause();
    require(suspended.mode()==Mode::Manual&&!suspended.leg()&&
        suspended.target()==f.target,"a temporary interruption silences guidance and retains the selected target");
    f=at(2.0,{0,370,0},{{0,370,0},{-200,400,0}});
    require(speech(suspended.observe(f)).find("left 2 metres")!=std::string::npos,
        "resuming uses a new route from the position where field control returned");
    suspended.pause();
    f.world="another-world";
    require(suspended.observe(f).mode==Mode::Off,"a temporary pause cannot allow guidance into a different world");

    NavigationFollower refreshed_manual;
    refreshed_manual.start(Mode::Manual,at(0,{0,0,0},{{0,0,0},{1500,0,0}}));
    f=at(.25,{25,100,0},{{25,100,0},{1500,0,0}});f.path_id=2;
    require(refreshed_manual.observe(f).speech.empty(),"a small shift on the route must stay quiet");
    f=at(.5,{25,320,0},{{25,320,0},{1500,0,0}});f.path_id=3;
    update=refreshed_manual.observe(f);
    require(update.mode==Mode::Manual&&interrupting(update),
        "a significant deviation must speak fresh guidance even if a periodic replan already refreshed the same endpoint");
    f=at(.75,{25,323,0},{{25,323,0},{1500,0,0}});f.path_id=4;
    require(refreshed_manual.observe(f).speech.empty(),"the new spoken route must not repeat for small shifts");

    NavigationFollower automatic;
    automatic.start(Mode::Automatic,frame());
    automatic.pause();
    require(automatic.mode()==Mode::Automatic&&!automatic.leg(),
        "a pause keeps the automatic request without an active walking leg");
    f=frame(.25,{-35,0,0});f.path.valid=false;f.route_unavailable_reason="No complete route.";
    update=automatic.observe(f);
    require(update.mode==Mode::Automatic&&!update.movement,
        "losing the current route must stop pulses while retaining the automatic request");
    f=at(2.0,{-35,0,0},{{-35,0,0},{-200,0,0},{-200,400,0}});f.path_id=2;f.ground->path_id=2;
    update=automatic.observe(f);
    require(update.mode==Mode::Automatic&&update.movement,
        "valid recovery can resume after the ordinary no-progress timeout");
    f.manual_input=true;
    require(automatic.observe(f).mode==Mode::Off,"genuine movement must still take over after a replan");
}
void target_read_failure_feedback() {
    NavigationFollower nav;auto f=frame();nav.start(Mode::Automatic,f);
    f=frame(.1);f.target_available=false;f.target_unavailable_reason="The target position could not be read. Try again.";
    const auto out=nav.observe(f);
    require(out.mode==Mode::Off&&!out.movement&&speech(out).find(f.target_unavailable_reason)!=std::string::npos,
        "unknown target reads must stop movement and speak their reason instead of claiming the target vanished");
    f=frame(.2);
    require(nav.observe(f).mode==Mode::Off&&!nav.observe(f).movement,"read recovery must never resume automatic walking by itself");
}
void speech_output_priority() {
    NavigationFollower nav;nav.start(Mode::Manual,frame());
    ReaderCore core;
    Frame native{"same-widget",MenuSnapshot{"choices","Continue","",0,2,true,true,true},
        DialogueSnapshot{"line-2","","A native caption.",true,true}};
    SpeechQueue queue;
    for(const auto& event:core.observe(native))queue.push(event);
    const auto stopped=nav.stop("Navigation stopped. A scripted scene is active.");
    require(!stopped.speech.empty()&&stopped.speech.front().interrupt,"the follower stop must retain its default explicit priority");
    for(const auto& event:navigation_speech_for_output(stopped,true))queue.push(event);
    auto event=queue.pop();
    require(event&&event->text=="A native caption.","a passive navigation stop must not erase the first native caption");
    event=queue.pop();
    require(event&&event->text=="Continue. 1 of 2","the current native choice must precede a passive stop notice");
    event=queue.pop();
    require(event&&event->text=="Navigation stopped. A scripted scene is active."&&!event->interrupt,
        "passive stop feedback must follow native dialogue and choices without interrupting");
    require(!queue.pop()&&core.observe(native).empty(),"native captions must survive their first capture because later captures dedupe them");
    require(stopped.speech.front().interrupt,"adapting output must not mutate the original update");

    queue.push({"A native caption.",true});
    for(const auto& output:navigation_speech_for_output(stopped,false))queue.push(output);
    event=queue.pop();
    require(event&&event->text=="Navigation stopped. A scripted scene is active."&&event->interrupt&&!queue.pop(),
        "an explicit stop must retain its interrupting command priority");

    NavigationFollower turn;
    turn.start(Mode::Manual,at(0,{0,0,0},{{0,0,0},{300,0,0},{300,300,0}}));
    const auto running=turn.observe(at(.5,{302,4,0},{{302,4,0},{300,300,0}}));
    require(running.mode==Mode::Manual&&interrupting(running),"the priority check needs an actual running real-turn instruction");
    const auto instruction=navigation_speech_for_output(running,true);
    require(!instruction.empty()&&instruction.front().interrupt,"passive output must not lower an important running turn instruction");
    queue.push({"Earlier speech",true});for(const auto& output:instruction)queue.push(output);
    event=queue.pop();
    require(event&&event->text==instruction.front().text&&event->interrupt,"a real turn must retain interrupting queue behavior");
    require(navigation_speech_for_output(NavigationUpdate{},true).empty(),"a silent stop must not invent a spoken notice");
}
void directional_beacon() {
    for (auto mode : {Mode::Manual,Mode::Automatic}) {
        NavigationFollower nav;
        auto f=frame();
        f.use_beacon=true;
        const auto start=nav.start(mode,f);
        require(start.mode==mode&&start.beacon&&near(start.beacon->azimuth_degrees,-90)
            &&near(start.beacon->remaining_metres,6),
            "the first pulse must point left to the corner and use the distance around it");
        require(start.speech.size()==1&&speech(start).find("Door")!=std::string::npos
            &&speech(start).find("metres")==std::string::npos,"start announces the selected target once");
        require(start.movement.has_value()==(mode==Mode::Automatic),"a beacon must preserve movement ownership");
        f=frame(.3,{-200,0,0});
        f.use_beacon=true;
        const auto turn=nav.observe(f);
        require(turn.beacon&&near(turn.beacon->azimuth_degrees,0)&&turn.speech.empty()
            &&route_beacon_interval_ms(turn.beacon->remaining_metres)<route_beacon_interval_ms(start.beacon->remaining_metres),
            "progress through the corner must turn and accelerate the cue without speech");
        f.sample_seconds=f.path_sample_seconds=.4;
        f.path.right={0,1,0};
        f.path.up={-1,0,0};
        checked_ground(f);
        const auto turned_camera=nav.observe(f);
        require(turned_camera.beacon&&near(turned_camera.beacon->azimuth_degrees,90),
            "the live control axes must turn the same leg to the right");
        require(!nav.observe(f,true).speech.empty(),"explicit directions must stay available");
        f.use_beacon=false;
        require(!nav.observe(f).speech.empty(),"failure of beacon playback must restore spoken directions");
        f.use_beacon=true;
        f.sample_seconds=f.path_sample_seconds=.5;
        f.interaction_available=true;
        const auto arrival=nav.observe(f);
        require(arrival.mode==Mode::Off&&!arrival.beacon&&speech(arrival).find("At Door")!=std::string::npos,
            "arrival must clear the cue and keep interaction feedback");
    }
    NavigationFollower nav;
    auto f=frame();
    f.use_beacon=true;
    nav.start(Mode::Manual,f);
    f.position={250,0,0};f.sample_seconds=.1;
    const auto replan=nav.observe(f);
    require(replan.replan==NavigationReplan::OffRoute&&!replan.beacon,"discard the old cue while rerouting");
    f.path.valid=false;f.route_unavailable_reason="No complete route.";
    const auto waiting=nav.observe(f);
    require(!waiting.beacon&&speech(waiting).find("Waiting")!=std::string::npos,"route problems keep speech feedback");
    require(route_beacon_interval_ms(0)==220&&route_beacon_interval_ms(1e9)<=1200,
        "the cadence must allow the cue to finish and remain bounded at long distances");

    NavigationFollower detour;
    f=at(0,{0,0,0},{{0,0,0},{4000,0,0},{4000,200,0},{0,200,0}});
    f.use_beacon=true;
    const auto around_wall=detour.start(Mode::Manual,f);
    require(around_wall.beacon&&speech(around_wall).find("Walking route is 82 metres.")!=std::string::npos,
        "a long detour to an adjacent target must explain the full walking distance at beacon start");

    NavigationFollower steep;
    f=at(0,{0,0,0},{{0,0,0},{100,0,1000}});
    f.use_beacon=true;
    require(steep.start(Mode::Manual,f).beacon.has_value(),"a directional leg starts the beacon");
    f.sample_seconds=.1;f.position={20,0,0};
    auto vertical=steep.observe(f);
    require(!vertical.beacon&&!vertical.speech.empty(),"a leg without horizontal guidance can be spoken");
    f.sample_seconds=.2;f.position={0,0,0};
    require(steep.observe(f).beacon.has_value(),"the cue resumes when a bearing returns");
    f.sample_seconds=.3;f.position={20,0,0};
    vertical=steep.observe(f);
    require(!vertical.beacon&&vertical.speech.empty(),"a brief loss of bearing must not repeat the same directions");
}
}
int main() {
    try {
        native_walking_basis();activation_endpoint_arrival();native_agent_and_speed_policy();capsule_speed_bound();manual_and_auto();corners_and_replanning();cancellation_guards();freshness_blocking_and_geometry();arrival_and_explicit_mode_switch();
        chatter_and_turns();manual_without_ground_proof();active_target_following();native_route_failure_feedback();target_read_failure_feedback();speech_output_priority();surface_following_corridor_and_progress();
        verified_capsule_routes();walking_path_around_a_town_building();nearby_target_does_not_require_a_route();capsule_route_renewal_keeps_the_turn();
        slow_progress_into_a_narrow_interaction();reroute_from_live_position();directional_beacon();rowing_around_obstacles();
        std::cout<<"Navigation follower checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
