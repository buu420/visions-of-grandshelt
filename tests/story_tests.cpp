#include "../src/StoryInputValidation.hpp"
#include "../src/OpeningStoryValidation.hpp"
#include "resonance/Story.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
using namespace resonance;
void checked_walking_ground(NavigationFrame& f) {
    f.path_id=1;
    GroundProbe probe{[](Vec3,Vec3)->std::optional<bool>{return false;},[](Vec3 p)->std::optional<Vec3>{return p;}};
    f.ground=plan_ground_coverage(f.path,f.path_id,GroundBudget{},probe);
}
namespace {
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
StorySnapshot objective(std::string id="one",std::string text="Reach the gate.") {
    StorySnapshot s; s.world="field-a";s.player="player-a";s.objective_id=std::move(id);s.objective=std::move(text);s.ready=true;
    s.map_id=1;s.destination_epoch="filtered-destinations-one";
    StoryDestination d;d.id="gate";d.label="Story marker";d.position=Vec3{100,200,0};d.disclosed=true;d.local=true;d.actor_available=true;
    s.destinations.push_back(d);return s;
}
}
int main() {
    int failed=0;
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
        {"resume a selected or unambiguous Story target",[] {
            auto s=objective();
            auto other=s.destinations.front();other.id="side-gate";
            s.destinations.push_back(other);
            const auto same=continue_story_target(s,"gate");
            require(same&&same->id=="gate","A returning Story walk keeps the still-current target");
            require(!continue_story_target(s,"old-portal"),"Two eligible successors need a player choice");
            s.destinations.front().actor_available=false;
            const auto successor=continue_story_target(s,"gate");
            require(successor&&successor->id=="side-gate","The sole verified successor can continue the route");
            s.destination_epoch.clear();
            require(!continue_story_target(s,"side-gate"),"Unread filtered destinations cannot resume walking");
        }},
        {"first objective and dedupe",[] {
            StoryTracker tracker;auto s=objective();auto u=tracker.observe(s,true,1);
            require(u.speech.size()==1&&u.speech[0].text=="Current objective. Reach the gate.","first available objective must read");
            require(!u.speech[0].interrupt,"objective announcements must not interrupt");
            require(tracker.observe(s,true,1).speech.empty(),"unchanged objective must stay quiet");
            require(tracker.snapshot().ready,"current native snapshot must be retained");
        }},
        {"local Story step changes once and retires the previous route",[] {
            StoryTracker tracker;auto s=objective("shrine","Go deeper into the Wind Shrine.");
            s.description="A chapter summary that is too long for a route prompt.";
            s.next_step="Use the call pedestal.";
            const auto call=tracker.observe(s,true,1);
            require(call.speech.size()==1&&story_details_status(s)=="Current objective. Go deeper into the Wind Shrine. Use the call pedestal.",
                "show the current action without rereading the journal summary");
            auto boarding=s;
            boarding.next_step="Board the elevator.";
            boarding.destination_epoch+="|board";
            boarding.destinations.front().id="lift";
            auto changed=tracker.observe(boarding,false,1);
            require(changed.destination_changed&&changed.navigation_invalidated&&!changed.objective_changed&&changed.speech.empty(),
                "calling the lift invalidates the old route but still respects the native speech gate");
            changed=tracker.observe(boarding,true,1);
            require(changed.speech.size()==1&&changed.speech[0].text.ends_with("Board the elevator.")&&
                tracker.observe(boarding,true,1).speech.empty(),"announce a stable boarding step once");
        }},
        {"a nearby clue speaks once without changing the route",[] {
            StoryTracker tracker;auto s=objective("escape","Escape before time runs out.");
            tracker.observe(s,true,1);
            s.nearby_clue="Painted floor number.";
            require(tracker.needs_announcement(s,1)&&tracker.observe(s,false,1).speech.empty(),
                "clues wait behind the same native dialogue and menu gate as objectives");
            const auto clue=tracker.observe(s,true,1);
            require(clue.speech.size()==1&&clue.speech[0].text==s.nearby_clue&&!clue.navigation_invalidated,
                "the clue does not change the current destination");
            const auto visited=s;s.nearby_clue.clear();tracker.observe(s,true,1);
            require(tracker.observe(visited,true,1).speech.empty(),"pacing near a marking must not repeat it");
        }},
        {"explicit Story details preserve current journal text without passive chatter",[] {
            auto s=objective();s.description="The current path leads to the gate.";s.destination_title="Gate Square";
            require(story_details_status(s)=="Current objective. Reach the gate. The current path leads to the gate. Destination. Gate Square",
                "explicit review should include only the captured current journal description and filtered goal title");
            require(story_status(s)=="Current objective. Reach the gate.","passive objective speech must stay concise");
            StoryTracker tracker;tracker.observe(s,true,1);s.description="An updated current journal summary.";
            const auto update=tracker.observe(s,true,1);
            require(update.speech.empty()&&!update.navigation_invalidated,"journal wording must not change native target identity or repeat passive objective speech");
            s.description=s.objective;s.destination_title=s.objective;
            require(story_details_status(s)==story_status(s),"already spoken objective text must not be appended again");
            s.ready=false;s.description="Unreadable retained journal text";s.destination_title="Unreadable retained title";
            require(story_details_status(s).find("retained")==std::string::npos,"an unready capture cannot disclose old journal strings");
        }},
        {"opening Story events use current control-relative geometry without claiming a route",[] {
            auto s=objective();auto& destination=s.destinations[0];destination.id="main-quest:1:opening:0";
            destination.label="Story event";destination.position=Vec3{300,400,0};destination.interactable=true;
            const StorySpatialContext context{{0,0,0},{1,0,0},{0,1,0},100};
            const auto described=describe_story_destination("story:opening:0",s,destination,context);
            require(described.target.id=="story:opening:0"&&described.target.label=="Story event"&&described.target.interactable&&
                described.target.prompt=="Interaction available","spatial description must preserve selectable identity and native interaction availability");
            require(described.detail=="Control-relative up-right, 5 metres in a straight line",
                "current point direction and distance should distinguish generic events without inventing an action or path");
            destination.position->z=300;
            require(describe_story_destination("story:opening:0",s,destination,context).detail==
                "Control-relative up-right, 5.8 metres in a straight line, target point 3 metres higher",
                "vertical facts describe the native target point rather than claiming reachable higher ground");
            require(s.destination_epoch=="filtered-destinations-one"&&destination.id=="main-quest:1:opening:0",
                "presentation must leave native eligibility identity and ordering untouched");
        }},
        {"Story geometry follows current walking-input axes and verified scale",[] {
            auto s=objective();auto& destination=s.destinations[0];destination.label="Story event";
            const StorySpatialContext context{{10,20,30},{0,2,0},{-3,0,0},100};
            destination.position=Vec3{10,220,30};
            require(describe_story_destination("story:event",s,destination,context).detail==
                "Control-relative right, 2 metres in a straight line","native heading and normalized input axes must control direction");
            destination.position=Vec3{-90,20,30};
            require(describe_story_destination("story:event",s,destination,context).detail==
                "Control-relative up, 1 metre in a straight line","a native world unit scale must control metre conversion");
            destination.position=Vec3{10,20,330};
            require(describe_story_destination("story:event",s,destination,context).detail==
                "3 metres in a straight line, target point 3 metres higher","an exactly vertical point must not invent a horizontal direction");
        }},
        {"Story spatial uncertainty retains generic labels without guessed geometry",[] {
            auto s=objective();auto& destination=s.destinations[0];destination.label="Story event";
            const StorySpatialContext good{{0,0,0},{1,0,0},{0,1,0},100};
            require(describe_story_destination("story:event",s,destination).detail.empty(),"missing context must not guess camera orientation or scale");
            for(int guard=0;guard<7;++guard) {
                auto context=good;
                if(guard==0)context.world_to_metres=0;
                if(guard==1)context.world_to_metres=std::numeric_limits<double>::quiet_NaN();
                if(guard==2)context.origin.x=std::numeric_limits<double>::infinity();
                if(guard==3)context.right={0,0,0};
                if(guard==4)context.up=context.right;
                if(guard==5)context.up.y=std::numeric_limits<double>::quiet_NaN();
                if(guard==6)context.origin.x=-std::numeric_limits<double>::max();
                if(guard==6)destination.position->x=std::numeric_limits<double>::max();
                const auto described=describe_story_destination("story:event",s,destination,context);
                require(described.target.label=="Story event"&&described.target.visible&&described.detail.empty(),
                    "invalid native geometry must retain the authorized generic selection without spatial claims");
            }
            destination.position=Vec3{300,400,0};destination.actor_available=false;
            require(describe_story_destination("story:event",s,destination,good).detail.empty(),"inactive actor coordinates cannot provide spatial labels");
            destination.actor_available=true;s.map_id=-1;
            require(describe_story_destination("story:event",s,destination,good).detail.empty(),"unknown map coordinates cannot provide spatial labels");
            s.map_id=1;s.destination_epoch.clear();
            require(describe_story_destination("story:event",s,destination,good).detail.empty(),"unverified current destinations cannot provide spatial labels");
        }},
        {"Story labels use only the current authorized destination",[] {
            auto s=objective();auto stale=s.destinations[0];stale.label="Old undisclosed label";stale.position=Vec3{1000,1000,0};
            s.destinations[0].label="Story event";s.destinations[0].position=Vec3{-400,0,0};
            const StorySpatialContext context{{0,0,0},{1,0,0},{0,1,0},100};
            const auto described=describe_story_destination("story:event",s,stale,context);
            require(described.target.label=="Story event"&&described.detail=="Control-relative left, 4 metres in a straight line",
                "cached caller labels and points cannot replace the current captured destination");
            stale.id="foreign";
            auto denied=describe_story_destination("story:foreign",s,stale,context);
            require(!denied.target.visible&&denied.target.label.empty()&&denied.detail.empty(),"an unrelated destination cannot manufacture a Story description");
            stale=s.destinations[0];s.destinations[0].disclosed=false;
            denied=describe_story_destination("story:hidden",s,stale,context);
            require(!denied.target.visible&&denied.detail.empty(),"undisclosed destination data cannot produce a description");
        }},
        {"latest objective waits for native UI",[] {
            StoryTracker tracker;tracker.observe(objective(),true,1);
            require(tracker.observe(objective("two","Speak to the guard."),false,1).speech.empty(),"busy UI must defer objectives");
            require(tracker.observe(objective("three","Leave through the gate."),false,1).speech.empty(),"latest pending objective must remain quiet");
            auto u=tracker.observe(objective("three","Leave through the gate."),true,1);
            require(u.speech.size()==1&&u.speech[0].text.find("Leave through the gate.")!=std::string::npos,"only latest objective should flush");
            require(tracker.observe(objective("three","Leave through the gate."),true,1).speech.empty(),"pending objective must flush once");
        }},
        {"progress invalidates old route",[] {
            StoryTracker tracker;tracker.observe(objective(),true,1);
            auto u=tracker.observe(objective("two","Speak to the guard."),false,1);
            require(u.objective_changed&&u.navigation_invalidated,"objective changes must immediately invalidate navigation even while busy");
            auto s=objective("two","Speak to the guard.");s.world="field-b";
            u=tracker.observe(s,false,1);require(u.context_changed&&u.navigation_invalidated,"area changes must invalidate Story points");
            s.player="player-b";u=tracker.observe(s,false,1);
            require(u.context_changed&&u.navigation_invalidated,"player changes must invalidate Story points");
        }},
        {"same objective text with changed destinations revokes quietly",[] {
            StoryTracker tracker;auto s=objective();tracker.observe(s,true,1);
            s.destination_epoch="filtered-destinations-two";
            auto u=tracker.observe(s,true,1);
            require(u.destination_changed&&u.navigation_invalidated&&!u.objective_changed,"native filtered destination changes must revoke the route independently of text");
            require(u.speech.empty(),"unchanged HUD title/subtitle must not be repeated for a marker-only change");
            s.destination_epoch.clear();u=tracker.observe(s,false,1);
            require(u.navigation_invalidated,"unreadable destination epoch must revoke a previously known route");
        }},
        {"same world map transitions and unknown areas cannot reuse points",[] {
            StoryTracker tracker;auto s=objective();tracker.observe(s,true,1);
            s.map_id=2;auto u=tracker.observe(s,false,1);
            require(u.context_changed&&u.navigation_invalidated,"streamed map changes must invalidate routes even with the same world and pawn");
            s.map_id=-1;require(!story_route_available(s,s.destinations[0]),"unknown map cannot authorize a finite actor point");
            require(!story_destinations(s)[0].position,"unknown map must remove route coordinates from selection");
            s=objective();s.destination_epoch.clear();
            require(!story_route_available(s,s.destinations[0]),"unknown filtered destination identity cannot authorize a point");
            require(story_status(s).find("Reach the gate.")!=std::string::npos,"destination uncertainty must retain the actual HUD objective");
        }},
        {"unknown capture revokes routes without false completion",[] {
            StoryTracker tracker;tracker.observe(objective(),true,1);
            auto unavailable=objective();unavailable.ready=false;
            auto u=tracker.observe(unavailable,true,1);
            require(u.navigation_invalidated&&u.speech.empty(),"unknown capture must revoke routes silently");
            require(!tracker.snapshot().ready&&tracker.snapshot().destinations.empty(),"unknown captures cannot retain routeable points");
            require(tracker.observe(objective(),true,1).speech.empty(),"brief unknown capture must not repeat a consumed objective");
            require(tracker.observe(objective("two","Speak to the guard."),true,1).speech.size()==1,"new objective after a gap must still read");
        }},
        {"unknown local capture retains known identity without retaining a route",[] {
            StoryTracker tracker;auto known=objective();tracker.observe(known,true,1);
            NavigationFrame f;f.world=known.world;f.player=known.player;f.target="story:gate";
            f.path.points={{0,0,0},{300,0,0}};f.path.world_to_metres=100;f.path.valid=true;f.field_eligible=true;f.walking_path=true;checked_walking_ground(f);
            NavigationFollower walking;require(walking.start(NavigationMode::Automatic,f).mode==NavigationMode::Automatic,"fixture needs active walking");
            auto unknown=known;unknown.map_id=-1;unknown.destination_epoch.clear();unknown.destinations.clear();
            unknown.unavailable_reason="The current story area could not be read.";
            require(!tracker.needs_announcement(unknown,1),"an unknown local read must not request another consumed objective announcement");
            const auto lost=tracker.observe(unknown,true,1);
            require(lost.navigation_invalidated&&!lost.context_changed&&!lost.objective_changed&&!lost.destination_changed,
                "unknown local values must withdraw routes without claiming positive identity changes");
            require(lost.speech.empty()&&tracker.snapshot().map_id==-1&&tracker.snapshot().destination_epoch.empty(),
                "the current snapshot must retain uncertainty without repeating the objective");
            for(const auto& destination:story_destinations(tracker.snapshot()))
                require(!destination.position&&!story_route_available(tracker.snapshot(),destination),"the last known identity cannot authorize stale coordinates");
            auto stopped=stop_changed_story_navigation(lost,true,walking);
            require(stopped&&stopped->mode==NavigationMode::Off&&!stopped->movement,"unknown local state must release walking immediately");
            const auto recovered=tracker.observe(known,true,1);
            require(!recovered.context_changed&&!recovered.objective_changed&&!recovered.destination_changed&&!recovered.navigation_invalidated&&recovered.speech.empty(),
                "an unchanged recovery must preserve the known identity and consumed announcement");
            require(walking.observe(f).mode==NavigationMode::Off&&!walking.observe(f).movement,"recovered native facts cannot restart walking");
        }},
        {"unknown epoch and map recover independently",[] {
            StoryTracker tracker;auto known=objective();tracker.observe(known,true,1);
            auto unknown=known;unknown.destination_epoch.clear();
            auto update=tracker.observe(unknown,true,1);
            require(update.navigation_invalidated&&!update.destination_changed&&!update.context_changed&&update.speech.empty(),
                "an unknown epoch must withdraw a route without changing its known identity");
            update=tracker.observe(known,true,1);
            require(!update.destination_changed&&!update.context_changed&&update.speech.empty(),"recovering the same known epoch must not look new");
            unknown=known;unknown.map_id=-1;unknown.destination_epoch="filtered-destinations-two";
            update=tracker.observe(unknown,true,1);
            require(update.destination_changed&&!update.context_changed&&update.navigation_invalidated&&update.speech.empty(),
                "a positive epoch change still invalidates while the current map is unknown");
            known.destination_epoch=unknown.destination_epoch;known.map_id=2;
            update=tracker.observe(known,true,1);
            require(update.context_changed&&!update.destination_changed&&update.navigation_invalidated&&update.speech.size()==1,
                "a positively different map after an unknown read must still announce and invalidate");
            known.map_id=3;known.destination_epoch.clear();update=tracker.observe(known,true,1);
            require(update.context_changed&&!update.destination_changed&&update.navigation_invalidated,
                "a known map change must not turn its unreadable epoch into a positive destination change");
            known.destination_epoch="filtered-destinations-three";update=tracker.observe(known,true,1);
            require(update.destination_changed&&update.navigation_invalidated&&update.speech.empty(),
                "the next readable epoch must still compare with the last known epoch across an area capture gap");
        }},
        {"unready source recovery compares against last known area and objective",[] {
            StoryTracker tracker;auto known=objective();tracker.observe(known,true,1);
            auto unavailable=known;unavailable.ready=false;unavailable.map_id=-1;unavailable.destination_epoch.clear();unavailable.destinations.clear();
            unavailable.unavailable_reason="The native current story objective could not be read safely.";
            auto update=tracker.observe(unavailable,true,1);
            require(update.navigation_invalidated&&!update.context_changed&&!update.objective_changed&&!update.destination_changed&&update.speech.empty(),
                "an unready source must stop routes without claiming known identity changes");
            update=tracker.observe(known,true,1);
            require(!update.context_changed&&!update.objective_changed&&!update.destination_changed&&update.speech.empty(),
                "the same native objective after an unready source must recover quietly");
            tracker.observe(unavailable,false,1);
            auto changed=objective("two","Speak to the guard.");changed.world="field-b";changed.player="player-b";changed.map_id=2;
            changed.destination_epoch="filtered-destinations-two";
            update=tracker.observe(changed,true,1);
            require(update.context_changed&&update.objective_changed&&update.destination_changed&&update.navigation_invalidated&&update.speech.size()==1,
                "positive new area, objective, and destination facts after a source failure must invalidate and announce");
            require(tracker.observe(changed,true,1).speech.empty(),"a real recovery change must announce only once");
        }},
        {"first known map resolves an announced unknown area without repetition",[] {
            StoryTracker tracker;auto unknown=objective();unknown.map_id=-1;unknown.destination_epoch.clear();unknown.destinations.clear();
            require(tracker.observe(unknown,true,1).speech.size()==1,"the current native objective remains readable before a map is identified");
            const auto known=objective();auto update=tracker.observe(known,true,1);
            require(!update.context_changed&&!update.destination_changed&&update.speech.empty(),
                "learning the first map and epoch must not repeat the already announced objective");
            auto next=known;next.map_id=2;update=tracker.observe(next,true,1);
            require(update.context_changed&&update.navigation_invalidated&&update.speech.size()==1,
                "a subsequent positively different map must still announce the objective in its new area");
        }},
        {"unheard objectives retry after backend recovery",[] {
            StoryTracker tracker;require(tracker.observe(objective(),false,1).speech.empty(),"unavailable output cannot consume speech");
            require(tracker.observe(objective(),true,2).speech.size()==1,"first heard capture must retry");
            require(tracker.observe(objective(),false,3).speech.empty(),"backend loss must remain quiet");
            require(tracker.observe(objective(),true,3).speech.size()==1,"new backend epoch must replay current objective once");
            require(tracker.observe(objective(),true,3).speech.empty(),"recovery must not chatter");
        }},
        {"only disclosed local finite points can route",[] {
            auto s=objective();auto d=s.destinations.front();
            require(story_route_available(s,d),"disclosed local point should be routeable");
            s.destinations[0].disclosed=false;require(!story_route_available(s,d),"caller cannot forge disclosure absent from current capture");
            require(story_destinations(s).size()==1&&!story_destinations(s)[0].position,"hidden destinations must yield only the actual objective");
            s=objective();s.destinations[0].local=false;d=s.destinations[0];
            require(!story_route_available(s,d),"world-map destinations cannot become local routes");
            require(!story_destinations(s)[0].position,"remote coordinates must be stripped");
            s=objective();s.destinations[0].position->x=std::numeric_limits<double>::quiet_NaN();
            require(!story_route_available(s,s.destinations[0]),"nonfinite markers must never route");
            require(!story_destinations(s)[0].position,"nonfinite points must be removed from selection facts");
        }},
        {"stale or ambiguous marker coordinates cannot route",[] {
            auto s=objective();auto old=s.destinations[0];s.destinations[0].position->x+=100;
            require(!story_route_available(s,old),"a cached point must match the current capture");
            require(story_route_available(s,s.destinations[0]),"the current disclosed point should remain available");
            s.destinations.push_back(old);
            auto choices=story_destinations(s);
            require(choices.size()==1&&!choices[0].position,"conflicting native identities must lose their route");
            require(!story_route_available(s,old),"ambiguous native markers cannot authorize a cached route");
            s=objective();s.world.clear();require(!story_route_available(s,s.destinations[0]),"a missing world cannot route");
        }},
        {"inactive area actor revokes Story coordinates",[] {
            auto s=objective();const auto earlier=s.destinations.front();
            s.destinations[0].actor_available=false;
            require(!story_route_available(s,s.destinations[0])&&!story_route_available(s,earlier),
                "an inactive Story actor cannot retain its former route even with finite cached coordinates");
            require(!story_destinations(s)[0].position,"selection must explain unavailability instead of keeping an inactive point");
            require(story_status(s).find("Reach the gate.")!=std::string::npos,"losing an actor must keep the actual objective readable");
        }},
        {"current objective stays accessible without a path",[] {
            auto s=objective();s.destinations.clear();s.unavailable_reason="No disclosed local destination was captured.";
            auto goals=story_destinations(s);
            require(goals.size()==1&&goals[0].label==s.objective&&!goals[0].position,"an objective without a path must remain selectable");
            require(!goals[0].unavailable_reason.empty(),"missing route must provide feedback");
            require(story_status(s)=="Current objective. Reach the gate.","objective text must not depend on having a path");
            s.ready=false;require(story_status(s).find("unavailable")!=std::string::npos,"unknown objective must explain unavailability");
            require(story_destinations(s).empty(),"unknown objective cannot yield stale choices");
        }},
        {"Story selection preserves native interaction availability",[] {
            auto s=objective();auto d=s.destinations[0];d.label="Person";d.actor_available=true;d.interactable=true;
            auto shown=story_target_snapshot("story:person",d);auto text=describe_target(shown);
            require(text&&*text=="Person. Interaction available","Story selection must retain the native interaction prompt");
            d.interactable=false;shown=story_target_snapshot("story:person",d);text=describe_target(shown);
            require(text&&*text=="Person"&&!shown.interactable,"unavailable interaction must stay silent");
            d.interactable=true;d.actor_available=false;shown=story_target_snapshot("story:person",d);
            require(!shown.interactable&&shown.prompt.empty(),"a destination without an available actor must not claim interaction");
            d.disclosed=false;require(!describe_target(story_target_snapshot("story:person",d)),"undisclosed Story targets cannot produce selection speech");
        }},
        {"target withdrawal and empty objective are explicit",[] {
            StoryTracker tracker;auto s=objective();tracker.observe(s,true,1);
            s.destinations.clear();auto u=tracker.observe(s,true,1);
            require(u.navigation_invalidated,"withdrawn marker must invalidate old Story routes");
            s.objective.clear();s.objective_id.clear();u=tracker.observe(s,true,1);
            require(u.navigation_invalidated&&u.speech.size()==1,"positive empty objective must revoke and report once");
            require(u.speech[0].text=="No current story objective is displayed.","empty objective must not guess completion");
            require(tracker.observe(s,true,1).speech.empty(),"empty objective must deduplicate");
        }},
        {"new objectives never inherit automatic walking",[] {
            NavigationFrame f;f.world="field-a";f.player="player-a";f.target="story:old";f.target_label="Story marker";
            f.path.points={{0,0,0},{300,0,0}};f.path.world_to_metres=100;f.path.valid=true;f.field_eligible=true;f.walking_path=true;checked_walking_ground(f);
            NavigationFollower walking;
            require(walking.start(NavigationMode::Automatic,f).mode==NavigationMode::Automatic,"fixture needs active ordinary auto-walking");
            StoryTracker tracker;tracker.observe(objective(),true,1);
            auto changed=tracker.observe(objective("two","Speak to the guard."),false,1);
            auto stopped=stop_changed_story_navigation(changed,true,walking);
            require(stopped&&stopped->mode==NavigationMode::Off&&stopped->stopped_auto&&!stopped->movement,"new objective must release old walking without producing input");
            f.target="story:new";f.sample_seconds=.1;f.path_sample_seconds=.1;
            require(walking.observe(f).mode==NavigationMode::Off&&!walking.observe(f).movement,"a new objective cannot restart or retarget walking");
            NavigationFollower ordinary;f.target="ordinary-npc";
            ordinary.start(NavigationMode::Automatic,f);
            require(!stop_changed_story_navigation(changed,false,ordinary)&&ordinary.mode()==NavigationMode::Automatic,"Story progress must not cancel unrelated selected-target walking");
            require(!stop_changed_story_navigation(changed,true,walking),"an already stopped mode must not repeat stop notices");
        }},
        {"one unreadable destination retains others and stops its old route",[] {
            using namespace story_input_detail;
            auto s=objective();auto other=s.destinations[0];other.id="other";other.position=Vec3{400,200,0};
            s.destinations.push_back(other);StoryTracker tracker;tracker.observe(s,true,1);
            NavigationFrame f;f.world=s.world;f.player=s.player;f.target="story:gate";f.target_label="Story destination";
            f.path.points={{0,0,0},{300,0,0}};f.path.world_to_metres=100;f.path.valid=true;f.field_eligible=true;f.walking_path=true;checked_walking_ground(f);
            NavigationFollower walking;require(walking.start(NavigationMode::Automatic,f).mode==NavigationMode::Automatic,"fixture must begin with active walking");
            StoryCandidate failed{s.destinations[0],{1,1},true},healthy{s.destinations[1],{1,2},true};
            const auto unavailable=capture_story_candidate(std::move(failed),[](StoryDestination&)->bool {
                throw std::runtime_error("The story target position could not be read.");
            });
            require(unavailable.has_value(),"the unreadable authorized destination must remain selectable");
            s.destinations=finalize_story_candidates({*unavailable,std::move(healthy)});
            const auto update=tracker.observe(s,false,1);auto stopped=stop_changed_story_navigation(update,true,walking);
            require(update.navigation_invalidated&&stopped&&stopped->mode==NavigationMode::Off&&!stopped->movement,
                "losing one target's readable position must release its old walking route without input");
            const auto choices=story_destinations(s);
            require(choices.size()==2&&choices[0].id=="gate"&&!choices[0].position&&!choices[0].unavailable_reason.empty()&&
                    story_route_available(s,choices[1]),
                "an unreadable target remains selectable while another verified destination keeps its route");
        }},
        {"opening menu preserves native epoch and passive speech gate",[] {
            using namespace story_input_detail;
            std::array evidence{OpeningContinuationEvidence{0,{2,0},"C01_001B","C01_001B","C01_001B",{10,20,0},true,true,true}};
            auto base=objective();base.map_id=10040;base.destinations.clear();base.destination_epoch="filtered:empty";
            const auto capture=[&](std::string& epoch) {
                epoch=opening_destination_epoch(epoch,1,10040,evidence);
                auto goal=objective().destinations.front();goal.id="opening:0";
                return std::vector<StoryDestination>{goal};
            };
            auto field=base;field.field_control=true;capture_opening_story(field,capture);
            StoryTracker tracker;tracker.observe(field,true,1);
            auto menu=base;menu.field_control=false;capture_opening_story(menu,capture);
            Frame ui;ui.menu=MenuSnapshot{};ui.menu->visible=true;
            const auto update=tracker.observe(menu,story_announcement_allowed(ui,true,true,menu.field_control,false),1);
            require(menu.destination_epoch==field.destination_epoch&&!update.destination_changed&&!update.navigation_invalidated&&update.speech.empty(),
                "a menu alone must keep native opening identity while still suppressing passive objective speech");
        }},
        {"help does not reveal objectives during locked scenes",[] {
            auto s=objective("next","Enter the next area.");s.field_control=false;
            auto help=story_help_status(s);
            require(help.find(s.objective)==std::string::npos&&help.find("unavailable")!=std::string::npos,"help cannot reveal a newly selected objective behind locked scene controls");
            s.field_control=true;require(story_help_status(s)==story_status(s),"help should read the objective during ordinary field play");
            s.ready=false;require(story_help_status(s).find("unavailable")!=std::string::npos,"missing capture must still provide feedback");
        }},
        {"pending announcements can request native voice checks",[] {
            StoryTracker tracker;auto s=objective();
            require(tracker.needs_announcement(s,1),"first objective needs a voice-readiness check");
            tracker.observe(s,true,1);require(!tracker.needs_announcement(s,1),"consumed objective must not cause continuous voice scans");
            s=objective("two","Speak to the guard.");tracker.observe(s,false,1);
            require(tracker.needs_announcement(s,1),"unheard changed objective must keep requesting readiness");
            tracker.observe(s,true,1);require(!tracker.needs_announcement(s,1),"heard objective must stop repeated voice scans");
            require(tracker.needs_announcement(s,2),"backend recovery needs a fresh readiness check");
            s.ready=false;require(!tracker.needs_announcement(s,2),"unknown objective must not scan voice objects");
        }},
        {"native voice tails defer current objectives",[] {
            Frame f;StoryTracker tracker;
            require(!story_announcement_allowed(f,true,true,true,false,false),"native ATC or voice tail must hold new objective speech even without a text window");
            auto s=objective();auto u=tracker.observe(s,story_announcement_allowed(f,true,true,true,false,false),1);
            require(u.speech.empty(),"playing or unknown voice must not consume an objective");
            u=tracker.observe(s,story_announcement_allowed(f,true,true,true,false,true),1);
            require(u.speech.size()==1,"objective should read once native voice is positively clear");
        }},
        {"native UI visibility and output gate announcements",[] {
            Frame f;
            require(story_announcement_allowed(f,true,true,true,false),"ordinary field should allow current objective speech");
            f.menu=MenuSnapshot{};f.dialogue=DialogueSnapshot{};
            require(story_announcement_allowed(f,true,true,true,false),"hidden optional captures must not suppress objectives");
            f.menu->visible=true;require(!story_announcement_allowed(f,true,true,true,false),"visible menu must defer objectives");
            f.menu->visible=false;f.dialogue->visible=true;require(!story_announcement_allowed(f,true,true,true,false),"visible dialogue must defer objectives");
            f.dialogue->visible=false;
            require(!story_announcement_allowed(f,false,true,true,false),"background game must stay quiet");
            require(!story_announcement_allowed(f,true,false,true,false),"unavailable speech must preserve pending objective");
            require(!story_announcement_allowed(f,true,true,false,false),"locked native field must defer objectives");
            require(!story_announcement_allowed(f,true,true,true,true),"recorded description must finish before objective speech");
        }},
        {"objectives preserve queued dialogue and choices",[] {
            ReaderCore core;SpeechQueue queue;StoryTracker tracker;
            Frame f;f.context="field-a";f.dialogue=DialogueSnapshot{"line","Guard","Please wait here.",true,true};
            for(auto& e:core.observe(f))queue.push(e);
            for(auto& e:tracker.observe(objective(),true,1).speech)queue.push(e);
            auto first=queue.pop();auto second=queue.pop();
            require(first&&first->text.find("Please wait here.")!=std::string::npos,"Story must not erase native dialogue");
            require(second&&second->text=="Current objective. Reach the gate."&&!second->interrupt,"Story follows native speech quietly");
            f.menu=MenuSnapshot{"choice","Wait","",0,2,true,true,true};
            for(auto& e:core.observe(f))queue.push(e);
            require(tracker.observe(objective("two","Speak to the guard."),false,1).speech.empty(),"a choice must hold new objective speech");
            require(queue.pop().has_value(),"native choice speech must survive");
        }}
    };
    for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    std::cout<<tests.size()-failed<<'/'<<tests.size()<<" Story groups passed\n";
    return failed?1:0;
}
