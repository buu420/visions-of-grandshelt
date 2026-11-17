#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/StoryInput.hpp"
#include "resonance/Speech.hpp"
#include "resonance/NativeMetadata.hpp"
#include "resonance/AreaInput.hpp"
#include "resonance/Localization.hpp"
#include "StoryInputValidation.hpp"
#include "OpeningStoryValidation.hpp"
#include "ChapterTwoStory.hpp"
#include "ChapterThreeStory.hpp"
#include "ChapterThreeWaterShrine.hpp"
#include "ChapterThreeShrineBridges.hpp"
#include "ChapterFourStory.hpp"
#include "ChapterSixLab.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <map>
#include <memory>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

// Exact-build evidence: story-navigation-native-audit-20261101/NATIVE-STORY.md.
// MainQuestId/QuestData authorizes main-story targets. Authored local steps
// additionally verify the current actors, and the post-credits route uses the
// clear flag and the Enterprise radar event's own progress slot.
// This adapter never executes transitions, events or world-map transforms.
namespace resonance {
namespace {
using namespace resonance::unreal;
using namespace story_input_detail;
constexpr std::uint32_t dying_flags=0x8000u|0x10000u|0x40000000u;
constexpr std::uint32_t ordinary_excluded=0x10u|0x20u|dying_flags;
bool live(UObject* object) {
    return object&&UObjectGlobals::IsValidObjectForFindXOf(object)&&
        !(static_cast<std::uint32_t>(object->GetObjectFlags())&ordinary_excluded);
}
std::wstring path(UObject* object) {
    if(!object)return {};
    auto result=object->GetFullName();
    const auto space=result.find(L' ');
    return space==std::wstring::npos?result:result.substr(space+1);
}
bool is(UObject* object,const wchar_t* type) {
    if(!object)return false;
    for(auto* cls=static_cast<UStruct*>(object->GetClassPrivate());cls;cls=cls->GetSuperStruct())
        if(path(cls)==type)return true;
    return false;
}
std::string utf8(const std::wstring& text) {
    if(text.empty())return {};
    if(text.size()>16384)throw std::runtime_error("Story text exceeds its capture limit");
    const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),
        static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(length<=0)throw std::runtime_error("Invalid localized story text");
    std::string result(static_cast<std::size_t>(length),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),
        result.data(),length,nullptr,nullptr)!=length)throw std::runtime_error("Invalid localized story text");
    return result;
}
std::string clean(const std::wstring& input) {
    std::wstring result;bool tag=false,space=false;
    for(auto c:input) {
        if(c==L'<'){tag=true;continue;}
        if(c==L'>'&&tag){tag=false;continue;}
        if(tag)continue;
        if(iswspace(c)){if(!result.empty())space=true;continue;}
        if(c<32||c==127)continue;
        if(space){result+=L' ';space=false;}
        result+=c;
    }
    return utf8(result);
}
std::vector<UObject*> instances(const wchar_t* type) {
    std::vector<UObject*> result;UObjectGlobals::FindAllOf(type,result);
    std::erase_if(result,[](auto* object){return !live(object);});
    return result;
}
void* property(UObject* object,const wchar_t* name,int offset,int size) {
    if(!live(object))return nullptr;
    auto* field=object->GetPropertyByNameInChain(name);
    if(!field||field->GetOffset_Internal()!=offset||field->GetSize()!=size)return nullptr;
    return object->GetValuePtrByPropertyNameInChain(name);
}
template<class T> std::optional<T> read(UObject* object,const wchar_t* name,int offset) {
    auto* value=property(object,name,offset,sizeof(T));if(!value)return {};
    T result{};std::memcpy(&result,value,sizeof result);return result;
}
UObject* object_value(UObject* object,const wchar_t* name,int offset) {
    auto value=read<UObject*>(object,name,offset);
    return value&&live(*value)?*value:nullptr;
}
UObject* actor_world(UObject* actor) {
    if(!live(actor)||!is(actor,L"/Script/Engine.Actor"))return nullptr;
    auto* level=actor->GetOuterPrivate();
    if(!live(level)||!is(level,L"/Script/Engine.Level"))return nullptr;
    auto* world=object_value(level,L"OwningWorld",192);
    return world&&is(world,L"/Script/Engine.World")?world:nullptr;
}
struct ScopedObject {UObject* object{};bool ambiguous{};};
struct WorldAccess {
    bool valid(UObject* object)const{return live(object);}
    bool is_world(UObject* object)const{return is(object,L"/Script/Engine.World");}
    bool is_level(UObject* object)const{return is(object,L"/Script/Engine.Level");}
    UObject* level_world(UObject* object)const{return object_value(object,L"OwningWorld",192);}
    UObject* outer(UObject* object)const{return object->GetOuterPrivate();}
};
ScopedObject world_scoped(const wchar_t* type,UObject* world) {
    auto candidates=instances(type);
    const auto selected=unique_world_member<UObject>(candidates,world,[](auto* object){return resolved_world(object,WorldAccess{});});
    return {selected.object,selected.ambiguous};
}
ScopedObject gi_scoped(const wchar_t* type,UObject* owner) {
    ScopedObject result;if(!live(owner))return result;
    for(auto* candidate:instances(type))if(candidate->GetOuterPrivate()==owner) {
        if(result.object)return {nullptr,true};
        result.object=candidate;
    }
    return result;
}
struct StructField {const wchar_t* name;int offset,size;const wchar_t* type{};};
bool struct_fields(UStruct* type,std::initializer_list<StructField> fields) {
    for(const auto& expected:fields) {
        FName name(expected.name,FNAME_Find);
        if(!name.ComparisonIndex&&!name.Number)return false;
        FProperty* found=nullptr;
        for(auto* parent=type;parent&&!found;parent=parent->GetSuperStruct())found=parent->FindProperty(name);
        if(!found||found->GetOffset_Internal()!=expected.offset||found->GetSize()!=expected.size)return false;
        if(expected.type) {
            auto* field_class=found->field_class();
            if(!field_class||field_class->GetName()!=expected.type)return false;
        }
    }
    return true;
}
bool output_struct(FProperty* field,const wchar_t* expected,int size) {
    // The pinned UE4SS TObjectPtr is one raw pointer (public upgrade guide).
    // Read its opaque reference; never invent a smart-pointer/handle resolver.
    auto& handle=static_cast<FStructProperty*>(field)->GetStruct();
    UScriptStruct* type{};std::memcpy(&type,std::addressof(handle),sizeof type);
    if(!native_metadata(type,NativeMetadataKind::ScriptStruct)||path(type)!=expected||type->GetPropertiesSize()!=size)return false;
    const std::wstring_view name(expected);
    if(name==L"/Script/mucha.QuestData")return struct_fields(type,{
        {L"Property",0,16},{L"ID",16,4},{L"Category",20,1},{L"Name",24,16},{L"Subtitle",40,16},
        {L"DestinationTitle",56,16},{L"Description",72,16,L"TextProperty"},
        {L"Progression",104,1},{L"DestinationList",112,16}});
    if(name==L"/Script/mucha.FieldObjectData")return struct_fields(type,{
        {L"TypeData",8,8},{L"RelatedMapId",24,4},{L"RelatedQuestIdList",32,16},{L"ReferenceActor",48,40}});
    if(name==L"/Script/mucha.MapEventData")return struct_fields(type,{
        {L"Condition",0,16},{L"EventId",16,8},{L"MoveInfo",24,16},{L"FadeInfo",40,16}});
    if(name==L"/Script/mucha.SaveData_Progress") {
        if(!struct_fields(type,{{L"Progress",0,16,L"ArrayProperty"}}))return false;
        auto* array=static_cast<FArrayProperty*>(type->FindProperty(FName(L"Progress",FNAME_Find)));
        auto* inner=array?array->GetInner():nullptr;
        return inner&&inner->GetSize()==4&&inner->field_class()&&inner->field_class()->GetName()==L"IntProperty";
    }
    if(name==L"/Script/mucha.TalkEventFindResult")return struct_fields(type,{
        {L"EventId",0,8},{L"PreActionInfo",8,32}});
    if(name==L"/Script/mucha.MAP_GIMMICK_SAVEPOINT_PARENT_DATA")return struct_fields(type,{
        {L"mapId",0,4,L"IntProperty"},{L"UniqueID",4,4,L"IntProperty"},{L"savePoint",8,8,L"ObjectProperty"}});
    return false;
}
struct OutParameter {const wchar_t* name{};int offset{},size{};const wchar_t* type{};const wchar_t* function{};};
class Call {
    UFunction* function_{};
    const char* rejected_at_{"receiver"};
    int observed_parms_{-1},observed_allocation_{-1},observed_return_offset_{-1};
    alignas(16) std::array<std::byte,512> storage_{};
    // Destruction order is return value, then the nontrivial OutData.
    std::unique_ptr<ScopedNativeValue<FProperty>> input_,out_,returned_;
public:
    Call(UObject* receiver,const wchar_t* name,int bytes,int return_offset,int return_size,
         OutParameter output={},bool public_library=false) {
        const std::wstring_view method(name);
        const bool progress=method==L"GetMainProgress";
        const bool progress_flag=method==L"GetFlag";
        const bool progress_list=method==L"GetProgressList";
        const auto library_type=progress?L"/Script/mucha.CPP_BFL_Progress":
            progress_flag?L"/Script/mucha.CPP_BFL_SaveDataProgress":
            progress_list?L"/Script/mucha.CPP_BFL_DBG_FlagSaveData":L"/Script/mucha.CPP_BFL_Map";
        if(public_library) {
            const auto default_path=progress?L"/Script/mucha.Default__CPP_BFL_Progress":
                progress_flag?L"/Script/mucha.Default__CPP_BFL_SaveDataProgress":
                progress_list?L"/Script/mucha.Default__CPP_BFL_DBG_FlagSaveData":L"/Script/mucha.Default__CPP_BFL_Map";
            if(!receiver||path(receiver)!=default_path||
               path(receiver->GetClassPrivate())!=library_type||
               !(static_cast<std::uint32_t>(receiver->GetObjectFlags())&0x10u)||
               (static_cast<std::uint32_t>(receiver->GetObjectFlags())&dying_flags)||
               (!progress&&!progress_flag&&!progress_list&&
                method!=L"GetCurrentMapId"&&method!=L"GetGroupMapIdListAtMap"))return;
        } else if(!live(receiver))return;
        rejected_at_="function lookup";
        auto* function=receiver->GetFunctionByNameInChain(name);if(!function)return;
        observed_parms_=function->GetParmsSize();observed_allocation_=function->GetPropertiesSize();
        observed_return_offset_=function->GetReturnValueOffset();
        const auto layout=native_call_layout(observed_parms_,observed_allocation_,observed_return_offset_,
            bytes,return_offset,return_size,static_cast<int>(storage_.size()));
        if(layout!=NativeLayoutResult::Valid){rejected_at_=native_layout_name(layout);return;}
        rejected_at_="native flag";
        if(!function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400)))return;
        rejected_at_="library identity/flags";
        if(public_library&&(path(function)!=std::wstring(library_type)+L":"+name||
           !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x2000))||
           !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(1))))return;
        auto* result=function->GetReturnProperty();
        rejected_at_="return property";
        if(!result||result->GetOffset_Internal()!=return_offset||result->GetSize()!=return_size||
           !result->HasAnyPropertyFlags(0x80)||!result->HasAnyPropertyFlags(0x400)||
           return_offset<0||return_offset+return_size>bytes)return;
        if(output.name) {
            rejected_at_="output function identity/flags";
            if(!output.type||!output.function||path(function)!=output.function||
               !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400000))||
               !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(1)))return;
            FName field_name(output.name,FNAME_Find);
            rejected_at_="output property name";
            if(!field_name.ComparisonIndex&&!field_name.Number)return;
            auto* field=function->FindProperty(field_name);
            rejected_at_="output property layout/flags";
            if(!field||field->GetOffset_Internal()!=output.offset||field->GetSize()!=output.size||
               !field->HasAnyPropertyFlags(0x80)||!field->HasAnyPropertyFlags(0x100)||
               output.offset<0||output.offset+output.size>return_offset)return;
            rejected_at_="output struct identity/layout";
            if(!output_struct(field,output.type,output.size))return;
            out_=std::make_unique<ScopedNativeValue<FProperty>>(*field,storage_.data()+output.offset);
        }
        returned_=std::make_unique<ScopedNativeValue<FProperty>>(*result,storage_.data()+return_offset);
        function_=function;
    }
    Call(const Call&)=delete;Call& operator=(const Call&)=delete;
    explicit operator bool()const{return function_!=nullptr;}
    std::string rejection()const {
        return std::string("stage=")+rejected_at_+" parms="+std::to_string(observed_parms_)+
            " allocation="+std::to_string(observed_allocation_)+" return_offset="+std::to_string(observed_return_offset_);
    }
    bool parameter(const wchar_t* name,int offset,int size,const wchar_t* type=nullptr)const {
        if(!function_)return false;
        FName key(name,FNAME_Find);if(!key.ComparisonIndex&&!key.Number)return false;
        auto* field=function_->FindProperty(key);
        return field&&field->GetOffset_Internal()==offset&&field->GetSize()==size&&field->HasAnyPropertyFlags(0x80)&&
            (!type||(field->field_class()&&field->field_class()->GetName()==type));
    }
    bool return_kind(const wchar_t* type)const {
        auto* field=function_?function_->GetReturnProperty():nullptr;
        return field&&field->field_class()&&field->field_class()->GetName()==type;
    }
    bool progress_list_return()const {
        return return_kind(L"StructProperty")&&
            output_struct(function_->GetReturnProperty(),L"/Script/mucha.SaveData_Progress",48);
    }
    bool event_array(FProperty* field)const {
        if(!field||field->GetSize()!=16||!field->field_class()||field->field_class()->GetName()!=L"ArrayProperty")return false;
        auto* inner=static_cast<FArrayProperty*>(field)->GetInner();
        return inner&&inner->GetOffset_Internal()==0&&inner->GetSize()==56&&
            output_struct(inner,L"/Script/mucha.MapEventData",56);
    }
    bool event_array_return()const {
        return function_&&path(function_)==L"/Script/mucha.CPP_MapGimmick:GetEventList"&&
            event_array(function_->GetReturnProperty());
    }
    bool copy_event_array_input(const void* source) {
        if(!function_||!source||path(function_)!=L"/Script/mucha.CPP_MapGimmick:FindMatchEventByEventList"||
           !parameter(L"EventList",0,16))return false;
        auto* field=function_->FindProperty(FName(L"EventList",FNAME_Find));
        if(!event_array(field)||!field->HasAnyPropertyFlags(0x2)||!field->HasAnyPropertyFlags(0x100)||
           !field->HasAnyPropertyFlags(0x8000000))return false;
        input_=std::make_unique<ScopedNativeValue<FProperty>>(*field,storage_.data());
        field->CopyCompleteValue(storage_.data(),source);
        return true;
    }
    void invoke(UObject* receiver){receiver->ProcessEvent(function_,storage_.data());}
    std::byte* data(){return storage_.data();}
    template<class T> T get(std::size_t offset)const {T result{};std::memcpy(&result,storage_.data()+offset,sizeof result);return result;}
    template<class T> void put(std::size_t offset,const T& value){std::memcpy(storage_.data()+offset,&value,sizeof value);}
};
void report_find_quest_rejection(const Call& call) {
    static NativeProblemDeduplicator reported;
    const auto problem="FindQuestData rejected "+call.rejection();
    if(reported.accept(problem))log("STORY INPUT "+problem);
}
template<class T> std::optional<T> getter(UObject* receiver,const wchar_t* name) {
    Call call(receiver,name,sizeof(T),0,sizeof(T));if(!call)return {};
    call.invoke(receiver);return call.get<T>(0);
}
std::optional<bool> bool_getter(UObject* receiver,const wchar_t* name) {
    const auto result=getter<unsigned char>(receiver,name);
    return result?std::optional<bool>(*result!=0):std::nullopt;
}
UObject* object_getter(UObject* receiver,const wchar_t* name) {
    const auto result=getter<UObject*>(receiver,name);
    return result&&live(*result)?*result:nullptr;
}
template<class T> struct NativeArray {T* data;int num,max;};
static_assert(sizeof(NativeArray<int>)==16&&sizeof(ObjectKey)==8&&offsetof(ObjectKey,id)==4);
template<class T> T pod(const std::byte* source,std::size_t offset) {
    T result{};std::memcpy(&result,source+offset,sizeof result);return result;
}
template<class T> std::optional<std::vector<T>> copy_array(NativeArray<T> header,int limit) {
    if(!valid_native_array(header.data,header.num,header.max,limit))return {};
    std::vector<T> result;result.reserve(static_cast<std::size_t>(header.num));
    for(int i=0;i<header.num;++i)result.push_back(pod<T>(reinterpret_cast<const std::byte*>(header.data),i*sizeof(T)));
    return result;
}
template<class T> std::optional<std::vector<T>> copied_array(UObject* owner,const wchar_t* name,int offset,int limit) {
    auto header=read<NativeArray<T>>(owner,name,offset);
    return header?copy_array(*header,limit):std::nullopt;
}
std::optional<std::vector<FilteredPoint>> quest_points(const std::byte* row) {
    const auto header=pod<NativeArray<std::byte>>(row,112);
    if(!valid_native_array(header.data,header.num,header.max,256))return {};
    std::vector<FilteredPoint> result;
    for(int i=0;i<header.num;++i) {
        const auto* point=header.data+static_cast<std::size_t>(i)*40;
        FilteredPoint value{pod<unsigned char>(point,0),pod<unsigned char>(point,4),pod<int>(point,8),
            (pod<unsigned char>(point,12)&1)!=0,pod<ObjectKey>(point,16)};
        if(!valid_filtered_point(value))return {};
        result.push_back(value);
    }
    return result;
}
bool field_control(UObject* pawn,UObject* world,UObject* game_instance,unsigned char travel_method=1) {
    const auto controlled=bool_getter(pawn,L"IsLocallyControlled"),enabled=bool_getter(pawn,L"IsEnableUnitInput");
    const auto paused=bool_getter(pawn,L"IsPaused"),ignored=bool_getter(pawn,L"IsMoveInputIgnored");
    const auto method=read<unsigned char>(pawn,L"m_MapMovementMethod",1920);
    if(!controlled||!*controlled||!enabled||!*enabled||!paused||*paused||!ignored||*ignored||!method||
        (*method!=1&&*method!=travel_method))return false;
    const auto input=world_scoped(L"CPP_InputManager",world);
    const auto input_enabled=bool_getter(input.object,L"IsEnableInput");
    if(input.ambiguous||!input_enabled||!*input_enabled)return false;
    const auto ui=gi_scoped(L"CPP_GameWidgetManagerSubsystem",game_instance);
    auto* manager=object_value(ui.object,L"m_GameWidgetInputManager",144);
    if(ui.ambiguous||!manager||!property(manager,L"InputBlockSet",48,80))return false;
    int count{},free_count{};
    std::memcpy(&count,reinterpret_cast<std::byte*>(manager)+56,4);
    std::memcpy(&free_count,reinterpret_cast<std::byte*>(manager)+100,4);
    if(count<0||count>4096||free_count<0||free_count>count||count-free_count>0)return false;
    const auto talk=gi_scoped(L"CPP_TalkEventSubsystem",game_instance);
    if(talk.ambiguous||!talk.object||!property(talk.object,L"m_pEventManager",88,8))return false;
    auto* event=object_value(talk.object,L"m_pEventManager",88);
    if(event) {unsigned char state{};std::memcpy(&state,reinterpret_cast<std::byte*>(event)+336,1);if(state==0)return false;}
    return true;
}
std::optional<int> current_map(UObject* world) {
    const auto manager=world_scoped(L"CPP_MapManager",world);
    if(manager.ambiguous||!manager.object||!is(manager.object,L"/Script/mucha.CPP_MapManager")||
       !property(manager.object,L"m_MapTransitionPointList",504,16))return {};
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/mucha.Default__CPP_BFL_Map");
    Call call(library,L"GetCurrentMapId",4,0,4,{},true);if(!call)return {};
    call.invoke(library);const auto id=call.get<int>(0);
    // Ghidra FUN_145055070 reads MapManager+0x30. Comparing the verified
    // current-world manager prevents a global BFL lookup selecting another world.
    int local_id{};std::memcpy(&local_id,reinterpret_cast<std::byte*>(manager.object)+48,4);
    return id>=0&&id==local_id?std::optional<int>(id):std::nullopt;
}
std::optional<std::vector<int>> native_group_maps(int map_id) {
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/mucha.Default__CPP_BFL_Map");
    // present-native-12.c: wrapper 144d47520 forwards InMapId to the exact
    // 145056880 used by native Quest_HasDestinationMap. No conditions are evaluated here.
    Call call(library,L"GetGroupMapIdListAtMap",24,8,16,{},true);
    if(!call||!call.parameter(L"InMapId",0,4))return {};
    call.put(0,map_id);call.invoke(library);
    return copy_array(call.get<NativeArray<int>>(8),512);
}
bool main_relation(const std::byte* object_data,int main_id) {
    const auto ids=copy_array(pod<NativeArray<int>>(object_data,32),256);
    return ids&&std::find(ids->begin(),ids->end(),main_id)!=ids->end();
}
bool point_matches(ObjectKey key,int map_id,std::span<const FilteredPoint> points) {
    return std::any_of(points.begin(),points.end(),[&](const auto& point){
        return point.map_kind==0&&point.map_id==map_id&&point.object_target&&point.object==key;
    });
}
std::optional<bool> story_exit(UObject* actor,UObject* world) {
    if(is(actor,L"/Script/mucha.CPP_MapTransitionTrigger"))return true;
    if(!is(actor,L"/Script/mucha.CPP_MapLandmarkPoint"))return false;
    Call linked(actor,L"GetTransitionTrigger",8,0,8);if(!linked)return {};
    linked.invoke(actor);auto* transition=linked.get<UObject*>(0);
    if(!live(transition)||actor_world(transition)!=world||!is(transition,L"/Script/mucha.CPP_MapTransitionTrigger"))return {};
    return true;
}
const char* target_label(ObjectKey key,bool exit) {
    return exit?"Story passage":key.type==1?"Story contact":"Story destination";
}
std::vector<StoryDestination> area_destinations(UObject* pawn,UObject* world,int map_id,int main_id,
        std::span<const FilteredPoint> points,std::span<const GoalArea> areas,const std::string& area) {
    const auto field=world_scoped(L"CPP_FieldObjectSubsystem",world);
    if(field.ambiguous||!field.object)throw std::runtime_error("Ambiguous or absent native field registry");
    Call list(field.object,L"GetObjectIdList",16,0,16);if(!list)throw std::runtime_error("Unsupported native field registry");
    list.invoke(field.object);
    const auto ids=copy_array(list.get<NativeArray<int>>(0),1024);
    if(!ids||!unique_capture_ids(*ids,1024))throw std::runtime_error("Invalid or ambiguous native field registry");
    const auto units=copied_array<UObject*>(pawn,L"m_InteractMapUnitList",2584,512);
    const auto gimmicks=copied_array<UObject*>(pawn,L"m_pInteractMapGimmickList",2600,512);
    std::vector<StoryCandidate> captured;
    std::string forward,withheld;
    for(const int id:*ids) {
        Call object(field.object,L"FindObjectData",104,96,1,{L"OutData",8,88,L"/Script/mucha.FieldObjectData",
            L"/Script/mucha.CPP_FieldObjectSubsystem:FindObjectData"});
        if(!object)throw std::runtime_error("Unsupported native field data");
        object.put(0,id);object.invoke(field.object);if(!object.get<unsigned char>(96))continue;
        const auto* data=object.data()+8;
        if(!main_relation(data,main_id))continue;
        const auto key=pod<ObjectKey>(data,8);
        const int related_map=pod<int>(data,24);
        const bool direct=point_matches(key,map_id,points);
        // Exits related only through the destination's map group must advance the
        // objective; the rest remain ordinary Exits.
        if(!direct) {
            const bool advances=story_exit_advances(map_id,related_map,points,areas);
            (advances?forward:withheld)+=std::to_string(id)+">"+std::to_string(related_map)+",";
            if(!advances)continue;
        }
        StoryCandidate candidate;candidate.key=key;candidate.direct=direct;
        candidate.destination.id="main-quest:"+std::to_string(main_id)+":field:"+std::to_string(id);
        candidate.destination.label=tr(target_label(key,false));candidate.destination.area=area;
        candidate.destination.disclosed=true;candidate.destination.local=true;
        // Authorization is complete. An individual actor read cannot revoke
        // independently verified candidates or remove this generic selection.
        auto current=capture_story_candidate(std::move(candidate),[&](StoryDestination& destination) {
            Call actor_call(field.object,L"GetActorAtId",16,8,8);
            if(!actor_call)throw std::runtime_error("The story target's actor could not be read safely.");
            actor_call.put(0,id);actor_call.invoke(field.object);
            auto* actor=actor_call.get<UObject*>(8);
            if(!live(actor))throw std::runtime_error("The story target's current actor is unavailable.");
            if(actor_world(actor)!=world)throw std::runtime_error("The story target could not be verified in the current area.");
            // Direct current quest points remain generic destinations. Only a
            // related-only exit needs native transition availability as well.
            const auto exit=direct?std::optional<bool>(false):story_exit(actor,world);
            if(!exit)throw std::runtime_error("The story exit's current transition could not be read safely.");
            if(!direct&&!*exit)return false;
            destination.exit=*exit;destination.label=tr(target_label(key,*exit));
            if(direct&&key.type==1) {
                const auto name=field_npc_label(actor);
                if(!name.empty()&&name!="Local resident")destination.label=tr(name);
            }
            if(direct&&main_id==1003&&map_id==2070)
                if(const auto* label=chapter_three::olderion_label(key.type,key.id))destination.label=tr(label);
            if(direct&&main_id==1002&&map_id==2061&&key.type==1&&(key.id==1||key.id==3))
                destination.label=tr("Lid");
            if(direct&&main_id==1003&&map_id==1000&&key.type==6&&key.id==93)
                destination.label=tr("Harbor for Zoldaad");
            if(direct&&main_id==1004)
                if(const auto* label=chapter_four::target_label(map_id,key.type,key.id))destination.label=tr(label);
            if(!story_actor_available(pawn,actor,*exit)) {
                const auto reason=area_actor_unavailable_reason();
                if(!reason.empty())throw std::runtime_error(reason);
                return false; // A positively inactive native actor is omitted.
            }
            std::optional<Vec3> position;
            if(*exit) {
                auto route=capture_story_exit_route(pawn,actor);
                if(!route)throw std::runtime_error("The story exit's activation area could not be verified.");
                if(!story_exit_advances(map_id,route->destination_map,points,areas))return false;
                destination.exit_source=path(actor);
                destination.exit_route_key=route->route_key;
                destination.id+="|gate:"+utf8(path(route->actor))+route->route_key;
                actor=route->actor;position=route->position;destination.label=std::move(route->label);
            } else position=tracked_actor_position(actor);
            if(!position||!std::isfinite(position->x)||!std::isfinite(position->y)||!std::isfinite(position->z))
                throw std::runtime_error("The story target's current position could not be read safely.");
            destination.position=*position;destination.actor_available=true;
            destination.actor_id=utf8(path(actor));destination.actor_type=actor->GetClassPrivate()->GetNamePrivate().ToString();
            destination.interactable=(units&&std::find(units->begin(),units->end(),actor)!=units->end())||
                (gimmicks&&std::find(gimmicks->begin(),gimmicks->end(),actor)!=gimmicks->end());
            return true;
        });
        if(current)captured.push_back(std::move(*current));
    }
    static std::string previous;
    auto diagnostic="map="+std::to_string(map_id)+" forward="+forward+" withheld="+withheld;
    if(diagnostic!=previous){previous=std::move(diagnostic);log("STORY exits "+previous);}
    return finalize_story_candidates(std::move(captured));
}
std::optional<int> collision_response(UObject* component,unsigned char channel) {
    Call call(component,L"GetCollisionResponseToChannel",2,1,1);
    if(!call||!call.parameter(L"Channel",0,1))return {};
    call.put(0,channel);call.invoke(component);return call.get<unsigned char>(1);
}
std::optional<bool> opening_overlap(UObject* pawn,UObject* actor) {
    auto* box=object_getter(actor,L"GetRootBoxComponent");
    auto* capsule=object_value(pawn,L"CapsuleComponent",824);
    if(!box||!is(box,L"/Script/Engine.BoxComponent")||!capsule||!is(capsule,L"/Script/Engine.CapsuleComponent")||
       object_value(actor,L"m_pRootBoxComponent",760)!=box||
       object_getter(box,L"GetOwner")!=actor||object_getter(capsule,L"GetOwner")!=pawn)return {};
    const auto actor_enabled=bool_getter(actor,L"GetActorEnableCollision"),player_enabled=bool_getter(pawn,L"GetActorEnableCollision");
    const auto trigger_mode=getter<unsigned char>(box,L"GetCollisionEnabled"),player_mode=getter<unsigned char>(capsule,L"GetCollisionEnabled");
    const auto trigger_generates=bool_getter(box,L"GetGenerateOverlapEvents"),player_generates=bool_getter(capsule,L"GetGenerateOverlapEvents");
    const auto trigger_channel=getter<unsigned char>(box,L"GetCollisionObjectType"),player_channel=getter<unsigned char>(capsule,L"GetCollisionObjectType");
    if(!actor_enabled||!player_enabled||!trigger_mode||!player_mode||!trigger_generates||!player_generates||
       !trigger_channel||*trigger_channel>=32||!player_channel||*player_channel>=32||*trigger_mode>5||*player_mode>5)return {};
    const auto trigger_response=collision_response(box,*player_channel),player_response=collision_response(capsule,*trigger_channel);
    if(!trigger_response||!player_response||*trigger_response>2||*player_response>2)return {};
    return enabled_overlap({true,*actor_enabled&&*player_enabled,*trigger_generates,*player_generates,
        *trigger_mode,*player_mode,*trigger_response,*player_response});
}
std::optional<std::string> short_native_string(const std::byte* source) {
    const auto value=pod<NativeArray<wchar_t>>(source,0);
    if(!valid_native_array(value.data,value.num,value.max,128)||value.num<=0||value.data[value.num-1]!=L'\0')return {};
    return utf8(std::wstring(value.data,value.data+value.num-1));
}
std::string matching_trigger_event(UObject* actor,Call& events) {
    // The native overlap handler uses this same selector. It only reads which
    // authored condition currently matches and does not start the event.
    Call matched(actor,L"FindMatchEventByEventList",64,56,1,{L"OutResult",16,40,L"/Script/mucha.TalkEventFindResult",
        L"/Script/mucha.CPP_MapGimmick:FindMatchEventByEventList"});
    if(!matched)throw std::runtime_error("Story FindMatchEventByEventList rejected: "+matched.rejection());
    if(!matched.copy_event_array_input(events.data()))throw std::runtime_error("Story native event input array is unsupported");
    matched.invoke(actor);
    if(!matched.get<unsigned char>(56))return {};
    auto name=matched.get<FName>(16);
    return utf8(name.ToString());
}
std::vector<UObject*> registered_story_gimmicks(UObject* world) {
    const auto manager=world_scoped(L"CPP_MapManager",world);
    auto* registry=object_value(manager.object,L"m_MapGimmickManager",448);
    if(manager.ambiguous||!registry||!is(registry,L"/Script/mucha.CPP_MapGimmickManager"))
        throw std::runtime_error("Story gimmick registry unavailable");
    auto ordinary=copied_array<UObject*>(registry,L"m_MapGimmickList",40,1024);
    const auto attached=copied_array<UObject*>(registry,L"m_AttachedGimmickList",56,1024);
    if(!ordinary||!attached||ordinary->size()+attached->size()>1024)throw std::runtime_error("Story registry shape unavailable");
    ordinary->insert(ordinary->end(),attached->begin(),attached->end());
    return std::move(*ordinary);
}
std::optional<int> main_progress() {
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/mucha.Default__CPP_BFL_Progress");
    Call call(library,L"GetMainProgress",4,0,4,{},true);
    if(!call)return {};
    call.invoke(library);return call.get<int>(0);
}
UObject* registered_gimmick(std::span<UObject* const> registry,UObject* world,int id,const wchar_t* type) {
    UObject* selected=nullptr;
    for(auto* actor:registry) {
        if(!live(actor)||actor_world(actor)!=world||!is(actor,type)||read<int>(actor,L"m_UniqueId",808)!=id)continue;
        if(selected&&selected!=actor)return nullptr;
        selected=actor;
    }
    return selected;
}
// Authored English instructions and labels are translated here, at the
// assignment boundary. Keys, epochs and IDs stay English. The *_text forms
// take text already assembled with trf() from a placeholder key.
void story_step_text(StorySnapshot& story,std::string_view key,std::string localized) {
    story.destination_epoch+="|step:";
    story.destination_epoch+=key;
    story.next_step=std::move(localized);
}
void story_step(StorySnapshot& story,std::string_view key,std::string_view english) {
    story_step_text(story,key,tr(english));
}
void story_step_actor_text(StorySnapshot& story,UObject* actor,Vec3 point,std::string key,std::string localized,
        bool overlap=false) {
    StoryDestination destination;
    destination.actor_id=utf8(path(actor));
    destination.actor_type=actor->GetClassPrivate()->GetNamePrivate().ToString();
    destination.id=story.objective_id+"|step:"+std::move(key);
    destination.label=std::move(localized);
    destination.position=point;
    destination.disclosed=destination.local=destination.actor_available=true;
    destination.event_trigger=overlap;
    destination.interactable=!overlap;
    story.destinations.push_back(std::move(destination));
}
void story_step_actor(StorySnapshot& story,UObject* actor,Vec3 point,std::string key,std::string_view english,
        bool overlap=false) {
    story_step_actor_text(story,actor,point,std::move(key),tr(english),overlap);
}
bool wind_story_leg(StorySnapshot& story,UObject* pawn,UObject* world,std::span<UObject* const> registry,
        const chapter_two::LiftLeg& leg) {
    using namespace chapter_two;
    auto* lift=registered_gimmick(registry,world,leg.lift,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact");
    const auto location=getter<Vec3>(lift,L"K2_GetActorLocation");
    const auto state=getter<unsigned char>(lift,L"GetGimmickCondition");
    const bool docked=location&&near_dock(*location,leg.dock);
    const bool away=location&&wind_at_other_dock(leg.lift,*location);
    const auto boarding=docked?capture_interaction_box(lift,pawn):std::nullopt;
    auto* call=away&&leg.call>=0?registered_gimmick(registry,world,leg.call,L"/Script/mucha.CPP_MapGimmick_CallSwitch"):nullptr;
    const auto call_box=call&&object_value(call,L"m_pElevator",1016)==lift&&
        read<int>(call,L"m_CallFloorIndex",1024)==leg.floor&&story_actor_available(pawn,call,false)?
            capture_interaction_box(call,pawn):std::nullopt;
    const auto step=lift_step(leg,{location.has_value(),state==0,docked,boarding.has_value(),call_box.has_value(),
        location&&!docked&&!away&&leg.call>=0});
    auto key="wind-"+std::to_string(leg.lift);
    const auto name=tr(leg.name);
    switch(step) {
    case LiftStep::Call:
        key+="-call";
        story_step_text(story,key,trf("Use the call pedestal to bring the {0} here.",{name}));
        story_step_actor_text(story,call,call_box->center,key,trf("Call pedestal for the {0}",{name}));
        break;
    case LiftStep::Board:
        key+="-board";
        story_step_text(story,key,trf("Board the {0} and interact to ride {1}.",{name,tr(leg.ride)}));
        story_step_actor_text(story,lift,boarding->center,key,name);
        break;
    case LiftStep::Wait:
        story_step_text(story,key+"-wait",trf("Wait for the {0} to stop at your landing.",{name}));
        story.unavailable_reason=tr("The lift is not ready to board.");
        break;
    case LiftStep::Unavailable:
        story_step_text(story,key+"-unavailable",trf("The {0} is not available at this landing.",{name}));
        story.unavailable_reason=tr("A route to the lift or its call pedestal could not be verified.");
        break;
    }
    return true;
}
bool story_event_matches(UObject* actor,std::string_view event) {
    Call events(actor,L"GetEventList",16,0,16);
    if(!events||!events.event_array_return())return false;
    events.invoke(actor);
    return matching_trigger_event(actor,events)==event;
}
std::optional<Vec3> active_story_trigger(UObject* pawn,UObject* actor,std::string_view event) {
    if(getter<unsigned char>(actor,L"GetGimmickCondition")!=0||opening_overlap(pawn,actor)!=true||
        !story_event_matches(actor,event))return {};
    const auto box=capture_activation_box(actor);
    return box?std::optional<Vec3>(box->center):std::nullopt;
}
bool harbor_story_leg(StorySnapshot& story,UObject* pawn,UObject* world) {
    const auto registry=registered_story_gimmicks(world);
    auto* actor=registered_gimmick(registry,world,191,L"/Script/mucha.CPP_MapGimmick_InteractEventTrigger");
    if(read<unsigned char>(actor,L"m_MapInteractType",833)!=4||
        getter<unsigned char>(actor,L"GetGimmickCondition")!=0||!story_actor_available(pawn,actor)||
        opening_overlap(pawn,actor)!=true||!story_event_matches(actor,"Gim_EV_ShipMovement_03_02_Choice"))return false;
    const auto point=getter<Vec3>(object_getter(actor,L"GetRootBoxComponent"),L"K2_GetComponentLocation");
    if(!point||!std::isfinite(point->x)||!std::isfinite(point->y)||!std::isfinite(point->z))return false;
    story_step(story,"olderion-harbor","Check the harbor to open the destination choices.");
    story_step_actor(story,actor,*point,"olderion-harbor","Harbor for Zoldaad");
    return true;
}
std::optional<Vec3> story_ladder_approach(UObject* pawn,UObject* actor,bool top) {
    auto* box=object_value(actor,top?L"m_pTopBoxComponent":L"m_pBottomBoxComponent",top?992:1000);
    if(!is(box,L"/Script/Engine.BoxComponent")||object_getter(box,L"GetOwner")!=actor||
        bool_getter(actor,L"GetActorEnableCollision")!=true||!story_actor_available(pawn,actor))return {};
    const auto mode=getter<unsigned char>(box,L"GetCollisionEnabled");
    if(!mode||(*mode!=1&&*mode!=3&&*mode!=5)||bool_getter(box,L"GetGenerateOverlapEvents")!=true)return {};
    const auto point=getter<Vec3>(box,L"K2_GetComponentLocation");
    if(!point||!std::isfinite(point->x)||!std::isfinite(point->y)||!std::isfinite(point->z))return {};
    if(!top)return point;
    // The top overlap box is on the edge. The native ladder landing is on the
    // walking surface, where the player can take over and start climbing.
    auto* landing=object_value(actor,L"m_TopLandingPoint",984);
    if(!is(landing,L"/Script/Engine.SceneComponent")||object_getter(landing,L"GetOwner")!=actor)return {};
    const auto approach=getter<Vec3>(landing,L"K2_GetComponentLocation");
    return approach&&std::hypot(approach->x-point->x,approach->y-point->y)<=250&&
        std::abs(approach->z-point->z)<=120?approach:std::nullopt;
}
bool story_gate(StorySnapshot& story,UObject* pawn,UObject* world,UObject* manager,int id,int map,int point,
        const std::string& key,const char* label,const char* instruction,bool event_only=false) {
    auto* storage=object_value(manager,L"m_pMapTransitionTriggerStorage",568);
    if(!is(storage,L"/Script/mucha.CPP_MapTransitionTriggerStorage"))return false;
    const auto gates=copied_array<UObject*>(storage,L"m_TransitionTriggerList",40,256);
    if(!gates)return false;
    UObject* gate=nullptr;
    for(auto* actor:*gates) {
        if(!live(actor)||actor_world(actor)!=world||!is(actor,L"/Script/mucha.CPP_MapTransitionTrigger")||
           read<int>(actor,L"m_UniqueId",1040)!=id)continue;
        if(gate&&gate!=actor)return false;
        gate=actor;
    }
    const auto route=capture_story_exit_route(pawn,gate);
    if(!route||route->actor!=gate||route->event_only!=event_only||route->destination_map!=map||
        route->destination_point!=point)return false;
    story_step(story,key,instruction);
    story_step_actor(story,gate,route->position,key,label,true);
    auto& destination=story.destinations.back();
    destination.exit=true;
    destination.event_trigger=false;
    destination.exit_source=path(gate);
    destination.exit_route_key=route->route_key;
    return true;
}
std::optional<double> shrine_floor_height(UObject* pawn,UObject* floor,Vec3 anchor) {
    if(!story_actor_available(pawn,floor))return {};
    auto* root=object_value(floor,L"m_pRootBoxComponent",760);
    auto* mesh=object_value(floor,L"m_pStaticMeshComponent",1144);
    if(!is(root,L"/Script/Engine.BoxComponent")||object_value(floor,L"RootComponent",440)!=root||
        object_getter(root,L"GetOwner")!=floor||!is(mesh,L"/Script/Engine.StaticMeshComponent")||
        object_getter(mesh,L"GetOwner")!=floor||bool_getter(mesh,L"IsVisible")!=true)return {};
    const auto position=tracked_actor_position(floor);
    if(!position||std::hypot(position->x-anchor.x,position->y-anchor.y)>60||!std::isfinite(position->z))return {};
    return position->z;
}
std::wstring shrine_crate_path(int suffix) {
    return L"/Game/Map/Dng/05Old/05Old_44/GD/Dng_05Old_44_01_GD.Dng_05Old_44_01_GD:"
        L"PersistentLevel.BP_MapGimmick_PushObject_WoodenBox_002_C_"+std::to_wstring(suffix);
}
std::optional<chapter_three::ShrineBridgeLeg> shrine_bridge_at_goal(UObject* target,Vec3 goal) {
    using namespace chapter_three;
    if(!live(target))return {};
    const auto identity=path(target);
    for(const auto bridge:{ShrineBridge::CrateRoom,ShrineBridge::BasinLong,ShrineBridge::BasinShort}) {
        for(bool returning:{false,true}) {
            const ShrineBridgeLeg leg{bridge,returning};
            const auto bank=shrine_bridge_goal(leg);
            if(std::hypot(goal.x-bank.x,goal.y-bank.y,goal.z-bank.z)<=.01&&
                identity==shrine_crate_path(shrine_bridge_marker(leg).suffix))return leg;
        }
    }
    return {};
}
bool shrine_floor_lists_crate(UObject* floor,UObject* actor) {
    auto* function=floor->GetFunctionByNameInChain(L"IsFlootingWaterActor");
    if(!function||path(function)!=L"/Script/mucha.CPP_MapGimmick_Floor:IsFlootingWaterActor")return false;
    Call contains(floor,L"IsFlootingWaterActor",16,8,1);
    if(!contains||!contains.parameter(L"pInActor",0,8,L"ObjectProperty")||!contains.return_kind(L"BoolProperty"))return false;
    contains.put(0,actor);
    contains.invoke(floor);
    return contains.get<unsigned char>(8)==1;
}
std::optional<chapter_three::ShrineCrateBounds> shrine_crate_bounds(UObject* mesh) {
    constexpr auto library_path=L"/Script/Engine.KismetSystemLibrary";
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/Engine.Default__KismetSystemLibrary");
    if(!library||path(library->GetClassPrivate())!=library_path||
        !(static_cast<std::uint32_t>(library->GetObjectFlags())&0x10u)||
        (static_cast<std::uint32_t>(library->GetObjectFlags())&dying_flags))return {};
    auto* function=library->GetFunctionByNameInChain(L"GetComponentBounds");
    if(!function||path(function)!=L"/Script/Engine.KismetSystemLibrary:GetComponentBounds"||
        !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400))||
        !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x2000))||
        function->GetPropertiesSize()!=64||function->GetParmsSize()<60||function->GetParmsSize()>64||
        !struct_fields(function,{{L"Component",0,8,L"ObjectProperty"},{L"Origin",8,24,L"StructProperty"},
            {L"BoxExtent",32,24,L"StructProperty"},{L"SphereRadius",56,4,L"FloatProperty"}}))return {};
    alignas(16) std::array<std::byte,64> parameters{};
    std::memcpy(parameters.data(),&mesh,sizeof(mesh));
    library->ProcessEvent(function,parameters.data());
    return chapter_three::ShrineCrateBounds{pod<Vec3>(parameters.data(),8),pod<Vec3>(parameters.data(),32),true};
}
std::optional<StoryShrineBridgeRoute> checked_shrine_bridge(UObject* pawn,UObject* world,UObject* floor,
        chapter_three::ShrineBridgeLeg leg,UObject*& marker) {
    using namespace chapter_three;
    const auto& bridge=shrine_bridge_spec(leg.bridge);
    const auto note=[&](int crate,const char* result) {
        static std::string previous;
        auto current="floor="+std::to_string(bridge.floor)+" crate="+std::to_string(crate)+" "+result;
        if(previous!=current){previous=std::move(current);log("STORY floating bridge "+previous);}
        return std::nullopt;
    };
    auto* capsule=object_value(pawn,L"CapsuleComponent",824);
    const auto channel=getter<unsigned char>(capsule,L"GetCollisionObjectType");
    if(!floor||!is(capsule,L"/Script/Engine.CapsuleComponent")||object_getter(capsule,L"GetOwner")!=pawn||
        !channel||*channel>=32)return note(-1,"current floor or player capsule unavailable");
    std::array<ShrineCrateBounds,6> observed{};
    UObject* end=nullptr;
    for(std::size_t i=0;i<bridge.crates.size();++i) {
        const auto& anchor=bridge.crates[i];
        const auto identity=shrine_crate_path(anchor.suffix);
        auto* actor=UObjectGlobals::StaticFindObject(identity.c_str());
        if(!live(actor)||path(actor)!=identity||actor_world(actor)!=world||!story_actor_available(pawn,actor))
            return note(anchor.suffix,"active crate unavailable");
        if(bool_getter(actor,L"GetActorEnableCollision")!=true||!shrine_floor_lists_crate(floor,actor))
            return note(anchor.suffix,"crate is outside the active collision or buoyancy state");
        UObject* mesh=nullptr;
        if(anchor.movable_id>=0&&is(actor,L"/Script/mucha.CPP_MapGimmick_PushObject")&&
            read<int>(actor,L"m_UniqueId",808)==anchor.movable_id)
            mesh=object_value(actor,L"m_pMeshComponent",984);
        else if(anchor.movable_id<0&&is(actor,L"/Script/Engine.StaticMeshActor"))
            mesh=object_value(actor,L"StaticMeshComponent",680);
        if(!is(mesh,L"/Script/Engine.StaticMeshComponent")||object_getter(mesh,L"GetOwner")!=actor||
            bool_getter(mesh,L"IsVisible")!=true)return note(anchor.suffix,"visible crate mesh unavailable");
        auto* asset=object_value(mesh,L"StaticMesh",1400);
        if(!is(asset,L"/Script/Engine.StaticMesh")||
            path(asset)!=L"/Game/Env/Gimmick/crate002/Mesh/SM_Env_Gimmick_crate002.SM_Env_Gimmick_crate002")
            return note(anchor.suffix,"crate mesh asset changed");
        // The small attached boxes sit near the crate pivots. The crate
        // meshes have the authored blocking hulls that support walking.
        const auto mode=getter<unsigned char>(mesh,L"GetCollisionEnabled");
        const auto support_channel=getter<unsigned char>(mesh,L"GetCollisionObjectType");
        if(!mode||(*mode!=1&&*mode!=3&&*mode!=5)||!support_channel||*support_channel>=32||
            collision_response(mesh,*channel)!=2||collision_response(capsule,*support_channel)!=2)
            return note(anchor.suffix,"crate does not block the player capsule");
        const auto bounds=shrine_crate_bounds(mesh);
        if(!bounds)return note(anchor.suffix,"crate support bounds unavailable");
        observed[i]=*bounds;
        if(anchor.suffix==shrine_bridge_marker(leg).suffix)end=actor;
    }
    const auto crates=std::span<const ShrineCrateBounds>(observed.data(),bridge.crates.size());
    if(!end||!shrine_crate_supports(bridge,crates))return note(-1,"crate row is not at a continuous walking height");

    StoryShrineBridgeRoute result{bridge.bounds,{}};
    auto& turns=result.turns;
    if(leg.bridge==ShrineBridge::BasinShort&&!leg.returning)
        turns.insert(turns.end(),shrine_basin_walkway.begin(),shrine_basin_walkway.end());
    else if(leg.bridge==ShrineBridge::BasinLong&&leg.returning)
        turns.insert(turns.end(),shrine_basin_walkway.rbegin(),shrine_basin_walkway.rend());
    else {
        if(leg.bridge==ShrineBridge::CrateRoom)turns.push_back(leg.returning?Vec3{-40,60886,102.15}:Vec3{-36,59825,102.15});
        turns.push_back(leg.returning?bridge.far_bank:bridge.near_bank);
    }
    for(std::size_t i=0;i<crates.size();++i) {
        const auto& crate=crates[leg.returning?crates.size()-1-i:i];
        turns.push_back({crate.center.x,crate.center.y,crate.center.z+crate.half.z+2.15});
    }
    turns.push_back(shrine_bridge_goal(leg));
    marker=end;
    return result;
}
bool story_blocker_open(UObject* world,std::span<UObject* const> registry,int id) {
    auto* blocker=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_BlockingBox");
    const auto collision=bool_getter(blocker,L"GetActorEnableCollision");
    if(!collision)return false;
    if(!*collision)return true;
    auto* box=object_value(blocker,L"m_pRootBoxComponent",760);
    return is(box,L"/Script/Engine.BoxComponent")&&object_getter(box,L"GetOwner")==blocker&&
        getter<unsigned char>(box,L"GetCollisionEnabled")==0;
}
bool factory_maze_connected(UObject* world,std::span<UObject* const> registry) {
    struct Turn {int id;const wchar_t* suffix;double yaw;};
    constexpr std::array turns{Turn{39,L"SequentialRotationBridge_002_C_6",180.},
        Turn{44,L"SequentialRotationBridge_001_C_3",90.},
        Turn{41,L"SequentialRotationBridge_001_C_1",0.},
        Turn{40,L"SequentialRotationBridge_001_C_0",-90.}};
    for(const auto& turn:turns) {
        auto* actor=registered_gimmick(registry,world,turn.id,L"/Script/mucha.CPP_MapGimmick");
        if(!actor||!path(actor).ends_with(turn.suffix))return false;
        auto* property=actor->GetPropertyByNameInChain(L"m_pDrawBridgeFulcrum");
        auto* field_class=property?property->field_class():nullptr;
        if(!field_class||field_class->GetName()!=L"ObjectProperty"||property->GetSize()!=sizeof(UObject*))return false;
        const auto offset=property->GetOffset_Internal();
        if(offset<0||offset+static_cast<int>(sizeof(UObject*))>actor->GetClassPrivate()->GetPropertiesSize())return false;
        auto* pivot=object_value(actor,L"m_pDrawBridgeFulcrum",offset);
        if(!is(pivot,L"/Script/Engine.SceneComponent")||object_getter(pivot,L"GetOwner")!=actor)return false;
        const auto angle=getter<Vec3>(pivot,L"K2_GetComponentRotation");
        if(!angle||!(std::abs(angle->x)<1.&&std::abs(angle->z)<1.&&
           std::abs(std::remainder(angle->y-turn.yaw,360.))<1.))return false;
    }
    return true;
}
bool story_board_raft(StorySnapshot& story,UObject* pawn,UObject* world,
        std::span<UObject* const> registry,int id,const char* instruction) {
    auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_Object_Raft");
    const auto box=capture_interaction_box(actor,pawn);
    if(!box||!story_actor_available(pawn,actor))return false;
    const auto key="board-raft-"+std::to_string(id);
    story_step(story,key,instruction);
    story_step_actor(story,actor,box->center,key,"Boat");
    return true;
}
bool capture_shrine_crossing(StorySnapshot& story,UObject* pawn,UObject* world,int area,Vec3 position) {
    using namespace chapter_three;
    const auto registry=registered_story_gimmicks(world);
    const auto height=shrine_floor_height(pawn,
        registered_gimmick(registry,world,75,L"/Script/mucha.CPP_MapGimmick_Floor"),{-1250,32150,0});
    const auto hall=height?shrine_hall_water(*height):HallWater::Unknown;
    if(hall==HallWater::Unknown)return false;
    const auto crossing=[&](StoryCrossing leg,const char* key,const char* instruction) {
        story_step(story,key,instruction);
        story.crossing=leg;
        return true;
    };
    if((area==7||area==8||area==17)&&position.y>=41400&&position.y<=44300) {
        if(hall==HallWater::High)return crossing(StoryCrossing::ShrinePedestalPassage,
            "shrine-row-passage","Row to the passage leading to the large crystal pedestal.");
        return crossing(StoryCrossing::ShrineHomeLanding,"shrine-row-home","Row to the landing by the way back to the central hall.");
    }
    if(area==9&&position.y>=49900&&position.y<=52450) {
        if(position.y>=51800)return crossing(StoryCrossing::ShrineThirdChannelReturn,
            "shrine-row-back","Row back through this channel to the previous room.");
        if(position.y>=51000)return crossing(StoryCrossing::ShrineSecondChannelReturn,
            "shrine-row-back","Row back through this channel to the previous room.");
        if(hall==HallWater::High)return crossing(StoryCrossing::ShrinePedestalLanding,
            "shrine-row-pedestal","Row to the landing by the large crystal pedestal.");
        return crossing(StoryCrossing::ShrineReturnPassage,"shrine-row-back","Row back through the passage to the first boat room.");
    }
    if((area==13||area==15)&&position.x>=-2800&&position.x<=-700&&position.y>=11450&&position.y<=12700) {
        const auto basin=shrine_floor_height(pawn,
            registered_gimmick(registry,world,73,L"/Script/mucha.CPP_MapGimmick_Floor"),{-158,13140,0});
        if(!basin||shrine_room_water(73,*basin)!=RoomWater::Low)return false;
        if(hall==HallWater::SecondDrop)return crossing(StoryCrossing::ShrineTowerLanding,
            "shrine-row-tower","Row to the tower in the middle of the basin and climb its ladder.");
        if(hall==HallWater::Drained)return crossing(StoryCrossing::ShrineWestLanding,
            "shrine-row-west","Row back to the landing by the water pedestal.");
    }
    return false;
}
bool capture_water_shrine_land(StorySnapshot& story,UObject* pawn,UObject* world,UObject* manager,int phase,int area,Vec3 position) {
    using namespace chapter_three;
    const auto gate=[&](const GateLeg& leg) {
        return story_gate(story,pawn,world,manager,leg.id,leg.destination,leg.point,leg.key,leg.label,
            leg.instruction,leg.event_only);
    };
    if(const auto leg=water_shrine_gate(phase,area,position,HallWater::Unknown,RoomWater::Unknown))return gate(*leg);
    if(phase!=3180)return false;

    const auto registry=registered_story_gimmicks(world);
    const auto floor=[&](int id) {
        return registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_Floor");
    };
    auto* hall_floor=floor(75);
    const auto hall_height=shrine_floor_height(pawn,hall_floor,{-1250,32150,0});
    const auto hall=hall_height?shrine_hall_water(*hall_height):HallWater::Unknown;
    const bool crates=area==10||area==11||area==12;
    const bool basin=area==13||area==14||area==15;
    auto* room_floor=crates?floor(74):basin?floor(73):nullptr;
    const auto height=room_floor?shrine_floor_height(pawn,room_floor,crates?Vec3{-400,60430,0}:Vec3{-158,13140,0}):std::nullopt;
    const auto room=height?shrine_room_water(crates?74:73,*height):RoomWater::Unknown;
    static std::string previous;
    auto diagnostic="area="+std::to_string(area)+" hall="+std::to_string(static_cast<int>(hall))+
        " room="+std::to_string(static_cast<int>(room));
    if(diagnostic!=previous){previous=std::move(diagnostic);log("STORY Water Shrine "+previous);}
    if(const auto leg=water_shrine_gate(phase,area,position,hall,room))return gate(*leg);
    if(hall==HallWater::High&&(area==7||area==8||area==17)&&position.y>=41750&&position.y<=44300&&
        story_board_raft(story,pawn,world,registry,65,"Board the boat for the crossing to the large crystal pedestal."))return true;
    if(shrine_hall_lowered(hall)&&area==9&&position.x>=230&&position.y>=50150&&position.y<=50800&&
        story_board_raft(story,pawn,world,registry,66,"Board the boat and return to the first boat room."))return true;
    if(basin&&room==RoomWater::Low) {
        const bool west_landing=position.x<=-1350&&position.y>=11100&&position.y<=11650&&position.z<575;
        if(hall==HallWater::SecondDrop&&(west_landing||shrine_basin_deck(area,position)==BasinDeck::West)&&
            story_board_raft(story,pawn,world,registry,70,"Board the boat at the foot of the stairs to reach the tower."))return true;
        if(hall==HallWater::Drained&&position.z<380&&position.x>=-1150&&position.y>=11750&&position.y<=12600&&
            story_board_raft(story,pawn,world,registry,69,"Board the boat to return to the pedestal across the basin."))return true;
    }

    if(const auto ladder=shrine_ladder(area,position,hall,room)) {
        auto* actor=registered_gimmick(registry,world,ladder->id,L"/Script/mucha.CPP_MapGimmickLadder");
        const auto approach=story_ladder_approach(pawn,actor,ladder->top);
        if(!approach||((area==3||area==4)&&!story_blocker_open(world,registry,18)))return false;
        story_step(story,ladder->key,ladder->instruction);
        story_step_actor(story,actor,*approach,ladder->key,ladder->label);
        story.destinations.back().interactable=false;
        return true;
    }
    if(area==5&&hall==HallWater::Drained) {
        auto* actor=registered_gimmick(registry,world,72,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact");
        const auto dock=tracked_actor_position(actor);
        if(!dock||!chapter_two::near_dock(*dock,{-700,32150,-3507})||
            read<unsigned char>(actor,L"m_MapInteractType",833)!=4||getter<unsigned char>(actor,L"GetGimmickCondition")!=0||
            !story_actor_available(pawn,actor))return false;
        const auto approach=capture_interaction_box(actor,pawn);
        if(!approach)return false;
        story_step(story,"shrine-elevator","Step onto the four-lamp elevator and interact to ride down.");
        story_step_actor(story,actor,approach->center,"shrine-elevator","Four-lamp elevator");
        return true;
    }

    const auto pedestal=[&](int id,UObject* surface,const char* label,const char* instruction) {
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_Switch");
        if(!surface||read<unsigned char>(actor,L"m_MapInteractType",833)!=4||
            read<unsigned char>(actor,L"m_CanPressed",1112)!=1||getter<unsigned char>(actor,L"GetGimmickCondition")!=0||
            !story_actor_available(pawn,actor))return false;
        const auto links=copied_array<UObject*>(actor,L"m_SwitchGimmickList",1040,16);
        if(!links||std::find(links->begin(),links->end(),surface)==links->end())return false;
        const auto approach=capture_interaction_box(actor,pawn);
        if(!approach)return false;
        const auto key="shrine-pedestal-"+std::to_string(id);
        story_step(story,key,instruction);
        story_step_actor(story,actor,approach->center,key,label);
        return true;
    };
    if(const auto large=shrine_large_pedestal(area,position,hall))
        return pedestal(*large,hall_floor,"Large crystal pedestal","Examine the large crystal pedestal on this platform.");

    if(const auto leg=shrine_bridge_leg(area,position,hall,room)) {
        UObject* marker=nullptr;
        if(checked_shrine_bridge(pawn,world,room_floor,*leg,marker)) {
            const auto& bridge=shrine_bridge_spec(leg->bridge);
            const auto* label=leg->returning?bridge.return_label:bridge.outbound_label;
            const auto key="shrine-float-bridge-"+std::to_string(static_cast<int>(leg->bridge))+(leg->returning?"-back":"-out");
            story_step(story,key,"The water is high. Follow the floating crates to the next landing.");
            story_step_actor(story,marker,shrine_bridge_goal(*leg),key,label);
            story.destinations.back().interactable=false;
            return true;
        }
    }

    if(crates&&room!=RoomWater::Unknown) {
        auto* loose=registered_gimmick(registry,world,45,L"/Script/mucha.CPP_MapGimmick_PushObject");
        const auto crate=read<unsigned char>(loose,L"m_IsInterctToMove",1034)==0&&story_actor_available(pawn,loose)?
            tracked_actor_position(loose):std::nullopt;
        if(shrine_crate_far_bank(position)) {
            if(room==RoomWater::High) {
                story_step(story,"shrine-crates-return","The crates are floating at walkway level. Follow them back across.");
                return true;
            }
            return false;
        }
        if(room==RoomWater::Low&&hall==HallWater::FirstDrop&&crate) {
            if(const auto push=shrine_crate_push_side(*crate)) {
                constexpr auto key="shrine-align-crate";
                story_step(story,key,"Follow the stairs down and go around the loose crate. Push it toward the row to line it up.");
                story_step_actor(story,loose,*push,key,"Push side of the loose crate");
                story.destinations.back().interactable=false;
                return true;
            }
            if(shrine_crate_aligned(*crate))return pedestal(54,room_floor,"Crate room water pedestal",
                "The crate is lined up with the row. Return to the pedestal and raise the water.");
        }
        if(room==RoomWater::High&&hall==HallWater::FirstDrop&&crate&&
            shrine_crate_aligned(*crate)) {
            story_step(story,"shrine-crates-cross","The floating crates line up across the gap. Cross them to reach the far terrace.");
            return true;
        }
        const auto* instruction=room==RoomWater::Low?
            "The water is low and the lower stairs are exposed. The small pedestal changes the water level.":
            "The water is high and the crates are floating. The small pedestal changes the water level.";
        return pedestal(54,room_floor,"Crate room water pedestal",instruction);
    }

    if(basin&&room!=RoomWater::Unknown) {
        const auto deck=shrine_basin_deck(area,position);
        const bool east_lower=area==13&&position.x>=-460&&position.y>=13950;
        if(deck==BasinDeck::East||east_lower) {
            if(room==RoomWater::High&&hall==HallWater::SecondDrop) {
                story_step(story,"shrine-basin-bridge","The floating crates form a bridge between the upper walkways.");
                return true;
            }
            return pedestal(53,room_floor,"Water pedestal by the upper stairs",room==RoomWater::Low?
                "The water is low. The stairs lead to a water pedestal.":
                "The water is high. The pedestal here controls the water by the stairs.");
        }
        const bool west_stairs=area==13&&position.x<=-1450&&position.y>=10950&&position.y<=11600;
        if(deck==BasinDeck::West||(west_stairs&&hall==HallWater::Drained)) {
            if(room==RoomWater::Low&&hall==HallWater::SecondDrop) {
                story_step(story,"shrine-basin-dock-stairs","The water is low. The stairs beside the pedestal lead down to the boat dock.");
                return true;
            }
            if(room==RoomWater::High&&hall==HallWater::Drained) {
                story_step(story,"shrine-basin-return-bridge","The floating crates form a bridge back to the opposite walkway.");
                return true;
            }
            return pedestal(52,room_floor,"Water pedestal by the dock stairs",room==RoomWater::High?
                "The water is high. This pedestal controls the water above the dock stairs.":
                "The water is low. The pedestal raises the floating crates toward the upper walkway.");
        }
    }
    return false;
}
std::string nearby_mobliz_marking(UObject* pawn,UObject* world,int area,Vec3 player) {
    struct Marking {const wchar_t* actor;Vec3 position;int area,number;};
    // Painted meshes and digits verified separately against the cooked map and
    // the live view. Reveal one only on its own deck and within viewing range.
    constexpr Marking markings[]{
        {L"StaticMeshActor_553",{4200,12910,801},1,3},
        {L"StaticMeshActor_571",{2200,16110,1601},2,8},
        {L"StaticMeshActor_567",{290,12600,1601},2,5}};
    constexpr auto prefix=L"/Game/Map/Dng/04Dir/04Dir_41/BG/Dng_04Dir_41_BG.Dng_04Dir_41_BG:PersistentLevel.";
    for(const auto& marking:markings) {
        if(area!=marking.area||std::abs(player.z-marking.position.z)>180||
           std::hypot(player.x-marking.position.x,player.y-marking.position.y)>650)continue;
        const auto identity=std::wstring(prefix)+marking.actor;
        auto* actor=UObjectGlobals::StaticFindObject(identity.c_str());
        if(!live(actor)||actor_world(actor)!=world||path(actor)!=identity||!is(actor,L"/Script/Engine.StaticMeshActor")||
           !story_actor_available(pawn,actor,false))continue;
        const auto observed=getter<Vec3>(actor,L"K2_GetActorLocation");
        if(observed&&std::hypot(observed->x-marking.position.x,observed->y-marking.position.y)<=40&&
           std::abs(observed->z-marking.position.z)<=35)return trf("Painted floor number: {0}.",{std::to_string(marking.number)});
    }
    return {};
}
std::optional<bool> story_progress_flag(int index) {
    auto* lib=UObjectGlobals::StaticFindObject(L"/Script/mucha.Default__CPP_BFL_SaveDataProgress");
    // The five consumed parameter bytes live in an eight-byte native allocation.
    Call read_flag(lib,L"GetFlag",8,4,1,{},true);
    if(!read_flag||!read_flag.parameter(L"InIndex",0,4,L"IntProperty")||!read_flag.return_kind(L"BoolProperty"))return {};
    read_flag.put(0,index);
    read_flag.invoke(lib);
    const auto flag=read_flag.get<unsigned char>(4);
    return flag<=1?std::optional<bool>(flag==1):std::nullopt;
}
std::optional<Vec3> mobliz_cargo_trigger(UObject* actor) {
    // This authored encounter is a separate kind of trigger from the talk
    // scenes. Its active condition is the two witnessed progress flags;
    // verify the exact live volume that starts the encounter on entry.
    const auto unavailable=[](std::string why)->std::optional<Vec3> {
        static NativeProblemDeduplicator reported;
        if(reported.accept(why))log("STORY cargo approach: "+why);
        return {};
    };
    if(!actor)return unavailable("trigger registry member unavailable");
    if(path(actor)!=chapter_two::mobliz_cargo_actor_path)
        return unavailable("trigger identity differs: "+utf8(path(actor)));
    if(getter<unsigned char>(actor,L"GetGimmickCondition")!=0||bool_getter(actor,L"GetActorEnableCollision")!=true)
        return unavailable("trigger inactive");
    auto* root=object_value(actor,L"m_pRootBoxComponent",760);
    if(!is(root,L"/Script/Engine.BoxComponent")||object_getter(root,L"GetOwner")!=actor||
       bool_getter(root,L"GetGenerateOverlapEvents")!=true||getter<unsigned char>(root,L"GetCollisionEnabled")!=1)
        return unavailable("trigger overlap component unavailable");
    const auto box=capture_activation_box(actor);
    if(!box)return unavailable(object_value(actor,L"RootComponent",440)==root?"trigger volume unreadable":"overlap volume is attached");
    if(std::hypot(box->center.x+940.,box->center.y-12019.)>100||std::abs(box->center.z)>180)
        return unavailable("unexpected trigger volume position "+std::to_string(box->center.x)+","+
            std::to_string(box->center.y)+","+std::to_string(box->center.z));
    return box->center;
}
bool mobliz_story_leg(StorySnapshot& story,UObject* pawn,UObject* world,UObject* manager,
        std::span<UObject* const> registry,int map,int main_id,int phase,int area,Vec3 position) {
    using namespace chapter_two;
    const auto elevator=[&](int id) {
        return registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact");
    };
    const auto interact=[&](UObject* actor) {
        if(getter<unsigned char>(actor,L"GetGimmickCondition")!=0||
           read<unsigned char>(actor,L"m_MapInteractType",833)!=4||!story_actor_available(pawn,actor,false))
            return std::optional<Vec3>{};
        const auto box=capture_interaction_box(actor,pawn);
        return box?std::optional<Vec3>(box->center):std::nullopt;
    };
    struct LiveStep {UObject* actor{};std::optional<Vec3> point;};
    const auto trigger=[&](int id,const char* event) {
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
        return LiveStep{actor,active_story_trigger(pawn,actor,event)};
    };
    const auto target=[&](const LiveStep& live,const char* key,const char* label,const char* instruction,bool overlap=false) {
        if(!live.actor||!live.point)return false;
        story_step(story,key,instruction);
        story_step_actor(story,live.actor,*live.point,key,label,overlap);
        return true;
    };
    if(map==3061) {
        auto* roof=elevator(0);
        if(escape_step(map,main_id,phase,area,position,{})!=EscapeStep::Rooftop)return false;
        const auto dock=getter<Vec3>(roof,L"K2_GetActorLocation");
        return dock&&near_dock(*dock,{4400,3600,-10})&&target({roof,interact(roof)},"mobliz-roof",
            "Rooftop return elevator","Take the rooftop elevator back into the shipyard.");
    }
    if(map!=3060||phase!=2095)return false;
    story.nearby_clue=nearby_mobliz_marking(pawn,world,area,position);
    const auto power=trigger(48,"C02_Moburizu_001_01");
    const auto fence=trigger(46,"C02_Moburizu_002_01");
    const auto blockade=trigger(49,"C02_Moburizu_005_01");
    const bool lower_deck=area==0&&position.y>8500;
    const bool entrance_side=area==4&&position.y<1000&&!blockade.point;
    // The visible blockade scene starts this detour. On the entrance side
    // show only the passage; verify the encounter volume on its own floor.
    const bool cargo_pending=(lower_deck||entrance_side)&&
        story_progress_flag(210)==true&&story_progress_flag(211)==false;
    auto* cargo=lower_deck&&cargo_pending?registered_gimmick(registry,world,47,L"/Script/mucha.CPP_MapGimmick_EventTrigger"):nullptr;
    const LiveStep discovery{cargo,lower_deck&&cargo_pending?mobliz_cargo_trigger(cargo):std::nullopt};
    auto* restored=elevator(73);
    const auto restore_state=getter<unsigned char>(restored,L"GetGimmickCondition");
    // Visible numbers from the native lever dialogs. Equal treatment for all
    // choices: no hint derived from the event that happens to solve the puzzle.
    constexpr std::array<std::pair<int,int>,5> levers{{{63,10},{65,12},{66,14},{64,16},{67,18}}};
    std::vector<std::pair<LiveStep,int>> options;
    if((area==1||area==2)&&!power.point&&!fence.point&&restore_state==1) {
        for(auto [id,number]:levers) {
            auto* lever=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_Switch");
            if(read<unsigned char>(lever,L"m_CanPressed",1112)!=1)continue;
            if(const auto point=interact(lever))options.push_back({{lever,point},number});
        }
    }
    switch(escape_step(map,main_id,phase,area,position,{power.point.has_value(),fence.point.has_value(),
            !options.empty(),restore_state==0,blockade.point.has_value(),discovery.point.has_value(),entrance_side&&cargo_pending})) {
    case EscapeStep::PowerFailure:
        return target(power,"mobliz-power","Entrance elevator approach","Head toward the entrance elevator on the upper deck.",true);
    case EscapeStep::Fence:
        return target(fence,"mobliz-fence","Fence by the elevator controls","Go to the fence by the elevator controls.",true);
    case EscapeStep::LeverBank: {
        story_step(story,"mobliz-levers","Check the memo or the numbered elevator levers.");
        auto* memo=registered_gimmick(registry,world,8,L"/Script/mucha.CPP_MapGimmick");
        if(const auto point=interact(memo))
            story_step_actor(story,memo,*point,"mobliz-memo","Elevator lever memo");
        for(const auto& [option,number]:options) {
            const auto n=std::to_string(number);
            story_step_actor_text(story,option.actor,*option.point,"mobliz-lever-"+n,trf("Lever {0}",{n}));
        }
        return true;
    }
    case EscapeStep::RestoredLift: {
        const auto dock=getter<Vec3>(restored,L"K2_GetActorLocation");
        if(dock&&near_dock(*dock,{1200,12000,1630}))
            return target({restored,interact(restored)},"mobliz-restored-lift","Restored entrance elevator",
                "Take the restored elevator down toward the entrance.");
        auto* call=registered_gimmick(registry,world,14,L"/Script/mucha.CPP_MapGimmick_CallSwitch");
        return dock&&near_dock(*dock,{1237,12000,38})&&object_value(call,L"m_pElevator",1016)==restored&&
            read<int>(call,L"m_CallFloorIndex",1024)==0&&target({call,interact(call)},"mobliz-restored-call",
                "Entrance elevator call pedestal","Call the restored entrance elevator up to this floor.");
    }
    case EscapeStep::EntrancePassage:
        return story_gate(story,pawn,world,manager,4,3060,100,"mobliz-entrance-passage","Passage to the entrance",
            "Follow the passage back to the entrance.");
    case EscapeStep::EntranceBlockade:
        return target(blockade,"mobliz-blockade","Entrance approach","Check the route toward the entrance.",true);
    case EscapeStep::ReturnPassage:
        return story_gate(story,pawn,world,manager,0,3060,1,"mobliz-return-passage","Passage back inside the shipyard",
            "The entrance is blocked. Return through the passage you came from.");
    case EscapeStep::CargoDiscovery:
        // The native ambush owns the remainder: its victory event moves the
        // party to the cargo train without another field walking leg.
        return target(discovery,"mobliz-cargo-approach","Cargo elevator approach","Go toward the cargo elevator.",true);
    default:return false;
    }
}
bool capture_chapter_two_legs(StorySnapshot& story,UObject* pawn,UObject* world,int map,int main_id,
        std::span<const FilteredPoint> points) {
    if(main_id!=1002||(map!=3070&&map!=3060&&map!=3061)||std::none_of(points.begin(),points.end(),[&](const auto& point) {
        return point.role==0&&point.map_kind==0&&point.map_id==(map==3070?3070:3060)&&!point.object_target;
    }))return false;
    const auto phase=main_progress();
    if(phase!=(map==3070?2130:2095))return false;
    const auto manager=world_scoped(L"CPP_MapManager",world);
    const auto area=manager.ambiguous?std::nullopt:getter<int>(manager.object,L"GetMapAreaId");
    const auto position=getter<Vec3>(pawn,L"K2_GetActorLocation");
    if(!area||!position)return false;
    const auto registry=registered_story_gimmicks(world);
    if(map!=3070)return mobliz_story_leg(story,pawn,world,manager.object,registry,map,main_id,*phase,*area,*position);
    const auto leg=chapter_two::wind_leg(map,main_id,*phase,*area,*position);
    if(!leg)return false;
    return wind_story_leg(story,pawn,world,registry,*leg);
}
bool capture_chapter_four_legs(StorySnapshot& story,UObject* pawn,UObject* world,int map,int quest,
        std::span<const FilteredPoint> points) {
    if(quest!=1004)return false;
    const auto phase=main_progress();
    if(!phase)return false;
    const auto goal_at=[&](int goal) {
        return std::any_of(points.begin(),points.end(),[&](const auto& point) {
            return point.role==0&&point.map_kind==0&&point.map_id==goal;
        });
    };
    const auto local_person=[&](int id,const char* key,const char* instruction) {
        const auto goal=std::find_if(points.begin(),points.end(),[&](const auto& point) {
            return point.role==0&&point.map_kind==0&&point.map_id==map&&
                point.object_target&&point.object==ObjectKey{1,id};
        });
        if(goal==points.end())return false;
        auto local=area_destinations(pawn,world,map,quest,std::span(&*goal,1),{},story.destination_title);
        if(local.empty())return false;
        story_step(story,key,instruction);
        story.destinations=std::move(local);
        return true;
    };
    const auto gate=[&](const chapter_four::Gate& leg) {
        const auto manager=world_scoped(L"CPP_MapManager",world);
        return !manager.ambiguous&&story_gate(story,pawn,world,manager.object,leg.id,leg.destination,leg.point,
            leg.key,leg.label,leg.instruction,leg.event_only);
    };
    if(map==3120) {
        if(phase!=4030||!goal_at(3120))return false;
        const auto position=getter<Vec3>(pawn,L"K2_GetActorLocation");
        const auto door=position?chapter_four::escape_door(map,quest,*phase,*position):std::nullopt;
        if(door&&gate(*door))return true;
        story.unavailable_reason=tr("The forward door in this train car could not be verified.");
        return true;
    }
    const auto section=[&]() -> std::optional<int> {
        const auto manager=world_scoped(L"CPP_MapManager",world);
        return manager.ambiguous?std::nullopt:getter<int>(manager.object,L"GetMapAreaId");
    };
    const auto event=[&](int id,std::string_view event_id,const char* key,const char* label,const char* instruction) {
        const auto registry=registered_story_gimmicks(world);
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
        const auto point=active_story_trigger(pawn,actor,event_id);
        if(!point)return false;
        story_step(story,key,instruction);
        story_step_actor(story,actor,*point,key,label,true);
        return true;
    };
    const auto door=[&](int id) {
        const auto registry=registered_story_gimmicks(world);
        return registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_Object_DoorBase");
    };
    const auto interact=[&](UObject* actor,const char* key,const char* label,const char* instruction) {
        if(!story_actor_available(pawn,actor))return false;
        const auto box=capture_interaction_box(actor,pawn);
        if(!box)return false;
        story_step(story,key,instruction);
        story_step_actor(story,actor,box->center,key,label);
        return true;
    };
    if(map==2110&&*phase==4090&&goal_at(2110)) {
        if(section()==1)
            return gate({2,2110,11,"zoldaad-inn-exit","Exit to the Imperial Hotel district",
                "Leave the inn and meet the party in the alley."});
        return local_person(2,"zoldaad-fina-alley","Talk to Fina in the alley beside the hotel.");
    }
    if(map==2120&&*phase>=4110&&*phase<=4113&&goal_at(2120)) {
        auto soldiers=area_destinations(pawn,world,map,quest,points,{},story.destination_title);
        if(!soldiers.empty()) {
            story_step(story,"hideout-soldiers","Speak to the rebel soldiers in the hideout.");
            story.destinations=std::move(soldiers);
            return true;
        }
    }
    if(map==2120&&(((*phase==4115||*phase==4120||*phase==4160)&&goal_at(2120))||
                    (*phase==4170&&goal_at(3140)))) {
        const auto area=section();
        if(area==5) {
            if(*phase==4120||*phase==4170)
                return gate({3,2120,2,"hideout-leave-office","Exit from Jake's office","Leave Jake's office and return to the hall."});
            return local_person(13,"hideout-speak-jake","Speak to Jake in his office.");
        }
        if(area&&(*phase==4115||*phase==4160)) {
            auto* office=door(13);
            if(chapter_four_story_door_open(office)==true)
                return gate({0,2120,4,"hideout-office-passage","Entrance to Jake's office","Go through the open door to speak to Jake."});
            if(interact(office,"hideout-office-door","Jake's office door","Open the door to Jake's office."))return true;
        }
    }
    if(map==3130) {
        const auto area=section();
        if(*phase==4140&&goal_at(3130)&&area==0)
            return gate({5,3130,3,"factory-bridge-hall","Door to the bridge hall",
                "Follow the entrance passage into the bridge hall."});
        if(*phase==4140&&goal_at(3130)&&area&&area!=0) {
            const auto position=getter<Vec3>(pawn,L"K2_GetActorLocation");
            const auto registry=registered_story_gimmicks(world);
            const auto settled=[&](int bridge_id,int blocker_id,Vec3 end) {
                auto* floor=registered_gimmick(registry,world,bridge_id,L"/Script/mucha.CPP_MapGimmick_Floor");
                const auto root=getter<Vec3>(floor,L"K2_GetActorLocation");
                return root&&std::hypot(root->x-end.x,root->y-end.y,root->z-end.z)<10.&&
                    story_blocker_open(world,registry,blocker_id);
            };
            const auto ladder=[&](int id,bool top,const char* key,const char* label,const char* instruction) {
                auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmickLadder");
                const auto point=story_ladder_approach(pawn,actor,top);
                if(!point)return false;
                story_step(story,key,instruction);
                story_step_actor(story,actor,*point,key,label);
                story.destinations.back().interactable=false;
                return true;
            };
            const auto pedestal=[&](int id) {
                auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_Switch");
                return bool_getter(actor,L"CanPressed")==true?actor:nullptr;
            };
            const bool upper_lowered=settled(60,23,{5438,13700,0});
            const bool west_lowered=settled(58,21,{2048,13700,0});
            const auto western=[&] {
                if(!position)return false;
                if(west_lowered) {
                    if(position->x>2200.||position->z<500.||position->y<11750.)return false;
                    if(story_blocker_open(world,registry,1))
                        return ladder(0,true,"factory-west-return","Ladder down to the save point",
                            "Descend the ladder to return to the bridge hall.");
                    auto* shortcut=registered_gimmick(registry,world,32,L"/Script/mucha.CPP_MapGimmick");
                    return interact(shortcut,"factory-west-shortcut","Folded ladder by the pedestal",
                        "Lower the ladder beside the pedestal to return to the hall.");
                }
                if(!settled(57,19,{2350,9700,700}))
                    return interact(pedestal(38),"factory-west-first","Pedestal on the west ramp",
                        "Follow the west ramp to the small pedestal.");
                const bool lower_side=position->x>2460.&&position->z<500.&&position->y<9540.;
                if(!settled(55,18,{3225,11800,705})) {
                    if(lower_side)
                        return interact(pedestal(33),"factory-west-low","Pedestal below the short ladder",
                            "Use the pedestal to raise the next catwalk.");
                    return ladder(31,true,"factory-west-lower-ladder","Short ladder down to the pedestal",
                        "Cross the raised catwalk, then descend to the next pedestal.");
                }
                if(lower_side)
                    return ladder(31,false,"factory-west-up-short","Short ladder to the side walkway",
                        "Climb back up to the side walkway.");
                const bool top_side=position->z>1000.;
                if(!settled(56,20,{2650,12000,700})) {
                    if(top_side)
                        return interact(pedestal(34),"factory-west-high","Pedestal on the high walkway",
                            "Use the pedestal to lower the last catwalk into place.");
                    return ladder(29,false,"factory-west-up-tall","Tall ladder to the upper pedestal",
                        "Follow the side walkway to the tall ladder and climb it.");
                }
                if(top_side)
                    return ladder(29,true,"factory-west-down-tall","Tall ladder down to the catwalks",
                        "Descend to the side walkway and cross the two catwalks.");
                return interact(pedestal(52),"factory-west-bridge","West bridge pedestal",
                    "Cross the catwalks and use the large pedestal to lower the west bridge section.");
            };
            if(*area==2&&western())return true;
            if(*area==4&&position) {
                if(upper_lowered)
                    return ladder(30,true,"factory-upper-return","Ladder down to the bridge hall",
                        "Return through the walkways and descend to the bridge hall.");
                if(position->x>=3920.||factory_maze_connected(world,registry))
                    return interact(pedestal(54),"factory-upper-bridge","Upper bridge pedestal",
                        "The walkways connect to the large pedestal. Use it to lower the bridge section below.");
                story_step(story,"factory-rotating-walkways",
                    "The left, centre and right pedestals turn the walkways.");
                constexpr std::array controls{std::pair{36,"Left rotation pedestal"},
                    std::pair{35,"Centre rotation pedestal"},std::pair{37,"Right rotation pedestal"}};
                for(const auto& [id,label]:controls) {
                    auto* control=pedestal(id);
                    if(story_actor_available(pawn,control))
                        if(const auto box=capture_interaction_box(control,pawn))
                            story_step_actor(story,control,box->center,"factory-rotate-"+std::to_string(id),label);
                }
                return !story.destinations.empty();
            }
            auto* bridge=registered_gimmick(registry,world,59,L"/Script/mucha.CPP_MapGimmick_Floor");
            const auto deck=getter<Vec3>(bridge,L"K2_GetActorLocation");
            if(position&&deck&&std::hypot(deck->x-3743.,deck->y-13700.)<10.&&std::isfinite(deck->z)) {
                const bool lowered=std::abs(deck->z)<10.&&story_blocker_open(world,registry,22);
                if(const auto leg=chapter_four::factory_conveyor_leg(*position,lowered)) {
                    const auto key="factory-conveyor-"+std::to_string(leg->id);
                    if(leg->pedestal) {
                        if(auto* control=pedestal(leg->id))
                            return interact(control,key.c_str(),leg->label,leg->instruction);
                        if(deck->z>10.&&deck->z<1680.) {
                            story_step(story,"factory-bridge-moving","Wait for the bridge section to settle.");
                            return true;
                        }
                    } else {
                        auto* conveyor=registered_gimmick(registry,world,leg->id,L"/Script/mucha.CPP_MapGimmickJumpPoint");
                        if(const auto box=capture_factory_conveyor(pawn,conveyor);box&&story_actor_available(pawn,conveyor)) {
                            story_step(story,key,leg->instruction);
                            story_step_actor(story,conveyor,box->center,key,leg->label,true);
                            return true;
                        }
                    }
                }
                const bool hall_side=*area==1||(*area==3&&position->x<1500.&&position->y>11400.&&position->y<16330.);
                if(lowered&&position->z<550.&&hall_side) {
                    if(!upper_lowered)
                        return ladder(30,false,"factory-upper-ladder","Ladder to the upper walkways",
                            "Climb the ladder to the upper walkways.");
                    if(!west_lowered)return western();
                    return event(28,"C04_421_01","factory-generator","Approach to the factory generator",
                        "Cross the lowered bridge to reach the factory generator.");
                }
            }
        }
        if(*phase==4150&&goal_at(2120)&&area==1)
            return gate({0,3130,4,"factory-hall-exit","Door out of the bridge hall",
                "Cross the lowered bridge back to the entrance passage."});
    }
    if(map==3150&&((*phase==4180&&goal_at(3150))||(*phase==4190&&goal_at(3160)))) {
        const auto area=section();
        if(!area)return false;
        if(const auto leg=chapter_four::dungeon_passage(*phase,*area))return gate(*leg);
        if(*area==5&&*phase==4180)
            return event(1,"SubEv_Dng_06Zol_43_GimEv_01","dungeon-guard","Approach to Shera's cell",
                "Continue toward Shera's cell.");
        if(*area==4&&*phase==4190) {
            auto* locked_gate=door(8);
            const auto location=getter<Vec3>(pawn,L"K2_GetActorLocation");
            if((location&&location->x>1500)||chapter_four_story_door_open(locked_gate)==true)
                return gate(chapter_four::dungeon_castle);
            if(bool_getter(locked_gate,L"IsHaveKeyItems")==true)
                return interact(locked_gate,"dungeon-use-key","Locked dungeon gate","Use the dungeon key on the gate.");
        }
    }
    if(map==3160&&goal_at(3160)) {
        if(*phase==4200)
            return event(7,"C04_430_01","castle-hall","Castle hall approach","Follow the hall toward the throne room.");
        if(*phase==4210) {
            auto* throne=door(21);
            if(story_progress_flag(422)==true)
                return gate({1,-1,0,"castle-throne","Passage into the throne room","Enter the throne room.",true});
            if(story_event_matches(throne,"SubEv_Dng_06Zol_44_GimEv_07")) {
                const auto position=getter<Vec3>(pawn,L"K2_GetActorLocation");
                if(position&&chapter_four::captain_room_side(*position)) {
                    const auto registry=registered_story_gimmicks(world);
                    auto* passage=registered_gimmick(registry,world,19,L"/Script/mucha.CPP_MapGimmick_Trigger_DoorBase");
                    if(const auto box=capture_activation_box(passage);box&&story_actor_available(pawn,passage)) {
                        story_step(story,"castle-captain-return","Leave the captain's quarters and return to the throne room doors.");
                        story_step_actor(story,passage,box->center,"castle-captain-return","Door out of the captain's quarters",true);
                        return true;
                    }
                }
                return interact(throne,"castle-throne-unlock","Throne room doors","Use the throne room key on the doors.");
            }
            if(story_progress_flag(407)==false)
                return event(9,"SubEv_Dng_06Zol_44_GimEv_01","castle-throne-approach","Approach to the throne room",
                    "Approach the throne room doors with Shera.");
            return event(6,"SubEv_Dng_06Zol_44_GimEv_03","castle-captain","Captain's quarters",
                "Go to the captain's quarters to look for the throne room key.");
        }
        if(*phase==4220) {
            const auto area=section();
            if(area==2)return gate({6,3160,8,"castle-roof-stairway","Door to the roof stairway",
                "Leave the throne room through the door to the roof stairway."});
            if(area==3||area==4)return gate({5,5100,0,"castle-rooftop","Exit to the castle rooftop",
                "Follow the stairway to the rooftop."});
        }
    }
    for(const auto& leg:chapter_four::field_legs)
        if(leg.map==map&&leg.phase==*phase&&goal_at(leg.goal))return gate(leg.gate);
    const auto stop=std::find_if(chapter_four::streetcar_stops.begin(),chapter_four::streetcar_stops.end(),
        [map](const auto& candidate){return candidate.map==map;});
    if(stop==chapter_four::streetcar_stops.end())return false;
    const bool disclosed_timetable=std::any_of(points.begin(),points.end(),[&](const auto& point) {
        return point.map_kind==0&&point.map_id==map&&point.object_target&&point.object==ObjectKey{2,stop->sign};
    });
    if(!disclosed_timetable)return false;
    const char* key=nullptr;
    const char* instruction=nullptr;
    if(*phase==4075&&goal_at(2114)) {
        const auto slums=story_progress_flag(405),commercial=story_progress_flag(406);
        if(slums==false&&map==2114)
            return local_person(1,"zoldaad-slums-contact","Talk to the man in blue in the Slums.");
        if(slums==false) {
            key="streetcar-slums";
            instruction="Read the streetcar timetable and choose the Slums to gather information.";
        } else if(slums==true&&commercial==false&&map!=2112) {
            key="streetcar-commercial";
            instruction="Read the streetcar timetable and choose the Commercial District to continue gathering information.";
        }
    } else if(*phase==4090&&map!=2110&&goal_at(2110)) {
        key="streetcar-hotel";
        instruction="Take the streetcar back to the Imperial Hotel district, then meet the party in the alley.";
    }
    if(!key)return false;
    const auto registry=registered_story_gimmicks(world);
    auto* sign=registered_gimmick(registry,world,stop->sign,L"/Script/mucha.CPP_MapGimmick_InteractEventTrigger");
    if(getter<unsigned char>(sign,L"GetGimmickCondition")!=0||!story_actor_available(pawn,sign)||
       !story_event_matches(sign,stop->event))return false;
    const auto box=capture_interaction_box(sign,pawn);
    if(!box)return false;
    story_step(story,key,instruction);
    story_step_actor(story,sign,box->center,key,"Streetcar timetable");
    return true;
}
#define RESONANCE_STORY_INPUT_SHRINE_CAPTURE
#include "ChapterSixShrines.hpp"
#undef RESONANCE_STORY_INPUT_SHRINE_CAPTURE

bool capture_chapter_six_field_leg(StorySnapshot& story,UObject* pawn,UObject* world,int map,int quest) {
    if(quest!=1006||!story.main_progress)return false;
    const int phase=*story.main_progress;
    const auto gate=[&](int id,int to,int entry,const char* key,const char* label,const char* instruction) {
        const auto manager=world_scoped(L"CPP_MapManager",world);
        return !manager.ambiguous&&story_gate(story,pawn,world,manager.object,id,to,entry,key,label,instruction);
    };
    const auto event=[&](int id,std::string_view event_id) -> std::optional<std::pair<UObject*,Vec3>> {
        const auto registry=registered_story_gimmicks(world);
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
        if(auto point=active_story_trigger(pawn,actor,event_id))return std::pair{actor,*point};
        return {};
    };
    const auto select_event=[&](const std::pair<UObject*,Vec3>& trigger,const char* key,const char* label,const char* instruction) {
        story_step(story,key,instruction);
        story_step_actor(story,trigger.first,trigger.second,key,label,true);
        return true;
    };
    if(map==2150&&(phase==6010||phase==6020||phase==6040||phase==6050||phase==6060)) {
        const auto field=capture_field_location(pawn);
        if(!field||field->map!=map)return false;
        const bool leaving=phase==6020||phase==6060;
        if(!leaving&&field->area==2) {
            const int id=phase==6050?2:1;
            const char* name=phase==6050?"Fina":"Village Elder";
            const char* key=phase==6050?"gaberada-fina":"gaberada-elder";
            const char* instruction=phase==6050?"Speak to Fina in the elder's home.":"Speak to the elder in her home.";
            for(const auto& target:capture_area_targets(pawn)) {
                if(target.category!=1||!is(target.actor,L"/Script/mucha.CPP_MapUnit_NPC")||
                   read<int>(target.actor,L"m_UniqueId",0x804)!=id)continue;
                story_step(story,key,instruction);
                story_step_actor(story,target.actor,target.position,key,name);
                return true;
            }
            return false;
        }
        switch(field->area) {
        case 2:return gate(3,2150,2157,"gaberada-elder-return","Door back to Gaberada","Leave the elder's home.");
        case 3:return gate(5,2150,2153,"gaberada-room-return","Door back to Gaberada","Return to the town lane.");
        case 4:return gate(6,2150,2155,"gaberada-room-return","Door back to Gaberada","Return to the town lane.");
        case 0:
        case 1: {
            if(leaving)return gate(1,1000,2151,"gaberada-overworld","Exit to the Gronoa overworld",
                "Use the town exit to continue on Gronoa.");
            const auto registry=registered_story_gimmicks(world);
            auto* door=registered_gimmick(registry,world,1,L"/Script/mucha.CPP_MapGimmick_Object_DoorBase");
            const auto open=gaberada_elder_door_open(door);
            if(!open)return false;
            if(*open)return gate(0,2150,2156,"gaberada-elder-entry","Entrance to the elder's home",
                "Walk through the open door to the elder's home.");
            const auto box=capture_interaction_box(door,pawn);
            if(!box)return false;
            story_step(story,"gaberada-elder-door","Open the door to the elder's home.");
            story_step_actor(story,door,box->center,"gaberada-elder-door","Door to the elder's home");
            return true;
        }
        default:return false;
        }
    }
    if(map==2160&&phase==6100) {
        const auto field=capture_field_location(pawn);
        if(!field||field->map!=map||field->area!=0)return false;
        return gate(1,1000,2161,"duggle-overworld","Exit to the Gronoa overworld",
            "Use the town exit to continue toward Gronoa Shrine.");
    }
    if(map==1000&&phase==6060) {
        auto closest=event(173,"C06_609_01");
        const auto east=event(176,"C06_609_01");
        if(east) {
            const auto origin=tracked_actor_position(pawn);
            if(!closest||(origin&&std::hypot(east->second.x-origin->x,east->second.y-origin->y)<
                std::hypot(closest->second.x-origin->x,closest->second.y-origin->y)))closest=east;
        }
        if(closest)return select_event(*closest,"gronoa-holy","Miasma story marker","Approach the miasma so Fina can cast Holy.");
    }
    if(map==1000&&phase==6090) {
        if(const auto trigger=event(72,"C06_614_Trigger"))return select_event(*trigger,"gronoa-weather",
            "Story marker beyond the mountain pass","Follow the trail beyond the mountain pass.");
    }
    if(map==3230&&phase==6080) {
        if(const auto trigger=event(5,"C06_611_01"))return select_event(*trigger,"rugged-overlook",
            "Story marker at the mountain overlook","Continue to the overlook on the mountain path.");
    }
    if(map==3230&&phase==6090) {
        return gate(1,1000,3232,"rugged-south","South exit of the mountain path","Leave the mountain path through the south exit.");
    }
    if(map==3240&&phase==6110) {
        const auto origin=tracked_actor_position(pawn);
        if(origin&&origin->z<-7000)if(const auto trigger=event(25,"C06_616_01"))return select_event(*trigger,
            "gronoa-shrine-depths","Lower shrine story marker","Continue to the bottom of the shrine.");
    }
    return false;
}

bool capture_chapter_six_lab(StorySnapshot& story,UObject* pawn,UObject* world,int map,int quest,
        std::span<const FilteredPoint> points) {
    using namespace chapter_six_lab;
    if(map!=map_id||quest!=quest_id||!story.main_progress)return false;
    const int phase=*story.main_progress;
    if(phase!=jade_objective&&phase!=return_objective)return false;
    const int goal=phase==jade_objective?map_id:2150;
    if(!std::any_of(points.begin(),points.end(),[&](const auto& point) {
        return point.role==0&&point.map_kind==0&&point.map_id==goal;
    }))return false;
    const auto unavailable=[&](std::string reason) {
        story.unavailable_reason=std::move(reason);
        return true;
    };
    const auto field=capture_field_location(pawn);
    const auto feet=getter<Vec3>(pawn,L"GetNavAgentLocation");
    if(!field||field->map!=map||!feet)return unavailable(tr("The current laboratory room could not be verified."));
    const auto place=locate(field->area,*feet);
    if(place==Place::Unknown)return unavailable(tr("The next laboratory approach is unavailable from this position."));
    const auto registry=registered_story_gimmicks(world);
    const auto lab_actor=[&](int id,const wchar_t* type) -> UObject* {
        auto* actor=registered_gimmick(registry,world,id,type);
        return actor&&path(actor).starts_with(gd_path)?actor:nullptr;
    };
    const auto native_bool_result=[](UObject* actor,const wchar_t* name,const wchar_t* identity) -> std::optional<bool> {
        if(!live(actor)||path(actor->GetFunctionByNameInChain(name))!=identity)return {};
        Call call(actor,name,1,0,1);
        if(!call||!call.return_kind(L"BoolProperty"))return {};
        call.invoke(actor);
        const auto value=call.get<unsigned char>(0);
        return value<=1?std::optional<bool>(value==1):std::nullopt;
    };
    const auto door=[&](int id) {
        return lab_actor(id,L"/Script/mucha.CPP_MapGimmick_Object_DoorBase");
    };
    const auto has_card=[&](UObject* actor,int item) -> std::optional<bool> {
        const auto ids=copied_array<int>(actor,L"m_KeyItemList",1040,8);
        if(!ids||ids->size()!=1||ids->front()!=item||
           read<unsigned char>(actor,L"m_CheckUseKeyItem",1032)!=1)return {};
        return native_bool_result(actor,L"IsHaveKeyItems",L"/Script/mucha.CPP_MapGimmick_Object_DoorBase:IsHaveKeyItems");
    };
    const auto console=[&](int id) {
        return lab_actor(id,L"/Script/mucha.CPP_MapGimmick_Switch");
    };
    const auto pressed=[&](int id) {
        return native_bool_result(console(id),L"IsSwitchPressed",L"/Script/mucha.CPP_MapGimmick_Switch:IsSwitchPressed");
    };
    const auto ladder=[&](int id) {
        return lab_actor(id,L"/Script/mucha.CPP_MapGimmickLadder");
    };
    const auto rope=[&] {
        return lab_actor(6,L"/Script/mucha.CPP_MapGimmick_StringLadder");
    };
    State state;
    if(phase==jade_objective) {
        switch(place) {
        case Place::Hall:
            for(int level=2;level>=0;--level) {
                auto* actor=door(41+level);
                state.cards[level]=has_card(actor,20009+level);
                if(state.cards[level]!=false) {
                    if(state.cards[level]==true)state.doors[level]=research_lab_door_open(actor);
                    break;
                }
            }
            break;
        case Place::WestCorridor:
        case Place::Card2Room:
            state.cards[1]=has_card(door(42),20010);
            if(place==Place::Card2Room&&state.cards[1]==true) {
                auto* actor=door(40);
                if(has_card(actor,20010)==true)state.doors[3]=research_lab_door_open(actor);
            }
            break;
        case Place::Containers:
            state.consoles[0]=pressed(28);
            state.consoles[1]=pressed(30);
            [[fallthrough]];
        case Place::ContainerShelf:
        case Place::FencedConsole:
            state.consoles[2]=pressed(29);
            break;
        case Place::EastEntry:
        case Place::Card3Room:
            state.cards[2]=has_card(door(43),20011);
            break;
        case Place::EastReturnShelf:
            state.rope_lowered=research_lab_rope_lowered(pawn,rope(),ladder(20));
            break;
        default:break;
        }
    }
    const auto selected=next_leg(phase,place,state);
    if(!selected)return unavailable(tr("The next laboratory step could not be verified."));
    const auto& leg=describe(*selected);
    const auto no_target=[&] {
        return unavailable(trf("{0} is unavailable from here.",{tr(leg.label)}));
    };
    if(leg.kind==Kind::Exit) {
        const auto manager=world_scoped(L"CPP_MapManager",world);
        const int destination=*selected==LegId::LeaveRuins?1000:map;
        if(!manager.ambiguous&&story_gate(story,pawn,world,manager.object,leg.unique_id,destination,leg.argument,
            leg.key,leg.label,leg.instruction))return true;
        return no_target();
    }
    UObject* actor=nullptr;
    std::optional<Vec3> approach;
    bool walk_on=false;
    switch(leg.kind) {
    case Kind::Card:
        actor=lab_actor(leg.unique_id,L"/Script/mucha.CPP_MapGimmick_FieldItem");
        if(read<int>(actor,L"m_ItemId",1016)!=leg.argument||
           native_bool_result(actor,L"IsOpen",L"/Script/mucha.CPP_MapGimmick_FieldItem:IsOpen")!=false)return no_target();
        break;
    case Kind::KeyDoor:
        actor=door(leg.unique_id);
        if(has_card(actor,leg.argument)!=true||research_lab_door_open(actor)!=false)return no_target();
        break;
    case Kind::Warp:
        actor=lab_actor(leg.unique_id,L"/Script/mucha.CPP_MapGimmick_Warp");
        break;
    case Kind::Console:
        actor=console(leg.unique_id);
        if(pressed(leg.unique_id)!=false||read<unsigned char>(actor,L"m_CanPressed",1112)!=1)return no_target();
        break;
    case Kind::Ladder:
        actor=ladder(leg.unique_id);
        approach=story_ladder_approach(pawn,actor,leg.argument==1);
        break;
    case Kind::Rope:
        actor=rope();
        break;
    case Kind::Lift:
        actor=lab_actor(leg.unique_id,L"/Script/mucha.CPP_MapGimmick_Floor_Elevator");
        if(const auto entry=capture_research_lab_lift_entry(pawn,actor))
            if(std::abs(entry->center.z-(leg.argument==1?180.:3180.))<=25.)approach=entry->center;
        walk_on=true;
        break;
    case Kind::Jade:
        actor=registered_gimmick(registry,world,leg.unique_id,L"/Script/mucha.CPP_MapGimmick_InteractEventTrigger");
        if(!actor||path(actor)!=jade_path||!story_event_matches(actor,"C06_606_01"))return no_target();
        break;
    case Kind::AutoDoor:
        actor=lab_actor(leg.unique_id,L"/Script/mucha.CPP_MapGimmick_Trigger_DoorBase");
        if(const auto entry=capture_research_lab_doorway(pawn,actor))approach=entry->center;
        walk_on=true;
        break;
    case Kind::Exit:break;
    }
    if(!actor||!story_actor_available(pawn,actor)||getter<unsigned char>(actor,L"GetGimmickCondition")!=0)
        return no_target();
    if(!walk_on&&leg.kind!=Kind::Ladder) {
        const auto expected_interaction=leg.kind==Kind::KeyDoor?13:4;
        if(read<unsigned char>(actor,L"m_MapInteractType",833)!=expected_interaction)return no_target();
        if(const auto box=capture_interaction_box(actor,pawn))approach=box->center;
    }
    if(!approach)return no_target();
    story_step(story,leg.key,leg.instruction);
    story_step_actor(story,actor,*approach,leg.key,leg.label,walk_on);
    if(leg.kind==Kind::Ladder)story.destinations.back().interactable=false;
    return true;
}
bool capture_chapter_seven_leg(StorySnapshot& story,UObject* pawn,UObject* world,UObject* game_instance,int map,int quest) {
    if(!story.main_progress)return false;
    const int phase=*story.main_progress;
    if(phase==6180 ? quest!=1006 : (quest!=1007||phase<7000||phase>7090))return false;

    const auto gate=[&](int id,int to,int point,const char* key,const char* label,const char* instruction) {
        const auto manager=world_scoped(L"CPP_MapManager",world);
        return !manager.ambiguous&&story_gate(story,pawn,world,manager.object,id,to,point,key,label,instruction);
    };
    const auto trigger=[&](int id,std::string_view event_id) -> std::optional<std::pair<UObject*,Vec3>> {
        const auto registry=registered_story_gimmicks(world);
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
        if(auto point=active_story_trigger(pawn,actor,event_id))return std::pair{actor,*point};
        return {};
    };
    const auto event=[&](int id,std::string_view event_id,const char* key,const char* label,const char* instruction) {
        const auto point=trigger(id,event_id);
        if(!point)return false;
        story_step(story,key,instruction);
        story_step_actor(story,point->first,point->second,key,label,true);
        return true;
    };
    if(phase==6180||phase==7090) {
        if(map==20002)return gate(0,20001,22,"enterprise-interior","Door into the Enterprise",
            "Go inside the Enterprise and make your way to the helm.");
        if(map==20001)return gate(1,1000,-1,"enterprise-helm","Enterprise helm",
            "Approach the wheel at the helm to take off.");
        if(map==1000&&is(pawn,L"/Script/mucha.CPP_MapUnit_AirShip")&&
           read<unsigned char>(pawn,L"m_MapMovementMethod",1920)==2) {
            const bool tower=phase==7090;
            const auto approach=trigger(tower?74:14,tower?"C08_801_MapTransition":"C07_701_Trigger");
            if(!approach)return false;
            if(tower) {
                story_step(story,"crystal-tower-flight","Steer the airship toward the Crystal Tower. Turn on guidance for the beacon.");
                story.flight_destination=StoryFlightDestination{approach->second,
                    "flight:crystal-tower",tr("Crystal Tower"),tr("At the Crystal Tower. Follow the game's scene.")};
            } else {
                story_step(story,"pharm-flight","Steer the airship toward the tornado over Pharm. Turn on guidance for the beacon.");
                story.flight_destination=StoryFlightDestination{approach->second,
                    "flight:pharm-tornado",tr("Tornado over Pharm"),tr("At the tornado over Pharm. Follow the game's scene.")};
            }
            story.field_control=field_control(pawn,world,game_instance,2);
            return true;
        }
        return false;
    }
    if(map==1000&&read<unsigned char>(pawn,L"m_MapMovementMethod",1920)==1) {
        switch(phase) {
        case 7005:
        case 7010:return gate(42,2170,0,"sian-entrance","Entrance to Sian","Enter Sian to ask about the Dark Crystal.");
        case 7020:
        case 7030:return gate(28,3260,0,"reizan-trailhead","Mt. Reizan trailhead","Take the mountain trail up Mt. Reizan.");
        case 7040:
            if(gate(30,2180,0,"ryusei-world-entry","Entrance to Lake Ryusei","Enter Lake Ryusei at the top of the mountain."))return true;
            break;
        case 7050:return gate(65,3271,0,"forest-path-entrance","Forest Path entrance","Follow the floating path to the Forest Path entrance.");
        case 7060:
        case 7070:
        case 7080:return gate(64,3270,0,"wandering-lands-entrance","Entrance to the Wandering Lands",
            "Cross the floating road toward the Wandering Lands entrance.");
        }
        return false;
    }
    if(map==2170&&(phase==7010||phase==7020)) {
        const auto location=capture_field_location(pawn);
        if(!location||location->map!=map)return false;
        // These interiors have their own native walking sections.
        switch(location->area) {
        case 1:return gate(2,2170,2,"sian-return-outdoors","Door back to Sian","Return to the town square.");
        case 2:return gate(4,2170,4,"sian-return-outdoors","Door back to Sian","Return to the town square.");
        case 3:return gate(5,2170,6,"sian-return-outdoors","Door back to Sian","Return to the town square.");
        case 4:return gate(8,2170,8,"sian-return-outdoors","Door back to Sian","Return to the town square.");
        case 0:break;
        default:return false;
        }
        if(phase==7020)return gate(1,1000,2171,"sian-to-pharm","Sian exit toward Mt. Reizan","Leave Sian and head for Mt. Reizan.");
        for(const auto& target:capture_area_targets(pawn)) {
            if(target.category!=1||!is(target.actor,L"/Script/mucha.CPP_MapUnit_NPC")||
               read<int>(target.actor,L"m_UniqueId",0x804)!=1)continue;
            story_step(story,"sian-ayaka","Speak to Ayaka outside the building at the top of town.");
            story_step_actor(story,target.actor,target.position,"sian-ayaka","Ayaka");
            return true;
        }
        return false;
    }
    if(map==3260) {
        if(phase==7030)return event(5,"C07_707_01","reizan-upper-trail","Upper trail on Mt. Reizan","Follow the mountain trail toward the summit.");
        if(phase==7040)return gate(0,2180,0,"reizan-to-ryusei","Passage to Lake Ryusei","Cross the summit passage to Lake Ryusei.");
    }
    if(map==2180&&phase==7050)return gate(0,1000,3262,"ryusei-to-pharm","Lake Ryusei exit toward Forest Path",
        "Leave the lake and follow the floating path toward Forest Path.");
    if(map==3271&&(phase==7050||phase==7060)) {
        const auto location=capture_field_location(pawn);
        if(!location||location->map!=map)return false;
        if(location->area==0)return gate(0,3271,2,"forest-path-upper","Passage to the upper Forest Path",
            "Follow the trail into the next section.");
        if(location->area!=1)return false;
        if(phase==7050)return event(6,"C07_713_01","forest-path-ahead","Trail beyond the Forest Path save point",
            "Continue along the trail beyond the save point.");
        return gate(2,1000,3273,"forest-path-to-pharm","Forest Path exit toward the Wandering Lands",
            "Leave Forest Path for the Wandering Lands.");
    }
    if(map==3270) {
        if(phase==7050)return event(0,"C07_713_01","wandering-old-trail","Story trail near the Wandering Lands",
            "Follow the trail ahead.");
        if(phase==7070||phase==7080)return gate(0,3272,1000,"wandering-inner-path","Passage into the Wandering Lands",
            "Cross into the next section of the Wandering Lands.");
    }
    if(map==3272) {
        if(phase==7070)return event(-1,"C07_716_01","wandering-raegen","Winding trail in the Wandering Lands",
            "Follow the winding trail past the fork.");
        if(phase==7080)return gate(3,5181,5181,"wandering-crystal-approach","Path to the Dark Crystal",
            "Continue past the save point to the Dark Crystal.");
    }
    return false;
}


bool capture_chapter_eight_leg(StorySnapshot& story,UObject* pawn,UObject* world,int map,int quest) {
    if(quest!=1008||!story.main_progress||(map!=3280&&map!=3281))return false;
    const auto location=capture_field_location(pawn);
    if(!location||location->map!=map)return false;
    const auto manager=world_scoped(L"CPP_MapManager",world);
    if(manager.ambiguous||!manager.object)return false;

    const auto gate=[&](int id,int to,int point,const char* key,const char* label,const char* instruction,
            bool event_only=false) {
        return story_gate(story,pawn,world,manager.object,id,to,point,key,label,instruction,event_only);
    };
    const auto event=[&](int id,std::string_view native_event,const char* key,const char* label,const char* instruction) {
        const auto registry=registered_story_gimmicks(world);
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
        const auto point=active_story_trigger(pawn,actor,native_event);
        if(!point)return false;
        story_step(story,key,instruction);
        story_step_actor(story,actor,*point,key,label,true);
        return true;
    };

    const int phase=*story.main_progress;
    const int area=location->area;
    if(map==3281) {
        if(phase!=8050)return false;
        if(area==0)return gate(0,3281,10001,"tower-summit-rest-return","Passage back to the spiral",
            "Return to the spiral path to the summit.");
        if(area<1||area>3)return false;
        return gate(1,5191,9200,"tower-summit","Path to the summit",
            "Follow the spiral to the summit. You can prepare at the rest stop along the way.");
    }
    if(phase<8000||phase>8040)return false;
    // A side room or an unlocked landing can only guide back through its
    // currently available native passage. The active objective then resumes.
    if(area==7)return gate(7,3280,8001,"tower-side-room-return","Passage back to the lower tower",
        "Return to the lower tower.");
    if(area==2)return gate(13,3280,40001,"tower-middle-return","Passage from Middle Reach",
        "Return inside from Middle Reach.");
    if(area==1)return gate(3,3280,10004,"tower-upper-return","Passage from Upper Reach",
        "Return inside from Upper Reach.");
    if(area==0) {
        if(phase==8000&&gate(5,-1,0,"tower-barrier","Crystal Tower barrier",
            "Approach the barrier at the tower entrance.",true))return true;
        return gate(5,3280,9000,"tower-entrance","Entrance to Crystal Tower",
            "Go through the entrance into the lower tower.");
    }

    switch(phase) {
    case 8000:
        return area==3&&event(142,"C08_802_01","tower-lower-hall","Trail through the lower tower",
            "Follow the trail through the lower tower.");
    case 8010:
        return area==3&&gate(2,-1,-1,"tower-lower-portal","Portal at the end of the lower tower",
            "Go to the portal beyond the save point and interact.",true);
    case 8020:
    case 8030:
    case 8040:
        if(area==3)return gate(2,3280,10001,"tower-lower-portal-exit","Portal to the next tower section",
            "Take the portal to the next section.");
        if(area==4)return gate(11,3280,30003,"tower-middle-stairs","Stairs beyond Middle Reach",
            "Follow the stairs beyond the save point.");
        if(area==5)return gate(9,3280,30001,"tower-bridge-passage","Passage into the next tower hall",
            "Continue through the passage ahead.");
        if(area!=6)return false;
        if(phase==8030)return event(144,"C08_811_02","tower-bridge-trail","Trail across the crystal platforms",
            "Follow the trail. The crystal paths appear as you approach.");
        if(phase!=8040)return false;
        break;
    default:return false;
    }

    const auto registry=registered_story_gimmicks(world);
    auto* portal=registered_gimmick(registry,world,133,L"/Script/mucha.CPP_MapGimmick_InteractEventTrigger");
    if(!story_actor_available(pawn,portal)||getter<unsigned char>(portal,L"GetGimmickCondition")!=0||
       !story_event_matches(portal,"C08_Dng_10Cry_41_CanNotRetrun_01"))return false;
    const auto approach=capture_interaction_box(portal,pawn);
    if(!approach)return false;
    story_step(story,"tower-white-portal","Prepare at the save point before confirming the white portal. It leads into the final sequence.");
    story_step_actor(story,portal,approach->center,"tower-white-portal","White teleport portal");
    return true;
}


std::optional<int> ultima_radar_progress() {
    constexpr int slot=24;
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/mucha.Default__CPP_BFL_DBG_FlagSaveData");
    Call read(library,L"GetProgressList",48,0,48,{},true);
    if(!read||!read.progress_list_return())return {};
    read.invoke(library);
    const auto values=copy_array(read.get<NativeArray<int>>(0),4096);
    if(!values||values->size()<=slot)return {};
    return (*values)[slot];
}

bool capture_post_clear_story(StorySnapshot& story,UObject* pawn,UObject* world,UObject* game_instance) {
    const auto cleared=story_progress_flag(3);
    if(cleared!=true)return false;

    story.ready=true;
    story.unavailable_reason=tr("No current main-story objective is displayed.");
    const auto progress=ultima_radar_progress();
    if(progress!=10)return true;
    const auto map=current_map(world);
    if(!map)return true;

    story.map_id=*map;
    story.main_progress=main_progress();
    story.objective_id="post-clear:ardra-radar";
    story.objective=tr("Investigate the radar signal at Ardra Crater.");
    story.destination_epoch="post-clear:ardra-radar";
    story.unavailable_reason.clear();

    const auto movement=read<unsigned char>(pawn,L"m_MapMovementMethod",1920);
    const auto leg=ultima_story_leg(cleared,progress,*map,movement);
    if(!leg)return true;
    if(*leg==UltimaStoryLeg::Deck||*leg==UltimaStoryLeg::Helm) {
        const auto manager=world_scoped(L"CPP_MapManager",world);
        if(manager.object&&!manager.ambiguous) {
            if(*leg==UltimaStoryLeg::Deck)
                story_gate(story,pawn,world,manager.object,0,20001,22,"ardra-enterprise-interior","Door into the Enterprise",
                    "Go inside the Enterprise and make your way to the helm.");
            else
                story_gate(story,pawn,world,manager.object,1,1000,-1,"ardra-enterprise-helm","Enterprise helm",
                    "Approach the wheel at the helm to take off.");
        }
        return true;
    }

    if(*leg==UltimaStoryLeg::Flight&&is(pawn,L"/Script/mucha.CPP_MapUnit_AirShip")) {
        // The Enterprise can land on this part of the crater floor.
        story_step(story,"ardra-crater-flight","Follow the beacon to Ardra Crater and land the Enterprise.");
        story.flight_destination=StoryFlightDestination{{-21622,20536,1500},
            "flight:ardra-crater",tr("Ardra Crater"),tr("At Ardra Crater. Land the Enterprise.")};
        story.field_control=field_control(pawn,world,game_instance,2);
        return true;
    }

    if(*leg==UltimaStoryLeg::Crater) {
        constexpr std::wstring_view boss=
            L"/Game/Map/Wld/11Adr/EV/Wld_11Adr_EV.Wld_11Adr_EV:PersistentLevel.BP_MapUnit_NPC_Field_C_1";
        for(const auto& target:capture_area_targets(pawn)) {
            if(target.category!=1||path(target.actor)!=boss)continue;
            story_step(story,"ardra-ultima-weapon","Approach Ultima Weapon and interact when ready.");
            story_step_actor(story,target.actor,target.position,"ardra-ultima-weapon","Ultima Weapon");
            return true;
        }
        story_step(story,"ardra-reach-crater","Use the Enterprise to reach Ardra Crater.");
    }
    return true;
}

bool capture_later_story_leg(StorySnapshot& story,UObject* pawn,UObject* world,UObject* game_instance,int map,int quest,
        std::span<const FilteredPoint> points) {
    if(quest!=1003&&(quest!=1002||(map!=1000&&map!=2050&&map!=2060&&map!=2061)))return false;
    const auto goal_at=[&](int goal) {
        return std::any_of(points.begin(),points.end(),[&](const auto& point) {
            return point.role==0&&point.map_kind==0&&point.map_id==goal;
        });
    };
    const auto phase=main_progress();
    if(!phase)return false;
    const auto manager=world_scoped(L"CPP_MapManager",world);
    if(manager.ambiguous||!manager.object)return false;
    const auto gate=[&](const chapter_three::GateLeg& leg) {
        return story_gate(story,pawn,world,manager.object,leg.id,leg.destination,leg.point,leg.key,leg.label,
            leg.instruction,leg.event_only);
    };
    for(const auto& leg:chapter_three::field_legs)
        if(leg.quest==quest&&leg.map==map&&leg.phase==*phase&&goal_at(leg.goal))return gate(leg.gate);

    const auto event=[&](int id,std::string_view native_id,const char* key,const char* label,const char* instruction) {
        const auto registry=registered_story_gimmicks(world);
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
        const auto point=active_story_trigger(pawn,actor,native_id);
        if(!point)return false;
        story_step(story,key,instruction);
        story_step_actor(story,actor,*point,key,label,true);
        return true;
    };
    const auto climb=[&](int id,const std::string& key,const char* label,const char* instruction,bool top=false) {
        const auto registry=registered_story_gimmicks(world);
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmickLadder");
        const auto point=story_ladder_approach(pawn,actor,top);
        if(!point)return false;
        story_step(story,key,instruction);
        story_step_actor(story,actor,*point,key,label);
        story.destinations.back().interactable=false;
        return true;
    };
    const auto return_save_point=[&](int id,int entrance_id,const char* key) {
        const auto registry=registered_story_gimmicks(world);
        auto* actor=registered_gimmick(registry,world,id,L"/Script/mucha.CPP_MapGimmick_SavePoint");
        auto* entrance=registered_gimmick(registry,world,entrance_id,L"/Script/mucha.CPP_MapGimmick_SavePoint");
        const auto* link=static_cast<const std::byte*>(property(entrance,L"m_ParentSavePointData",1008,16));
        auto* field=link?entrance->GetPropertyByNameInChain(L"m_ParentSavePointData"):nullptr;
        if(!actor||!field||!field->field_class()||field->field_class()->GetName()!=L"StructProperty"||
            !output_struct(field,L"/Script/mucha.MAP_GIMMICK_SAVEPOINT_PARENT_DATA",16)||
            pod<UObject*>(link,8)!=actor||read<unsigned char>(entrance,L"m_IsTeleport",1001)!=1||
            bool_getter(actor,L"CanTeleprot")!=true)return false;
        const auto approach=capture_interaction_box(actor,pawn);
        if(!approach||!story_actor_available(pawn,actor))return false;
        story_step(story,key,"Use the save point to return to the entrance.");
        story_step_actor(story,actor,approach->center,key,"Save point for the return to the entrance");
        return true;
    };
    if(quest==1002&&map==2061&&*phase==2150&&goal_at(2061))
        return event(5,"C02_227_01","lid-platform","Lid's upper platform","Approach Lid on the upper platform.");
    if(quest!=1003)return false;
    if(map==1000) {
        const auto method=read<unsigned char>(pawn,L"m_MapMovementMethod",1920);
        if(method!=1)return false;
        if(*phase==3210&&std::any_of(points.begin(),points.end(),[](const auto& point) {
            return point.map_kind==0&&point.map_id==1000&&point.object_target&&
                ((point.role==0&&point.object==ObjectKey{3,1998})||(point.role==1&&point.object==ObjectKey{6,93}));
        })&&harbor_story_leg(story,pawn,world))return true;
        if(const auto position=getter<Vec3>(pawn,L"K2_GetActorLocation")) {
            for(const auto& point:points) {
                if(point.role!=0||point.map_kind!=0)continue;
                if(const auto leg=chapter_three::world_gate(*phase,point.map_id,*position))return gate(*leg);
            }
        }
    }
    if(map==3090&&goal_at(2070)) {
        if(*phase==3060)
            return event(1,"C03_311_01","old-town-first","Old Town street","Follow the main street to the monsters.");
        if(*phase==3070) {
            const auto bridge=story_progress_flag(306);
            if(bridge==false)
                return event(3,"C03_312_B_01","old-town-bridge","Old Town bridge","Go to the bridge.");
            if(bridge!=true)return false;
            const auto town_gate=story_progress_flag(307);
            if(town_gate==false)
                return event(2,"C03_312_C_01","old-town-gate","Old Town gate","Continue to the gate.");
            if(town_gate==true)
                return event(4,"C03_313_01","old-town-boss","Far end of Old Town","Continue to the far end of Old Town.");
        }
    }
    if(map==2070&&goal_at(2070)) {
        for(const auto& site:chapter_three::purification_legs) {
            if(site.phase!=*phase)continue;
            const auto target=std::find_if(points.begin(),points.end(),[&](const auto& point) {
                return point.role==0&&point.map_kind==0&&point.map_id==2070&&
                    point.object_target&&point.object==ObjectKey{2,site.id};
            });
            if(target==points.end())break;
            auto local=area_destinations(pawn,world,map,quest,std::span<const FilteredPoint>(&*target,1),{},
                story.destination_title);
            if(local.empty())break;
            story_step(story,"olderion-purification-"+std::to_string(site.id),site.instruction);
            story.destinations=std::move(local);
            return true;
        }
    }
    if(map==2070&&*phase==3120&&goal_at(2070)) {
        const auto area=getter<int>(manager.object,L"GetMapAreaId");
        if(area==0||area==2) {
            const bool outside=area==0;
            const ObjectKey key=outside?ObjectKey{6,5}:ObjectKey{1,16};
            const auto target=std::find_if(points.begin(),points.end(),[&](const auto& point) {
                return point.role==(outside?1:0)&&point.map_kind==0&&point.map_id==2070&&
                    point.object_target&&point.object==key;
            });
            if(target!=points.end()) {
                auto local=area_destinations(pawn,world,map,quest,std::span<const FilteredPoint>(&*target,1),{},story.destination_title);
                if(!local.empty()) {
                    if(outside) {
                        const auto registry=registered_story_gimmicks(world);
                        auto* door=registered_gimmick(registry,world,73,L"/Script/mucha.CPP_MapGimmick_Object_DoorBase");
                        const auto open=olderion_inn_door_open(door);
                        if(open==true&&gate({13,2070,101,"olderion-inn-passage","Passage into the inn",
                            "Walk through the open inn door."}))return true;
                        if(open==false&&story_actor_available(pawn,door))
                            if(const auto prompt=capture_interaction_box(door,pawn)) {
                                constexpr auto step="olderion-inn-door";
                                story_step(story,step,"Use the interaction control to open the inn door.");
                                story_step_actor(story,door,prompt->center,step,"Olderion inn door");
                                const auto nearby=copied_array<UObject*>(pawn,L"m_pInteractMapGimmickList",2600,512);
                                story.destinations.back().interactable=nearby&&
                                    std::find(nearby->begin(),nearby->end(),door)!=nearby->end();
                                return true;
                            }
                        story_step(story,"olderion-inn-passage-unavailable",open==true?
                            "The inn door is open. Continue through it when the passage is clear.":
                            "Open the inn door when the game's interaction appears.");
                        return true;
                    }
                    story_step(story,"olderion-innkeeper","Talk to the innkeeper to stay the night.");
                    story.destinations=std::move(local);
                    return true;
                }
            }
        }
    }
    if(map==2070&&*phase==3200&&goal_at(2070))
        return event(0,"C03_330_01","olderion-arsha","Plaza with the tree","Follow Arsha to the plaza with the tree.");
    if(map==1000&&*phase==3160&&goal_at(1000))
        return event(20,"C03_324_01","wardens-cape","Warden's Cape","Approach Warden's Cape.");
    if(map==3100&&((*phase==3140&&goal_at(3100))||(*phase==3150&&goal_at(1000))||(*phase==3190&&goal_at(2070)))) {
        const auto area=getter<int>(manager.object,L"GetMapAreaId");
        if(!area)return false;
        if(const auto leg=chapter_three::waterfall_gate(*phase,*area))return gate(*leg);
        if(*phase==3190&&*area==11)return return_save_point(3,6,"waterfall-return-save-point");
        if(*phase==3140) {
            if(*area==1||*area==2||*area==5)
                return climb(0,"waterfall-first-ladder","First long ladder","Climb the long ladder to the upper path.");
            if(*area==4)return climb(1,"waterfall-balcony-ladder","Ladder back to the upper path",
                "Climb back up from the lower ledge.");
            if(*area==7)return climb(2,"waterfall-second-ladder","Second long ladder","Climb to the next level.");
        }
    }
    if(map==3110&&((*phase==3180&&goal_at(3110))||(*phase==3190&&goal_at(2070)))) {
        const auto area=getter<int>(manager.object,L"GetMapAreaId");
        const auto position=tracked_actor_position(pawn);
        const auto method=read<unsigned char>(pawn,L"m_MapMovementMethod",1920);
        if(*phase==3180&&area&&method==4&&is(pawn,L"/Script/mucha.CPP_MapUnit_Ship")) {
            const auto hull=getter<Vec3>(pawn,L"K2_GetActorLocation");
            if(hull&&capture_shrine_crossing(story,pawn,world,*area,*hull)) {
                story.field_control=field_control(pawn,world,game_instance,4);
                return true;
            }
        }
        if(area&&position&&method==1) {
            if(*phase==3190&&*area==6)return return_save_point(9,12,"water-shrine-return-save-point");
            if(capture_water_shrine_land(story,pawn,world,manager.object,*phase,*area,*position))return true;
        }
    }
    if(map==3080&&*phase==3110&&goal_at(2070)&&read<unsigned char>(pawn,L"m_MapMovementMethod",1920)==1) {
        const auto area=getter<int>(manager.object,L"GetMapAreaId");
        const auto position=getter<Vec3>(pawn,L"K2_GetActorLocation");
        if(!area||!position)return false;
        if(const auto leg=chapter_three::cave_return_gate(*phase,*area,*position))return gate(*leg);
        if(const auto ladder=chapter_three::cave_return_ladder(*phase,*area,*position))
            return climb(*ladder,"lake-return-ladder-"+std::to_string(*ladder),"Ladder down toward Olderion",
                "Climb down to the next landing.",true);
    }
    if(map==3080&&(goal_at(3080)||(*phase==3040&&goal_at(2070))||(*phase==3100&&goal_at(2081)))) {
        const auto area=getter<int>(manager.object,L"GetMapAreaId");
        const auto position=getter<Vec3>(pawn,L"K2_GetActorLocation");
        const auto method=read<unsigned char>(pawn,L"m_MapMovementMethod",1920);
        if(!area||!position||!method)return false;
        if(*phase==3030&&*method==4&&is(pawn,L"/Script/mucha.CPP_MapUnit_Ship")) {
            const auto pool=chapter_three::cave_pool(*area,*position);
            if(pool) {
                story_step(story,std::string(pool->key)+"-rowing",pool->rowing);
                story.crossing=pool->pedestal==36?StoryCrossing::CaveFarBank:StoryCrossing::CaveNextPool;
            } else if(*area==9) {
                // The hidden side pool has a separate landing and no walking
                // connection to the main path. Return through its boat gate.
                const bool side_pool=position->x<470&&position->y<68090;
                story_step(story,side_pool?"cave-side-pool-return":"cave-third-water-rowing",side_pool?
                    "Row back to the second pool to take the passage to the upper path.":
                    "Row to the landing below the upper passage back to the entrance hall.");
                story.crossing=side_pool?StoryCrossing::CaveHiddenReturn:StoryCrossing::CaveUpperPassage;
            } else return false;
            story.field_control=field_control(pawn,world,game_instance,4);
            return true;
        }
        if(*method!=1)return false;
        if(const auto leg=chapter_three::cave_gate(*phase,*area,*position))return gate(*leg);
        if(*phase==3030&&*area==9&&position->x>=-70&&position->x<270&&
            position->y>=66850&&position->y<=67410) {
            const auto registry=registered_story_gimmicks(world);
            return story_board_raft(story,pawn,world,registry,51,
                "Board the boat to return to the second pool and take the other passage.");
        }
        if(*phase==3040&&*area==3&&position->y>=-1700&&position->y<=2500&&position->z>730)
            return return_save_point(19,13,"cave-return-save-point");
        if(*phase==3100&&(*area==1||*area==7)&&position->y<-8100) {
            if(position->z<500)return climb(1,"lake-ladder-1","First ladder toward Lake Dorr","Climb the first ladder.");
            if(position->z<950)return climb(2,"lake-ladder-2","Second ladder toward Lake Dorr","Climb the second ladder.");
            return climb(3,"lake-ladder-3","Third ladder toward Lake Dorr","Climb the last ladder to the north exit.");
        }
        const auto pool=*phase==3030?chapter_three::cave_pool(*area,*position):std::nullopt;
        if(!pool)return false;
        const auto registry=registered_story_gimmicks(world);
        auto* pedestal=registered_gimmick(registry,world,pool->pedestal,L"/Script/mucha.CPP_MapGimmick_Switch");
        const auto pressed=bool_getter(pedestal,L"IsSwitchPressed");
        if(!pressed||getter<unsigned char>(pedestal,L"GetGimmickCondition")!=0)return false;
        int raft=pool->boat;
        if(!*pressed&&pool->pedestal==35) {
            auto* surface=registered_gimmick(registry,world,54,L"/Script/mucha.CPP_MapGimmick_Floor");
            const auto water=tracked_actor_position(surface);
            if(!water||!std::isfinite(water->z))return false;
            if(water->z<25.) {
                story_step(story,std::string(pool->key)+"-rising","Wait for the water to rise to the bank before boarding.");
                return true;
            }
            raft=position->x>=1400?49:48;
        }
        auto* target=*pressed?pedestal:registered_gimmick(registry,world,raft,L"/Script/mucha.CPP_MapGimmick_Object_Raft");
        if(!*pressed&&pool->pedestal==35&&!story_actor_available(pawn,target))
            target=registered_gimmick(registry,world,raft==49?48:49,L"/Script/mucha.CPP_MapGimmick_Object_Raft");
        const auto approach=capture_interaction_box(target,pawn);
        const auto boarding=*pressed?std::nullopt:capture_raft_boarding_point(target,pawn);
        if(boarding&&!boarding->level_with_bank) {
            story_step(story,std::string(pool->key)+"-rising","Wait for the water to rise to the bank before boarding.");
            return true;
        }
        const auto key=std::string(pool->key)+(*pressed?"-pedestal":"-boat");
        const auto pool_name=tr(pool->name);
        story_step_text(story,key,*pressed?trf("Use the pedestal to raise the water in the {0}.",{pool_name}):
            trf("Board the boat to cross the {0}.",{pool_name}));
        if(approach&&story_actor_available(pawn,target))
            story_step_actor(story,target,boarding?boarding->position:approach->center,key,*pressed?"Water-level pedestal":"Boat");
        else story.unavailable_reason=tr(*pressed?"The pedestal's approach is unavailable.":
            "The boat's boarding point is unavailable.");
        return true;
    }
    return false;
}

std::vector<StoryDestination> opening_destinations(UObject* pawn,UObject* world,UObject* game_instance,
        int map_id,int main_id,std::string& epoch,std::string& reason) {
    std::vector<StoryDestination> result;
    if(map_id!=10040&&map_id!=10041)return result;
    const auto scenario=gi_scoped(L"CPP_ScenarioSubsystem",game_instance);
    const auto chapter=getter<int>(scenario.object,L"GetCurrentChapter");
    if(scenario.ambiguous||!chapter)throw std::runtime_error("Opening current-chapter getter is unavailable");
    if(*chapter!=1)return result;
    const auto section=capture_walking_section(pawn);
    if(!section) {
        reason=tr("The current Battlefield walking section could not be verified.");
        return result;
    }
    std::set<UObject*> seen;
    std::vector<OpeningContinuationEvidence> evidence;
    std::vector<UObject*> actors;
    const auto capture=[&](UObject* actor) {
        if(!live(actor)||actor_world(actor)!=world||!seen.insert(actor).second||
           path(actor->GetClassPrivate())!=L"/Script/mucha.CPP_MapGimmick_EventTrigger")return;
        const auto destroying=read<unsigned char>(actor,L"bActorIsBeingDestroyed",93);
        const auto id=read<int>(actor,L"m_UniqueId",808);
        const auto key=read<ObjectKey>(actor,L"TypeData",712);
        const auto kind=read<unsigned char>(actor,L"m_MapObjectType",832);
        if(!destroying||(*destroying&2)||!id||!key||!kind||*kind!=2)return;
        const auto* spec=opening_continuation_spec(map_id,*id);
        if(!spec)return;
        OpeningContinuationEvidence value;value.unique_id=*id;value.key=*key;value.current_registered_actor=true;
        value.position={std::numeric_limits<double>::quiet_NaN(),0,0};
        evidence.push_back(value);actors.push_back(actor);auto& current=evidence.back();
        const auto state=getter<unsigned char>(actor,L"GetGimmickCondition");
        if(!state||*state>1)throw std::runtime_error("Opening effective gimmick state is unreadable for ID "+std::to_string(*id));
        current.active=state&&*state==0;
        if(!current.active)return;
        Call events(actor,L"GetEventList",16,0,16);
        if(!events)throw std::runtime_error("Opening GetEventList rejected: "+events.rejection());
        if(!events.event_array_return())throw std::runtime_error("Opening GetEventList array identity is unsupported");
        events.invoke(actor);
        const auto array=events.get<NativeArray<std::byte>>(0);
        const int expected_events=map_id==10041?2:1;
        if(!valid_native_array(array.data,array.num,array.max,16)||array.num!=expected_events)
            throw std::runtime_error("Opening active event list differs from the audited shape");
        const auto condition=short_native_string(array.data);
        if(!condition)throw std::runtime_error("Opening active event condition string is unreadable");
        current.condition=*condition;auto event=pod<FName>(array.data,16);current.event=utf8(event.ToString());
        if(current.condition!=spec->condition||current.event!=spec->event)
            throw std::runtime_error("Opening event identity differs from the audited continuation for ID "+std::to_string(*id));
        if(expected_events==2) {
            const auto* alternate=array.data+56;
            const auto alternate_condition=short_native_string(alternate);
            auto alternate_event=pod<FName>(alternate,16);
            if(!alternate_condition||*alternate_condition!="{progress:0}==5270"||utf8(alternate_event.ToString())!="C05_537_Trigger")
                throw std::runtime_error("The next Battlefield section's event list differs from the audited shape.");
        }
        current.matched_event=matching_trigger_event(actor,events);
        const auto overlap=opening_overlap(pawn,actor);
        if(!overlap)throw std::runtime_error("Opening trigger/player overlap state is unreadable for ID "+std::to_string(*id));
        current.overlap_enabled=*overlap;
        const auto position=getter<Vec3>(actor,L"K2_GetActorLocation");
        if(!position||!std::isfinite(position->x)||!std::isfinite(position->y)||!std::isfinite(position->z))
            throw std::runtime_error("Opening continuation position is unreadable for ID "+std::to_string(*id));
        current.position=*position;
        const auto nearest=closest_activation_point(section->bounds,*position);
        if(!nearest)throw std::runtime_error("The Battlefield section bounds could not be read.");
        current.in_walking_section=nearest->x==position->x&&nearest->y==position->y&&nearest->z==position->z;
    };
    for(auto* actor:registered_story_gimmicks(world))capture(actor);
    const auto eligible=eligible_opening_continuations(*chapter,map_id,evidence);
    epoch=opening_destination_epoch(epoch,*chapter,map_id,evidence);
    epoch+="|section:"+std::to_string(section->id);
    std::string diagnostic="map="+std::to_string(map_id)+" section="+std::to_string(section->id)+
        " candidates="+std::to_string(evidence.size())+" enabled=";
    for(const auto index:eligible) {
        const auto& e=evidence[index];auto* actor=actors[index];
        StoryDestination d;
        d.id="main-quest:"+std::to_string(main_id)+":opening:"+std::to_string(e.unique_id);
        d.label=tr("Story event");d.position=e.position;d.local=true;d.disclosed=true;
        d.actor_id=utf8(path(actor));d.actor_type=actor->GetClassPrivate()->GetNamePrivate().ToString();d.actor_available=true;
        result.push_back(std::move(d));diagnostic+=std::to_string(e.unique_id)+",";
    }
    if(section->id==0&&opening_needs_next_section(*chapter,map_id,evidence)) {
        constexpr int next_battlefield_map=10041;
        auto* exit=active_section_exit(pawn,*section,next_battlefield_map,reason);
        if(!exit)return result;
        const auto position=tracked_actor_position(exit);
        if(!position) {
            reason=tr("The Battlefield exit position could not be read.");
            return result;
        }
        StoryDestination destination;
        destination.id="main-quest:"+std::to_string(main_id)+":opening-exit";
        destination.label=tr("Exit to the next Battlefield section");
        destination.position=*position;
        destination.disclosed=destination.local=destination.actor_available=destination.exit=true;
        destination.actor_id=utf8(path(exit));
        destination.actor_type=exit->GetClassPrivate()->GetNamePrivate().ToString();
        destination.transition_map=next_battlefield_map;
        epoch+="|exit:"+destination.actor_id+"|map:"+std::to_string(*destination.transition_map);
        diagnostic+="exit="+destination.actor_id;
        result.push_back(std::move(destination));
    }
    for(const auto& e:evidence)diagnostic+=" ["+std::to_string(e.unique_id)+":active="+std::to_string(e.active)+
        " match="+std::to_string(!e.matched_event.empty())+" overlap="+std::to_string(e.overlap_enabled)+"]";
    static std::string previous;
    if(diagnostic!=previous){previous=diagnostic;log("STORY opening "+diagnostic);}
    return result;
}

std::optional<StoryDestination> shrine_story_event(UObject* pawn,UObject* world,int main_id) {
    // Dng_01Gra_40_EV on the supported build. Three volumes start the same
    // second scene. Prefer the central volume when that scene is enabled.
    struct Event {const wchar_t* actor;int id;const char* event;const char* condition;};
    constexpr std::array events{
        Event{L"BP_MapGimmick_EventTrigger_C_1",8,"C01_008_01_B","{progress:0}==1030"},
        Event{L"BP_MapGimmick_EventTrigger_C_0",7,"C01_008_02","{progress:0}==1040"},
        Event{L"BP_MapGimmick_EventTrigger_C_2",1,"C01_008_02","{progress:0}==1040"},
        Event{L"BP_MapGimmick_EventTrigger_C_3",10,"C01_008_02","{progress:0}==1040"}};
    constexpr std::wstring_view level=L"/Game/Map/Dng/01Gra/01Gra_40/EV/Dng_01Gra_40_EV.Dng_01Gra_40_EV:PersistentLevel.";
    std::array<UObject*,events.size()> candidates{};
    for(auto* actor:registered_story_gimmicks(world)) {
        if(!live(actor)||!is(actor,L"/Script/mucha.CPP_MapGimmick_EventTrigger")||actor_world(actor)!=world)continue;
        const auto id=path(actor);
        if(!id.starts_with(level))continue;
        const auto name=std::wstring_view(id).substr(level.size());
        for(std::size_t i=0;i<events.size();++i)if(name==events[i].actor) {
            if(candidates[i]&&candidates[i]!=actor)return {};
            candidates[i]=actor;
            break;
        }
    }
    for(std::size_t i=0;i<events.size();++i) {
        auto* actor=candidates[i];
        const auto& expected=events[i];
        if(!actor||read<int>(actor,L"m_UniqueId",808)!=expected.id||
           getter<unsigned char>(actor,L"GetGimmickCondition")!=0)continue;
        Call list(actor,L"GetEventList",16,0,16);
        if(!list||!list.event_array_return())continue;
        list.invoke(actor);
        const auto array=list.get<NativeArray<std::byte>>(0);
        if(!valid_native_array(array.data,array.num,array.max,16)||array.num!=1||
           short_native_string(array.data)!=expected.condition)continue;
        auto event=pod<FName>(array.data,16);
        if(utf8(event.ToString())!=expected.event||matching_trigger_event(actor,list)!=expected.event||
           opening_overlap(pawn,actor)!=true)continue;
        const auto position=tracked_actor_position(actor);
        if(!position||!std::isfinite(position->x)||!std::isfinite(position->y)||!std::isfinite(position->z))continue;
        StoryDestination destination;
        destination.id="main-quest:"+std::to_string(main_id)+":shrine-event:"+std::to_string(expected.id);
        destination.label=tr("Next story event in the Earth Shrine");
        destination.actor_id=utf8(path(actor));destination.actor_type=actor->GetClassPrivate()->GetNamePrivate().ToString();
        destination.position=*position;
        destination.disclosed=destination.local=destination.actor_available=destination.event_trigger=true;
        static std::string previous;
        if(previous!=destination.actor_id) {
            previous=destination.actor_id;
            log("STORY Shrine event "+std::string(expected.event)+" actor="+previous);
        }
        return destination;
    }
    return {};
}

std::optional<StoryDestination> mitra_vision_event(UObject* pawn,UObject* world,int main_id) {
    // Town_01Gra_20_EV, Gimmick 4. The native selector requires the current
    // equip-a-vision stage and an equipped Vision before this route is shown.
    constexpr std::wstring_view actor_path=
        L"/Game/Map/Town/01Gra/01Gra_20/EV/Town_01Gra_20_EV.Town_01Gra_20_EV:PersistentLevel.CPP_MapGimmick_EventTrigger_1";
    constexpr std::string_view condition="({equip:1001, Vision}|{equip:1002, Vision})&{progress:0}==1090";
    constexpr std::string_view event_id="C01_015_01";
    UObject* selected=nullptr;
    for(auto* actor:registered_story_gimmicks(world)) {
        if(!live(actor)||actor_world(actor)!=world||path(actor)!=actor_path)continue;
        if(selected&&selected!=actor)return {};
        selected=actor;
    }
    if(!selected||path(selected->GetClassPrivate())!=L"/Script/mucha.CPP_MapGimmick_EventTrigger"||
       selected->GetClassPrivate()->GetPropertiesSize()!=1040||read<int>(selected,L"m_UniqueId",808)!=4||
       getter<unsigned char>(selected,L"GetGimmickCondition")!=0||opening_overlap(pawn,selected)!=true)return {};
    Call list(selected,L"GetEventList",16,0,16);
    if(!list||!list.event_array_return())return {};
    list.invoke(selected);
    const auto array=list.get<NativeArray<std::byte>>(0);
    if(!valid_native_array(array.data,array.num,array.max,16)||array.num!=1||
       short_native_string(array.data)!=condition||utf8(pod<FName>(array.data,16).ToString())!=event_id||
       matching_trigger_event(selected,list)!=event_id)return {};
    const auto box=capture_activation_box(selected);
    if(!box)return {};
    StoryDestination target;
    target.id="main-quest:"+std::to_string(main_id)+":mitra-vision-talk";
    target.label=tr("Talk to Lasswell");
    target.actor_id=utf8(path(selected));target.actor_type=selected->GetClassPrivate()->GetNamePrivate().ToString();
    target.position=box->center;
    target.disclosed=target.local=target.actor_available=target.event_trigger=true;
    return target;
}

}
bool is_story_shrine_bridge_goal(UObject* target,Vec3 goal) {
    return shrine_bridge_at_goal(target,goal).has_value();
}
std::optional<StoryShrineBridgeRoute> capture_story_shrine_bridge(UObject* pawn,UObject* target,Vec3 goal) {
    using namespace chapter_three;
    try {
        const auto requested=shrine_bridge_at_goal(target,goal);
        auto* world=actor_world(pawn);
        if(!requested||!world||actor_world(target)!=world||read<unsigned char>(pawn,L"m_MapMovementMethod",1920)!=1||
            current_map(world)!=3110||main_progress()!=3180)return {};
        const auto manager=world_scoped(L"CPP_MapManager",world);
        const auto area=manager.ambiguous?std::nullopt:getter<int>(manager.object,L"GetMapAreaId");
        const auto position=tracked_actor_position(pawn);
        if(!area||!position)return {};
        const auto registry=registered_story_gimmicks(world);
        const int floor_id=shrine_bridge_spec(requested->bridge).floor;
        constexpr auto floor_type=L"/Script/mucha.CPP_MapGimmick_Floor";
        auto* floor=registered_gimmick(registry,world,floor_id,floor_type);
        auto* hall=registered_gimmick(registry,world,75,floor_type);
        const auto room_height=shrine_floor_height(pawn,floor,floor_id==74?Vec3{-400,60430,0}:Vec3{-158,13140,0});
        const auto hall_height=shrine_floor_height(pawn,hall,{-1250,32150,0});
        if(!room_height||!hall_height)return {};
        const auto current=shrine_bridge_leg(*area,*position,shrine_hall_water(*hall_height),shrine_room_water(floor_id,*room_height));
        if(!current||current->bridge!=requested->bridge||current->returning!=requested->returning)return {};
        UObject* marker=nullptr;
        auto route=checked_shrine_bridge(pawn,world,floor,*requested,marker);
        return marker==target?std::move(route):std::nullopt;
    } catch(const std::exception& error) {
        static NativeProblemDeduplicator reported;
        if(reported.accept(error.what()))log(std::string("STORY Water Shrine bridge unavailable: ")+error.what());
        return {};
    }
}
StorySnapshot capture_story_input(UObject* pawn) {
    StorySnapshot result;
    try {
        if(!live(pawn)||!is(pawn,L"/Script/mucha.CPP_MapUnit_Playable")) {
            result.unavailable_reason=tr("A current field character is unavailable.");return result;
        }
        auto* world=actor_world(pawn);
        auto* game_instance=object_value(world,L"OwningGameInstance",552);
        if(!world||!game_instance||!is(game_instance,L"/Script/Engine.GameInstance")) {
            result.unavailable_reason=tr("The current story area could not be identified.");return result;
        }
        result.world=utf8(path(world));result.player=utf8(path(pawn));
        auto* controller=object_getter(pawn,L"GetController");
        const auto local=bool_getter(controller,L"IsLocalPlayerController");
        if(!controller||!is(controller,L"/Script/Engine.PlayerController")||!local||!*local||
           object_getter(controller,L"K2_GetPawn")!=pawn) {
            result.unavailable_reason=tr("The current player could not be identified.");return result;
        }
        try {result.field_control=field_control(pawn,world,game_instance);}
        catch(const std::exception&) {result.field_control=false;}
        const auto quest=gi_scoped(L"CPP_QuestSubsystem",game_instance);
        if(quest.ambiguous||!quest.object) {
            result.unavailable_reason=tr(quest.ambiguous?"The current main objective is ambiguous.":"The current main objective source is unavailable.");
            return result;
        }
        const auto main_id=read<int>(quest.object,L"MainQuestId",152);
        if(!main_id) {result.unavailable_reason=tr("The current main objective could not be identified.");return result;}
        if((*main_id==1008||*main_id<0)&&capture_post_clear_story(result,pawn,world,game_instance))return result;
        if(*main_id<0) {result.ready=true;result.unavailable_reason=tr("No current main-story objective is displayed.");return result;}
        std::optional<std::vector<FilteredPoint>> points;
        std::string chapter_title,destination_title;
        {
            Call row(quest.object,L"FindQuestData",168,160,1,{L"OutData",8,152,L"/Script/mucha.QuestData",
                L"/Script/mucha.CPP_QuestSubsystem:FindQuestData"});
            if(!row) {
                report_find_quest_rejection(row);
                result.unavailable_reason=tr("The current main objective could not be read safely.");return result;
            }
            row.put(0,*main_id);row.invoke(quest.object);
            if(!row.get<unsigned char>(160)) {result.unavailable_reason=tr("The current main objective data is not available yet.");return result;}
            const auto* data=row.data()+8;
            if(pod<int>(data,16)!=*main_id||pod<unsigned char>(data,20)!=1) {
                result.unavailable_reason=tr("The active main objective could not be verified.");return result;
            }
            const auto progression=pod<unsigned char>(data,104);
            if(progression==3) {result.ready=true;result.unavailable_reason=tr("No current main-story objective is displayed.");return result;}
            if(progression!=1&&progression!=2) {result.unavailable_reason=tr("The current main objective is not active.");return result;}
            const auto name=clean(reinterpret_cast<const FText*>(data+24)->ToString());
            const auto subtitle=clean(reinterpret_cast<const FText*>(data+40)->ToString());
            chapter_title=name;
            result.objective=name.empty()||subtitle.empty()?name+subtitle:trf("{0}. {1}",{name,subtitle});
            result.objective_id="main-quest:"+std::to_string(*main_id)+"|name:"+std::to_string(name.size())+":"+name+
                "|subtitle:"+std::to_string(subtitle.size())+":"+subtitle;
            result.ready=true;
            points=quest_points(data);
            destination_title=clean(reinterpret_cast<const FText*>(data+56)->ToString());
            result.description=clean(reinterpret_cast<const FText*>(data+72)->ToString());
            // With no current filtered Goal, DestinationTitle is the journal's
            // unspecified-destination placeholder rather than a named target.
            if(points&&std::any_of(points->begin(),points->end(),[](const auto& point) {
                return point.role==0&&point.map_kind==0&&point.map_id>=0;
            }))result.destination_title=destination_title;
        }
        if(result.objective.empty()) {result.unavailable_reason=tr("No current main-story objective is displayed.");return result;}
        if(!points) {result.unavailable_reason=tr("The objective is available, but its filtered destinations could not be read safely.");return result;}
        result.destination_epoch=filtered_destination_epoch(std::span<const FilteredPoint>(*points));
        if(result.destination_epoch.empty()) {result.unavailable_reason=tr("The objective is available, but its filtered destinations could not be verified.");return result;}
        const auto map=current_map(world);
        if(!map) {result.unavailable_reason=tr("The current map could not be verified. Walking guidance is unavailable.");return result;}
        result.map_id=*map;
        result.main_progress=main_progress();
        try {
            if(chapter_six_shrines::capture(result,pawn,world,*map,*main_id))return result;
            if(capture_chapter_six_field_leg(result,pawn,world,*map,*main_id))return result;
            if(capture_chapter_six_lab(result,pawn,world,*map,*main_id,*points))return result;
            if(capture_chapter_seven_leg(result,pawn,world,game_instance,*map,*main_id))return result;
            if(capture_chapter_eight_leg(result,pawn,world,*map,*main_id))return result;
            if(capture_chapter_two_legs(result,pawn,world,*map,*main_id,*points))return result;
            if(capture_chapter_four_legs(result,pawn,world,*map,*main_id,*points))return result;
            if(capture_later_story_leg(result,pawn,world,game_instance,*map,*main_id,*points))return result;
        } catch(const std::exception& error) {
            static NativeProblemDeduplicator reported;
            if(reported.accept(error.what()))log(std::string("STORY INPUT next step unavailable: ")+error.what());
            result.destinations.clear();
            result.next_step.clear();
            result.crossing.reset();
            result.flight_destination.reset();
            result.destination_epoch=filtered_destination_epoch(std::span<const FilteredPoint>(*points));
            result.unavailable_reason=tr("The next local Story step could not be read safely.");
            return result;
        }
        if(points->empty()) {
            if(*map==2000&&*main_id==1001) {
                if(auto target=mitra_vision_event(pawn,world,*main_id)) {
                    result.objective=chapter_title.empty()?target->label:trf("{0}. {1}",{chapter_title,target->label});
                    result.objective_id+="|mitra-vision-talk";
                    result.destination_epoch+="|"+target->id+"|"+target->actor_id;
                    result.destinations.push_back(std::move(*target));
                    return result;
                }
            }
            capture_opening_story(result,[&](std::string& epoch) {
                return opening_destinations(pawn,world,game_instance,*map,*main_id,epoch,result.unavailable_reason);
            });
            if(result.destinations.empty()&&result.unavailable_reason.empty())result.unavailable_reason=tr((*map==10040||*map==10041)?
                "No enabled story continuation is available in this area.":"The game has not supplied a destination for this objective.");
            return result;
        }
        const auto areas=goal_areas(std::span<const FilteredPoint>(*points),native_group_maps);
        if(!areas) {
            static NativeProblemDeduplicator reported;
            if(reported.accept("map groups"))log("STORY INPUT destination map groups unavailable; related exits withheld");
        }
        const auto verified=areas?std::span<const GoalArea>(*areas):std::span<const GoalArea>{};
        result.destinations=area_destinations(pawn,world,*map,*main_id,*points,verified,destination_title);
        if(result.destinations.empty()) {
            if(*map==2000&&*main_id==1001&&std::any_of(points->begin(),points->end(),[](const auto& point) {
                // MainProgress_0070's filtered goal is the Royal Capital.
                return point.role==0&&point.map_kind==0&&point.map_id==2010&&!point.object_target;
            })) {
                if(const auto leg=capture_mitra_leg(pawn)) {
                    StoryDestination destination;
                    destination.actor_id=utf8(path(leg->actor));
                    destination.actor_type=leg->actor->GetClassPrivate()->GetNamePrivate().ToString();
                    destination.id="main-quest:1001:mitra-return:"+destination.actor_id;
                    destination.label=tr(leg->label);destination.position=leg->position;
                    destination.disclosed=destination.local=destination.actor_available=destination.exit=destination.mitra_leg=true;
                    result.destination_epoch+="|mitra:"+destination.actor_id;
                    result.destinations.push_back(std::move(destination));
                    return result;
                }
            }
            if(*map==3000&&points->size()==1) {
                const auto& goal=points->front();
                if(goal.role==0&&goal.map_kind==0&&goal.map_id==3001&&!goal.object_target) {
                    if(auto next=shrine_story_event(pawn,world,*main_id)) {
                        result.destination_epoch+="|"+next->id+"|"+next->actor_id;
                        result.destinations.push_back(std::move(*next));
                        return result;
                    }
                }
            }
            const bool elsewhere=std::any_of(points->begin(),points->end(),[](const auto& p){return p.role==0&&p.map_kind==0;})&&
                std::none_of(points->begin(),points->end(),[&](const auto& p){return p.role==0&&p.map_kind==0&&p.map_id==*map;});
            if(elsewhere&&*map==2014&&*main_id==1001) {
                if(const auto leg=capture_castle_leg(pawn)) {
                    StoryDestination destination;
                    destination.actor_id=utf8(path(leg->actor));
                    destination.actor_type=leg->actor->GetClassPrivate()->GetNamePrivate().ToString();
                    destination.id="main-quest:1001:castle:"+destination.actor_id;
                    destination.label=leg->label;destination.position=leg->position;
                    destination.disclosed=destination.local=destination.actor_available=destination.castle_leg=true;
                    result.destination_epoch+="|castle:"+destination.actor_id;
                    result.destinations.push_back(std::move(destination));
                    return result;
                }
            }
            if(destination_area_reached(*map,*points,verified)) {
                result.unavailable_reason=destination_area_reason(result.destination_title);
                return result;
            }
            result.unavailable_reason=tr(elsewhere?"The destination is in another area. An active current-area exit could not be verified.":
                "An active story target or exit could not be verified in this area.");
            if(elsewhere&&!destination_title.empty()) {
                StoryDestination destination;destination.id="main-quest:"+std::to_string(*main_id)+":destination";
                destination.label=destination_title;destination.area=destination_title;destination.disclosed=true;
                destination.unavailable_reason=trf("The destination is in {0}. An active current-area exit could not be verified.",{destination_title});
                result.destinations.push_back(std::move(destination));
            }
        }
    } catch(const std::exception& error) {
        static NativeProblemDeduplicator reported;
        if(reported.accept(error.what()))log(std::string("STORY INPUT capture unavailable: ")+error.what());
        fail_story_capture(result);
    }
    return result;
}
}
