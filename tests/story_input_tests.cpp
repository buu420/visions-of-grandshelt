#include "../src/StoryInputValidation.hpp"
#include "../src/OpeningStoryValidation.hpp"
#include "../src/ChapterTwoStory.hpp"
#include "../src/ChapterThreeStory.hpp"
#include "../src/ChapterThreeWaterShrine.hpp"
#include "../src/ChapterThreeShrineBridges.hpp"
#include "../src/ChapterFourStory.hpp"
#include "../src/ChapterSixLab.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
using namespace resonance;
using namespace resonance::story_input_detail;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void ultima_story_uses_unlocked_clear_save_and_current_movement() {
    using Leg=UltimaStoryLeg;
    require(!ultima_story_leg(false,10,20001,1)&&!ultima_story_leg({},10,20001,1),
        "a visible post-credits route requires a verified cleared save");
    require(!ultima_story_leg(true,{},20001,1)&&!ultima_story_leg(true,0,20001,1),
        "wait for the game's radar scene before offering the route");
    require(ultima_story_leg(true,10,20002,1)==Leg::Deck&&
        ultima_story_leg(true,10,20001,1)==Leg::Helm,
        "a clear save follows the Enterprise deck and helm instead of the tower objective");
    require(ultima_story_leg(true,10,1000,2)==Leg::Flight&&
        ultima_story_leg(true,10,1000,1)==Leg::Crater,
        "Story switches from airship guidance to the world-map walking leg");
    require(!ultima_story_leg(true,10,3281,1)&&!ultima_story_leg(true,10,1000,{}),
        "other areas and unknown movement cannot select the crater walking leg");
    require(!ultima_story_leg(true,20,1000,1)&&!ultima_story_leg(true,30,1000,2),
        "the native battle and reward retire the approach route");
}
void research_lab_requires_current_clearance_and_room() {
    using namespace chapter_six_lab;
    State state;
    const auto choose=[&](int phase, int area, Vec3 point) {
        return next_leg(phase, locate(area, point), state);
    };
    const Vec3 hall{800,0,10};
    require(!choose(6030,0,hall), "unreadable clearance cannot select a pickup or a locked passage");
    state.cards={false,false,false};
    require(choose(6030,0,hall)==LegId::Card1, "guide to the visible first card from the entrance hall");
    state.cards[0]=true;
    require(!choose(6030,0,hall), "unknown door state cannot select the far side");
    state.doors[0]=false;
    require(choose(6030,0,hall)==LegId::Door1, "guide to the native west door with the first card");
    state.doors[0]=true;
    require(choose(6030,0,hall)==LegId::WestWing, "open west door exposes the west exit");
    state.cards[1]=true;
    state.doors[1]=false;
    require(choose(6030,0,hall)==LegId::Door2, "a copied save with level 2 starts at the east door");
    state.cards[2]=true;
    state.doors[2]=true;
    require(choose(6030,0,hall)==LegId::NorthWing, "level 3 clearance leads through the north wing");
    require(!choose(6020,0,hall)&&!choose(6030,99,hall),
        "wrong phase and unknown walking section do not supply a lab leg");
    state.cards[1]=false;
    require(choose(6030,1,{100,-5900,10})==LegId::ContainersCorridor&&
        choose(6030,1,{1350,-5500,80})==LegId::Card2,
        "only the connected side of the west wing offers the card");
    require(choose(6030,3,{1690,-9300,10})==LegId::ContainersWarp, "west lab offers its own portal");
    state.cards[1]=true;
    state.doors[3]=false;
    require(choose(6030,1,{1350,-5500,80})==LegId::CardReturnDoor&&
        choose(6030,1,{100,-5900,10})==LegId::WestReturn, "the level 2 card sends each side through its reachable return");
}
void research_lab_follows_native_puzzle_changes() {
    using namespace chapter_six_lab;
    State state;
    const auto choose=[&](int area, Vec3 point) {return next_leg(6030,locate(area,point),state);};
    const Vec3 floor{1650,-16900,80};
    const Vec3 shelf{1700,-13700,310};
    const Vec3 fenced{-675,-13630,0};
    require(!choose(4,floor), "unreadable console state suppresses a speculative puzzle step");
    state.consoles={false,false,false};
    require(choose(4,floor)==LegId::ConsoleWest&&choose(6,shelf)==LegId::FencedLadderDown&&
        choose(4,fenced)==LegId::ConsoleFenced,
        "the current floor, shelf, and fenced area each supply a physically local step");
    state.consoles[0]=true;
    require(choose(4,floor)==LegId::ConsoleSouth, "an activated console falls out of the route");
    state.consoles[1]=true;
    require(choose(4,floor)==LegId::ShelfLadderUp, "the remaining console is reached from the shelf ladder");
    state.consoles[2]=true;
    require(choose(4,fenced)==LegId::FencedLadderUp&&choose(6,shelf)==LegId::ShelfLadderDown&&
        choose(4,floor)==LegId::CenterWarp, "return via the same shelf and let the active portal lead onward");
    require(warp_allowed(0,1)&&warp_allowed(1,1)&&!warp_allowed(2,1)&&!warp_allowed(3,1)&&
        !warp_allowed(0,0), "a physically adjacent warp must accept the chosen native side");
}
void research_lab_routes_each_portal_landing_and_elevator() {
    using namespace chapter_six_lab;
    State state;
    const auto choose=[&](int phase, int area, Vec3 point) {return next_leg(phase,locate(area,point),state);};
    constexpr std::array positions{Vec3{103,6150,-100}, Vec3{445,7566,-105}, Vec3{-607,9884,-105},
        Vec3{419,8783,-105}, Vec3{1536,8785,-105}, Vec3{1426,7650,-170}};
    constexpr std::array steps{LegId::EastWarp1,LegId::EastWarp2,LegId::EastWarp3,
        LegId::EastWarp4,LegId::EastWarp5,LegId::EastLadderUp};
    require(!choose(6030,7,positions[0]), "unknown clearance cannot decide whether to enter or leave the maze");
    state.cards[2]=false;
    for(std::size_t i=0;i<steps.size();++i)
        require(choose(6030,7,positions[i])==steps[i], "the portal maze selects a leg from the current ground pocket");
    require(choose(6030,8,{0,7000,1560})==LegId::EastUpperExit, "the upper catwalk leads to the card room");
    state.cards[2]=false;
    require(choose(6030,10,{-480,14500,112})==LegId::Card3, "the uncollected third card is the next goal");
    state.cards[2]=true;
    require(choose(6030,10,{-480,14500,112})==LegId::Card3Warp, "the card room's return portal becomes relevant");
    const Vec3 return_shelf{1510,5880,320};
    require(!choose(6030,7,return_shelf), "an unreadable rope does not supply a climb");
    state.rope_lowered=false;
    require(choose(6030,7,return_shelf)==LegId::LowerRope, "the rolled ladder is approached for manual interaction");
    state.rope_lowered=true;
    require(choose(6030,7,return_shelf)==LegId::EastLadderDown&&
        choose(6030,7,positions[0])==LegId::EastReturn, "the lowered rope leads down before the entrance exit");
    require(choose(6030,11,{-800,19500,3160})==LegId::LabLiftDown&&
        choose(6030,12,{800,19500,160})==LegId::JadeHallDoor&&
        choose(6030,12,{1710,19500,160})==LegId::JadeLabExit&&
        choose(6030,13,{840,27299,110})==LegId::WhiteJade,
        "the lift landing and visible White Jade check remain separate steps");
    require(!choose(6030,11,{-160,19500,1600})&&!choose(6030,7,{640,7750,-180}),
        "a moving platform and an unverified portal pocket withhold a route");
    require(choose(6040,13,{840,27299,110})==LegId::JadeReturn&&
        choose(6040,12,{1710,19500,160})==LegId::JadeHallReturnDoor&&
        choose(6040,12,{800,19500,160})==LegId::LabLiftUp&&
        choose(6040,11,{-800,19500,3160})==LegId::NorthReturn&&
        choose(6040,0,{2700,0,10})==LegId::LeaveRuins,
        "the updated Elder objective exposes the verified local route out of the ruins");
}
void train_escape_selects_the_current_forward_door() {
    using chapter_four::escape_door;
    const std::array<Vec3,4> cars{{{0,-800,100},{0,1000,100},{0,5900,100},{0,10800,100}}};
    const std::array<int,4> doors{0,2,4,6};
    std::string previous;
    for(std::size_t car=0;car<cars.size();++car) {
        const auto next=escape_door(3120,1004,4030,cars[car]);
        require(next&&next->id==doors[car]&&next->point==doors[car],
            "the active escape uses the forward door in the player's own car");
        require(previous!=next->key,"each forward door has a new Story selection identity");
        previous=next->key;
    }
    require(!escape_door(3120,1004,4023,cars[0])&&!escape_door(3120,1004,4040,cars[3])&&
        !escape_door(1000,1004,4060,cars[0])&&!escape_door(3120,1003,4030,cars[0]),
        "escape doors expire when the pursuit ends and never replace other objectives");
    require(!escape_door(3120,1004,4030,{0,5300,100})&&
        !escape_door(3120,1004,4030,{0,15800,100})&&
        !escape_door(3120,1004,4030,{0,2000,500}),
        "between cars, past the escape door, or away from the car floor, no leg is inferred");
}
void dungeon_story_routes_to_the_current_side_of_the_key_gate() {
    using chapter_four::dungeon_passage;
    const std::array<std::pair<int,int>,4> rescue{{{2,4},{1,2},{3,6},{4,12}}};
    for(const auto [section,id]:rescue) {
        const auto passage=dungeon_passage(4180,section);
        require(passage&&passage->id==id,"the rescue objective leads toward the guarded wing");
    }
    const auto returning=dungeon_passage(4190,5);
    require(!dungeon_passage(4180,5)&&returning&&returning->id==5&&
        !dungeon_passage(4190,4)&&chapter_four::dungeon_castle.id==13,
        "the return passage appears after the jailer, and the locked gate is handled in its own room");
    require(!dungeon_passage(4170,4)&&!dungeon_passage(4200,5)&&!dungeon_passage(4180,-1),
        "a different objective or unknown dungeon section supplies no local passage");
}
void captain_story_stays_forward_along_the_hall() {
    using chapter_four::captain_room_side;
    require(captain_room_side({30270.836,5600,1282.15}),"the key scene returns the party inside the captain's quarters");
    require(!captain_room_side({29380.876,5590.224,1282.048})&&
        !captain_room_side({29353.89,4852.71,1282.15}),
        "the hall and its approach turn keep the throne room objective after leaving the quarters");
}
void factory_story_follows_the_native_conveyor_landings() {
    using chapter_four::factory_conveyor_leg;
    const std::array<std::pair<Vec3,int>,8> landings{{
        {{500,13695,102},7},{{1856,16250,402},16},{{1323,16949,400},17},{{1956,17450,402},4},
        {{1250,17650,402},5},{{1200,18256,402},8},{{2656,18850,402},13},{{3750,15744,402},53}
    }};
    std::string_view previous_instruction;
    for(const auto& [position,id]:landings) {
        const auto leg=factory_conveyor_leg(position,false);
        require(leg&&leg->id==id&&leg->pedestal==(id==53),"each native landing advances the conveyor route");
        require(leg->instruction!=previous_instruction,"the next conveyor is announced when its landing changes");
        previous_instruction=leg->instruction;
    }
    const auto returning=factory_conveyor_leg(landings.back().first,true);
    const auto hall=factory_conveyor_leg({2850,16050,402},true);
    require(returning&&returning->id==14&&hall&&hall->id==12&&
        !factory_conveyor_leg(landings.front().first,true),"the lowered bridge changes Story to the hall return");
    const auto ramp=factory_conveyor_leg({2656,17447,802},false);
    require(ramp&&ramp->id==13,"the raised platform keeps the same conveyor target while climbing");
    require(!factory_conveyor_leg({1856,16250,1300},false),"walkways on another floor do not share conveyor targets");
}
void wind_shrine_landing_steps() {
    using namespace chapter_two;
    require(!wind_leg(3060,1002,2130,0,{100,37000,100})&&
        !wind_leg(3070,1002,2120,0,{100,37000,100})&&
        !wind_leg(3070,1001,2130,0,{100,37000,100}),
        "Wind lifts require the active, local Chapter 2 objective");
    auto leg=wind_leg(3070,1002,2130,0,{100,37000,100});
    require(leg&&leg->lift==71&&leg->call==55&&leg->floor==0,"entrance directs to the first call pedestal");
    require(lift_step(*leg,{true,true,false,false,true})==LiftStep::Call,"guide to the call control while the lift is away");
    require(lift_step(*leg,{true,true,true,true,true})==LiftStep::Board,"board only the docked lift");
    require(lift_step(*leg,{true,true,true,false,true})==LiftStep::Wait,"withhold the approach while the boarding volume is off");
    require(lift_step(*leg,{true,true,false,true,true,true})==LiftStep::Wait,"do not offer a moving platform or its control");
    require(lift_step(*leg,{true,true,false,false,false})==LiftStep::Unavailable&&
        lift_step(*leg,{false,false,false,false,true})==LiftStep::Unavailable,
        "a stale pedestal or an unverified linked lift cannot authorize a route");
    require(wind_leg(3070,1002,2130,3,{7240,37000,2900})->lift==69&&
        wind_leg(3070,1002,2130,3,{9590,39400,3300})->lift==72&&
        wind_leg(3070,1002,2130,3,{13780,37500,3290})->call==54,
        "the upper walkway is split by a lift and uses the upper call pedestal");
    require(wind_leg(3070,1002,2130,1,{13600,37000,1300})->lift==70&&
        wind_leg(3070,1002,2130,1,{13700,40200,2490})->lift==68&&
        wind_leg(3070,1002,2130,5,{9100,40200,1300})->lift==73&&
        wind_leg(3070,1002,2130,4,{10400,40200,5295})->lift==41,
        "the lower and west crossings lead to the summit lift, then to the chamber");
    require(wind_leg(3070,1002,2130,4,{14025,43400,4900})->lift==74,
        "the isolated treasure balcony returns down before going to the chamber");
    require(!wind_leg(3070,1002,2130,6,{200,0,110}),"the chamber approach has ended after the final lift");
}
void mobliz_escape_steps() {
    using namespace chapter_two;
    const Vec3 upper{418,13100,1730},ground{-800,12000,100},entrance{3300,-4800,100};
    EscapeFacts facts;
    require(escape_step(3061,1002,2090,3,{428,5412,100},facts)==EscapeStep::None&&
        escape_step(3061,1002,2095,3,{428,5412,100},facts)==EscapeStep::Rooftop,
        "the return elevator is a Story leg only after the escape begins");
    facts.power_failure=true;
    require(escape_step(3060,1002,2095,1,{6800,12000,902},facts)==EscapeStep::PowerFailure,
        "the first event remains the target from the middle deck while the route checks the connecting stairs");
    require(escape_step(3060,1002,2095,2,upper,facts)==EscapeStep::PowerFailure,
        "follow the current failure scene before exposing the levers");
    facts={};facts.fence=true;
    require(escape_step(3060,1002,2095,2,upper,facts)==EscapeStep::Fence,
        "the fence must open before Story can list the bank");
    facts={};facts.levers=true;
    require(escape_step(3060,1002,2095,2,upper,facts)==EscapeStep::LeverBank,
        "the live lever bank is exposed as a set of choices");
    facts.restored=true;facts.entrance_blockade=true;
    require(escape_step(3060,1002,2095,2,upper,facts)==EscapeStep::RestoredLift&&
        escape_step(3060,1002,2095,0,ground,facts)==EscapeStep::EntrancePassage&&
        escape_step(3060,1002,2095,4,entrance,facts)==EscapeStep::EntranceBlockade,
        "the restored lift, passage and entrance discovery are distinct steps");
    facts.entrance_blockade=false;facts.cargo_discovery=true;
    require(escape_step(3060,1002,2095,4,entrance,facts)==EscapeStep::None,
        "a cargo trigger in another section does not establish that the blockade scene was seen");
    facts.return_passage=true;facts.cargo_discovery=false;
    require(escape_step(3060,1002,2095,4,entrance,facts)==EscapeStep::ReturnPassage,
        "the seen blockade gives a return route even while the cargo overlap is inactive across the passage");
    facts.cargo_discovery=true;
    require(escape_step(3060,1002,2095,4,entrance,facts)==EscapeStep::ReturnPassage&&
        escape_step(3060,1002,2095,0,ground,facts)==EscapeStep::CargoDiscovery,
        "after the blockade, Story goes back through the passage to the cargo lift");
    require(escape_step(3060,1002,2100,0,ground,facts)==EscapeStep::None,
        "the cargo encounter target expires when the timed objective ends");
    facts.cargo_discovery=false;
    require(escape_step(3060,1002,2095,0,ground,facts)==EscapeStep::None,
        "the native encounter handoff must not invent a route to the platform below");
}
void cave_story_tracks_the_current_bank() {
    using namespace chapter_three;
    require(cave_gate(3030,0,{0,0,100})->id==0,"the lower entrance leads to the first pool");
    const auto pool=cave_pool(5,{90,11200,100});
    require(pool&&pool->pedestal==36&&pool->boat==44&&!cave_gate(3030,5,{90,11200,100}),
        "the first pool entrance needs its pedestal and boat before the far bank");
    require(cave_gate(3030,5,{2360,11650,102})->id==5,"the far bank leads to the next pool");
    require(!cave_gate(3030,9,{105,67317,162})&&cave_gate(3030,9,{595,67327,162})->id==12,
        "only the main third-pool landing has a walking route to the upper passage");
    const auto chamber_return=cave_gate(3040,4,{408,17513,102});
    require(chamber_return&&chamber_return->id==15&&chamber_return->point==8,
        "after the Water Maiden fight Story first leads out of her chamber");
    require(cave_pool(8,{380,39910,100})->pedestal==35,"the second pool has its own pedestal");
    const auto upper=cave_gate(3030,3,{2550,1480,902});
    require(upper&&upper->id==14&&upper->event_only,"the upper entrance leads to the active Water Maiden event");
    require(!cave_gate(3040,3,{2550,1480,902})&&cave_gate(3040,3,{0,0,100})->id==9&&
        cave_gate(3085,3,{0,0,100})->id==16,"later visits have distinct current exit and lake legs");
}
void olderion_return_routes_follow_the_current_side() {
    using namespace chapter_three;
    const Vec3 lake{4200,5760,462},amore_south{3707,-525,420},shrine{6307,-1715,165},
        waterfall_south{1887,-445,110};
    const auto lake_gate=world_gate(3100,2081,lake);
    const auto cave_north=world_gate(3110,2070,lake);
    require(lake_gate&&lake_gate->id==43&&lake_gate->destination==2081&&
        cave_north&&cave_north->id==23&&cave_north->destination==3080&&cave_north->point==13,
        "the Lake Dorr plateau takes the lake approach before the scene and the north cave entrance on return");
    require(!world_gate(3110,2070,waterfall_south)&&!world_gate(3110,3100,lake)&&
        !world_gate(3100,2081,{4200,5760,-200}),
        "the plateau legs require the live objective and a position on that side");
    const auto cave_south=world_gate(3110,2070,{1646,4169,240});
    require(cave_south&&cave_south->id==19&&cave_south->destination==2070&&
        !world_gate(3110,2070,{1646,4169,420}),
        "the south cave return selects Olderion on the lower road");
    require(world_gate(3150,1000,amore_south)->id==24&&!world_gate(3150,1000,shrine)&&
        world_gate(3190,2070,shrine)->id==25&&world_gate(3190,2070,amore_south)->id==21&&
        !world_gate(3190,2070,waterfall_south),
        "the same Amore shore points forward before the shrine and back through Waterfall Way afterward");
    const auto waterfall_road=world_gate(3190,2070,{1887,-446,11});
    require(waterfall_road&&waterfall_road->id==19&&waterfall_road->destination==2070&&
        !world_gate(3190,3110,{1887,-446,11}),
        "the lower return road from Waterfall Way selects the Olderion entrance for the active return objective");
    require(cave_return_ladder(3110,1,{2770,-9200,1885})==3&&
        cave_return_ladder(3110,7,{1610,-8850,1205})==2&&
        cave_return_ladder(3110,1,{1210,-8800,805})==1&&
        !cave_return_ladder(3100,1,{2770,-9200,1885}),
        "return from Lake Dorr descends the current ladder deck");
    const auto chamber=cave_return_gate(3110,1,{850,-9750,205});
    const auto passage=cave_return_gate(3110,6,{2900,-4950,-615});
    const auto entrance=cave_return_gate(3110,3,{1300,-900,85});
    require(chamber&&chamber->id==3&&chamber->point==12&&passage&&passage->id==17&&passage->point==10&&
        entrance&&entrance->id==9&&entrance->point==3081&&!cave_return_gate(3110,1,{2770,-9200,1885}),
        "each return passage stays on its own deck and leads toward Olderion");
}
void water_shrine_routes_follow_water_and_landings() {
    using namespace chapter_three;
    require(shrine_hall_water(45)==HallWater::High&&shrine_hall_water(-1655)==HallWater::FirstDrop&&
        shrine_hall_water(-1885)==HallWater::SecondDrop&&shrine_hall_water(-6485)==HallWater::Drained&&
        shrine_hall_water(-2100)==HallWater::Unknown&&shrine_hall_water(-6455)==HallWater::Unknown,
        "the hall resolves only settled authored water heights");
    require(shrine_room_water(73,530)==RoomWater::High&&shrine_room_water(73,130)==RoomWater::Low&&
        shrine_room_water(74,-260)==RoomWater::Low&&shrine_room_water(74,132)==RoomWater::High&&
        shrine_room_water(74,-10)==RoomWater::Unknown&&shrine_room_water(75,132)==RoomWater::Unknown,
        "the two rooms have distinct high and low surfaces");
    const Vec3 hall{-700,33540,-1500},boat_bank{750,43350,225},crate_near{50,59250,200},
        crate_far{1180,61350,200},shrine_tower{-870,12090,220},west_dock{-2365,11442,220};
    const auto crate_entry=water_shrine_gate(3180,3,hall,HallWater::FirstDrop,RoomWater::Unknown);
    const auto basin_entry=water_shrine_gate(3180,3,hall,HallWater::SecondDrop,RoomWater::Unknown);
    require(crate_entry&&crate_entry->id==11&&basin_entry&&basin_entry->id==16&&
        !water_shrine_gate(3180,3,hall,HallWater::Unknown,RoomWater::Unknown),
        "each drop reveals its own hall passage only while that water level is established");
    require(water_shrine_gate(3180,7,boat_bank,HallWater::FirstDrop,RoomWater::Unknown)->id==10&&
        !water_shrine_gate(3180,7,boat_bank,HallWater::High,RoomWater::Unknown),
        "the boat-room walking exit becomes the return leg after the first pedestal");
    require(shrine_large_pedestal(9,{310,50450,205},HallWater::High)==56&&
        !shrine_large_pedestal(9,{310,52100,210},HallWater::High)&&
        shrine_large_pedestal(10,crate_far,HallWater::FirstDrop)==57&&
        !shrine_large_pedestal(10,crate_near,HallWater::FirstDrop)&&
        shrine_large_pedestal(14,{-440,12250,620},HallWater::SecondDrop)==55,
        "large pedestals are offered only from their own current platform");
    require(shrine_ladder(1,hall,HallWater::FirstDrop,RoomWater::Unknown)->id==2&&
        shrine_ladder(2,hall,HallWater::FirstDrop,RoomWater::Unknown)->id==30&&
        shrine_ladder(4,hall,HallWater::Drained,RoomWater::Unknown)->id==1&&
        !shrine_ladder(4,hall,HallWater::SecondDrop,RoomWater::Unknown),
        "the central stairs ladder is gated by the lowest live water level");
    require(shrine_ladder(13,shrine_tower,HallWater::SecondDrop,RoomWater::Low)->id==76&&
        !shrine_ladder(13,west_dock,HallWater::SecondDrop,RoomWater::Low)&&
        !shrine_ladder(13,shrine_tower,HallWater::SecondDrop,RoomWater::High),
        "the tower ladder is local to its dry landing");
    const Vec3 basin_pedestal{2035,13886,602};
    const auto basin_exit=water_shrine_gate(3180,14,basin_pedestal,HallWater::Drained,RoomWater::Low);
    require(basin_exit&&basin_exit->id==4&&basin_exit->point==16&&
        !water_shrine_gate(3180,14,basin_pedestal,HallWater::Drained,RoomWater::High),
        "the open basin return is selectable from the upper pedestal after lowering the water");
    const auto boss=water_shrine_gate(3180,6,{465,31683,-6400},HallWater::Drained,RoomWater::Unknown);
    require(boss&&boss->id==17&&boss->destination==3110&&boss->point==19&&boss->event_only&&
        !water_shrine_gate(3190,6,{465,31683,-6400},HallWater::Drained,RoomWater::Unknown),
        "the boss approach expires when the return objective begins");
}
void shrine_bridges_require_current_water_and_aligned_support() {
    using namespace chapter_three;
    const auto push_side=shrine_crate_push_side({740,60650,-225});
    require(push_side&&push_side->x==970&&push_side->y==60650&&
        shrine_crate_aligned({-160,60650,-225})&&!shrine_crate_push_side({-160,60650,-225})&&
        !shrine_crate_push_side({740,60650,130})&&!shrine_crate_push_side({740,60700,-225}),
        "the loose crate approach requires the live lowered crate on its push lane and ends when it lines up");
    const auto crate_out=shrine_bridge_leg(10,{-160,59900,200},HallWater::FirstDrop,RoomWater::High);
    const auto crate_back=shrine_bridge_leg(10,{-160,60840,200},HallWater::SecondDrop,RoomWater::High);
    require(crate_out&&crate_out->bridge==ShrineBridge::CrateRoom&&!crate_out->returning&&
        crate_back&&crate_back->bridge==ShrineBridge::CrateRoom&&crate_back->returning&&
        !shrine_bridge_leg(10,{-160,59900,200},HallWater::FirstDrop,RoomWater::Low),
        "the crate room bridge follows the active water and Story stage in both directions");
    const auto first=shrine_bridge_leg(14,{1855,13600,600},HallWater::SecondDrop,RoomWater::High);
    const auto second=shrine_bridge_leg(14,{1855,12295,600},HallWater::SecondDrop,RoomWater::High);
    const auto back_second=shrine_bridge_leg(14,{-920,11348,600},HallWater::Drained,RoomWater::High);
    const auto back_first=shrine_bridge_leg(14,{260,11348,600},HallWater::Drained,RoomWater::High);
    require(first&&first->bridge==ShrineBridge::BasinLong&&second&&second->bridge==ShrineBridge::BasinShort&&
        back_second&&back_second->bridge==ShrineBridge::BasinShort&&back_second->returning&&
        back_first&&back_first->bridge==ShrineBridge::BasinLong&&back_first->returning&&
        !shrine_bridge_leg(13,{1855,13600,200},HallWater::SecondDrop,RoomWater::High),
        "the basin chooses its current mandatory row and excludes the lower dock and optional chest route");
    const auto& bridge=shrine_bridge_spec(ShrineBridge::CrateRoom);
    std::vector<ShrineCrateBounds> crates;
    for(const auto& anchor:bridge.crates)crates.push_back(anchor.movable_id>=0?
        ShrineCrateBounds{{anchor.x,anchor.y,30},{78,78,72},true}:
        ShrineCrateBounds{{anchor.x,anchor.y,98},{64,64,4},true});
    require(shrine_crate_supports(bridge,crates),"the current colliding crates meet the authored row");
    auto changed=crates;
    changed.back().center.x=740;
    require(!shrine_crate_supports(bridge,changed),"a loose crate off the row cannot authorize a crossing");
    changed=crates;
    changed[2].collision=false;
    require(!shrine_crate_supports(bridge,changed),"a hidden or non-colliding crate cannot authorize a crossing");
    changed=crates;
    changed[2].center.z-=170;
    require(!shrine_crate_supports(bridge,changed),"a submerged crate cannot authorize a crossing");
    changed=crates;
    changed[2].half.y=35;
    require(!shrine_crate_supports(bridge,changed),"unmatched geometry leaving a wide gap cannot authorize a crossing");
}
void opening_continuations_from_entry() {
    const OpeningContinuationEvidence first{0,{2,0},"C01_001B","C01_001B","C01_001B",{10,20,0},true,true,true};
    const OpeningContinuationEvidence second{1,{2,1},"C01_001D","C01_001D_Encount","C01_001D_Encount",{30,40,0},true,true,true};
    std::array candidates{second,first};
    require(eligible_opening_continuations(1,10040,candidates)==std::vector<std::size_t>({1,0}),
        "the opening must expose every enabled audited continuation from entry in authored order, independently of camera or prior sight");
    require(eligible_opening_continuations(2,10040,candidates).empty()&&eligible_opening_continuations(1,10041,candidates).empty(),
        "the exact opening fallback cannot authorize another chapter or map");
    for(int guard=0;guard<10;++guard) {
        auto changed=first;
        if(guard==0)changed.current_registered_actor=false;
        if(guard==1)changed.active=false;
        if(guard==2)changed.overlap_enabled=false;
        if(guard==3)changed.condition="future";
        if(guard==4)changed.event="C01_001F_Encount";
        if(guard==5)changed.matched_event.clear();
        if(guard==6)changed.key.type=1;
        if(guard==7)changed.key.id=1;
        if(guard==8)changed.unique_id=3;
        if(guard==9)changed.position.x=std::numeric_limits<double>::infinity();
        const std::array one{changed};
        require(eligible_opening_continuations(1,10040,one).empty(),
            "foreign, inactive, collision-disabled, condition-false, unaudited or nonfinite continuations must stay unavailable");
    }
    std::array duplicates{first,first,second};
    require(eligible_opening_continuations(1,10040,duplicates)==std::vector<std::size_t>({2}),
        "two actors resolving the same authored ID are ambiguous; do not choose one by proximity");
    duplicates[0].position.x=std::numeric_limits<double>::quiet_NaN();
    require(eligible_opening_continuations(1,10040,duplicates)==std::vector<std::size_t>({2}),
        "a registered duplicate with an unreadable position must still make its identity ambiguous");
    candidates[1].active=false;
    require(eligible_opening_continuations(1,10040,candidates)==std::vector<std::size_t>({0}),
        "a completed continuation must disappear without blocking another currently enabled continuation");
}
void opening_section_continuation() {
    OpeningContinuationEvidence far{2,{2,2},"C01_001F","C01_001F_Encount","C01_001F_Encount",{17642,104925,232},true,true,true,false};
    const std::array candidates{far};
    require(eligible_opening_continuations(1,10040,candidates).empty(),"a staged event outside the current walking section is not a local walking goal");
    require(opening_needs_next_section(1,10040,candidates),"the staged continuation requires the matching scene's next Battlefield section");
    auto next=far;next.unique_id=6;next.key={2,6};next.position={17500,1450,232};next.in_walking_section=true;
    const std::array next_map{next};
    require(eligible_opening_continuations(1,10041,next_map)==std::vector<std::size_t>{0},"the same native scene resumes at its walkable trigger in the next map");
    require(eligible_opening_continuations(2,10041,next_map).empty(),"the later chapter's alternate event cannot borrow this opening rule");
    auto local=far;local.unique_id=1;local.key={2,1};local.condition="C01_001D";local.event=local.matched_event="C01_001D_Encount";local.in_walking_section=true;
    const std::array unfinished{local,far};
    require(!opening_needs_next_section(1,10040,unfinished),"an unfinished local scene retains priority over the onward exit");
    const std::array ambiguous_local{local,local,far};
    require(!opening_needs_next_section(1,10040,ambiguous_local),"an ambiguous earlier scene cannot authorize the onward exit");
}
StoryCandidate authorized_candidate(const char* id,ObjectKey key,bool direct=true) {
    StoryCandidate candidate;candidate.key=key;candidate.direct=direct;
    candidate.destination.id=id;candidate.destination.label="Story destination";
    candidate.destination.disclosed=true;candidate.destination.local=true;
    return candidate;
}
void partial_candidate_read_failure() {
    std::vector<StoryCandidate> candidates;
    auto first=capture_story_candidate(authorized_candidate("first",{1,10}),[](StoryDestination& destination) {
        destination.position=Vec3{10,20,0};destination.actor_available=true;return true;
    });
    require(first.has_value(),"a verified candidate must be retained");
    candidates.push_back(std::move(*first));
    auto unreadable=capture_story_candidate(authorized_candidate("second",{1,11}),[](StoryDestination& destination)->bool {
        destination.position=Vec3{99,99,0};destination.actor_available=true;destination.interactable=true;
        destination.actor_id="stale.actor";destination.actor_type=L"stale.class";
        throw std::runtime_error("The story target position could not be read.");
    });
    require(unreadable.has_value(),"an authorized unreadable target must remain selectable");
    candidates.push_back(std::move(*unreadable));
    auto result=finalize_story_candidates(std::move(candidates));
    require(result.size()==2&&result[0].position&&result[0].position->x==10&&result[0].actor_available,
        "one actor read failure must retain another independently verified destination");
    require(result[1].id=="second"&&result[1].label=="Story destination"&&result[1].disclosed&&!result[1].position&&
            !result[1].actor_available&&!result[1].interactable&&result[1].actor_id.empty()&&result[1].actor_type.empty()&&
            result[1].unavailable_reason=="The story target position could not be read.",
        "an unreadable candidate keeps only its authorized generic selection and specific failure reason");
    require(!capture_story_candidate(authorized_candidate("inactive",{1,12}),[](StoryDestination&){return false;}),
        "positively inactive native candidates must still be omitted");
}
void unreadable_duplicate_stays_ambiguous() {
    std::vector<StoryCandidate> candidates;
    auto first=capture_story_candidate(authorized_candidate("one",{1,42}),[](StoryDestination& d) {
        d.position=Vec3{1,2,3};d.actor_available=true;return true;
    });
    require(first.has_value(),"a verified candidate must be retained");
    candidates.push_back(std::move(*first));
    auto duplicate=capture_story_candidate(authorized_candidate("two",{1,42}),[](StoryDestination&)->bool {
        throw std::runtime_error("The story target position could not be read.");
    });
    require(duplicate.has_value(),"an authorized unreadable duplicate must remain part of identity validation");
    candidates.push_back(std::move(*duplicate));
    auto other=capture_story_candidate(authorized_candidate("other",{2,42}),[](StoryDestination& d) {
        d.position=Vec3{3,4,5};d.actor_available=true;return true;
    });
    require(other.has_value(),"another verified typed key must be retained");
    candidates.push_back(std::move(*other));
    auto result=finalize_story_candidates(std::move(candidates));
    require(result.size()==3&&!result[0].position&&!result[1].position&&!result[0].actor_available&&!result[1].actor_available&&
            result[0].unavailable_reason.find("More than one")!=std::string::npos&&
            result[1].unavailable_reason.find("More than one")!=std::string::npos,
        "a direct typed-key duplicate remains ambiguous even when one candidate has no readable coordinates");
    require(result[2].position&&result[2].actor_available,"another typed key remains independently routeable");
}
void linked_story_exit_uses_one_route() {
    const auto exit=[](const char* id,int key,const char* actor) {
        auto c=authorized_candidate(id,{2,key},false);
        c.destination.exit=true;
        c.destination.actor_available=true;
        c.destination.actor_id=actor;
        return c;
    };
    auto result=finalize_story_candidates({exit("gate",1,"physical-gate"),
        exit("map landmark",2,"physical-gate"),exit("other exit",3,"second-gate")});
    require(result.size()==2&&result[0].id=="gate"&&result[1].id=="other exit",
        "aliases of the same verified activation volume share one Story entry");
}
void opening_menu_capture_keeps_epoch() {
    std::array evidence{OpeningContinuationEvidence{0,{2,0},"C01_001B","C01_001B","C01_001B",{10,20,0},true,true,true}};
    StorySnapshot base;base.ready=true;base.world="world";base.player="pawn";base.map_id=10040;
    base.objective_id="chapter-one";base.objective="Chapter 1";base.destination_epoch="filtered:empty";
    const auto capture=[&](std::string& epoch) {
        epoch=opening_destination_epoch(epoch,1,10040,evidence);
        std::vector<StoryDestination> destinations;
        for(const auto index:eligible_opening_continuations(1,10040,evidence)) {
            StoryDestination d;d.id="opening:"+std::to_string(evidence[index].unique_id);d.label="Story continuation";
            d.position=evidence[index].position;d.disclosed=true;d.local=true;d.actor_available=true;
            destinations.push_back(std::move(d));
        }
        return destinations;
    };
    auto field=base;field.field_control=true;capture_opening_story(field,capture);
    auto menu=base;menu.field_control=false;capture_opening_story(menu,capture);
    require(!field.destinations.empty()&&menu.destinations.size()==field.destinations.size()&&
            menu.destinations[0].id==field.destinations[0].id&&menu.destination_epoch==field.destination_epoch,
        "opening eligibility and its epoch must survive a menu while the same native pawn/world/chapter remains valid");
    evidence[0].overlap_enabled=false;
    auto disabled=base;disabled.field_control=false;capture_opening_story(disabled,capture);
    require(disabled.destinations.empty()&&disabled.destination_epoch!=field.destination_epoch,
        "an actual native eligibility change during a menu must still revoke the opening set");
    auto missing=base;missing.world.clear();capture_opening_story(missing,capture);
    require(missing.destinations.empty()&&missing.destination_epoch==base.destination_epoch,
        "a missing native context cannot invoke opening fallback capture");
}
void opening_overlap_rules() {
    OverlapEvidence e{true,true,true,true,1,3,1,2};
    require(enabled_overlap(e),"a query-only logic volume overlapping a player that blocks its channel must be eligible");
    e.trigger_response=2;e.player_response=1;
    require(enabled_overlap(e),"the reverse block/overlap response pair also generates overlap");
    for(int guard=0;guard<10;++guard) {
        e={true,true,true,true,1,3,1,2};
        if(guard==0)e.owned_components=false;
        if(guard==1)e.actors_enabled=false;
        if(guard==2)e.trigger_generates=false;
        if(guard==3)e.player_generates=false;
        if(guard==4)e.trigger_mode=0;
        if(guard==5)e.player_mode=2;
        if(guard==6)e.trigger_response=0;
        if(guard==7)e.player_response=0;
        if(guard==8)e.trigger_response=2;
        if(guard==9)e.player_response=3;
        require(!enabled_overlap(e),"disabled, physics-only, ignored, blocking-only or unreadable overlap state cannot activate Story");
    }
}
PointEvidence point() {return {{1,42},{10,20,30},true,true,true,true,true,CoordinateSpace::World,DestinationSource::FilteredMainQuest};}
void native_array_bounds() {
    std::array<int,8> data{};
    require(valid_native_array(data.data(),2,8,128),"valid bounded native arrays must be accepted");
    require(valid_native_array(nullptr,0,0,128),"an empty native list is valid input");
    for(const auto values:std::array<std::array<int,2>,6>{{{-1,2},{3,2},{129,129},{0,-1},{1,0},{0,65537}}})
        require(!valid_native_array(data.data(),values[0],values[1],128),"negative, inconsistent or oversized native arrays must fail");
    require(!valid_native_array(nullptr,1,1,128)&&!valid_native_array(nullptr,0,8,128),
        "a missing allocation must not authorize reading native elements");
    require(!valid_native_array(data.data(),0,0,128),"a non-null allocation with zero capacity is inconsistent");
}
void active_key_validation() {
    require(valid_object_key({1,42})&&valid_object_key({3,0})&&valid_object_key({6,99}),"verified native object kinds and nonnegative IDs must remain usable");
    require(!valid_object_key({0,42})&&!valid_object_key({7,42})&&!valid_object_key({1,-1}),
        "None, unknown object kinds and negative object IDs must not become destinations");
}
struct Node {enum Kind{Object,World,Level} kind{Object};Node* outer{};Node* owning_world{};};
struct NodeAccess {
    bool valid(Node* n)const{return n!=nullptr;}
    bool is_world(Node* n)const{return n->kind==Node::World;}
    bool is_level(Node* n)const{return n->kind==Node::Level;}
    Node* level_world(Node* n)const{return n->owning_world;}
    Node* outer(Node* n)const{return n->outer;}
};
void current_world_scope() {
    Node a{Node::World},b{Node::World},package,level{Node::Level,&package,&a};
    Node direct{Node::Object,&a},actor{Node::Object,&level},foreign{Node::Object,&b};
    const auto world=[](Node* n){return resolved_world(n,NodeAccess{});};
    require(world(&direct)==&a&&world(&actor)==&a,"both direct-world subsystems and level-owned actors must resolve to the current world");
    std::array<Node*,2> candidates{&actor,&foreign};
    auto found=unique_world_member<Node>(candidates,&a,world);
    require(found.object==&actor&&!found.ambiguous,"current-world selection must use resolved ownership rather than a direct-outer assumption");
    candidates[1]=&direct;found=unique_world_member<Node>(candidates,&a,world);
    require(!found.object&&found.ambiguous,"multiple current-world managers must be rejected rather than selecting the first");
    level.owning_world=nullptr;require(!world(&actor),"an unreadable Level.OwningWorld must fail closed");
    Node cycle;cycle.outer=&cycle;require(!world(&cycle),"malformed outer cycles must be bounded");
}
void destination_identity_changes() {
    const std::array<FilteredPoint,3> points{{{0,0,3,true,{3,9}},{0,0,1,true,{1,2}},{0,0,1,true,{1,2}}}};
    const std::array<FilteredPoint,2> reordered{{points[1],points[0]}};
    const auto first=filtered_destination_epoch(std::span<const FilteredPoint>(points));
    require(first==filtered_destination_epoch(std::span<const FilteredPoint>(reordered)),
        "reordering and duplicate native destinations must not create false objective changes");
    auto changed_maps=reordered;changed_maps[0].map_id=7;
    require(first!=filtered_destination_epoch(std::span<const FilteredPoint>(changed_maps)),
        "a same-text objective with different filtered map IDs must revoke its earlier destination identity");
    auto changed_keys=reordered;changed_keys[0].object.id=8;
    require(first!=filtered_destination_epoch(std::span<const FilteredPoint>(changed_keys)),
        "a same-text objective with different filtered main keys must change identity");
    require(first!=filtered_destination_epoch({}),
        "an unavailable destination list must never reuse an authorized signature");
}
void filtered_quest_authority() {
    auto p=point();require(point_authorized(p),"a proven filtered-main-quest visible destination must remain usable");
    p.source=DestinationSource::RawScenario;
    require(!point_authorized(p),"raw ScenarioSection destination lists are unfiltered and must never authorize coordinates");
    p.source=DestinationSource::Unknown;require(!point_authorized(p),"unknown destination provenance must fail closed");
    p=point();p.key={0,0};p.field_object_id=12;
    require(point_authorized(p),"a registered main-quest marker may have no typed map-object key");
}
void filtered_point_epoch() {
    const std::array<FilteredPoint,3> points{{{0,0,7,true,{1,5}},{1,0,8,false,{0,0}},{0,0,7,true,{1,5}}}};
    const std::array<FilteredPoint,2> reordered{{points[1],points[0]}};
    const auto epoch=filtered_destination_epoch(std::span<const FilteredPoint>(points));
    require(!epoch.empty()&&epoch==filtered_destination_epoch(std::span<const FilteredPoint>(reordered)),
        "the filtered native point list must have a stable sorted and deduplicated epoch");
    auto changed=reordered;changed[0].role=0;
    require(epoch!=filtered_destination_epoch(std::span<const FilteredPoint>(changed)),"a role change with identical maps/keys must revoke the destination epoch");
    changed=reordered;changed[1].map_id=12;
    require(epoch!=filtered_destination_epoch(std::span<const FilteredPoint>(changed)),"a filtered destination map change must alter the epoch");
    require(filtered_destination_epoch({}).empty(),"an unreadable destination list must not retain an authorized epoch");
}
void point_authorization() {
    auto p=point();require(point_authorized(p),"a current disclosed local world point must be usable");
    for(int guard=0;guard<9;++guard) {
        p=point();
        if(guard==0)p.current_objective=false;
        if(guard==1)p.current_main=false;
        if(guard==2)p.disclosed=false;
        if(guard==3)p.same_world=false;
        if(guard==4)p.local=false;
        if(guard==5)p.coordinates=CoordinateSpace::Unknown;
        if(guard==6)p.coordinates=CoordinateSpace::Map2D;
        if(guard==7)p.key={0,0};
        if(guard==8)p.position.z=std::numeric_limits<double>::quiet_NaN();
        require(!point_authorized(p),"undisclosed, unrelated, cross-world, map2D or nonfinite evidence must not authorize a Vec3");
    }
    p=point();p.position.x=std::numeric_limits<double>::infinity();
    require(!point_authorized(p),"infinite world coordinates must fail");
}
void unique_point_selection() {
    auto p=point();
    std::array<PointEvidence,1> one{p};
    auto result=unique_disclosed_point(one);
    require(result&&result->x==10&&result->y==20&&result->z==30,"one positively authorized point must preserve its world coordinates");
    std::array<PointEvidence,2> ambiguous{p,p};ambiguous[1].position.x=50;
    require(!unique_disclosed_point(ambiguous),"multiple qualifying destinations are ambiguous, never nearest-target guesses");
    one[0].same_world=false;require(!unique_disclosed_point(one),"a foreign marker must not become a local point");
    one[0]=point();one[0].disclosed=false;require(!unique_disclosed_point(one),"an undisplayed marker must not authorize a destination");
    require(!unique_disclosed_point({}),"an empty marker list must not fabricate a point");
}
void objective_survives_destination_failure() {
    StorySnapshot known;
    known.world="world";known.player="player";known.objective_id="current-section";
    known.objective="The displayed current objective";known.ready=true;known.field_control=false;
    known.map_id=7;known.destination_epoch="earlier-authorized-points";
    StoryDestination point;point.id="current";point.label="Story destination";point.position=Vec3{1,2,3};
    point.disclosed=true;point.local=true;known.destinations.push_back(point);
    fail_story_capture(known);
    require(known.ready&&known.objective=="The displayed current objective"&&known.objective_id=="current-section"&&known.destination_epoch.empty(),
        "a destination failure must retain the positively captured current objective");
    require(known.world=="world"&&known.player=="player"&&!known.field_control&&known.destinations.empty()&&!known.unavailable_reason.empty(),
        "route failure must clear coordinates with feedback and keep passive speech gating independent");
    known.world.clear();known.destinations.push_back(point);
    fail_story_capture(known);
    require(!known.ready&&known.objective.empty()&&known.objective_id.empty()&&known.destinations.empty(),
        "a missing capture context must fail closed and never reuse earlier objective data");
}
struct WidgetNode {
    RenderFacts facts{true,true,true,4,1,1};
    WidgetNode* parent{};
    WidgetNode* tree_owner{};
    bool viewport{};
};
struct WidgetAccess {
    bool valid(WidgetNode* n)const{return n!=nullptr;}
    RenderFacts facts(WidgetNode* n)const{return n->facts;}
    std::optional<WidgetNode*> parent(WidgetNode* n)const{return n->parent;}
    std::optional<WidgetNode*> tree_owner(WidgetNode* n)const{return n->tree_owner;}
    std::optional<bool> in_viewport(WidgetNode* n)const{return n->viewport;}
};
void rendered_ancestor_chain() {
    WidgetNode root;root.viewport=true;
    WidgetNode minimap;minimap.tree_owner=&root;
    WidgetNode canvas;canvas.parent=&minimap;canvas.facts.user_widget=false;
    WidgetNode entry;entry.parent=&canvas;
    require(rendered_widget_chain(&entry,WidgetAccess{},&minimap),"an actual edge child with a rendered owning chain must be usable");
    root.facts.visibility=1;
    require(!rendered_widget_chain(&entry,WidgetAccess{},&minimap),"a collapsed HUD root must revoke a marker even when entry alpha is positive");
    root.facts.visibility=4;root.facts.opacity=0;
    require(!rendered_widget_chain(&entry,WidgetAccess{},&minimap),"transparent ancestors must revoke presentation");
    root.facts.opacity=1;entry.facts.alpha=0;
    require(!rendered_widget_chain(&entry,WidgetAccess{},&minimap),"a user widget with zero effective color alpha is not presented");
    entry.facts.alpha=1;entry.facts.owner_matches=false;
    require(!rendered_widget_chain(&entry,WidgetAccess{},&minimap),"foreign owning players must fail closed");
    entry.facts.owner_matches=true;entry.parent=&entry;
    require(!rendered_widget_chain(&entry,WidgetAccess{},&minimap),"widget cycles must be bounded and rejected");
    entry.parent=&canvas;root.viewport=false;
    require(!rendered_widget_chain(&entry,WidgetAccess{},&minimap),"a detached HUD chain must never be treated as rendered");
    root.viewport=true;WidgetNode foreign;
    require(!rendered_widget_chain(&entry,WidgetAccess{},&foreign),"the edge child must reach its verified minimap, not another rendered root");
}
// CDT_MapData_Prod GroupId rows for build 24250053 (2026-11-05 all-map catalog).
std::optional<std::vector<int>> cooked_group(int id) {
    static const std::vector<std::vector<int>> groups{{3000,3001},{2060,2061},
        {2010,2011,2012,2013,2014,2015,2016,2017,3020},{2110,2111,2112,2113,2114,3121,3140,3150,3160,5100},{3130,3131}};
    for(const auto& group:groups)if(std::find(group.begin(),group.end(),id)!=group.end())return group;
    if(id==2000||id==1000)return std::vector<int>{id};
    throw std::runtime_error("unexpected native group query");
}
void filtered_goal_areas() {
    std::array<FilteredPoint,3> points{{{0,0,7,false,{}},{1,0,99,false,{}},{0,0,7,false,{}}}};
    std::vector<int> calls;
    auto areas=goal_areas(points,[&](int id)->std::optional<std::vector<int>>{calls.push_back(id);return std::vector<int>{8,7,8};});
    require(areas&&areas->size()==1&&(*areas)[0].map==7&&(*areas)[0].maps==std::vector<int>({7,8})&&calls==std::vector<int>({7}),
        "query only unique current filtered Goal/Map IDs and retain exact plus native group IDs");
    areas=goal_areas(points,[](int)->std::optional<std::vector<int>>{return std::vector<int>{};});
    require(areas&&(*areas)[0].maps==std::vector<int>({7}),"the exact goal map stays in its area when the native row has no group");
    points[0].map_id=12;points[2].map_id=12;
    areas=goal_areas(points,[](int)->std::optional<std::vector<int>>{return std::vector<int>{13};});
    require(areas&&(*areas)[0].map==12&&(*areas)[0].maps==std::vector<int>({12,13}),"a changed destination epoch must not reuse earlier cached group maps");
    require(!goal_areas(points,[](int)->std::optional<std::vector<int>>{return {};}),"unavailable native group data must not fabricate equivalence");
    require(!goal_areas(points,[](int)->std::optional<std::vector<int>>{return std::vector<int>{-1};}),"invalid native map IDs must fail closed");
    require(!goal_areas(points,[](int)->std::optional<std::vector<int>>{return std::vector<int>(513,1);}),"oversized native groups must fail closed");
    areas=goal_areas(std::span<const FilteredPoint>{},[](int)->std::optional<std::vector<int>>{throw std::runtime_error("no query expected");});
    require(areas&&areas->empty(),"an empty filtered destination list must not query arbitrary groups");
    const std::array<FilteredPoint,2> two{{{0,0,3001,false,{}},{0,0,2000,true,{6,4}}}};
    areas=goal_areas(two,cooked_group);
    require(areas&&areas->size()==2&&(*areas)[0].map==2000&&(*areas)[0].maps==std::vector<int>({2000})&&
            (*areas)[1].map==3001&&(*areas)[1].maps==std::vector<int>({3000,3001}),"each filtered goal keeps its own native area");
}
void forward_only_story_exits() {
    // Earth Shrine: the filtered objective names entrance 3001; its native group adds interior 3000.
    const std::array<FilteredPoint,1> shrine{{{0,0,3001,false,{0,-1}}}};
    const auto shrine_areas=goal_areas(shrine,cooked_group);
    require(shrine_areas&&shrine_areas->size()==1,"the shrine fixture needs its native area");
    require(story_exit_advances(1000,3001,shrine,*shrine_areas),"an exit entering the destination area from outside advances");
    require(!story_exit_advances(3000,3001,shrine,*shrine_areas),
        "inside the destination area, the interior exit back to the entrance goal map does not advance");
    require(!story_exit_advances(3000,3000,shrine,*shrine_areas),
        "a same-map side passage never advances; its related map is the current map by native construction");
    require(!story_exit_advances(3001,1000,shrine,*shrine_areas),"an exit unrelated to the destination stays outside Story");
    const std::array<FilteredPoint,1> mitra{{{0,0,2000,false,{6,4}}}},rain{{{0,0,2000,true,{6,4}}}};
    require(!story_exit_advances(2000,2000,mitra,*goal_areas(mitra,cooked_group))&&!story_exit_advances(2000,2000,rain,*goal_areas(rain,cooked_group)),
        "town doors that stay in the destination map are not Story exits");
    // Dilmagia information NPCs are pointed targets in 2060.
    const std::array<FilteredPoint,3> dilmagia{{{0,0,2060,true,{1,12}},{0,0,2060,true,{1,15}},{0,0,2060,true,{1,17}}}};
    const auto dilmagia_areas=goal_areas(dilmagia,cooked_group);
    require(story_exit_advances(2061,2060,dilmagia,*dilmagia_areas),"a native-required return to the map holding pointed targets advances");
    require(!story_exit_advances(2060,2061,dilmagia,*dilmagia_areas),"leaving the map that holds the pointed targets does not advance");
    // Underground tunnel row: the pointed city waypoint (2110 Point 2) is behind a player already in goal map 3140.
    const std::array<FilteredPoint,2> tunnel_waypoint{{{0,0,3140,false,{0,-1}},{1,0,2110,true,{6,2}}}};
    const auto tunnel_waypoint_areas=goal_areas(tunnel_waypoint,cooked_group);
    require(!story_exit_advances(3140,2110,tunnel_waypoint,*tunnel_waypoint_areas),"a pointed waypoint in another map is not proven onward");
    const std::array<FilteredPoint,2> local_waypoint{{{0,0,2060,true,{1,12}},{1,0,2061,true,{2,9}}}};
    require(!story_exit_advances(2061,2060,local_waypoint,*goal_areas(local_waypoint,cooked_group)),
        "a pointed waypoint in the current map comes before exits toward a pointed goal");
    // Shipyard row: each map holds its own pointed target, so neither direction is offered.
    const std::array<FilteredPoint,3> shipyard{{{0,0,2060,true,{6,4}},{0,0,2061,false,{1,2}},{0,0,2061,true,{2,9}}}};
    const auto shipyard_areas=goal_areas(shipyard,cooked_group);
    require(!story_exit_advances(2060,2061,shipyard,*shipyard_areas)&&!story_exit_advances(2061,2060,shipyard,*shipyard_areas),
        "a pointed target in the current map takes priority, so two pointed maps cannot create a cycle");
    const std::array<FilteredPoint,1> king{{{0,0,3020,true,{6,0}}}};
    const auto king_areas=goal_areas(king,cooked_group);
    require(story_exit_advances(2010,3020,king,*king_areas),"the castle gate leads to the pointed goal's map");
    require(!story_exit_advances(3020,2010,king,*king_areas),"leaving the castle that holds the pointed goal does not advance");
    require(!story_exit_advances(2011,2012,king,*king_areas),"moving between other members of the goal's group is not proven forward");
    // Map-level goals give no native direction between exits inside their area.
    const std::array<FilteredPoint,1> tunnel{{{0,0,3140,false,{0,-1}}}};
    const auto tunnel_areas=goal_areas(tunnel,cooked_group);
    require(!story_exit_advances(3140,2110,tunnel,*tunnel_areas)&&!story_exit_advances(3140,3150,tunnel,*tunnel_areas),
        "an unaudited map-level goal map cannot choose among its group's exits");
    const std::array<FilteredPoint,2> factory{{{0,0,3130,false,{0,-1}},{0,0,3131,false,{0,-1}}}};
    const auto factory_areas=goal_areas(factory,cooked_group);
    require(!story_exit_advances(3130,3131,factory,*factory_areas)&&!story_exit_advances(3131,3130,factory,*factory_areas),
        "two map-level goals in one group cannot create a two-way Story cycle");
    require(!story_exit_advances(1000,3001,shrine,{}),"without verified native group areas no related exit is proven forward");
    require(!story_exit_advances(-1,3001,shrine,*shrine_areas)&&!story_exit_advances(1000,-1,shrine,*shrine_areas),"unknown maps fail closed");
}
void audited_forward_story_exits() {
    const std::array<FilteredPoint,1> shrine{{{0,0,3001,false,{0,-1}}}};
    const auto areas=goal_areas(shrine,cooked_group);
    require(areas&&story_exit_advances(3001,3000,shrine,*areas),
        "the shrine entrance keeps its exit into the interior holding C01_008_01_B and C01_008_02");
    const std::array<FilteredPoint,1> capital{{{0,0,2010,false,{3,12}}}};
    const auto capital_areas=goal_areas(capital,cooked_group);
    require(story_exit_advances(2010,3020,capital,*capital_areas),"the burning capital keeps its audited castle gate");
    require(!story_exit_advances(2010,2011,capital,*capital_areas)&&!story_exit_advances(3020,2010,capital,*capital_areas),
        "an audited goal map authorizes only its audited direction");
    const std::array<FilteredPoint,2> extra{{shrine[0],{0,0,2000,false,{0,-1}}}};
    require(!story_exit_advances(3001,3000,extra,*goal_areas(extra,cooked_group)),"a different filtered destination list cannot borrow the audit");
    auto pointed=shrine;pointed[0].object_target=true;pointed[0].object={2,8};
    require(!story_exit_advances(3001,3000,pointed,*goal_areas(pointed,cooked_group)),"a pointed objective uses its own target rather than the audit");
    const std::array<FilteredPoint,1> interior{{{0,0,3000,false,{0,-1}}}};
    require(!story_exit_advances(3001,3000,interior,*goal_areas(interior,cooked_group)),"the audit is specific to the entrance destination");
}
void destination_area_feedback() {
    const std::array<FilteredPoint,1> shrine{{{0,0,3001,false,{0,-1}}}};
    const auto areas=goal_areas(shrine,cooked_group);
    require(areas&&destination_area_reached(3000,shrine,*areas)&&destination_area_reached(3001,shrine,*areas),
        "both shrine maps are inside the objective's destination area");
    require(!destination_area_reached(1000,shrine,*areas)&&!destination_area_reached(2014,shrine,*areas),
        "the world map and the opening castle remain outside it, keeping cross-area feedback and the castle route");
    const std::array<FilteredPoint,1> rain{{{0,0,2000,true,{6,4}}}};
    require(!destination_area_reached(2000,rain,*goal_areas(rain,cooked_group)),"a pointed target in this map is not an unmarked destination area");
    const std::array<FilteredPoint,3> dilmagia{{{0,0,2060,true,{1,12}},{0,0,2060,true,{1,15}},{0,0,2060,true,{1,17}}}};
    require(!destination_area_reached(2061,dilmagia,*goal_areas(dilmagia,cooked_group)),"a pointed target elsewhere still needs an onward exit");
    const std::array<FilteredPoint,2> split{{shrine[0],{0,0,2000,false,{0,-1}}}};
    require(!destination_area_reached(3000,split,*goal_areas(split,cooked_group)),"another goal outside this area keeps cross-area feedback");
    require(!destination_area_reached(3000,shrine,{})&&!destination_area_reached(-1,shrine,*areas),"unverified areas or maps fail closed");
    const auto reason=destination_area_reason("Earth Shrine");
    require(reason.find("Earth Shrine")!=std::string::npos&&reason.find("another area")==std::string::npos,
        "in-area feedback names the native destination without claiming it is elsewhere");
    require(!destination_area_reason("").empty()&&destination_area_reason("").find(", .")==std::string::npos,"an untitled destination area still explains itself");
}
void registry_marker_identity() {
    const std::array<int,2> distinct{0,1},duplicates{7,7},invalid{-1,7};
    require(unique_capture_ids(distinct,1024),"distinct registered IDs remain distinct even when their typed object keys are both None/default");
    require(!unique_capture_ids(duplicates,1024),"duplicate actual widget or VM IDs cannot identify one presented actor safely");
    require(!unique_capture_ids(invalid,1024),"negative registry IDs must fail closed");
    require(!unique_capture_ids(distinct,1),"oversized native capture lists must remain bounded");
}
void native_parameter_extent_and_allocation() {
    for(const auto layout:std::array<std::array<int,3>,4>{{{168,160,1},{104,96,1},{232,224,1},{64,56,1}}}) {
        const auto valid=[&](int parms,int properties,int offset){return native_call_layout(parms,properties,offset,layout[0],layout[1],layout[2],512);};
        require(valid(layout[1]+layout[2],layout[0],layout[1])==NativeLayoutResult::Valid,
            "the exact logical parameter end is valid independently of the audited reflected allocation");
        require(valid(layout[0],layout[0],layout[1])==NativeLayoutResult::Valid,"the audited padded parameter representation remains valid");
        for(int parms=layout[1]+layout[2]+1;parms<layout[0];++parms)
            require(valid(parms,layout[0],layout[1])==NativeLayoutResult::WrongParameterExtent,"unexpected intermediate sizes must not be accepted");
        require(valid(layout[1],layout[0],layout[1])==NativeLayoutResult::WrongParameterExtent&&
                valid(layout[0]+1,layout[0],layout[1])==NativeLayoutResult::WrongParameterExtent,
            "underlength or oversized parameter extents remain invalid");
        require(valid(layout[0],layout[0]+1,layout[1])==NativeLayoutResult::WrongAllocation,
            "the exact allocation size is still mandatory");
        require(valid(layout[0],layout[0],layout[1]+1)==NativeLayoutResult::WrongReturnOffset,
            "the return offset is still exact");
    }
    require(native_call_layout(8,8,0,8,0,8,512)==NativeLayoutResult::Valid,"unambiguous unpadded native getters remain valid");
    for(const auto bad:std::array<std::array<int,3>,5>{{{0,0,1},{513,0,1},{168,-1,1},{168,160,0},{168,160,9}}})
        require(native_call_layout(bad[0],bad[0],bad[1],bad[0],bad[1],bad[2],512)==NativeLayoutResult::InvalidBounds,
            "zero/oversized allocations, negative offsets and out-of-bounds returns remain rejected");
}
void distinct_call_problem_logging() {
    NativeProblemDeduplicator reported;
    require(reported.accept("layout parms=161 allocation=168"),"the first concrete layout problem must be reportable");
    require(!reported.accept("layout parms=161 allocation=168"),"repeating an 80ms capture must not spam the same problem");
    require(reported.accept("return property")&&!reported.accept("layout parms=161 allocation=168"),
        "alternating failures must not re-log earlier distinct problems");
    for(int i=0;i<14;++i)require(reported.accept("other "+std::to_string(i)),"the bounded diagnostic set should retain distinct problems");
    require(!reported.accept("over capacity")&&!reported.accept(std::string(513,'x')),
        "diagnostic memory and log volume remain bounded");
}
struct Value {
    virtual ~Value()=default;
    virtual int tag()const{return 73;}
    std::string text;
    std::vector<int> values;
};
struct Property {
    int initialized{},destroyed{};
    bool throws{};
    std::string* order{};
    char name{'A'};
    void InitializeValue(void* value) {
        ++initialized;if(throws)throw std::runtime_error("initializer failure");
        new(value) Value;if(order)*order+=name;
    }
    void DestroyValue(void* value) {
        ++destroyed;static_cast<Value*>(value)->~Value();if(order)*order+=static_cast<char>(name-'A'+'a');
    }
};
using Storage=std::array<std::byte,sizeof(Value)>;
static_assert(!std::is_copy_constructible_v<ScopedNativeValue<Property>>);
void nontrivial_lifetime() {
    Property property;alignas(Value) Storage storage;
    {
        ScopedNativeValue owned(property,storage.data());
        require(property.initialized==1,"native values must be initialized before reading any nontrivial field");
        auto* value=reinterpret_cast<Value*>(storage.data());
        require(value->tag()==73&&value->text.empty()&&value->values.empty(),"the initializer must construct vptr, text and array state");
        value->text="localized objective";value->values={1,2,3};
        require(property.destroyed==0,"the native output must remain alive while snapshot values are copied");
    }
    require(property.destroyed==1,"each constructed native output must be destroyed exactly once");
}
void exception_cleanup() {
    Property outer,failed;failed.throws=true;alignas(Value) Storage a,b;
    bool caught=false;
    try {
        ScopedNativeValue first(outer,a.data());
        ScopedNativeValue second(failed,b.data());
    } catch(const std::runtime_error&) {caught=true;}
    require(caught&&outer.initialized==1&&outer.destroyed==1,"later initialization failure must unwind earlier constructed native values");
    require(failed.initialized==1&&failed.destroyed==0,"a failed initializer must not destroy an unconstructed value");
}
void reverse_cleanup_order() {
    std::string order;Property a,b;a.order=&order;b.order=&order;b.name='B';
    alignas(Value) Storage x,y;
    {
        ScopedNativeValue first(a,x.data());ScopedNativeValue second(b,y.data());
        require(order=="AB","native output parameters must be initialized before use");
    }
    require(order=="ABba","native output parameters must be destroyed in reverse ownership order");
}
void burning_castle_uses_the_current_floor_and_rescue_objective() {
    using resonance::story_input_detail::BurningCastleLeg;
    using resonance::story_input_detail::burning_castle_leg;
    require(burning_castle_leg(3020,1001,1110,1,{-40000,0,2.15})==BurningCastleLeg::Hall,
        "the rescue starts at the entrance hall doors");
    require(burning_castle_leg(3020,1001,1110,2,{-31797,0,22.15})==BurningCastleLeg::UpperLadder,
        "the burning lower hall leads around the blocked main stairs to the upper ladder");
    require(burning_castle_leg(3020,1001,1110,3,{-27700,6550,1320})==BurningCastleLeg::EastWing,
        "the top of the ladder leads onward into the east wing");
    require(burning_castle_leg(3020,1001,1110,5,{-10925,5800,1182.15})==BurningCastleLeg::KingChamber,
        "the east wing leads to the king's chamber");
    require(!burning_castle_leg(3020,1001,1120,2,{-31797,0,22.15})&&
        !burning_castle_leg(2014,1001,1110,2,{-31797,0,22.15})&&
        !burning_castle_leg(3020,1002,1110,2,{-31797,0,22.15}),
        "the rescue route cannot leak into other castle visits or objectives");
}
void kol_uses_the_current_objective_and_room() {
    using Kind=KolStoryLeg::Kind;
    constexpr std::array inn{FilteredPoint{0,0,2020,false,{}},FilteredPoint{0,0,2020,true,{1,10}}};
    constexpr std::array rental{FilteredPoint{0,0,2020,true,{1,11}}};
    const auto approach=kol_story_leg(1140,0,inn);
    const auto keeper=kol_story_leg(1140,3,inn);
    const auto exit=kol_story_leg(1150,3,rental);
    const auto stablehand=kol_story_leg(1150,0,rental);
    require(approach&&approach->kind==Kind::InnApproach&&keeper&&keeper->kind==Kind::Innkeeper&&
        keeper->contact==&inn[1],"the innkeeper replaces the village marker after entering the inn");
    require(exit&&exit->kind==Kind::InnExit&&stablehand&&stablehand->kind==Kind::Stablehand&&
        stablehand->contact==&rental[0],"the rental objective leads out of the inn and then to the stablehand");
    constexpr std::array path_points{FilteredPoint{1,0,2020,true,{1,11}},FilteredPoint{0,1,2020,true,{1,11}},
        FilteredPoint{0,0,2040,true,{1,11}}};
    require(!kol_story_leg(1140,3,std::span{inn}.first(1))&&!kol_story_leg(1150,0,path_points)&&
        !kol_story_leg(1150,0,inn)&&!kol_story_leg(1150,2,rental)&&!kol_story_leg(1160,0,rental),
        "Kol steps require the current native goal in a matching room and phase");
}
void kolobos_story_follows_the_current_room_and_bridge_state() {
    using Leg=KolobosStoryLeg;
    constexpr std::array goal{FilteredPoint{0,0,3040,false,{}}};
    const auto leg=[&](int area,Vec3 player,std::optional<bool> bridge_event=false) {
        return kolobos_story_leg(3040,1001,1190,area,player,goal,bridge_event);
    };
    require(leg(0,{1736,0,102.15})==Leg::BridgePassage&&
        leg(0,{7800,300,-1497.85},true)==Leg::DeeperPassage,
        "the entrance and lower hall share an area ID but lead to different passages");
    require(leg(1,{5300,8700,-297.6})==Leg::Bridge&&
        leg(3,{5400,24000,102.15},true)==Leg::Ladder&&
        leg(1,{6390.642,10696.266,-697.924},true)==Leg::LowerPassage,
        "follow the live bridge event, then the ladder and the passage from its landing");
    require(leg(7,{5200,850,-1200})==Leg::SideRoomReturn&&
        leg(1,{7600,9250,-297.85})==Leg::Bridge&&
        leg(5,{7600,-1700,300})==Leg::BridgePassage&&
        leg(6,{7600,8940,-297.85})==Leg::Bridge&&
        leg(4,{7100,29950,0},true)==Leg::InnerRoomReturn&&
        leg(2,{7450,28000,0},true)==Leg::InnerPassage,
        "a detour uses the exit on the current side of the ruins");
    constexpr std::array markers{FilteredPoint{1,0,3040,false,{}},FilteredPoint{0,1,3040,false,{}}};
    require(!kolobos_story_leg(3040,1001,1190,0,{1736,0,102.15},markers,false)&&
        !kolobos_story_leg(3040,1001,1205,0,{1736,0,102.15},goal,false)&&
        !kolobos_story_leg(3040,1002,1190,0,{1736,0,102.15},goal,false)&&
        !kolobos_story_leg(2020,1001,1190,0,{1736,0,102.15},goal,false)&&
        !leg(0,{1736,0,-700})&&!leg(1,{5300,8700,-297.6},std::nullopt),
        "the route requires the current native goal and a known room, floor and bridge state");
}
void kolobos_handoff_uses_the_next_displayed_goal() {
    constexpr std::array goal{FilteredPoint{0,0,2030,true,{1,1}}};
    constexpr std::array waypoint{FilteredPoint{1,0,2030,true,{1,1}}};
    require(grandport_from_felicitas(2040,1001,1205,goal)&&
        !grandport_from_felicitas(2040,1001,1200,goal)&&
        !grandport_from_felicitas(3040,1001,1205,goal)&&
        !grandport_from_felicitas(2040,1001,1205,waypoint),
        "the exit from Felicitas requires the current Grandport objective after the ruins");
}
}
int main() {
    int failures=0;
    const std::array<std::pair<const char*,void(*)()>,45> tests{{
        {"kolobos_story_follows_the_current_room_and_bridge_state",kolobos_story_follows_the_current_room_and_bridge_state},
        {"kolobos_handoff_uses_the_next_displayed_goal",kolobos_handoff_uses_the_next_displayed_goal},
        {"kol_uses_the_current_objective_and_room",kol_uses_the_current_objective_and_room},
        {"burning_castle_uses_the_current_floor_and_rescue_objective",burning_castle_uses_the_current_floor_and_rescue_objective},
        {"ultima_story_uses_unlocked_clear_save_and_current_movement",ultima_story_uses_unlocked_clear_save_and_current_movement},
        {"research_lab_requires_current_clearance_and_room",research_lab_requires_current_clearance_and_room},
        {"research_lab_follows_native_puzzle_changes",research_lab_follows_native_puzzle_changes},
        {"research_lab_routes_each_portal_landing_and_elevator",research_lab_routes_each_portal_landing_and_elevator},
        {"train_escape_selects_the_current_forward_door",train_escape_selects_the_current_forward_door},
        {"dungeon_story_routes_to_the_current_side_of_the_key_gate",dungeon_story_routes_to_the_current_side_of_the_key_gate},
        {"factory_story_follows_the_native_conveyor_landings",factory_story_follows_the_native_conveyor_landings},
        {"captain_story_stays_forward_along_the_hall",captain_story_stays_forward_along_the_hall},
        {"wind_shrine_landing_steps",wind_shrine_landing_steps},
        {"mobliz_escape_steps",mobliz_escape_steps},
        {"cave_story_tracks_the_current_bank",cave_story_tracks_the_current_bank},
        {"olderion_return_routes_follow_the_current_side",olderion_return_routes_follow_the_current_side},
        {"water_shrine_routes_follow_water_and_landings",water_shrine_routes_follow_water_and_landings},
        {"shrine_bridges_require_current_water_and_aligned_support",shrine_bridges_require_current_water_and_aligned_support},
        {"opening_section_continuation",opening_section_continuation},
        {"opening_continuations_from_entry",opening_continuations_from_entry},
        {"opening_overlap_rules",opening_overlap_rules},
        {"partial_candidate_read_failure",partial_candidate_read_failure},
        {"unreadable_duplicate_stays_ambiguous",unreadable_duplicate_stays_ambiguous},
        {"linked_story_exit_uses_one_route",linked_story_exit_uses_one_route},
        {"opening_menu_capture_keeps_epoch",opening_menu_capture_keeps_epoch},
        {"native_array_bounds",native_array_bounds},{"active_key_validation",active_key_validation},
        {"current_world_scope",current_world_scope},{"destination_identity_changes",destination_identity_changes},
        {"filtered_quest_authority",filtered_quest_authority},{"filtered_point_epoch",filtered_point_epoch},
        {"point_authorization",point_authorization},{"unique_point_selection",unique_point_selection},
        {"objective_survives_destination_failure",objective_survives_destination_failure},
        {"rendered_ancestor_chain",rendered_ancestor_chain},{"filtered_goal_areas",filtered_goal_areas},
        {"forward_only_story_exits",forward_only_story_exits},{"audited_forward_story_exits",audited_forward_story_exits},
        {"destination_area_feedback",destination_area_feedback},
        {"registry_marker_identity",registry_marker_identity},
        {"native_parameter_extent_and_allocation",native_parameter_extent_and_allocation},
        {"distinct_call_problem_logging",distinct_call_problem_logging},
        {"nontrivial_lifetime",nontrivial_lifetime},{"exception_cleanup",exception_cleanup},
        {"reverse_cleanup_order",reverse_cleanup_order}}};
    for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++failures;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    std::cout<<tests.size()-failures<<"/"<<tests.size()<<" adapter groups passed\n";
    return failures?1:0;
}
