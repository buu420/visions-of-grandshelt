#include "resonance/Battle.hpp"
#include <iostream>
#include <stdexcept>
using namespace resonance;
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
bool contains(const std::vector<SpeechEvent>& e,const std::string& s){for(const auto& x:e)if(x.text.find(s)!=std::string::npos)return true;return false;}
BattleFrame frame(){
    BattleFrame f;f.context="battle:1";f.active=true;f.actor=1;
    f.units={{1,BattleSide::Ally,"???","",-1,5151,433},{2,BattleSide::Enemy,"Magitek Armor","A",30,{},{}}};
    f.choosing_target=true;f.targets={2};return f;
}
BattleEvent hit(std::uint64_t sequence,std::string text="12 damage"){
    return {"battle:1",sequence,2,BattleSide::Enemy,"Magitek Armor","A",std::move(text),true};
}
void roles_and_mystery_names(){BattleCore c;auto f=frame();auto e=c.observe(f);
    require(contains(e,"??? acting"),"turn changes keep the displayed mystery name");
    require(!contains(e,"HP 5151")&&!contains(e,"MP 433"),"turn changes speak the actor's name by itself");
    require(contains(e,"Magitek Armor A. level 30"),"enemy focus reads the name, duplicate letter and displayed level");
    require(c.observe(f).empty(),"an unchanged frame is not repeated");
}
void turns_and_target_changes(){BattleCore c;auto f=frame();c.observe(f);f.actor=2;
    require(contains(c.observe(f),"Magitek Armor A acting"),"enemy turns read the acting enemy and duplicate letter");
    f.targets={1};require(contains(c.observe(f),"Ally ???"),"healing target is announced as an ally");
    f.choosing_target=false;f.targets.clear();c.observe(f);f.choosing_target=true;f.targets={1};
    require(contains(c.observe(f),"Ally ???"),"reopening target selection announces its current target");
}
void hidden_enemy_stats(){BattleCore c;auto f=frame();f.units[1].hp=99999;f.units[1].mp=99999;
    f.units.push_back({3,BattleSide::Enemy,"Watcher","B",20,{},{}});
    f.choosing_target=false;f.targets.clear();
    const auto s=c.review(f);require(s.find("99999")==std::string::npos,"raw enemy numbers cannot enter the party HUD review");
    require(s.find("Enemies.")!=std::string::npos&&s.find("Enemy Watcher B")!=std::string::npos,"screen review separates the full presented enemy side from the party");
    f.libra=BattleLibra{2,900,1000,{"Weak to lightning"}};
    const auto r=c.review(f);require(r.find("HP 900 of 1000")!=std::string::npos&&r.find("Weak to lightning")!=std::string::npos,"explicitly presented Libra values and labels are readable");
}
void identical_hits_and_pooling(){BattleCore c;auto f=frame();c.observe(f);const auto a=hit(1),b=hit(2);
    auto e=c.events(f,{a,b});require(e.size()==2&&contains(e,"Enemy Magitek Armor A. 12 damage"),"identical hits keep separate presentation identities");
    for(const auto& x:e)require(!x.interrupt,"damage must not interrupt pending speech");
    require(c.events(f,{a,b}).empty(),"reprocessing a presentation does not duplicate it");
    require(c.events(f,{hit(3)}).size()==1,"a reused popup can present another identical hit");
}
void display_text_and_context(){BattleCore c;auto f=frame();c.observe(f);
    auto a=hit(1,"Miss"),b=hit(2,"1,234 HP recovered. Critical");
    require(contains(c.events(f,{a,b}),"1,234 HP recovered. Critical"),"rendered formatting and recovery/title text survive unchanged");
    auto hidden=hit(3);hidden.presented=false;require(c.events(f,{hidden}).empty(),"a calculation without a presented popup is silent");
    hidden.presented=true;require(c.events(f,{hidden}).size()==1,"a previously unpresented event may become ready with the same sequence");
    auto empty=hit(4,"");require(c.events(f,{empty}).empty(),"empty display text is deferred without consuming identity");
    empty.text="24 damage";require(c.events(f,{empty}).size()==1,"late display text retains its event identity");
    require(c.events(f,{hit(6),hit(5)}).size()==2,"out-of-order records in one captured batch preserve both hits");
    auto old=hit(4);old.context="battle:old";require(c.events(f,{old}).empty(),"a prior battle cannot announce into the current battle");
    f.active=false;require(c.observe(f).empty()&&c.events(f,{hit(5)}).empty(),"no combat speech after returning to the field");
}
void missing_identity_and_removed_unit(){BattleCore c;auto f=frame();c.observe(f);f.units.pop_back();
    auto a=hit(1);a.name.clear();a.letter.clear();a.side=BattleSide::Unknown;
    require(contains(c.events(f,{a}),"Enemy Magitek Armor A"),"a disappearing defeated target retains its already presented identity");
    auto b=hit(2);b.unit=99;b.name.clear();b.letter.clear();b.side=BattleSide::Unknown;
    const auto e=c.events(f,{b});require(contains(e,"Battle result. 12 damage")&&!contains(e,"99"),"unidentified results remain readable without invented unit names or ids");
}
void current_status_and_new_context(){BattleCore c;auto f=frame();c.observe(f);f.units[0].hp=5100;
    f.choosing_target=false;
    require(c.review(f).find("HP 5100")!=std::string::npos,"party review uses the current HP");
    f.choosing_target=true;
    f.context="battle:2";require(!c.observe(f).empty(),"a fresh battle reannounces actor and target");
    auto a=hit(1);a.context=f.context;require(c.events(f,{a}).size()==1,"presentation sequence can restart in a fresh battle context");
}
void banner_and_unreadable_feedback(){BattleCore c;auto f=frame();f.banner="Thundaga";
    require(contains(c.observe(f),"Thundaga"),"presented skill banner is readable");
    require(c.observe(f).empty(),"a persistent banner is not repeated");f.banner.clear();c.observe(f);f.banner="Thundaga";
    require(contains(c.observe(f),"Thundaga"),"a new presentation of the same skill remains readable");
    f.unavailable_reason="Battle target could not be read.";require(contains(c.observe(f),f.unavailable_reason),"missing public state has explicit feedback");
    require(c.observe(f).empty(),"an unchanged failure is not spammed");
}
void next_command_preserves_hit_speech(){
    BattleCore battle;const auto f=frame();battle.observe(f);SpeechQueue queue;
    for(const auto& e:battle.events(f,{hit(1),hit(2),hit(3),hit(4)}))queue.push(e);
    ReaderCore ui;Frame command{"battle-menu",MenuSnapshot{"command","Attack","",0,4,true,true,true}};
    command.menu_interrupts=false;
    for(const auto& e:ui.observe(command))queue.push(e);
    for(int i=0;i<4;++i){const auto e=queue.pop();require(e&&e->text.find("12 damage")!=std::string::npos,"the next command menu must not erase queued repeated hit results");}
    const auto e=queue.pop();require(e&&e->text.find("Attack")!=std::string::npos,"the current command follows the captured hit results");
    for(const auto& event:battle.events(f,{hit(5),hit(6)}))queue.push(event);
    command.context="battle-results";command.menu->label="Victory";
    for(const auto& event:ui.observe(command))queue.push(event);
    for(int i=0;i<2;++i){const auto event=queue.pop();require(event&&event->text.find("12 damage")!=std::string::npos,"the automatic result menu cannot erase the final queued hits");}
    require(queue.pop()->text.find("Victory")!=std::string::npos,"results follow the final hit reports");
    ReaderCore delayed;command.introduction.clear();
    for(const auto& event:battle.events(f,{hit(7),hit(8)}))queue.push(event);
    for(const auto& event:delayed.observe(command))queue.push(event);
    command.introduction="Victory. Experience 100";
    for(const auto& event:delayed.observe(command))queue.push(event);
    for(int i=0;i<2;++i){const auto event=queue.pop();require(event&&event->text.find("12 damage")!=std::string::npos,"a late result heading preserves the final hit reports");}
    require(queue.pop()->text.find("Victory")!=std::string::npos,"results remain queued before the delayed heading");
    require(queue.pop()->text.find("Experience 100")!=std::string::npos,"a delayed heading follows the result selection");
    command.menu->label="Attack";command.introduction.clear();
    ReaderCore normal;queue.push({"Old menu",false});command.menu_interrupts=true;
    for(const auto& event:normal.observe(command))queue.push(event);
    require(queue.size()==1&&queue.pop()->text.find("Attack")!=std::string::npos,"ordinary menu interruption stays responsive outside battle");
}
void latest_native_selection_preserves_each_hit(){
    BattleCore battle;
    const auto f=frame();
    ReaderCore reader;
    SpeechQueue queue;
    Frame command{"battle-menu",MenuSnapshot{"command","Attack","",0,4,true,true,true}};
    command.menu_interrupts=false;
    for(const auto& e:battle.events(f,{hit(1),hit(2)}))queue.push(e);
    for(const auto& e:reader.observe(command))queue.push(e);
    for(const auto& e:battle.events(f,{hit(3)}))queue.push(e);
    command.menu->index=1;
    command.menu->label="Skills";
    for(const auto& e:reader.observe(command))queue.push(e);
    for(int i=0;i<3;++i) {
        const auto event=queue.pop();
        require(event&&event->delivery==SpeechDelivery::OrderedBattle&&event->text.find("12 damage")!=std::string::npos,
            "every ordered hit, including repeats, survives selection replacement");
    }
    const auto selection=queue.pop();
    require(selection&&selection->delivery==SpeechDelivery::LatestSelection&&selection->text=="Skills. 2 of 4",
        "only the latest selection follows the ordered hits");
    require(queue.size()==0,"an obsolete command must not remain in the queue");
}
void selected_ally_hp_as_presented(){
    BattleCore core;BattleFrame f;f.context="battle:1";f.active=true;f.actor=1;
    f.units={{1,BattleSide::Ally,"Rain","",1,249,25},{2,BattleSide::Ally,"Lasswell","",1,199,40}};
    f.choosing_target=true;f.targets={1};f.focused_target=1;f.libra=BattleLibra{1};
    auto e=core.observe(f);
    require(contains(e,"Target. Focused. Ally Rain. level 1. HP 249. MP 25")&&!contains(e," of "),
        "an ally target speaks the party HUD's current HP and MP, and no maximum the HUD does not show");
    f.libra=BattleLibra{1,249,270};e=core.observe(f);
    require(contains(e,"Target. Focused. Ally Rain. level 1. HP 249 of 270. MP 25")&&!contains(e,"HP 249. "),
        "a presented Libra HP entry gives current and maximum HP in place of the HUD's current HP");
    f.targets={2};f.focused_target=2;f.libra=BattleLibra{2};e=core.observe(f);
    require(contains(e,"Target. Focused. Ally Lasswell. level 1. HP 199. MP 40")&&!contains(e,"270"),
        "the next ally target gets its own presented values only");
}
void party_status_follows_review_selection(){
    BattleCore core;
    auto f=frame();
    f.units[0].party_status.emplace();
    f.units.push_back({3,BattleSide::Ally,"Lasswell","",1,201,12,BattlePartyStatus{}});
    f.units.push_back({4,BattleSide::Ally,"Off-screen ally","",1,1,1});
    require(core.status(f,BattleStatusAction::Vitals)=="Ally ??? HP 5151. MP 433",
        "the first request reviews the active ally's presented values");
    require(core.status(f,BattleStatusAction::Next)=="Ally Lasswell",
        "next cycles visible party rows in order and skips enemy and off-screen rows");
    f.actor=1;
    f.focused_target=1;
    f.units[2].hp=199;
    require(core.status(f,BattleStatusAction::Hp)=="Ally Lasswell. HP 199",
        "review selection stays independent of combat focus and reads live HP");
    require(core.status(f,BattleStatusAction::Mp)=="Ally Lasswell. MP 12",
        "MP reads the selected ally on its own");
    require(core.status(f,BattleStatusAction::Next)=="Ally ???"&&
        core.status(f,BattleStatusAction::Previous)=="Ally Lasswell",
        "next and previous wrap within the presented party");
    f.units.pop_back();
    f.units.pop_back();
    require(core.status(f,BattleStatusAction::Hp)=="Ally ??? HP 5151",
        "a removed member returns review to the currently available ally");
    f.prompt=BattlePrompt{"test","Visible combat prompt"};
    require(core.status(f,BattleStatusAction::Hp).empty(),
        "the combat prompt owns input while it is shown");
    f.prompt.reset();
    f.active=false;
    require(core.status(f,BattleStatusAction::Hp).empty(),"ended battles have no party status");
}
void party_status_empty_and_unknown_are_distinct(){
    BattleCore core;
    auto f=frame();
    auto& status=f.units[0].party_status.emplace();
    require(core.status(f,BattleStatusAction::Gauges).find("unavailable")!=std::string::npos,
        "unknown gauge has explicit unavailable feedback");
    require(core.status(f,BattleStatusAction::Buffs).find("unavailable")!=std::string::npos,
        "an unreadable status list cannot announce that there are no effects");
    status.limit_percent=75;
    status.limit_level=1;
    require(core.status(f,BattleStatusAction::Gauges)=="Ally ??? Limit gauge 75 percent. Level 1",
        "the visible Limit gauge and attained level are readable on demand");
    status.effects=BattleConditions{{"Protect","Haste"},{"Poison"}};
    require(core.status(f,BattleStatusAction::Buffs)=="Ally ??? Buffs. Protect, Haste",
        "buff readout returns only presented buffs");
    require(core.status(f,BattleStatusAction::Debuffs)=="Ally ??? Debuffs. Poison",
        "debuff readout returns only presented harmful conditions");
    require(core.status(f,BattleStatusAction::Effects)=="Ally ??? Buffs. Protect, Haste. Debuffs. Poison",
        "combined effects distinguishes the two categories");
    status.effects->buffs.clear();
    require(core.status(f,BattleStatusAction::Buffs)=="Ally ??? No buffs",
        "a verified empty category has explicit feedback");
    f.libra=BattleLibra{1,5151,5200};
    require(core.status(f,BattleStatusAction::Hp)=="Ally ??? HP 5151 of 5200",
        "an actually displayed Libra maximum can supplement ally HP");
    f.context="battle:2";
    f.units[0].party_status.reset();
    require(core.status(f,BattleStatusAction::Hp)=="Party status is unavailable.",
        "encounter changes cannot replay the prior ally or private values");
}
struct ResultStep{std::uint64_t at;std::string text;std::optional<bool> staging;std::string focus{};};
struct Heard{std::uint64_t at;SpeechEvent speech;};
// Replays reader ticks (every 16 ms) from the first step to until; each step's
// presentation holds until the next step begins.
std::vector<Heard> play(BattleResultCore& core,const std::string& page,const std::vector<ResultStep>& steps,std::uint64_t until){
    std::vector<Heard> heard;
    for(std::uint64_t t=steps.front().at;t<=until;t+=16){
        const ResultStep* step=&steps.front();
        for(const auto& s:steps)if(s.at<=t)step=&s;
        for(const auto& e:core.observe({"result:1",page,step->staging,{step->text},step->focus,t}))heard.push_back({t,e});
    }
    return heard;
}
// The 0.2.20 log's reward page: title fade-in, then EXP and Gil counted up every 94 ms.
std::vector<ResultStep> reward_count(std::uint64_t start){
    std::vector<ResultStep> s{{start,"EXP Gained. 0. Gil Obtained. Items Obtained. Potion. x 1",true},
        {start+187,"440. EXP Gained. 0. Gil Obtained. Items Obtained. Potion. x 1",true},
        {start+375,"Battle Result. 440. EXP Gained. 0. Gil Obtained. Items Obtained. Potion. x 1",true}};
    for(int i=1;i<15;++i)s.push_back({start+750+94u*(i-1),"Battle Result. 440. EXP Gained. "+std::to_string(i*24/15)+". Gil Obtained. "+
        std::to_string(i*40/15)+". Items Obtained. Potion. x 1",true});
    s.push_back({start+2066,"Battle Result. 440. EXP Gained. 24. Gil Obtained. 40. Items Obtained. Potion. x 1",false});
    return s;
}
const std::string final_reward="Battle Result. 440. EXP Gained. 24. Gil Obtained. 40. Items Obtained. Potion. x 1";
void result_page_spoken_once_with_final_counts(){
    BattleResultCore core;const auto heard=play(core,"0:Reward",reward_count(0),5000);
    require(heard.size()==1,"the reward page is spoken once, not on every counted value");
    require(heard[0].speech.text==final_reward,"it is spoken with its final counted values");
    require(!heard[0].speech.interrupt,"the first page does not cut off the final battle reports");
    require(heard[0].at>2066&&heard[0].at<2066+1500,"it follows the end of the count-up promptly");
}
void result_next_page_replaces_the_last(){
    BattleResultCore core;play(core,"0:Reward",reward_count(0),5000);
    // The growth page's units slide in with static text for 1.3 s before its gil count starts.
    std::vector<ResultStep> growth{{6000,"440",true},{6265,"Growth. 440",true},{6750,"Growth. 440. EXP. Rain. Lasswell",true}};
    for(int i=1;i<17;++i)growth.push_back({8062+94u*(i-1),"Growth. "+std::to_string(440+i*40/17)+". EXP. Rain. Lasswell",true});
    growth.push_back({8062+94u*16,"Growth. 480. EXP. Rain. Lasswell",false});
    const auto heard=play(core,"1:Growth",growth,12000);
    require(heard.size()==1&&heard[0].speech.text=="Growth. 480. EXP. Rain. Lasswell",
        "the growth page is spoken once, with its counted gil, not during its slide-in");
    require(heard[0].speech.interrupt,"a later page replaces the previous page's speech");
}
void result_focus_is_spoken_with_its_page(){
    BattleResultCore core;
    auto heard=play(core,"2:Ability",{{0,"Abilities. Rain",false,"Fire. 1 of 2"}},1200);
    require(heard.size()==1&&heard[0].speech.text=="Abilities. Rain. Fire. 1 of 2","a page is spoken once together with its focused control");
    heard=play(core,"2:Ability",{{1216,"Abilities. Rain",false,"Blizzard. 2 of 2"}},1300);
    require(heard.size()==1&&heard[0].at==1216&&heard[0].speech.text=="Blizzard. 2 of 2"&&heard[0].speech.interrupt,
        "moving the focus on a spoken page speaks the new control at once");
    require(play(core,"2:Ability",{{1316,"Abilities. Rain",false,"Blizzard. 2 of 2"}},2600).empty(),"an unchanged focus is not repeated");
}
void result_long_presentation_still_speaks(){
    BattleResultCore core;const auto heard=play(core,"1:LevelUp",{{0,"Level Up. Lv. 2. Lasswell",true}},8000);
    require(heard.size()==1&&heard[0].speech.text=="Level Up. Lv. 2. Lasswell"&&heard[0].at>1000,
        "a page whose presentation step never reports finished is still spoken once, after a longer stillness");
}
void result_unreadable_staging_waits_for_still_text(){
    BattleResultCore core;auto steps=reward_count(0);for(auto& s:steps)s.staging.reset();
    const auto heard=play(core,"",steps,5000);
    require(heard.size()==1&&heard[0].speech.text==final_reward,"without native page state, counting text still gives one announcement with final values");
}
void result_late_count_does_not_repeat_a_page(){
    BattleResultCore core;std::vector<ResultStep> steps{{0,"Growth. 440. EXP. Rain. Lasswell",false}};
    for(int i=1;i<17;++i)steps.push_back({2000+94u*(i-1),"Growth. "+std::to_string(440+i*40/17)+". EXP. Rain. Lasswell",false});
    steps.push_back({2000+94u*16,"Growth. 480. EXP. Rain. Lasswell",false});
    steps.push_back({6000,"Growth. 481. EXP. Rain. Lasswell",false});
    const auto heard=play(core,"1:Growth",steps,8000);
    require(heard.size()==1&&heard[0].speech.text=="Growth. 440. EXP. Rain. Lasswell",
        "count changes cannot announce the same native page a second time");
}
void result_native_page_identity_survives_text_changes(){
    BattleResultCore core;
    const auto heard=play(core,"0:Reward",{{0,"Battle Result. 440. EXP Gained. 24",false},{2000,"Battle Result. 440. EXP Gained. 24. No Damage Bonus. +10%",false},
        {4000,"Battle Result. 440. EXP Gained. 24",false}},6000);
    require(heard.size()==1&&heard[0].speech.text=="Battle Result. 440. EXP Gained. 24",
        "text changes do not turn a native result page into a different page");
}
void result_screen_closed_forgets_pages(){
    BattleResultCore core;play(core,"0:Reward",reward_count(0),5000);
    require(core.observe({"",{},{},{},{},5016}).empty(),"no result speech while no result screen is shown");
    require(play(core,"0:Reward",reward_count(9000),14000).size()==1,"the next battle's identical result page is spoken again");
}
void result_counters_and_review() {
    BattleResultCore core;
    BattleResultFrame f{"result:1","0:Reward",false,{"Battle Result","EXP Gained 0","Gil Obtained 0"},"Next",0,true};
    core.observe(f);f.now_ms=6000;
    require(core.observe(f).empty()&&core.review().find(" 0")==std::string::npos,
        "a stuck zero during count-up is neither announced nor available for review");
    f.counting_rewards=false;f.lines={"Battle Result","EXP Gained 24","Gil Obtained 0"};
    core.observe(f);f.now_ms+=1000;
    const auto heard=core.observe(f);
    require(heard.size()==1&&heard.front().text=="Battle Result. EXP Gained 24. Gil Obtained 0. Next",
        "the page includes the settled values, including a real zero, and the focused control");
    require(core.browse(1)=="Battle Result. 1 of 4"&&core.browse(1)=="EXP Gained 24. 2 of 4"&&
        core.browse(-1)=="Battle Result. 1 of 4","the same rows can be browsed in both directions");
    f.page="1:Growth";f.lines={"Growth","Rain"};core.observe(f);f.now_ms+=1000;core.observe(f);
    require(core.browse(1)=="Growth. 1 of 3","a new page resets the result cursor");
}
void interrupt_same_skill_and_resume() {
    BattleCore core;auto f=frame();f.actor=2;f.banner="Barrage";f.choosing_target=false;f.targets.clear();
    BattleEvent hit{f.context,1,1,BattleSide::Ally,"Rain",{},"100",true};
    require(core.events(f,{hit}).size()==1,"hands-off hit announcements still speak");
    core.interrupt_action(f);
    f.banner.clear();hit.sequence=2;
    require(core.events(f,{hit}).empty(),"a disappearing banner does not restart cancelled multi-hit announcements");
    f.command_visible=true;hit.sequence=3;
    require(core.events(f,{hit}).empty(),"tail hits do not interrupt the returned menu");
    f.command_visible=false;hit.sequence=4;
    require(core.events(f,{hit}).size()==1,"the next action can announce hits from the same actor again");
    require(core.events(f,{{f.context,2,1,BattleSide::Ally,"Rain",{},"100",true}}).empty(),
        "a suppressed hit cannot replay later");
}
void enemy_target_indicators_are_current() {
    BattleCore core;auto f=frame();f.focused_target=2;
    f.units[1].target_prediction="Weak";f.units[1].stagger="Staggered";f.units[1].health_indicator="Weakened";
    require(contains(core.observe(f),"Weak. Staggered. Weakened"),"displayed affinity, stagger and health band are in the selected enemy row");
    f.units[1].target_prediction.clear();f.units[1].stagger.clear();f.units[1].health_indicator.clear();
    const auto changed=core.observe(f);
    require(contains(changed,"Magitek Armor A. level 30")&&!contains(changed,"Weak")&&!contains(changed,"Staggered"),
        "a different skill or recovered target cannot reuse indicators");

    f.units[1].stagger_percent=75;
    f.libra=BattleLibra{2,240,384,{"Weak to Earth"}};
    const auto analyzed=core.observe(f);
    require(contains(analyzed,"HP 240 of 384")&&contains(analyzed,"Stagger gauge 75 percent")&&
        contains(analyzed,"Weak to Earth"),"the selected enemy reads its presented HP, gauge and Libra affinity");
    f.units[0].name="Lasswell";
    f.units.push_back({4,BattleSide::Ally,"Rain","",11,305,42});
    require(core.review(f)=="Lasswell acting. Magitek Armor A. level 30. HP 240 of 384. Stagger gauge 75 percent. Weak to Earth",
        "F9 during enemy targeting reads the actor and the focused enemy");
    core.observe(f);
    require(core.observe(f).empty(),"a steady stagger bar does not repeat the target");
    f.units.push_back({3,BattleSide::Enemy,"Damia","B",12,416});
    f.units.back().stagger_percent=100;
    f.targets={3};f.focused_target=3;f.libra=BattleLibra{3};
    const auto unscanned=core.observe(f);
    require(contains(unscanned,"Damia B. level 12. Stagger gauge full")&&
        !contains(unscanned,"HP")&&!contains(unscanned,"Earth"),"a new target reads its own visible gauge without unshown details");

    f.units.back().stagger_percent=0;
    f.units.back().stagger="Staggered";
    const auto staggered=core.observe(f);
    require(contains(staggered,"Staggered")&&!contains(staggered,"percent"),
        "the displayed staggered label replaces the bar reading");

    f.choosing_target=false;f.targets.clear();f.units[1].target_prediction="Weak";
    f.libra.reset();
    const auto review=core.review(f);
    require(review.find("Rain. HP 305")!=std::string::npos&&review.find("Weak")==std::string::npos&&review.find("Stagger")==std::string::npos,
        "target indicators stay scoped to selection");
}
void focused_target_and_level_visibility(){
    BattleCore core;auto f=frame();f.units.push_back({3,BattleSide::Enemy,"Watcher","B",20,{},{}});f.targets={2,3};f.focused_target=2;
    require(contains(core.observe(f),"Focused. Enemy Magitek Armor A"),"multi-target speech must identify its current primary focus");
    f.focused_target=3;require(contains(core.observe(f),"Focused. Enemy Watcher B"),"changing primary focus within the same target set remains audible");
    require(core.review(f)=="??? acting. Watcher B. level 20","review of a group selection follows the focused enemy");
    f.choosing_target=false;f.targets.clear();f.focused_target=-1;f.actor=2;
    require(core.review(f).find("level 30")==std::string::npos,"an acting enemy cannot disclose its level without a presented target role");
    f.libra=BattleLibra{2};require(core.review(f).find("level 30")!=std::string::npos,"visible target information may disclose the displayed enemy level");
}
void visible_battle_prompt_takes_precedence(){
    BattleCore core;auto f=frame();
    const std::string gamepad="Select Resonance. D-pad Up, Hibernal Fury. D-pad Left, Healing Wind. B, Cancel.";
    const std::string keyboard="Select Resonance. W, Hibernal Fury. A, Healing Wind. C, Cancel.";
    f.prompt=BattlePrompt{"resonance-options",gamepad};
    const auto opened=core.observe(f);
    require(opened.size()==1&&opened[0].text==gamepad&&!opened[0].interrupt,
        "the presented prompt replaces HUD speech without cutting off the last damage report");
    require(core.review(f)==gamepad,"screen review repeats the current prompt");
    f.prompt->text=keyboard;
    require(core.observe(f).empty(),"changing displayed keys alone does not repeat the prompt");
    require(core.review(f)==keyboard,"review reads the current input device's bindings");
    f.prompt->selection="Hibernal Fury. Deal ice magic damage.";
    const auto focused=core.observe(f);
    require(focused.size()==1&&focused[0].text==f.prompt->selection&&focused[0].delivery==SpeechDelivery::LatestSelection,
        "focus speaks the new visible tooltip once");
    require(core.observe(f).empty()&&core.review(f)==keyboard+" "+f.prompt->selection,
        "repeat includes the active tooltip without repeating it automatically");
    f.prompt=BattlePrompt{"resonance-options-updated","Select Resonance. W, Hibernal Fury. C, Cancel."};
    const auto changed=core.observe(f);
    require(changed.size()==1&&changed[0].text==f.prompt->text,"new visible choices are announced");
    f.prompt.reset();
    require(contains(core.observe(f),"??? acting"),"regular combat speech resumes after the prompt closes");
    require(core.review(f).find("Select Resonance")==std::string::npos,"review drops the closed prompt");
    f.prompt=BattlePrompt{"resonance-options",gamepad};
    require(contains(core.observe(f),"Select Resonance"),"a reopened prompt is announced again");
    f.context.clear();f.active=false;
    require(core.observe(f).empty()&&core.review(f).empty(),"an old prompt cannot speak outside the active battle");
}
}
int main(){int failed=0;for(const auto& [name,test]:{std::pair{"roles_and_mystery_names",roles_and_mystery_names},
    {"turns_and_target_changes",turns_and_target_changes},{"hidden_enemy_stats",hidden_enemy_stats},
    {"identical_hits_and_pooling",identical_hits_and_pooling},{"display_text_and_context",display_text_and_context},
    {"missing_identity_and_removed_unit",missing_identity_and_removed_unit},{"current_status_and_new_context",current_status_and_new_context},
    {"banner_and_unreadable_feedback",banner_and_unreadable_feedback},{"next_command_preserves_hit_speech",next_command_preserves_hit_speech},
    {"latest_native_selection_preserves_each_hit",latest_native_selection_preserves_each_hit},
    {"focused_target_and_level_visibility",focused_target_and_level_visibility},{"selected_ally_hp_as_presented",selected_ally_hp_as_presented},
    {"visible_battle_prompt_takes_precedence",visible_battle_prompt_takes_precedence},
    {"party_status_follows_review_selection",party_status_follows_review_selection},
    {"party_status_empty_and_unknown_are_distinct",party_status_empty_and_unknown_are_distinct},
    {"result_page_spoken_once_with_final_counts",result_page_spoken_once_with_final_counts},{"result_next_page_replaces_the_last",result_next_page_replaces_the_last},
    {"result_focus_is_spoken_with_its_page",result_focus_is_spoken_with_its_page},{"result_long_presentation_still_speaks",result_long_presentation_still_speaks},
    {"result_unreadable_staging_waits_for_still_text",result_unreadable_staging_waits_for_still_text},
    {"result_late_count_does_not_repeat_a_page",result_late_count_does_not_repeat_a_page},
    {"result_native_page_identity_survives_text_changes",result_native_page_identity_survives_text_changes},
    {"result_screen_closed_forgets_pages",result_screen_closed_forgets_pages},
    {"result_counters_and_review",result_counters_and_review},
    {"interrupt_same_skill_and_resume",interrupt_same_skill_and_resume},
    {"enemy_target_indicators_are_current",enemy_target_indicators_are_current}}){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return failed?1:0;
}
