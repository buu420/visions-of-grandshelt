#include "resonance/Battle.hpp"
#include "resonance/Localization.hpp"
#include <algorithm>
namespace resonance {
namespace {
const BattleUnit* unit(const std::vector<BattleUnit>& units,int id){
    const auto found=std::find_if(units.begin(),units.end(),[&](const auto& u){return u.id==id;});
    return found==units.end()?nullptr:&*found;
}
std::string identity(const BattleUnit& u,bool include_role=true){
    const auto role=u.side==BattleSide::Ally?"Ally":u.side==BattleSide::Enemy?"Enemy":"Unit";
    auto s=u.name;
    if(s.empty())s=tr(role);
    else if(include_role)s=trf("{0} {1}",{tr(role),s});
    if(!u.letter.empty())s+=' '+u.letter;
    return s;
}
void append(std::string& to,const std::string& text){
    if(text.empty())return;
    if(!to.empty()){
        const char end=to.back();
        const bool complete=end=='.'||end=='!'||end=='?'||
            to.ends_with("。")||to.ends_with("！")||to.ends_with("？");
        to+=(complete?" ":". ");
    }
    to+=text;
}
// HP as presented: a shown Libra HP entry gives current and maximum HP for
// either side, else only an ally's party row shows its current HP. Enemy
// HP/MP never comes from a raw unit row.
std::string describe(const BattleUnit& u,bool status,bool level,const BattleLibra* libra=nullptr,bool include_role=true){
    auto s=identity(u,include_role);
    if(level&&u.level>=0)append(s,trf("level {0}",{std::to_string(u.level)}));
    if(libra&&libra->hp&&*libra->hp>=0) {
        const auto current=std::to_string(*libra->hp);
        const auto hp=libra->hp_max&&*libra->hp_max>=0?
            trf("HP {0} of {1}",{current,std::to_string(*libra->hp_max)}):trf("HP {0}",{current});
        append(s,hp);
    } else if(status&&u.side==BattleSide::Ally&&u.hp&&*u.hp>=0)append(s,trf("HP {0}",{std::to_string(*u.hp)}));
    if(status&&u.side==BattleSide::Ally&&u.mp&&*u.mp>=0)append(s,trf("MP {0}",{std::to_string(*u.mp)}));
    return s;
}
bool level_presented(const BattleFrame& f,int id){
    return (f.choosing_target&&std::find(f.targets.begin(),f.targets.end(),id)!=f.targets.end())||
        (f.libra&&f.libra->unit==id);
}
std::string target_details(const BattleFrame& f,const BattleUnit& u,bool include_role){
    const auto* libra=f.libra&&f.libra->unit==u.id?&*f.libra:nullptr;
    auto s=describe(u,true,level_presented(f,u.id),libra,include_role);
    if(f.choosing_target&&u.side==BattleSide::Enemy) {
        append(s,u.target_prediction);
        if(!u.stagger.empty())append(s,u.stagger);
        else if(u.stagger_percent==100)append(s,tr("Stagger gauge full"));
        else if(u.stagger_percent)append(s,trf("Stagger gauge {0} percent",{std::to_string(*u.stagger_percent)}));
        append(s,u.health_indicator);
    }
    if(libra)for(const auto& detail:libra->details)append(s,detail);
    return s;
}
const BattleUnit* focused_enemy(const BattleFrame& f) {
    const auto selected=[&](int id){return std::find(f.targets.begin(),f.targets.end(),id)!=f.targets.end();};
    int id=-1;
    if(f.choosing_target&&selected(f.focused_target))id=f.focused_target;
    else if(f.choosing_target&&f.targets.size()==1)id=f.targets.front();
    else if(f.libra&&(!f.choosing_target||selected(f.libra->unit)))id=f.libra->unit;
    const auto* focus=unit(f.units,id);
    return focus&&focus->side==BattleSide::Enemy?focus:nullptr;
}
std::string targets(const BattleFrame& f,bool enemy_review=false){
    if(enemy_review||!f.choosing_target||f.targets.size()==1)
        if(const auto* focus=focused_enemy(f))return target_details(f,*focus,false);

    std::string s;
    bool has_enemy=false;
    std::vector<int> ids=f.choosing_target?f.targets:std::vector<int>{};
    if(f.libra&&std::find(ids.begin(),ids.end(),f.libra->unit)==ids.end())ids.push_back(f.libra->unit);
    std::vector<int> seen;
    for(const int id:ids){
        if(std::find(seen.begin(),seen.end(),id)!=seen.end())continue;
        seen.push_back(id);
        if(const auto* u=unit(f.units,id)){
            has_enemy=has_enemy||u->side==BattleSide::Enemy;
            auto description=target_details(f,*u,true);
            if(f.choosing_target&&f.focused_target==id)description=trf("Focused. {0}",{description});
            append(s,description);
        }
    }
    if(enemy_review&&!has_enemy)return {};
    return s.empty()?s:trf("Target. {0}",{s});
}
void remember(std::vector<BattleUnit>& known,const std::vector<BattleUnit>& current){
    for(const auto& u:current){
        if(u.id<0)continue;
        const auto old=std::find_if(known.begin(),known.end(),[&](const auto& prior){return prior.id==u.id;});
        if(old!=known.end())*old=u;
        else {if(known.size()>=256)known.erase(known.begin());known.push_back(u);}
    }
}
}
std::vector<SpeechEvent> BattleCore::observe(const BattleFrame& f){
    if(!f.active||f.context.empty()){reset();return {};}
    if(context_!=f.context){reset();context_=f.context;}
    remember(known_units_,f.units);
    if(f.prompt&&!f.prompt->id.empty()&&!f.prompt->text.empty()) {
        if(prompt_!=f.prompt->id) {
            prompt_=f.prompt->id;prompt_selection_=f.prompt->selection;
            actor_.clear();targets_.clear();banner_.clear();unavailable_.clear();
            return {{review(f),false}};
        }
        if(prompt_selection_==f.prompt->selection)return {};
        prompt_selection_=f.prompt->selection;
        if(!prompt_selection_.empty())return {{prompt_selection_,false,SpeechDelivery::LatestSelection}};
        return {};
    }
    prompt_.clear();prompt_selection_.clear();
    std::vector<SpeechEvent> result;
    const auto* acting=unit(f.units,f.actor);
    const auto actor_name=acting?identity(*acting,false):std::string{};
    const auto actor_key=acting?std::to_string(acting->id)+'|'+actor_name:std::string{};
    if(actor_key!=actor_){
        actor_=actor_key;
        if(acting)result.push_back({trf("{0} acting",{actor_name}),false});
    }
    const auto selected=targets(f);
    if(selected!=targets_){targets_=selected;if(!selected.empty())result.push_back({selected,false,SpeechDelivery::LatestSelection});}
    if(f.banner!=banner_){banner_=f.banner;if(!banner_.empty())result.push_back({banner_,false});}
    if(f.unavailable_reason!=unavailable_){unavailable_=f.unavailable_reason;if(!unavailable_.empty())result.push_back({unavailable_,false});}
    return result;
}
std::vector<SpeechEvent> BattleCore::events(const BattleFrame& f,const std::vector<BattleEvent>& events){
    if(!f.active||f.context.empty())return {};
    if(context_!=f.context){reset();context_=f.context;}
    remember(known_units_,f.units);
    if(muted_action_) {
        auto& muted=*muted_action_;
        if((muted.actor>=0&&f.actor>=0&&f.actor!=muted.actor)||
           (!muted.banner.empty()&&!f.banner.empty()&&f.banner!=muted.banner)||
           (muted.command_seen&&!f.command_visible))muted_action_.reset();
        else {
            if(muted.actor<0)muted.actor=f.actor;
            if(muted.banner.empty())muted.banner=f.banner;
            muted.command_seen|=f.command_visible;
        }
    }
    std::vector<SpeechEvent> result;
    std::vector<const BattleEvent*> ready;
    for(const auto& e:events)if(e.context==f.context&&e.sequence>last_event_&&e.presented&&!e.text.empty())ready.push_back(&e);
    std::stable_sort(ready.begin(),ready.end(),[](const auto* a,const auto* b){return a->sequence<b->sequence;});
    for(const auto* event:ready){
        const auto& e=*event;
        if(e.sequence<=last_event_)continue;
        last_event_=e.sequence;
        if(muted_action_)continue;
        BattleUnit recipient{e.unit,e.side,e.name,e.letter};
        if(const auto* known=unit(known_units_,e.unit)){
            if(recipient.name.empty())recipient.name=known->name;
            if(recipient.letter.empty())recipient.letter=known->letter;
            if(recipient.side==BattleSide::Unknown)recipient.side=known->side;
        }
        const auto label=recipient.name.empty()&&recipient.side==BattleSide::Unknown?tr("Battle result"):identity(recipient);
        result.push_back({label+". "+e.text,false,SpeechDelivery::OrderedBattle});
    }
    return result;
}
void BattleCore::interrupt_action(const BattleFrame& f) {
    if(!f.active||f.context.empty())return;
    if(context_!=f.context){reset();context_=f.context;}
    muted_action_=MutedAction{f.actor,f.banner,f.command_visible};
}
std::string BattleCore::review(const BattleFrame& f)const{
    if(!f.active)return {};
    if(f.prompt&&!f.prompt->text.empty()){
        auto text=f.prompt->text;append(text,f.prompt->selection);return text;
    }
    const auto* acting=unit(f.units,f.actor);
    const auto target_review=targets(f,true);
    if(!target_review.empty()) {
        auto s=acting?trf("{0} acting",{identity(*acting,false)}):std::string{};
        append(s,target_review);
        return s;
    }
    std::string s,party,enemies;
    for(const auto& u:f.units){
        if(u.side==BattleSide::Ally)append(party,describe(u,true,false));
        else if(u.side==BattleSide::Enemy)append(enemies,describe(u,false,level_presented(f,u.id)));
    }
    if(!party.empty())append(s,trf("Party. {0}",{party}));
    if(!enemies.empty())append(s,trf("Enemies. {0}",{enemies}));
    if(acting)append(s,trf("Acting. {0}",{identity(*acting)}));
    append(s,targets(f));append(s,f.banner);append(s,f.unavailable_reason);
    return s.empty()?tr("Battle information is unavailable."):s;
}
std::string BattleCore::status(const BattleFrame& f, BattleStatusAction action) {
    if(!f.active||f.context.empty()) {
        reviewed_ally_=-1;
        return {};
    }
    if(context_!=f.context){reset();context_=f.context;}
    if(f.prompt&&!f.prompt->id.empty()&&!f.prompt->text.empty())return {};

    std::vector<const BattleUnit*> allies;
    for(const auto& row:f.units)if(row.id>=0&&row.side==BattleSide::Ally&&row.party_status)allies.push_back(&row);
    if(allies.empty())return tr("Party status is unavailable.");
    const auto by_id=[&](int id) {
        return std::find_if(allies.begin(),allies.end(),[&](const auto* row){return row->id==id;});
    };
    auto member=by_id(reviewed_ally_);
    if(member==allies.end())member=by_id(f.actor);
    if(member==allies.end())member=allies.begin();
    if(action==BattleStatusAction::Next&&++member==allies.end())member=allies.begin();
    if(action==BattleStatusAction::Previous) {
        if(member==allies.begin())member=allies.end();
        --member;
    }
    const auto& row=**member;
    reviewed_ally_=row.id;
    const auto& shown=*row.party_status;
    auto speech=identity(row);

    const auto hp=[&] {
        if(f.libra&&f.libra->unit==row.id&&f.libra->hp&&*f.libra->hp>=0) {
            const auto current=std::to_string(*f.libra->hp);
            const auto text=f.libra->hp_max&&*f.libra->hp_max>0?
                trf("HP {0} of {1}",{current,std::to_string(*f.libra->hp_max)}):trf("HP {0}",{current});
            append(speech,text);
        } else append(speech,row.hp&&*row.hp>=0?trf("HP {0}",{std::to_string(*row.hp)}):tr("HP unavailable"));
    };
    const auto mp=[&] {
        append(speech,row.mp&&*row.mp>=0?trf("MP {0}",{std::to_string(*row.mp)}):tr("MP unavailable"));
    };
    const auto effects=[&](bool harmful) {
        if(!shown.effects) {
            append(speech,tr(harmful?"Debuffs unavailable":"Buffs unavailable"));
            return;
        }
        const auto& values=harmful?shown.effects->debuffs:shown.effects->buffs;
        if(values.empty()) {
            append(speech,tr(harmful?"No debuffs":"No buffs"));
            return;
        }
        std::string list;
        for(const auto& value:values) {
            if(!list.empty())list+=", ";
            list+=value;
        }
        append(speech,tr(harmful?"Debuffs":"Buffs"));
        append(speech,list);
    };
    switch(action) {
    case BattleStatusAction::Previous:
    case BattleStatusAction::Next:break;
    case BattleStatusAction::Hp:hp();break;
    case BattleStatusAction::Mp:mp();break;
    case BattleStatusAction::Vitals:hp();mp();break;
    case BattleStatusAction::Gauges:
        if(shown.limit_percent) {
            append(speech,trf("Limit gauge {0} percent",{std::to_string(*shown.limit_percent)}));
            if(shown.limit_level)append(speech,trf("Level {0}",{std::to_string(*shown.limit_level)}));
        } else append(speech,tr("Limit gauge unavailable"));
        break;
    case BattleStatusAction::Buffs:effects(false);break;
    case BattleStatusAction::Debuffs:effects(true);break;
    case BattleStatusAction::Effects:effects(false);effects(true);break;
    }
    return speech;
}
void BattleCore::reset(){context_.clear();actor_.clear();targets_.clear();banner_.clear();unavailable_.clear();prompt_.clear();prompt_selection_.clear();known_units_.clear();last_event_=0;reviewed_ally_=-1;muted_action_.reset();}
namespace {
// Counted values (digits with their group and decimal marks) do not change
// which page is shown; the rest of its presented text does.
std::string page_shape(const std::string& text){
    std::string s;
    for(std::size_t i=0;i<text.size();++i){
        const char c=text[i];
        const bool digit=c>='0'&&c<='9';
        const bool mark=(c==','||c=='.')&&!s.empty()&&s.back()=='#'&&i+1<text.size()&&text[i+1]>='0'&&text[i+1]<='9';
        if(digit||mark){if(s.empty()||s.back()!='#')s+='#';continue;}
        s+=c;
    }
    return s;
}
// Still periods before a page counts as settled. Count-ups change every
// ~94 ms; a step that never reports finished is spoken after the long wait.
constexpr std::uint64_t result_still_ms=800,result_staging_still_ms=3000;
}
std::vector<SpeechEvent> BattleResultCore::observe(const BattleResultFrame& f){
    if(f.context.empty()){reset();return {};}
    if(context_!=f.context){reset();context_=f.context;}
    std::string text;
    for(const auto& line:f.lines) {
        if(text.size()+line.size()>8192)break;
        append(text,line);
    }
    const auto key=f.page.empty()?(text.empty()?(f.focus.empty()?"":"controls"):"text:"+page_shape(text)):"page:"+f.page;
    if(key!=key_){key_=key;position_.reset();readable_=false;since_=f.now_ms;}
    if(f.lines!=lines_){lines_=f.lines;since_=f.now_ms;}
    std::vector<SpeechEvent> result;
    const auto still=f.now_ms>since_?f.now_ms-since_:0;
    const bool settled=!key_.empty()&&(!lines_.empty()||!f.focus.empty())&&!f.counting_rewards&&
        still>=(f.staging.value_or(false)?result_staging_still_ms:result_still_ms);
    const bool spoken=std::find(spoken_.begin(),spoken_.end(),key_)!=spoken_.end();
    readable_=!f.counting_rewards&&(settled||spoken);
    const bool focus_changed=f.focus!=focus_;
    focus_=f.focus;
    auto speech=std::move(text);append(speech,focus_);
    if(settled&&!spoken){
        // The first page follows the final battle reports; later pages replace the last.
        result.push_back({speech,!spoken_.empty()});
        if(spoken_.size()>=32)spoken_.erase(spoken_.begin());
        spoken_.push_back(key_);
    } else if(readable_&&spoken&&focus_changed&&!focus_.empty()){
        result.push_back({focus_,true,SpeechDelivery::LatestSelection});
    }
    return result;
}
std::string BattleResultCore::browse(int direction) {
    if(!readable_)return tr("Battle results are still appearing.");
    const auto count=lines_.size()+(!focus_.empty()?1:0);
    if(!count)return {};
    auto index=position_?std::min(*position_,count-1):direction<0?count-1:0;
    if(position_) {
        if(direction<0&&index>0)--index;
        if(direction>0&&index+1<count)++index;
    }
    position_=index;
    auto line=index<lines_.size()?lines_[index]:focus_;
    append(line,trf("{0} of {1}",{std::to_string(index+1),std::to_string(count)}));
    return line;
}
std::string BattleResultCore::review() const {
    if(!readable_)return tr("Battle results are still appearing.");
    std::string text;
    for(const auto& line:lines_) {
        if(text.size()+line.size()>8192)break;
        append(text,line);
    }
    append(text,focus_);
    return text;
}
void BattleResultCore::reset(){context_.clear();key_.clear();lines_.clear();focus_.clear();since_=0;spoken_.clear();position_.reset();readable_=false;}
}
