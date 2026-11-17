#pragma once
#include "resonance/Localization.hpp"
#include "resonance/TutorialInput.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace resonance::tutorial_input_detail {
struct Guide {
    std::string tag,label;
    bool known{},visible{},enabled{},close{},bindings_known{};
    std::vector<std::string> keys;
};
struct Observation {
    bool supported{},owner_valid{},readiness_known{},can_input{},opening{},closing{},input_enabled{};
    bool pages_known{};int page_index{},page_count{};
    bool guide_owner_valid{},guide_list_known{};
    std::vector<Guide> guides;
};
inline TutorialControls controls_for(const Observation& o) {
    TutorialControls result;result.supported=o.supported;
    if(!o.supported)return result;
    result.readiness_known=o.owner_valid&&o.readiness_known;
    result.ready=result.readiness_known&&o.can_input&&!o.opening&&!o.closing&&o.input_enabled;
    if(!result.ready)return result;
    const auto unavailable=[&]{result.instructions=tr("Tutorial controls are unavailable.");return result;};
    if(!o.guide_owner_valid||!o.guide_list_known||!o.pages_known||o.page_count<1||o.page_count>64||
       o.page_index<0||o.page_index>=o.page_count||o.guides.size()>16)return unavailable();
    std::vector<std::string> tags;
    for(const auto& guide:o.guides) {
        if(guide.tag.empty()||guide.tag.size()>256||std::find(tags.begin(),tags.end(),guide.tag)!=tags.end())return unavailable();
        tags.push_back(guide.tag);
    }
    bool unknown=false;
    for(const auto& guide:o.guides) {
        if(!guide.known){unknown=true;continue;}
        if(!guide.visible||!guide.enabled||(guide.close&&o.page_index+1!=o.page_count))continue;
        if(guide.label.empty()||guide.label.size()>512){unknown=true;continue;}
        std::string keys;
        bool known=guide.bindings_known&&!guide.keys.empty()&&guide.keys.size()<=16;
        std::vector<std::string> seen;
        if(known)for(const auto& key:guide.keys) {
            if(key.empty()||key.size()>128){known=false;break;}
            if(std::find(seen.begin(),seen.end(),key)!=seen.end())continue;
            seen.push_back(key);keys=keys.empty()?key:trf("{0} or {1}",{keys,key});
        }
        auto clause=trf("{0}: {1}.",{guide.label,known?keys:tr("current binding unavailable")});
        if(result.instructions.size()+clause.size()+1>2048){unknown=true;break;}
        if(!result.instructions.empty())result.instructions+=' ';result.instructions+=clause;
    }
    if(o.page_index+1<o.page_count) {
        if(!result.instructions.empty())result.instructions+=' ';
        result.instructions+=tr("Go to the last page to close.");
    }
    if(unknown) {
        if(!result.instructions.empty())result.instructions+=' ';
        result.instructions+=tr("Some tutorial controls are unavailable.");
    }
    if(result.instructions.empty())result.instructions=tr("No enabled tutorial controls are shown.");
    return result;
}
inline bool valid_array(const void* data,int count,int capacity,int limit) {
    return count>=0&&count<=limit&&capacity>=count&&capacity<=65536&&
        ((capacity==0&&!data)||(capacity>0&&data));
}
template<class Property> class NativeValue {
    Property* property_;void* storage_;
public:
    NativeValue(Property& property,void* storage):property_(&property),storage_(storage) {
        if(!storage_)throw std::invalid_argument("Missing native tutorial storage");
        property_->InitializeValue(storage_);
    }
    ~NativeValue(){property_->DestroyValue(storage_);}
    void copy_from(const void* source){property_->CopyCompleteValue(storage_,source);}
    NativeValue(const NativeValue&)=delete;NativeValue& operator=(const NativeValue&)=delete;
};
}
