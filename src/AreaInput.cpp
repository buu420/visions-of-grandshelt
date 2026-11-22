#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/AreaInput.hpp"
#include "ChapterTwoStory.hpp"
#include "ChapterThreeStory.hpp"
#include "ChapterFourStory.hpp"
#include "ChapterSixLab.hpp"
#include "resonance/Localization.hpp"
#include "resonance/NativeBoundary.hpp"
#include "resonance/NativeMetadata.hpp"
#include "resonance/StoryInput.hpp"
#include "resonance/WorldPlaceNames.hpp"
#include "AreaInputValidation.hpp"
#include "FieldLabels.hpp"
#include "TownInteractions.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <tuple>
#include <memory>
#include <unordered_set>
#include <utility>

// Exact-build evidence (Steam 24250053): area-navigation-fix-20261103/AREA-ENTITY-AUDIT.md,
// EXIT-AVAILABILITY.md and area-entity-facts.json. Every field read first matches its
// reflected offset and size; AActor bitfield masks are exact-build constants. Registries come
// from the unique current-world CPP_MapManager; no camera, render, line-of-sight or prior sight.
namespace resonance {
namespace {
using namespace resonance::unreal;
using namespace area_input_detail;
constexpr std::uint32_t dying_flags=0x8000u|0x10000u|0x40000000u;
constexpr std::uint32_t ordinary_excluded=0x10u|0x20u|dying_flags;
constexpr int registry_limit=4096;
// Latest capture_area_targets/capture_area_target only.
AreaCaptureReport scan_report;
std::string capture_summary;
// Latest story_actor_available/area_actor_available query only.
std::string actor_reason;
// Transitional area_capture_unavailable_reason(): whichever of the two ran last.
std::string legacy_reason;

template<class T> struct NativeArray { T* data; std::int32_t num,max; };
static_assert(sizeof(NativeArray<UObject*>)==16&&sizeof(Vec3)==24);
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
    int depth=0;
    for(auto* cls=static_cast<UStruct*>(object->GetClassPrivate());cls&&depth++<64;cls=cls->GetSuperStruct())
        if(path(cls)==type)return true;
    return false;
}
// Absent: the audited class is not in the chain. Mismatch: it is, with another layout.
enum class Shape { Absent, Exact, Mismatch };
Shape class_shape(UObject* object,std::wstring_view type,int size) {
    if(!object)return Shape::Absent;
    int depth=0;
    for(auto* cls=static_cast<UStruct*>(object->GetClassPrivate());cls&&depth++<64;cls=cls->GetSuperStruct())
        if(path(cls)==type)return cls->GetPropertiesSize()==size?Shape::Exact:Shape::Mismatch;
    return Shape::Absent;
}
// The audited native class must be in the chain with its exact PropertiesSize.
bool native_shape(UObject* object,std::wstring_view type,int size) {return live(object)&&class_shape(object,type,size)==Shape::Exact;}
const std::wstring& kind_path(AreaKind kind) {
    static const auto paths=[] {
        std::array<std::wstring,area_kinds.size()> result;
        for(std::size_t i=0;i<result.size();++i)result[i]=std::wstring(L"/Script/mucha.")+area_kinds[i].type;
        return result;
    }();
    return paths[static_cast<std::size_t>(kind)];
}
bool native_kind(UObject* object,AreaKind kind) {return native_shape(object,kind_path(kind),area_kind_info(kind).native_size);}
// One class-chain walk per live registry entry; the first audited kind in the chain wins.
// exact=false means the audited class is present with another layout (unreadable).
struct KindMatch { AreaKind kind; bool exact; };
std::optional<KindMatch> chain_kind(UObject* object,bool gimmicks) {
    int depth=0;
    for(auto* cls=static_cast<UStruct*>(object->GetClassPrivate());cls&&depth++<64;cls=cls->GetSuperStruct()) {
        const auto type=path(cls);
        for(const auto& info:area_kinds) {
            if(gimmick_kind(info.kind)!=gimmicks||type!=kind_path(info.kind))continue;
            return KindMatch{info.kind,cls->GetPropertiesSize()==info.native_size};
        }
    }
    return std::nullopt;
}
std::string utf8(const std::wstring& text) {
    if(text.empty()||text.size()>4096)return {};
    const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(length<=0)return {};
    std::string result(static_cast<std::size_t>(length),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),
        result.data(),length,nullptr,nullptr)!=length)return {};
    return result;
}
void* property(UObject* object,const wchar_t* name,int offset,int size) {
    if(!live(object))return nullptr;
    auto* field=object->GetPropertyByNameInChain(name);
    if(!field||field->GetOffset_Internal()!=offset||field->GetSize()!=size)return nullptr;
    return object->GetValuePtrByPropertyNameInChain(name);
}
template<class T> std::optional<T> read(UObject* object,const wchar_t* name,int offset) {
    auto* value=property(object,name,offset,static_cast<int>(sizeof(T)));if(!value)return std::nullopt;
    T result{};std::memcpy(&result,value,sizeof result);return result;
}
std::optional<std::uint8_t> byte_value(UObject* object,const wchar_t* name,int offset) {return read<std::uint8_t>(object,name,offset);}
UObject* object_value(UObject* object,const wchar_t* name,int offset) {
    auto value=read<UObject*>(object,name,offset);
    return value&&live(*value)?*value:nullptr;
}
UObject* blueprint_object_value(UObject* object,const wchar_t* name,int native_size) {
    if(!live(object))return nullptr;
    auto* field=object->GetPropertyByNameInChain(name);
    if(!field||!field->field_class()||field->field_class()->GetName()!=L"ObjectProperty"||field->GetSize()!=8)return nullptr;
    const auto offset=field->GetOffset_Internal();
    if(offset<native_size||offset>object->GetClassPrivate()->GetPropertiesSize()-8)return nullptr;
    return object_value(object,name,offset);
}
UObject* castle_inner_trigger(UObject* door) {
    if(!is(door,L"/Game/BP/Map/Gimmick/BP_MapGimmick_TriggerDoor.BP_MapGimmick_TriggerDoor_C"))return nullptr;
    return blueprint_object_value(door,L"InnerBox",1064);
}
std::optional<std::vector<UObject*>> object_array(UObject* owner,const wchar_t* name,int offset) {
    auto value=read<NativeArray<UObject*>>(owner,name,offset);
    if(!value||!valid_array(value->data,value->num,value->max,registry_limit))return std::nullopt;
    if(value->num==0)return std::vector<UObject*>{};
    return std::vector<UObject*>(value->data,value->data+value->num);
}
UObject* level_world(UObject* level) {
    auto* world=object_value(level,L"OwningWorld",0xC0);
    return world&&is(world,L"/Script/Engine.World")?world:nullptr;
}
UObject* actor_world(UObject* actor) {
    if(!live(actor)||!is(actor,L"/Script/Engine.Actor"))return nullptr;
    auto* level=actor->GetOuterPrivate();
    return live(level)&&is(level,L"/Script/Engine.Level")?level_world(level):nullptr;
}
// A dying or worldless level means the actor left this world; an outer that is not a
// level, or an unreadable OwningWorld, cannot be verified either way.
std::optional<bool> in_world(UObject* actor,UObject* world) {
    auto* level=actor->GetOuterPrivate();
    if(!live(level))return false;
    if(!is(level,L"/Script/Engine.Level"))return std::nullopt;
    const auto owning=read<UObject*>(level,L"OwningWorld",0xC0);
    if(!owning)return std::nullopt;
    return *owning==world&&live(*owning);
}
// WorldSubsystems are outered to their UWorld; levels resolve through OwningWorld.
UObject* object_world(UObject* object) {
    for(int depth=0;depth<16&&live(object);++depth,object=object->GetOuterPrivate()) {
        if(is(object,L"/Script/Engine.World"))return object;
        if(is(object,L"/Script/Engine.Level"))return level_world(object);
    }
    return nullptr;
}

class Getter {
    UFunction* function_{};
    FProperty* result_{};
    int result_offset_{};
    alignas(16) std::array<std::byte,512> storage_{};
public:
    // Return value last: consumed end = return offset + size (native_call_extent).
    Getter(UObject* receiver,const wchar_t* name,int allocation,int return_offset,int return_size) {
        if(!live(receiver))return;
        auto* function=receiver->GetFunctionByNameInChain(name);if(!function)return;
        const bool native=function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400));
        if(!getter_call_extent(native,function->GetParmsSize(),function->GetPropertiesSize(),allocation,
            function->GetReturnValueOffset(),return_offset,return_offset+return_size,static_cast<int>(storage_.size())))return;
        auto* result=function->GetReturnProperty();
        if(!result||result->GetOffset_Internal()!=return_offset||result->GetSize()!=return_size||
           !result->HasAnyPropertyFlags(0x80)||!result->HasAnyPropertyFlags(0x400))return;
        result->InitializeValue(storage_.data()+return_offset);
        function_=function;result_=result;result_offset_=return_offset;
    }
    ~Getter() {if(result_)result_->DestroyValue(storage_.data()+result_offset_);}
    Getter(const Getter&)=delete;Getter& operator=(const Getter&)=delete;
    explicit operator bool()const {return function_!=nullptr;}
    UFunction* function()const {return function_;}
    template<class T> void put(std::size_t offset,const T& value) {std::memcpy(storage_.data()+offset,&value,sizeof value);}
    template<class T> T call(UObject* receiver) {
        receiver->ProcessEvent(function_,storage_.data());
        T value{};std::memcpy(&value,storage_.data()+result_offset_,sizeof value);return value;
    }
};
bool sight_argument(UFunction* function,const wchar_t* name,int offset,int size,const wchar_t* type) {
    auto* field=function->FindProperty(FName(name,FNAME_Find));
    return field&&field->GetOffset_Internal()==offset&&field->GetSize()==size&&field->field_class()&&
        field->field_class()->GetName()==type&&field->HasAnyPropertyFlags(0x80);
}
std::optional<Vec3> box_vector(UObject* box,const wchar_t* name,const wchar_t* function_path) {
    Getter call(box,name,24,0,24);
    if(!call||path(call.function())!=function_path)return {};
    auto* result=call.function()->GetReturnProperty();
    if(!result->field_class()||result->field_class()->GetName()!=L"StructProperty")return {};
    return call.call<Vec3>(box);
}
std::optional<ActivationBox> owned_navigation_box(UObject* actor,UObject* box,double tilt_limit=0.,
        bool vertical=false) {
    if(!live(actor)||!live(box)||!is(box,L"/Script/Engine.BoxComponent"))return {};
    Getter owner(box,L"GetOwner",8,0,8);
    if(!owner||path(owner.function())!=L"/Script/Engine.ActorComponent:GetOwner"||owner.call<UObject*>(box)!=actor)return {};
    const auto center=box_vector(box,L"K2_GetComponentLocation",L"/Script/Engine.SceneComponent:K2_GetComponentLocation");
    const auto extent=box_vector(box,L"GetScaledBoxExtent",L"/Script/Engine.BoxComponent:GetScaledBoxExtent");
    const auto rotation=box_vector(box,L"K2_GetComponentRotation",L"/Script/Engine.SceneComponent:K2_GetComponentRotation");
    if(!center||!extent||!rotation||!std::isfinite(rotation->x)||!std::isfinite(rotation->z))return {};
    Vec3 half=*extent;
    if(vertical&&std::abs(std::abs(std::remainder(rotation->x,360.))-90.)<1e-4&&
       std::abs(std::remainder(rotation->z,180.))<1e-4) {
        // A right-angle pitch swaps the horizontal and vertical extents.
        std::swap(half.x,half.z);
    } else if(std::abs(rotation->x)>1e-4||std::abs(rotation->z)>1e-4) {
        const double tilt=std::abs(std::remainder(rotation->x,360.))+std::abs(std::remainder(rotation->z,360.));
        if(tilt_limit<=0.||tilt>tilt_limit)return {};
        // Inset by the maximum displacement of any corner under the tilt.
        // The yaw-only box then lies entirely inside the real overlap volume.
        constexpr double degrees_to_radians=3.14159265358979323846/180.;
        const auto inset=2.*std::sin(tilt*degrees_to_radians*.5)*std::hypot(half.x,half.y,half.z);
        half={half.x-inset,half.y-inset,half.z-inset};
    }
    const ActivationBox result{*center,half,rotation->y};
    return closest_activation_point(result,*center)?std::optional<ActivationBox>{result}:std::nullopt;
}
std::optional<std::uint8_t> byte_call(UObject* object,const wchar_t* name) {
    Getter getter(object,name,1,0,1);
    return getter?std::optional<std::uint8_t>(getter.call<std::uint8_t>(object)):std::nullopt;
}
std::optional<bool> bool_call(UObject* object,const wchar_t* name) {return native_bool(byte_call(object,name));}
std::optional<float> reflected_alpha(UObject* object,const wchar_t* name,int offset) {
    const auto color=read<std::array<float,4>>(object,name,offset);
    return color?std::optional<float>{(*color)[3]}:std::nullopt;
}
UObject* widget_parent(UObject* widget) {
    const auto slot=read<UObject*>(widget,L"Slot",48);
    if(!slot)return nullptr;
    if(*slot) {
        if(!native_shape(*slot,L"/Script/UMG.PanelSlot",56)||object_value(*slot,L"Content",48)!=widget)return nullptr;
        auto* parent=object_value(*slot,L"Parent",40);
        if(!is(parent,L"/Script/UMG.PanelWidget"))return nullptr;
        const auto slots=read<NativeArray<UObject*>>(parent,L"Slots",360);
        if(!slots||!valid_array(slots->data,slots->num,slots->max,128)||!slots->num)return nullptr;
        return std::find(slots->data,slots->data+slots->num,*slot)!=slots->data+slots->num?parent:nullptr;
    }
    auto* tree=widget->GetOuterPrivate();
    if(!native_shape(tree,L"/Script/UMG.WidgetTree",136)||object_value(tree,L"RootWidget",48)!=widget)return nullptr;
    auto* parent=tree->GetOuterPrivate();
    return native_shape(parent,L"/Script/UMG.UserWidget",832)&&object_value(parent,L"WidgetTree",728)==tree?parent:nullptr;
}
bool presented_widget_chain(UObject* widget,UObject* root) {
    for(int depth=0;depth<16&&live(widget)&&is(widget,L"/Script/UMG.Widget");++depth,widget=widget_parent(widget)) {
        if(!widget_presented(byte_value(widget,L"Visibility",220),read<float>(widget,L"RenderOpacity",224)))return false;
        if(is(widget,L"/Script/UMG.UserWidget")&&!positive_ui_alpha(reflected_alpha(widget,L"ColorAndOpacity",416)))return false;
        if(widget==root)return true;
    }
    return false;
}
// The area list stays available off camera. A service role comes only from the
// NPC's own displayed overhead brush (npc-label-audit-20261105).
const char* presented_npc_role(UObject* npc) {
    constexpr auto generic="";
    auto* head=blueprint_object_value(npc,L"BP_ObjectHeadComponent",2912);
    if(!native_shape(head,L"/Script/mucha.CPP_WC_ObjectHead",1776)||head->GetOuterPrivate()!=npc)return generic;
    Getter owner(head,L"GetOwner",8,0,8);
    if(!owner||path(owner.function())!=L"/Script/Engine.ActorComponent:GetOwner"||owner.call<UObject*>(head)!=npc)return generic;
    auto* vm=object_value(head,L"ViewModel",1752);
    if(!native_shape(vm,L"/Script/mucha.CPP_VM_ObjectHead",208)||native_bool(byte_value(vm,L"bIsDisplay",120))!=true)return generic;
    auto* layout=object_value(head,L"Widget",1688);
    if(!is(layout,L"/Game/UI/Field/Object/Head/Widget/WBP_ObjectHeadLayout.WBP_ObjectHeadLayout_C"))return generic;
    auto* border=blueprint_object_value(layout,L"Border_Content",832);
    if(!is(border,L"/Script/UMG.Border"))return generic;
    Getter content(border,L"GetContent",8,0,8);
    if(!content||path(content.function())!=L"/Script/UMG.ContentWidget:GetContent")return generic;
    auto* standard=content.call<UObject*>(border);
    if(!live(standard)||!is(standard,L"/Game/UI/Field/Object/Head/Widget/WBP_ObjectHead_Standard.WBP_ObjectHead_Standard_C"))return generic;
    auto* brush=blueprint_object_value(standard,L"CPP_WBP_GameplayTagBrush_26",832);
    if(!native_shape(brush,L"/Script/mucha.CPP_WBP_GameplayTagBrush",464))return generic;
    // Native brush apply 0x1451ec8d0 sets bIsValidBrush only after installing
    // a matching Slate image. This byte is unreflected in the exact build.
    if(*(reinterpret_cast<const std::uint8_t*>(brush)+0x1b8)!=1)return generic;
    auto* table=object_value(brush,L"BrushTable",368);
    if(path(table)!=L"/Game/UI/Field/Object/Head/Data/DT_ObjectHeadImage.DT_ObjectHeadImage")return generic;
    auto tag=read<FName>(brush,L"BrushTag",392);
    const auto* head_data=property(vm,L"HeadData",160,40);
    if(!tag||!tag->ComparisonIndex||!head_data||std::memcmp(&*tag,head_data,sizeof(FName))!=0)return generic;
    const auto hidden=byte_value(head,L"bHiddenInGame",417);
    if(!hidden||masked(*hidden,0x08)||byte_value(head,L"Space",1408)!=0||
       bool_call(head,L"IsVisible")!=true||!positive_ui_alpha(reflected_alpha(head,L"TintColorAndOpacity",1512))||
       !presented_widget_chain(brush,layout))return generic;
    Getter rendered(head,L"WasRecentlyRendered",8,4,1);
    if(!rendered||path(rendered.function())!=L"/Script/Engine.PrimitiveComponent:WasRecentlyRendered")return generic;
    rendered.put(0,0.1f);
    return person_service_label(tag->ToString(),native_bool(rendered.call<std::uint8_t>(head))==true);
}
const char* person_label(UObject* npc) {
    if(const auto* role=presented_npc_role(npc);*role)return role;
    if(const auto* role=person_service_label(byte_value(npc,L"m_MapInteractType",2862));*role)return role;
    constexpr std::wstring_view deck=L"/Game/Map/Town/00Com/00Com_03/EV/Town_00Com_03_01_EV."
        L"Town_00Com_03_01_EV:PersistentLevel.";
    const auto actor_path=path(npc);
    if(actor_path.starts_with(deck)) {
        const auto placement=std::wstring_view(actor_path).substr(deck.size());
        if(placement==L"BP_MapUnit_NPC_C_6")return "Aileen";
        if(placement==L"BP_MapUnit_NPC_C_7")return "Gilgamesh";
    }
    if(actor_path==L"/Game/Map/Town/08Gro/08Gro_01/EV/Town_08Gro_01_01_EV.Town_08Gro_01_01_EV:"
        L"PersistentLevel.BP_MapUnit_NPC_C_18")return "Fryevia";
    return npc_appearance_label(read<int>(npc,L"m_MapUnitId",2048).value_or(-1));
}
std::optional<Vec3> vector_call(UObject* object,const wchar_t* name) {
    Getter getter(object,name,24,0,24);if(!getter)return std::nullopt;
    const auto value=getter.call<Vec3>(object);
    return finite_position(value)?std::optional<Vec3>(value):std::nullopt;
}
// UPrimitiveComponent::GetCollisionResponseToChannel: Channel u8 @0, return u8 @1.
std::optional<std::uint8_t> response_to(UObject* component,std::uint8_t channel) {
    Getter getter(component,L"GetCollisionResponseToChannel",2,1,1);if(!getter)return std::nullopt;
    getter.put(0,channel);return getter.call<std::uint8_t>(component);
}
// Registered NPCs may have a live root while CharacterMovement::UpdatedComponent
// is detached. This build's GetNavAgentLocation then dereferences null in
// GetActorFeetLocation. Tracking needs the actor's current transform, not its
// movement agent; a missing root must stay unknown rather than become (0,0,0).
std::optional<Vec3> navigation_position(UObject* actor) {
    auto* root=object_value(actor,L"RootComponent",440);
    if(!root||!is(root,L"/Script/Engine.SceneComponent"))return std::nullopt;
    return vector_call(actor,L"K2_GetActorLocation");
}

ActorFacts actor_facts(UObject* actor,UObject* world) {
    ActorFacts facts;facts.live=live(actor);
    // A live non-actor has no readable world or flags: it stays unknown, never available.
    if(!facts.live||!is(actor,L"/Script/Engine.Actor"))return facts;
    facts.same_world=world?in_world(actor,world):std::nullopt;
    if(const auto bits=byte_value(actor,L"bActorIsBeingDestroyed",0x5D))facts.destroying=masked(*bits,destroying_mask);
    if(const auto bits=byte_value(actor,L"bHidden",0x58))facts.hidden=masked(*bits,hidden_mask);
    return facts;
}
PersonFacts person_facts(UObject* npc,UObject* world) {
    PersonFacts facts{actor_facts(npc,world)};
    if(actor_verdict(facts.actor).state==Availability::Inactive)return facts;
    facts.unit_visible=native_bool(byte_value(npc,L"m_IsVisible",0x678));
    facts.tick_enabled=bool_call(npc,L"IsActorTickEnabled");
    return facts;
}
// CPP_Unit (all map units, 1856 bytes) and CPP_MapUnit_Object (an Actor, 1256 bytes) own a
// native bool m_IsVisible. Other registered actors use bHidden alone. No tick read.
StoryActorFacts story_facts(UObject* actor,UObject* world) {
    StoryActorFacts facts{actor_facts(actor,world)};
    if(removed_or_foreign(facts.actor))return facts;
    if(const auto unit=class_shape(actor,L"/Script/mucha.CPP_Unit",1856);unit!=Shape::Absent) {
        facts.flagged=true;
        if(unit==Shape::Exact)facts.visible=native_bool(byte_value(actor,L"m_IsVisible",1656));
    } else if(const auto object=class_shape(actor,L"/Script/mucha.CPP_MapUnit_Object",1256);object!=Shape::Absent) {
        facts.flagged=true;
        if(object==Shape::Exact)facts.visible=native_bool(byte_value(actor,L"m_IsVisible",848));
    }
    facts.positioned=navigation_position(actor).has_value();
    return facts;
}
class NativeValue {
    FProperty* property_;void* value_;
public:
    NativeValue(FProperty* property,void* value):property_(property),value_(value){property_->InitializeValue(value_);}
    ~NativeValue(){property_->DestroyValue(value_);}
    NativeValue(const NativeValue&)=delete;NativeValue& operator=(const NativeValue&)=delete;
};
template<class T> T pod(const std::byte* source,std::size_t offset) {
    T value{};std::memcpy(&value,source+offset,sizeof value);return value;
}
std::optional<bool> native_condition(const std::byte* source) {
    const auto condition=pod<NativeArray<wchar_t>>(source,0);
    if(!valid_array(condition.data,condition.num,condition.max,2048))return {};
    if(condition.num<2)return true; // Matches the native resolver's short-string rule.
    if(condition.data[condition.num-1]!=L'\0')return {};
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/mucha.Default__CPP_BFL_Condition");
    if(!library||path(library)!=L"/Script/mucha.Default__CPP_BFL_Condition"||
       (static_cast<std::uint32_t>(library->GetObjectFlags())&dying_flags)||
       !(static_cast<std::uint32_t>(library->GetObjectFlags())&0x10u)||
       !native_metadata(library->GetClassPrivate(),NativeMetadataKind::Class)||
       path(library->GetClassPrivate())!=L"/Script/mucha.CPP_BFL_Condition")return {};
    auto* function=library->GetFunctionByNameInChain(L"IsMeetCondition");
    if(!function||path(function)!=L"/Script/mucha.CPP_BFL_Condition:IsMeetCondition"||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400))||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x2000))||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(1))||
       !getter_call_extent(true,function->GetParmsSize(),function->GetPropertiesSize(),24,
           function->GetReturnValueOffset(),17,18,24))return {};
    auto* input=function->FindProperty(FName(L"InCondition",FNAME_Find));
    auto* print=function->FindProperty(FName(L"InIsPrintScreen",FNAME_Find));
    auto* returned=function->GetReturnProperty();
    if(!input||input->GetOffset_Internal()!=0||input->GetSize()!=16||!input->HasAnyPropertyFlags(0x80)||
       !print||print->GetOffset_Internal()!=16||print->GetSize()!=1||!print->HasAnyPropertyFlags(0x80)||
       !returned||returned->GetOffset_Internal()!=17||returned->GetSize()!=1||
       !returned->HasAnyPropertyFlags(0x80)||!returned->HasAnyPropertyFlags(0x400))return {};
    alignas(16) std::array<std::byte,24> storage{};
    NativeValue own_input(input,storage.data()),own_return(returned,storage.data()+17);
    input->CopyCompleteValue(storage.data(),source);
    // InIsPrintScreen remains false. This read-only exact-build evaluator is
    // the one called by the native resolver; never call the mutating resolver.
    library->ProcessEvent(function,storage.data());
    return native_bool(pod<std::uint8_t>(storage.data(),17));
}
bool transition_array(UFunction* function) {
    if(!function||path(function)!=L"/Script/mucha.CPP_MapTransitionTrigger:GetTransitionDataList")return false;
    auto* field=function->GetReturnProperty();if(!field||field->GetSize()!=16||field->GetOffset_Internal()!=0)return false;
    auto* inner=static_cast<FArrayProperty*>(field)->GetInner();
    if(!inner||inner->GetSize()!=136||inner->GetOffset_Internal()!=0)return false;
    auto& handle=static_cast<FStructProperty*>(inner)->GetStruct();UScriptStruct* type{};
    std::memcpy(&type,std::addressof(handle),sizeof type);
    if(!native_metadata(type,NativeMetadataKind::ScriptStruct)||path(type)!=L"/Script/mucha.MAP_TRANSITION_TRIGGER_DATA"||
       type->GetPropertiesSize()!=136)return false;
    for(const auto& [name,offset,size]:{std::tuple{L"FlagCondition",0,16},std::tuple{L"isEventOnly",16,1},
            std::tuple{L"mapId",20,4},std::tuple{L"pointId",24,4}}) {
        auto* member=type->FindProperty(FName(name,FNAME_Find));
        if(!member||member->GetOffset_Internal()!=offset||member->GetSize()!=size)return false;
    }
    return true;
}
UObject* map_manager(UObject* world) {
    std::vector<UObject*> all;UObjectGlobals::FindAllOf(L"CPP_MapManager",all);
    UObject* found=nullptr;
    for(auto* candidate:all) {
        if(!native_shape(candidate,L"/Script/mucha.CPP_MapManager",1256)||object_world(candidate)!=world)continue;
        if(found)return nullptr;
        found=candidate;
    }
    return found;
}
// The current map id is read at most once per capture or query, and only when a
// same-map exit needs it. Exact Ghidra 145055070: current-world MapManager+0x30.
class MapContext {
    UObject* world_;UObject* manager_;bool resolved_{};std::optional<int> current_;
public:
    explicit MapContext(UObject* world,UObject* manager=nullptr):world_(world),manager_(manager){}
    UObject* world()const {return world_;}
    std::optional<int> current_map() {
        if(resolved_)return current_;
        resolved_=true;
        auto* manager=manager_?manager_:map_manager(world_);
        if(manager&&property(manager,L"m_MapTransitionPointList",504,16)) {
            const int id=pod<int>(reinterpret_cast<const std::byte*>(manager),48);
            if(id>=0)current_=id;
        }
        return current_;
    }
};
TransitionResult effective_transition(UObject* trigger,MapContext& map) {
    TransitionDefaults defaults;
    defaults.point_id=read<int>(trigger,L"m_PointID",780);
    defaults.use_conditions=native_bool(byte_value(trigger,L"m_IsUseCondion",745));
    defaults.same_map=native_bool(byte_value(trigger,L"m_IsSameMapTrasition",744));
    defaults.event_only=native_bool(byte_value(trigger,L"m_IsEventOnly",768));
    defaults.map_id=read<int>(trigger,L"m_MapId",772);
    if(!defaults.use_conditions||!defaults.same_map)return unread_transition("current availability could not be read");
    if(*defaults.same_map&&defaults.event_only==false)defaults.current_map=map.current_map();
    if(!*defaults.use_conditions||*defaults.same_map)
        return select_transition(defaults,std::span<const TransitionRow>{},[](std::size_t)->std::optional<bool>{return {};});
    Getter list(trigger,L"GetTransitionDataList",16,0,16);
    if(!list||!transition_array(list.function()))return unread_transition("conditional destinations could not be read");
    const auto array=list.call<NativeArray<std::byte>>(trigger);
    if(!valid_array(array.data,array.num,array.max,512))return unread_transition("conditional destination list is unreadable");
    std::vector<TransitionRow> rows;
    for(int i=0;i<array.num;++i) {
        const auto* row=array.data+static_cast<std::size_t>(i)*136;
        rows.push_back({native_bool(pod<std::uint8_t>(row,16)),pod<int>(row,20),pod<int>(row,24)});
    }
    return select_transition(defaults,rows,[&](std::size_t i){return native_condition(array.data+i*136);});
}
// Owned root box and player capsule, both sides' collision, overlap generation, query
// modes and responses to each other's channel. Any unreadable or out-of-range value
// (ECollisionEnabled 0..5, ECollisionResponse 0..2) is unknown, not disabled.
std::optional<OverlapEvidence> overlap_facts(UObject* pawn,UObject* trigger,UObject* box=nullptr) {
    if(!box)box=object_value(trigger,L"m_pRootBoxComponent",720);
    auto* capsule=object_value(pawn,L"CapsuleComponent",824);
    if(!box||!is(box,L"/Script/Engine.BoxComponent")||!capsule||!is(capsule,L"/Script/Engine.CapsuleComponent"))return std::nullopt;
    const auto owner=[](UObject* component) {
        Getter getter(component,L"GetOwner",8,0,8);
        return getter?getter.call<UObject*>(component):nullptr;
    };
    if(owner(box)!=trigger||owner(capsule)!=pawn)return std::nullopt;
    const auto enabled=bool_call(trigger,L"GetActorEnableCollision"),player_enabled=bool_call(pawn,L"GetActorEnableCollision");
    const auto generates=bool_call(box,L"GetGenerateOverlapEvents"),player_generates=bool_call(capsule,L"GetGenerateOverlapEvents");
    const auto mode=byte_call(box,L"GetCollisionEnabled"),player_mode=byte_call(capsule,L"GetCollisionEnabled");
    const auto channel=byte_call(box,L"GetCollisionObjectType"),pawn_channel=byte_call(capsule,L"GetCollisionObjectType");
    if(!enabled||!player_enabled||!generates||!player_generates||!mode||!player_mode||!channel||!pawn_channel||
       *channel>=32||*pawn_channel>=32||*mode>5||*player_mode>5)return std::nullopt;
    const auto response=response_to(box,*pawn_channel),player_response=response_to(capsule,*channel);
    if(!response||!player_response||*response>2||*player_response>2)return std::nullopt;
    return OverlapEvidence{true,*enabled&&*player_enabled,*generates,*player_generates,*mode,*player_mode,*response,*player_response};
}
ExitFacts exit_facts(UObject* trigger,MapContext& map,UObject* pawn) {
    ExitFacts facts{actor_facts(trigger,map.world())};
    if(removed_or_foreign(facts.actor))return facts;
    facts.transition=effective_transition(trigger,map);
    if(facts.transition.state!=Availability::Inactive)facts.overlap=overlap_facts(pawn,trigger);
    return facts;
}
const MitraPassage* mitra_exit_passage(UObject* trigger,const EffectiveTransition& transition) {
    const auto id=read<int>(trigger,L"m_UniqueId",1040);
    return id&&transition.point_id?mitra_passage(*id,transition.map_id,*transition.point_id):nullptr;
}
const MitraDoor* mitra_door(UObject* actor) {
    if(!native_kind(actor,AreaKind::Door))return nullptr;
    constexpr std::wstring_view prefix=L"/Game/Map/Town/01Gra/01Gra_20/GD/Town_01Gra_20_GD.Town_01Gra_20_GD:PersistentLevel.BP_MapGimmick_Object_Door_Single_C_";
    const auto id=path(actor);
    if(!id.starts_with(prefix))return nullptr;
    const auto suffix=std::wstring_view(id).substr(prefix.size());
    for(const auto& door:mitra_doors)if(door.suffix==suffix)return &door;
    return nullptr;
}
bool siren_tower_door(UObject* actor) {
    return native_kind(actor,AreaKind::Door)&&read<int>(actor,L"m_UniqueId",808)==12&&
        path(actor)==L"/Game/Map/Dng/02Lan/02Lan_40/GD/Dng_02Lan_40_01_GD.Dng_02Lan_40_01_GD:"
            L"PersistentLevel.BP_MapGimmick_Object_Door002_C_1";
}
const SirenLiftPlacement* siren_lift(UObject* actor) {
    return native_kind(actor,AreaKind::AutomaticLift)?
        siren_lift_placement(path(actor),read<int>(actor,L"m_UniqueId",808).value_or(-1)):nullptr;
}
bool mitra_mailbox(UObject* actor) {
    return native_kind(actor,AreaKind::Mailbox)&&path(actor)==
        L"/Game/Map/Town/01Gra/01Gra_20/GD/Town_01Gra_20_GD.Town_01Gra_20_GD:PersistentLevel.BP_MapGimmick_DLC_C_1";
}
std::optional<std::uint8_t> mailbox_collision(UObject* actor) {
    const auto collision=bool_call(actor,L"GetActorEnableCollision");
    if(!collision)return {};
    if(!*collision)return std::uint8_t{0};
    auto* box=object_value(actor,L"m_pRootBoxComponent",760);
    if(!is(box,L"/Script/Engine.BoxComponent"))return {};
    Getter owner(box,L"GetOwner",8,0,8);
    if(!owner||owner.call<UObject*>(box)!=actor)return {};
    return byte_call(box,L"GetCollisionEnabled");
}
constexpr auto library_book_class=
    L"/Game/BP/Map/Gimmick/Child/BlockObject/BP_MapGimmick_MagicBook.BP_MapGimmick_MagicBook_C";
bool library_book(UObject* actor) {return is(actor,library_book_class);}
bool placed_pot(UObject* actor) {
    return exact_placement_label(AreaKind::Book,path(actor),read<int>(actor,L"m_UniqueId",808).value_or(-1));
}
const town_interactions::Placement* town_interaction(UObject* actor) {
    if(!native_kind(actor,AreaKind::Interaction))return nullptr;
    return town_interactions::find(path(actor),read<int>(actor,L"m_UniqueId",808).value_or(-1));
}
bool mysidia_warp(UObject* actor) {
    if(!native_shape(actor,L"/Script/mucha.CPP_MapGimmick_Warp",1368))return false;
    const auto id=read<int>(actor,L"m_UniqueId",808);
    const auto name=path(actor);
    constexpr std::wstring_view level=L"/Game/Map/Town/07Mys/07Mys_01/GD/Town_07Mys_01_01_GD.Town_07Mys_01_01_GD:PersistentLevel.";
    if(!name.starts_with(level))return false;
    const auto placement=std::wstring_view(name).substr(level.size());
    return (id==14&&placement==L"BP_MapGimmick_Warp_Child01_C_1")||
           (id==15&&placement==L"BP_MapGimmick_Warp_Child01_C_5");
}
struct MysidiaWarpEnd {
    ActivationBox box;
    int side;
    bool goes_up;
};
constexpr std::string_view warp_key_prefix="|warp=";
std::optional<MysidiaWarpEnd> mysidia_warp_end(UObject* actor,UObject* pawn);
std::optional<bool> visible_owned_mesh(UObject* actor,UObject* mesh) {
    if(!is(mesh,L"/Script/Engine.StaticMeshComponent"))return {};
    Getter owner(mesh,L"GetOwner",8,0,8);
    if(!owner||path(owner.function())!=L"/Script/Engine.ActorComponent:GetOwner"||owner.call<UObject*>(mesh)!=actor)return {};
    const auto visible=byte_value(mesh,L"bVisible",416),hidden=byte_value(mesh,L"bHiddenInGame",417);
    if(!visible||!hidden)return {};
    return masked(*visible,0x20)&&!masked(*hidden,0x08)&&object_value(mesh,L"StaticMesh",1400);
}
std::span<const wchar_t* const> trigger_door_meshes(UObject* actor) {
    static constexpr const wchar_t* paired[]{L"DoorMesh1",L"DoorMesh2"};
    static constexpr const wchar_t* sliding[]{L"LeftDoorMesh",L"RightDoorMesh"};
    static constexpr const wchar_t* single[]{L"StaticMesh"};
    if(is(actor,L"/Game/BP/Map/Gimmick/BP_MapGimmick_TriggerDoor.BP_MapGimmick_TriggerDoor_C"))return paired;
    if(is(actor,L"/Game/BP/Map/Gimmick/BP_MapGimmick_TriggerDoor_Slid.BP_MapGimmick_TriggerDoor_Slid_C"))return sliding;
    if(is(actor,L"/Game/BP/Map/Gimmick/BP_MapGimmick_Trigger_Door_Single.BP_MapGimmick_Trigger_Door_Single_C"))return single;
    return {};
}
std::optional<bool> door_mesh_visible(UObject* actor,std::span<const wchar_t* const> fields) {
    bool unreadable=false;
    for(auto field:fields) {
        const auto visible=visible_owned_mesh(actor,blueprint_object_value(actor,field,1064));
        if(visible==true)return true;
        unreadable|=!visible.has_value();
    }
    return unreadable?std::nullopt:std::optional(false);
}
std::optional<bool> interaction_box_enabled(UObject* actor,UObject* component) {
    const auto mode=byte_call(component,L"GetCollisionEnabled");
    const auto overlap=bool_call(component,L"GetGenerateOverlapEvents"),collision=bool_call(actor,L"GetActorEnableCollision");
    if(!mode||*mode>5||!overlap||!collision)return {};
    return query_collision(*mode)&&*overlap&&*collision;
}
std::optional<ActivationBox> queryable_interaction_box(UObject* actor,UObject* component) {
    if(interaction_box_enabled(actor,component)!=true)return {};
    return owned_navigation_box(actor,component);
}
Verdict interactive_prop(UObject* actor,UObject* world,AreaKind kind) {
    if(kind==AreaKind::ElevatorCall) {
        auto* elevator=object_value(actor,L"m_pElevator",1016);
        if(!native_shape(elevator,L"/Script/mucha.CPP_MapGimmick_FloorBase",1136)||actor_world(elevator)!=world)
            return inactive(Rule::Conditional);
    }
    auto* mesh=kind==AreaKind::Elevator?object_value(actor,L"m_pStaticMeshComponent",1144):
        object_value(actor,L"m_pMeshComponent",984);
    // Elevator_Interact binds its prompt callbacks to ActiveBox. The platform
    // disables that box while it moves.
    auto* component=kind==AreaKind::Elevator?object_value(actor,L"m_pActiveBoxComponent",1504):
        object_value(actor,L"m_pRootBoxComponent",760);
    // Pearly Pots register their own prompt while the native interact mode is None.
    const bool pearly_pot=kind==AreaKind::Book&&
        path(actor).starts_with(L"/Game/Map/Dng/10Cry/10Cry_40/GD/")&&placed_pot(actor);
    const std::uint8_t interact_mode=pearly_pot?0:4;
    const auto verdict=interactive_prop_verdict(visible_owned_mesh(actor,mesh),byte_call(actor,L"GetGimmickCondition"),
        byte_value(actor,L"m_MapInteractType",833),interaction_box_enabled(actor,component),interact_mode);
    if(verdict.state==Availability::Available&&!owned_navigation_box(actor,component))
        return unknown("interaction box geometry could not be read");
    return verdict;
}
std::optional<bool> mailbox_has_delivery() {
    constexpr auto library_path=L"/Script/mucha.Default__CPP_BFL_SaveDataDLC";
    auto* library=UObjectGlobals::StaticFindObject(library_path);
    if(!library||path(library)!=library_path||
       (static_cast<std::uint32_t>(library->GetObjectFlags())&dying_flags)||
       !(static_cast<std::uint32_t>(library->GetObjectFlags())&0x10u)||
       !native_metadata(library->GetClassPrivate(),NativeMetadataKind::Class)||
       path(library->GetClassPrivate())!=L"/Script/mucha.CPP_BFL_SaveDataDLC")return {};
    auto* function=library->GetFunctionByNameInChain(L"IsAllDLCReceivingEnabled");
    if(!function||path(function)!=L"/Script/mucha.CPP_BFL_SaveDataDLC:IsAllDLCReceivingEnabled"||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400))||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x2000))||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(1))||
       !getter_call_extent(true,function->GetParmsSize(),function->GetPropertiesSize(),1,
           function->GetReturnValueOffset(),0,1,1))return {};
    auto* returned=function->GetReturnProperty();
    if(!returned||returned->GetOffset_Internal()!=0||returned->GetSize()!=1||
       !returned->HasAnyPropertyFlags(0x80)||!returned->HasAnyPropertyFlags(0x400)||
       !returned->field_class()||returned->field_class()->GetName()!=L"BoolProperty")return {};
    // Exact-build Ghidra 145121ab0 reads current reward and receipt flags.
    // The visible shell can persist briefly while the final pickup closes.
    std::uint8_t result{};
    NativeValue value(returned,&result);
    library->ProcessEvent(function,&result);
    return native_bool(result);
}
// CPP_MapLandmarkPoint.TransitionTrigger @0x320: unreadable is unknown, null or dead is no exit.
struct Link { std::optional<bool> linked; ExitFacts transition; UObject* actor{}; };
Link landmark_link(UObject* landmark,MapContext& map,UObject* pawn) {
    Link link;
    const auto value=read<UObject*>(landmark,L"TransitionTrigger",0x320);
    if(!value)return link;
    if(!*value||!live(*value)) {link.linked=false;return link;}
    if(class_shape(*value,kind_path(AreaKind::Exit),area_kind_info(AreaKind::Exit).native_size)!=Shape::Exact)return link;
    link.linked=true;link.transition=exit_facts(*value,map,pawn);link.actor=*value;
    return link;
}

const char* authored_transition_label(int map_id,UObject* actor,const EffectiveTransition& transition) {
    if(transition.point_id) {
        if(map_id==2000) {
            if(const auto* passage=mitra_exit_passage(actor,transition))return passage->label;
        } else if(map_id==3000&&transition.map_id==3000) {
            const auto exit=read<int>(actor,L"m_UniqueId",1040);
            if(exit)if(const auto* passage=shrine_passage(*exit,*transition.point_id))return passage;
        } else if(transition.map_id==1000) {
            const auto exit=read<int>(actor,L"m_UniqueId",1040);
            if(exit&&map_id==2050&&*exit==0&&*transition.point_id==44)return "South exit of the Dwarven Forge";
            if(exit)if(const auto* label=chapter_three::exit_label(map_id,*exit,transition.map_id,*transition.point_id))
                return label;
        }
        if(transition.map_id==map_id) {
            const auto exit=read<int>(actor,L"m_UniqueId",1040);
            if(exit)if(const auto* facility=facility_passage(map_id,*exit,*transition.point_id))return facility;
        }
    }
    if(transition.point_id&&map_id==3281&&transition.map_id==5191&&*transition.point_id==9200&&
       read<int>(actor,L"m_UniqueId",1040)==1)return "Path to the summit";
    if(transition.point_id&&map_id==3280&&transition.map_id==1000&&*transition.point_id==50000) {
        const auto exit=read<int>(actor,L"m_UniqueId",1040);
        if(exit==14||exit==15||exit==16)return "Enterprise takeoff point";
    }
    return nullptr;
}
std::string transition_label(MapContext& map,UObject* actor,const EffectiveTransition& transition,bool doorway=false) {
    const auto current=map.current_map();
    if(!current)return {};
    if(const auto* key=authored_transition_label(*current,actor,transition)) {
        // The English authored key describes whether this name belongs to a door.
        // The rendered label can have a different word order in another language.
        if(!doorway||std::string_view(key).starts_with("Door "))return tr(key);
        return {};
    }
    const auto title=localized_map_title(transition.map_id,current_language());
    if(title.empty())return {};
    if(*current>0&&transition.map_id!=*current)return trf(doorway?"Door to {0}":"Exit to {0}",{title});
    if(transition.map_id==*current&&!doorway)return trf("Passage within {0}",{title});
    return {};
}

// These paired door meshes and triggers share a visible doorway. Check the
// active transition and current positions before borrowing its facility name.
std::string facility_door_label(UObject* door,MapContext& map,const std::vector<UObject*>& exits) {
    const auto map_id=map.current_map();
    const auto door_id=read<int>(door,L"m_UniqueId",808);
    if(!map_id||!door_id)return {};
    const auto exit_id=facility_door_exit(*map_id,*door_id);
    if(!exit_id)return {};
    const auto position=navigation_position(door);
    if(!position)return {};
    UObject* matched=nullptr;
    for(auto* exit:exits) {
        if(!native_kind(exit,AreaKind::Exit)||actor_world(exit)!=map.world()||
           read<int>(exit,L"m_UniqueId",1040)!=exit_id)continue;
        const auto entrance=navigation_position(exit);
        if(!entrance||std::hypot(entrance->x-position->x,entrance->y-position->y,entrance->z-position->z)>300.)continue;
        if(matched)return {};
        matched=exit;
    }
    if(!matched)return {};
    const auto destination=effective_transition(matched,map);
    if(destination.state!=Availability::Available||destination.transition.event_only||
       destination.transition.map_id!=*map_id||!destination.transition.point_id)return {};
    const auto* label=facility_passage(*map_id,*exit_id,*destination.transition.point_id);
    return label?tr(label):std::string{};
}

struct Registry { UObject* manager{}; std::vector<UObject*> units,gimmicks,exits; const char* problem{}; };
Registry registry(UObject* world) {
    Registry result;
    auto* manager=map_manager(world);
    if(!manager){result.problem="current-world map manager unavailable or ambiguous";return result;}
    result.manager=manager;
    auto* units=object_value(manager,L"m_MapUnitStorage",0x1B0);
    auto* gimmicks=object_value(manager,L"m_MapGimmickManager",0x1C0);
    auto* triggers=object_value(manager,L"m_pMapTransitionTriggerStorage",0x238);
    if(!native_shape(units,L"/Script/mucha.CPP_MapUnitStorage",168)||!native_shape(gimmicks,L"/Script/mucha.CPP_MapGimmickManager",80)||
       !native_shape(triggers,L"/Script/mucha.CPP_MapTransitionTriggerStorage",56)) {result.problem="registry storage shape";return result;}
    const auto unit_list=object_array(units,L"m_MapUnits",0x28);
    const auto gimmick_list=object_array(gimmicks,L"m_MapGimmickList",0x28);
    const auto exit_list=object_array(triggers,L"m_TransitionTriggerList",0x28);
    if(!unit_list||!gimmick_list||!exit_list) {result.problem="registry array shape";return result;}
    // Empty is valid; unreadable is not empty. Preserve the distinction so an
    // attached-registry failure cannot silently remove available objects.
    const auto attached=object_array(gimmicks,L"m_AttachedGimmickList",0x38);
    if(!attached){result.problem="attached gimmick registry array shape";return result;}
    const auto limit=static_cast<std::size_t>(registry_limit);
    auto u=unique_members<UObject>({std::span<UObject* const>(*unit_list)},limit);
    auto g=unique_members<UObject>({std::span<UObject* const>(*gimmick_list),std::span<UObject* const>(*attached)},limit);
    auto e=unique_members<UObject>({std::span<UObject* const>(*exit_list)},limit);
    if(!u||!g||!e) {result.problem="registry exceeds capture bound";return result;}
    result.units=std::move(*u);result.gimmicks=std::move(*g);result.exits=std::move(*e);
    return result;
}
// complete=false: a nearby-interaction list could not be read, so prompts may be missing.
struct Nearby { std::unordered_set<UObject*> actors; bool complete{true}; };
Nearby interactions(UObject* pawn) {
    Nearby result;
    for(const auto& [name,offset]:{std::pair{L"m_InteractMapUnitList",0xA18},std::pair{L"m_pInteractMapGimmickList",0xA28}}) {
        if(const auto list=object_array(pawn,name,offset)) {for(auto* item:*list)if(item)result.actors.insert(item);}
        else result.complete=false;
    }
    return result;
}

std::vector<AreaTarget> scan(UObject* pawn,const std::string* only,const StorySpatialContext* proximity=nullptr,
        std::vector<ActivationBox>* passages=nullptr) {
    std::vector<AreaTarget> result;
    ScanTally tally;
    const auto finish=[&](std::string summary) {
        if(proximity)return;
        scan_report=tally.report;capture_summary=std::move(summary);legacy_reason=scan_report.unavailable_reason;
    };
    auto* world=actor_world(pawn);
    if(!world||!native_shape(pawn,L"/Script/mucha.CPP_MapUnit_Playable",3072)) {
        tally.fail(tr("The current field character is unavailable."));finish("pawn or world unavailable");return result;
    }
    const auto origin=navigation_position(pawn);
    if(!origin) {tally.fail(tr("The player position could not be read."));finish("pawn position unavailable");return result;}
    const auto native=registry(world);
    if(native.problem) {tally.fail(tr("The current area's entity list could not be read."));finish(native.problem);return result;}
    MapContext map(world,native.manager);
    // Find the selected path once, then verify registry membership this frame.
    // A lookup miss keeps the bounded scan, without reusing an old actor pointer.
    UObject* selected=nullptr;
    if(only&&!only->empty()&&only->size()<=4096&&only->find('\0')==std::string::npos) {
        const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,only->data(),static_cast<int>(only->size()),nullptr,0);
        if(count>0) {
            std::wstring name(static_cast<std::size_t>(count),L'\0');
            if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,only->data(),static_cast<int>(only->size()),name.data(),count)==count) {
                auto* found=UObjectGlobals::StaticFindObject(name.c_str());
                if(live(found)&&utf8(path(found))==*only)selected=found;
            }
        }
    }
    const auto nearby=interactions(pawn);
    std::size_t considered=0;
    // Dead registry entries are counted only by full captures; a selected capture
    // cannot attribute an unidentifiable dead entry to its target.
    const auto skip_dead=[&](UObject* actor) {
        if(live(actor))return false;
        if(!only)tally.reject(Rule::Removed);
        return true;
    };
    auto identity=[&](UObject* actor,AreaKind kind)->std::optional<std::string> {
        if(proximity) {
            const auto position=navigation_position(actor);
            const double scale=proximity->world_to_metres;
            if(!position||std::abs(position->z-origin->z)>field_cue_height_metres*scale||
               std::hypot(position->x-origin->x,position->y-origin->y)>field_cue_radius_metres*scale)return {};
        }
        auto id=utf8(path(actor));
        if(id.empty()) {
            // In a selected-ID fallback, an unrelated unreadable actor cannot
            // be attributed to the selected target and must not taint its report.
            if(!only||actor==selected)tally.admit(unknown("identity could not be read"),kind);
            return std::nullopt;
        }
        if(only&&id!=*only)return std::nullopt;
        ++considered;return id;
    };
    auto add=[&](UObject* actor,std::string id,AreaKind kind,std::optional<bool> object_state,
            const EffectiveTransition* transition=nullptr,const town_interactions::Placement* town=nullptr) {
        const auto& info=area_kind_info(kind);
        const auto category=town?town->category:info.category;
        const auto ladder=kind==AreaKind::Ladder&&is_town_ladder(actor);
        const auto end=ladder?capture_interaction_box(actor,pawn):std::nullopt;
        if(ladder&&!end)return;
        const auto position=end?std::optional{end->center}:navigation_position(actor);
        if(!position) {tally.admit(unknown("position could not be read"),kind);return;}
        tally.listed();
        const bool interactable=nearby.actors.contains(actor);
        if(proximity) {
            if(kind==AreaKind::TreasureChest&&object_state!=false)return;
            if(kind!=AreaKind::Person&&!interactable) {
                if(byte_call(actor,L"GetGimmickCondition")!=0)return;
                bool enabled=capture_interaction_box(actor,pawn).has_value();
                if(!enabled&&(kind==AreaKind::Door||kind==AreaKind::TriggerDoor||kind==AreaKind::Ladder)) {
                    auto* box=kind==AreaKind::Ladder?object_value(actor,L"m_pBottomBoxComponent",1000):
                        object_value(actor,L"m_pRootBoxComponent",760);
                    enabled=interaction_box_enabled(actor,box)==true;
                    if(!enabled&&kind==AreaKind::Ladder)
                        enabled=interaction_box_enabled(actor,object_value(actor,L"m_pTopBoxComponent",992))==true;
                }
                if(!enabled)return;
            }
            result.push_back({{std::move(id),{},{},true,interactable},actor,0,category,{},*position});
            return;
        }
        std::string label=tr(kind==AreaKind::Person?person_label(actor):
            object_appearance_label(kind,actor->GetClassPrivate()->GetNamePrivate().ToString(),object_state));
        if(town)label=tr(town->label);
        if(kind==AreaKind::FieldItem||kind==AreaKind::Interaction||kind==AreaKind::Book) {
            if(const auto* name=exact_placement_label(kind,path(actor),read<int>(actor,L"m_UniqueId",808).value_or(-1)))
                label=tr(name);
        }
        if(kind==AreaKind::AutomaticLift)if(const auto* lift=siren_lift(actor))label=tr(lift->label);
        if(kind==AreaKind::Door) {
            if(const auto* door=mitra_door(actor)) label=tr(door->label);
            else if(siren_tower_door(actor))label=tr("Door to Siren's Tower");
            else if(auto* linked=blueprint_object_value(actor,L"m_TransitionTrigger",info.native_size);
                native_kind(linked,AreaKind::Exit)&&actor_world(linked)==world) {
                const auto destination=effective_transition(linked,map);
                if(destination.state==Availability::Available&&!destination.transition.event_only) {
                    auto name=transition_label(map,linked,destination.transition,true);
                    if(!name.empty())label=std::move(name);
                }
            }
        }
        if(kind==AreaKind::Door||kind==AreaKind::TriggerDoor)
            if(auto facility=facility_door_label(actor,map,native.exits);!facility.empty())label=std::move(facility);
        if(transition) {
            auto named=transition_label(map,actor,*transition);
            if(!named.empty())label=std::move(named);
            id+=exit_route_key(*transition);
        }
        const auto distance=std::hypot(position->x-origin->x,position->y-origin->y,position->z-origin->z);
        result.push_back({{std::move(id),std::move(label),interactable?tr("Interaction available"):std::string{},true,interactable},
            actor,distance,category,info.type,*position});
    };
    const auto layout=unknown("native layout differs from the audited build");
    for(auto* actor:native.units) {
        if(selected&&actor!=selected)continue;
        if(skip_dead(actor)||actor==pawn)continue;
        // Player, party and other unit classes are not People targets.
        const auto kind=chain_kind(actor,false);if(!kind||kind->kind!=AreaKind::Person)continue;
        const auto id=identity(actor,AreaKind::Person);if(!id)continue;
        if(!kind->exact) {tally.admit(layout,AreaKind::Person);continue;}
        if(tally.admit(person_verdict(person_facts(actor,world)),AreaKind::Person))add(actor,*id,AreaKind::Person,std::nullopt);
    }
    for(auto* actor:native.gimmicks) {
        if(selected&&actor!=selected)continue;
        if(skip_dead(actor))continue;
        // Unsupported gimmick classes (triggers, event volumes) are not listed targets.
        const auto kind=chain_kind(actor,true);
        if(!kind) {
            if(map.current_map()!=2130||proximity)continue;
            const auto warp=mysidia_warp_end(actor,pawn);
            if(!warp)continue;
            auto id=utf8(path(actor));
            if(only&&id!=*only)continue;
            id+=warp_key_prefix;
            id+=std::to_string(warp->side);
            ++considered;
            tally.listed();
            const auto at=nearby.actors.contains(actor);
            const auto position=warp->box.center;
            const auto distance=std::hypot(position.x-origin->x,position.y-origin->y,position.z-origin->z);
            result.push_back({{std::move(id),tr(warp->goes_up?"Portal to upper Mysidia":"Portal to lower Mysidia"),
                at?tr("Interaction available"):std::string{},true,at},actor,distance,3,L"CPP_MapGimmick_Warp",position});
            continue;
        }
        if(kind->kind==AreaKind::Book&&!library_book(actor)&&!placed_pot(actor))continue;
        if(kind->kind==AreaKind::AutomaticLift&&!capture_siren_lift_entry(pawn,actor))continue;
        const auto* town=kind->kind==AreaKind::Interaction?town_interaction(actor):nullptr;
        if(kind->kind==AreaKind::Interaction&&!town&&
           !exact_placement_label(kind->kind,path(actor),read<int>(actor,L"m_UniqueId",808).value_or(-1)))continue;
        const auto door_meshes=kind->kind==AreaKind::TriggerDoor?trigger_door_meshes(actor):std::span<const wchar_t* const>{};
        if(kind->kind==AreaKind::TriggerDoor&&door_meshes.empty())continue;
        const auto id=identity(actor,kind->kind);if(!id)continue;
        if(!kind->exact) {tally.admit(layout,kind->kind);continue;}
        const auto facts=actor_facts(actor,world);
        if(kind->kind==AreaKind::Mailbox) {
            if(!mitra_mailbox(actor))continue;
            auto verdict=mailbox_verdict(facts,mailbox_collision(actor),byte_value(actor,L"m_MapInteractType",833));
            if(verdict.state==Availability::Available)
                verdict=check(mailbox_has_delivery(),Rule::Conditional,"delivery availability could not be read");
            if(!tally.admit(verdict,AreaKind::Mailbox))continue;
        } else if(!tally.admit(actor_verdict(facts),kind->kind))continue;
        if(kind->kind==AreaKind::TriggerDoor) {
            const auto state=byte_call(actor,L"GetGimmickCondition");
            if(!tally.admit(combine({check(door_mesh_visible(actor,door_meshes),Rule::Invisible,"mesh visibility could not be read"),
                check(state?std::optional<bool>(*state==0):std::nullopt,Rule::Conditional,"door state could not be read")}),
                kind->kind))continue;
        }
        if(kind->kind==AreaKind::Interaction&&
           (byte_call(actor,L"GetGimmickCondition")!=0||!capture_interaction_box(actor,pawn)))continue;
        if(town&&(map.current_map()!=town->map||
           !field_gimmick_offers_event(actor,utf8(std::wstring(town->event_id)))))continue;
        if(kind->kind==AreaKind::ElevatorCall||kind->kind==AreaKind::Elevator||kind->kind==AreaKind::Book) {
            if(!tally.admit(interactive_prop(actor,world,kind->kind),kind->kind))continue;
        }
        std::optional<bool> object_state;
        if(kind->kind==AreaKind::TreasureChest)object_state=bool_call(actor,L"IsOpen");
        if(kind->kind==AreaKind::Switch)object_state=bool_call(actor,L"IsSwitchPressed");
        if(kind->kind==AreaKind::FieldItem) {
            const auto opened=bool_call(actor,L"IsOpen");
            std::optional<bool> uncollected;
            if(opened)uncollected=!*opened;
            if(!tally.admit(check(uncollected,Rule::Conditional,"collection state could not be read"),AreaKind::FieldItem))continue;
        }
        add(actor,*id,kind->kind,object_state,nullptr,town);
    }
    if(proximity) {
        auto* capsule=object_value(pawn,L"CapsuleComponent",824);
        Getter radius(capsule,L"GetScaledCapsuleRadius",4,0,4);
        if(!passages||!radius||path(radius.function())!=L"/Script/Engine.CapsuleComponent:GetScaledCapsuleRadius")
            return result;
        const auto player_radius=radius.call<float>(capsule);
        const auto range=field_wall_radius_metres*proximity->world_to_metres+player_radius;
        if(!std::isfinite(player_radius)||player_radius<=0.||!std::isfinite(range))return result;
        for(auto* actor:native.exits) {
            if(!native_kind(actor,AreaKind::Exit)||native_bool(byte_value(actor,L"m_IsAutoTransition",788))!=true)continue;
            const auto box=capture_activation_box(actor);
            const auto closest=box?closest_activation_point(*box,proximity->origin):std::nullopt;
            if(closest&&std::hypot(closest->x-proximity->origin.x,closest->y-proximity->origin.y)<=range&&
               exit_available(exit_facts(actor,map,pawn)))
                passages->push_back(*box);
        }
        return result;
    }
    for(auto* actor:native.exits) {
        if(selected&&actor!=selected)continue;
        if(skip_dead(actor))continue;
        const auto id=identity(actor,AreaKind::Exit);if(!id)continue;
        if(!native_kind(actor,AreaKind::Exit)) {tally.admit(layout,AreaKind::Exit);continue;}
        const auto facts=exit_facts(actor,map,pawn);
        if(tally.admit(exit_verdict(facts),AreaKind::Exit))add(actor,*id,AreaKind::Exit,std::nullopt,&facts.transition.transition);
    }
    order_targets(result);
    finish("units="+std::to_string(native.units.size())+" gimmicks="+std::to_string(native.gimmicks.size())+
        " exits="+std::to_string(native.exits.size())+" considered="+std::to_string(considered)+
        " available="+std::to_string(tally.listed_count)+" direct_lookup="+std::to_string(selected!=nullptr)+
        (nearby.complete?"":" interactions=unreadable")+" "+tally_summary(tally));
    return result;
}
// Individual actor queries: answer and reason only; the capture report is untouched.
bool answer(const Verdict& verdict) {
    actor_reason=verdict.state==Availability::Unknown?issue_text("The story destination's",verdict.issue):std::string{};
    legacy_reason=actor_reason;
    return verdict.state==Availability::Available;
}
bool unreadable_query(const char* reason) {actor_reason=tr(reason);legacy_reason=actor_reason;return false;}
UObject* factory_conveyor_volume(UObject* actor) {
    if(!native_shape(actor,L"/Script/mucha.CPP_MapGimmickJumpPoint",1272)||
       !path(actor).starts_with(L"/Game/Map/Dng/06Zol/06Zol_41/GD/Dng_06Zol_41_01_GD.Dng_06Zol_41_01_GD:"
           L"PersistentLevel.BP_MapGimmickJumpPoint_Walk_001_C_")||
       byte_value(actor,L"m_InteractType",1096)!=1||byte_value(actor,L"m_TransitionRule",1136)!=1||
       byte_call(actor,L"GetGimmickCondition")!=0)return nullptr;
    return object_value(actor,L"m_BoxComponent",984);
}
}

std::vector<AreaTarget> capture_area_targets(UObject* pawn) {return scan(pawn,nullptr);}
std::optional<FieldLocation> capture_field_location(UObject* pawn) {
    auto* world=actor_world(pawn);
    auto* manager=world?map_manager(world):nullptr;
    Getter area(manager,L"GetMapAreaId",4,0,4);
    if(!area||path(area.function())!=L"/Script/mucha.CPP_MapManager:GetMapAreaId")return {};
    MapContext map(world,manager);
    const auto id=map.current_map();
    const int room=area.call<int>(manager);
    if(!id||room<0)return {};
    return FieldLocation{*id,room};
}
std::optional<TravelLocation> capture_travel_location(UObject* pawn) {
    auto* world=actor_world(pawn);
    auto* manager=world?map_manager(world):nullptr;
    Getter area(manager,L"GetMapAreaId",4,0,4);
    if(!area||path(area.function())!=L"/Script/mucha.CPP_MapManager:GetMapAreaId")return {};
    MapContext map(world,manager);
    const auto id=map.current_map();
    if(!id)return {};
    // Same current-world manager and exact build as MapContext. The adjacent
    // fields are the native previous map and resolved entry point.
    const auto* address=reinterpret_cast<const std::byte*>(manager);
    const int previous=pod<int>(address,0x34),entry=pod<int>(address,0x38);
    const int room=area.call<int>(manager);
    if(previous<-1||entry<-2||room<-1)return {};
    return TravelLocation{{*id,entry},previous,room};
}
std::vector<LiveTravelPassage> capture_travel_passages(UObject* pawn,const TravelLocation& location) {
    auto* world=actor_world(pawn);
    if(!world)return {};
    const auto current=registry(world);
    if(current.problem)return {};
    MapContext map(world,current.manager);
    if(map.current_map()!=location.point.map)return {};
    std::vector<LiveTravelPassage> result;
    for(auto& target:capture_area_targets(pawn)) {
        auto* trigger=target.actor;
        if(target.category!=3||!native_kind(trigger,AreaKind::Exit))continue;
        const auto facts=exit_facts(trigger,map,pawn);
        const auto id=read<int>(trigger,L"m_UniqueId",1040);
        if(!id||*id<0||!exit_available(facts)||!facts.transition.transition.point_id)continue;
        if(!capture_activation_box(trigger))continue;
        const auto& to=facts.transition.transition;
        result.push_back({std::move(target),{location.point.map,*id,{to.map_id,*to.point_id}}});
    }
    return result;
}
std::optional<bool> evaluate_travel_condition(std::wstring_view condition) {
    if(condition.empty())return true;
    if(condition.size()>2047)return {};
    std::wstring text(condition);
    const auto size=static_cast<std::int32_t>(text.size()+1);
    const NativeArray<wchar_t> value{text.data(),size,size};
    return native_condition(reinterpret_cast<const std::byte*>(&value));
}
std::string field_npc_label(UObject* actor) {
    return native_kind(actor,AreaKind::Person)?person_label(actor):std::string{};
}

std::vector<FieldProximityTarget> capture_field_proximity(UObject* pawn,const StorySpatialContext& spatial,
        std::vector<ActivationBox>& passages) {
    auto* world=actor_world(pawn);
    auto* controller=object_value(pawn,L"Controller",728);
    if(!world||!is(controller,L"/Script/Engine.PlayerController")||actor_world(controller)!=world||
       object_value(controller,L"Pawn",744)!=pawn||bool_call(controller,L"IsLocalPlayerController")!=true)return {};
    Getter sight(controller,L"LineOfSightTo",40,33,1);
    if(!sight||path(sight.function())!=L"/Script/Engine.Controller:LineOfSightTo"||
       !sight_argument(sight.function(),L"Other",0,8,L"ObjectProperty")||
       !sight_argument(sight.function(),L"ViewPoint",8,24,L"StructProperty")||
       !sight_argument(sight.function(),L"bAlternateChecks",32,1,L"BoolProperty"))return {};
    // Listen from the party, using the same control-relative horizontal axes as the route beacon.
    auto eye=spatial.origin;
    eye.z+=.65*spatial.world_to_metres;
    sight.put(8,eye);
    std::vector<FieldProximityTarget> result;
    for(auto& target:scan(pawn,nullptr,&spatial,&passages)) {
        const double distance=std::hypot(target.position.x-spatial.origin.x,target.position.y-spatial.origin.y);
        bool audible=false;
        if(distance<=field_cue_radius_metres*spatial.world_to_metres) {
            sight.put(0,target.actor);
            audible=target.snapshot.interactable||native_bool(sight.call<std::uint8_t>(controller))==true;
        }
        result.push_back({std::move(target.snapshot.id),target.position,
            target.category==1?FieldCueKind::Npc:FieldCueKind::Interactable,audible});
    }
    return result;
}
std::optional<Vec3> tracked_actor_position(UObject* actor) {return navigation_position(actor);}
std::optional<ActivationBox> capture_activation_box(UObject* actor) {
    if(!live(actor))return {};
    if(auto* conveyor=factory_conveyor_volume(actor))return queryable_interaction_box(actor,conveyor);
    // This small native box marks the door. The Blueprint's much larger
    // InnerBox controls overlap and may already contain the player at spawn.
    if(native_kind(actor,AreaKind::TriggerDoor))
        return owned_navigation_box(actor,object_value(actor,L"m_pInnerBoxComponent",992));
    const bool exit=native_kind(actor,AreaKind::Exit);
    const bool story_trigger=is(actor,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
    if(!exit&&!story_trigger)return {};
    auto* root=object_value(actor,L"RootComponent",440);
    if(!root||object_value(actor,L"m_pRootBoxComponent",exit?720:760)!=root)return {};
    // The Mobliz cargo trigger has a verified 0.199-degree roll.
    return owned_navigation_box(actor,root,story_trigger&&path(actor)==chapter_two::mobliz_cargo_actor_path?.5:0.);
}
std::optional<ActivationBox> capture_factory_conveyor(UObject* pawn,UObject* actor) {
    auto* component=factory_conveyor_volume(actor);
    if(!component||actor_world(pawn)!=actor_world(actor))return {};
    const auto overlap=overlap_facts(pawn,actor,component);
    return overlap&&enabled_overlap(*overlap)?owned_navigation_box(actor,component):std::nullopt;
}
std::optional<ActivationBox> capture_swing_door_trigger(UObject* pawn,UObject* actor) {
    if(!native_kind(actor,AreaKind::TriggerDoor)||actor_world(actor)!=actor_world(pawn)||
       byte_call(actor,L"GetGimmickCondition")!=0)return {};
    auto* trigger=object_value(actor,L"m_pTriggerBox",984);
    const auto overlap=overlap_facts(pawn,actor,trigger);
    return overlap&&enabled_overlap(*overlap)?owned_navigation_box(actor,trigger):std::nullopt;
}
namespace {
bool mobliz_lever_memo(UObject* actor) {
    return live(actor)&&path(actor)==L"/Game/Map/Dng/04Dir/04Dir_41/GD/Dng_04Dir_41_02_GD.Dng_04Dir_41_02_GD:PersistentLevel.BP_MapGimmick_BlockObject010_C_1"&&
        read<int>(actor,L"m_UniqueId",808)==8&&byte_value(actor,L"m_MapInteractType",833)==4;
}
bool story_check_trigger(UObject* actor) {
    return is(actor,L"/Script/mucha.CPP_MapGimmick_InteractEventTrigger")&&byte_value(actor,L"m_MapInteractType",833)==4;
}
bool streetcar_timetable(UObject* actor) {
    if(!is(actor,L"/Script/mucha.CPP_MapGimmick_InteractEventTrigger"))return false;
    const auto name=path(actor);
    if(!name.starts_with(L"/Game/Map/Town/06Zol/")||
       !name.ends_with(L":PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1"))return false;
    const auto id=read<int>(actor,L"m_UniqueId",808);
    return std::any_of(chapter_four::streetcar_stops.begin(),chapter_four::streetcar_stops.end(),[&](const auto& stop) {
        return id==stop.sign&&name.find(stop.level)!=std::wstring::npos;
    });
}
bool olderion_inn_door(UObject* actor) {
    return native_kind(actor,AreaKind::Door)&&read<int>(actor,L"m_UniqueId",808)==73&&
        path(actor)==L"/Game/Map/Town/05Old/05Old_01/GD/Town_05Old_01_01_GD.Town_05Old_01_01_GD:"
            L"PersistentLevel.BP_MapGimmick_Object_Door_Single004_C_0";
}
bool chapter_four_story_door(UObject* actor) {
    if(!native_kind(actor,AreaKind::Door))return false;
    const auto id=read<int>(actor,L"m_UniqueId",808);
    const auto name=path(actor);
    return (id==13&&name==L"/Game/Map/Town/06Zol/06Zol_21/GD/Town_06Zol_21_01_GD.Town_06Zol_21_01_GD:"
            L"PersistentLevel.BP_MapGimmick_Object_Door_Single003_C_1")||
        (id==8&&name==L"/Game/Map/Dng/06Zol/06Zol_43/GD/Dng_06Zol_43_01_GD.Dng_06Zol_43_01_GD:"
            L"PersistentLevel.BP_MapGimmick_Object_Door_Single_Slid_005_C_1")||
        (id==21&&name==L"/Game/Map/Dng/06Zol/06Zol_44/GD/Dng_06Zol_44_01_GD.Dng_06Zol_44_01_GD:"
            L"PersistentLevel.BP_MapGimmick_Object_Door_Child_06Zol_44_C_1");
}
bool factory_shortcut_ladder(UObject* actor) {
    return path(actor)==L"/Game/Map/Dng/06Zol/06Zol_41/GD/Dng_06Zol_41_01_GD.Dng_06Zol_41_01_GD:"
        L"PersistentLevel.BP_MapGimmick_StringLadder001_C_1"&&read<int>(actor,L"m_UniqueId",808)==32&&
        byte_value(actor,L"m_MapInteractType",833)==4;
}
bool research_lab_actor(UObject* actor,int id,const wchar_t* name) {
    return read<int>(actor,L"m_UniqueId",808)==id&&
        path(actor)==std::wstring(chapter_six_lab::gd_path)+name;
}
const chapter_six_lab::WarpSite* research_lab_warp(UObject* actor) {
    if(!native_shape(actor,L"/Script/mucha.CPP_MapGimmick_Warp",1368))return nullptr;
    for(const auto& site:chapter_six_lab::warp_sites)
        if(research_lab_actor(actor,site.id,site.actor_name))return &site;
    return nullptr;
}
bool research_lab_rope(UObject* actor) {
    return is(actor,L"/Script/mucha.CPP_MapGimmick_StringLadder")&&
        research_lab_actor(actor,6,L"BP_MapGimmick_StringLadder_C_3");
}
bool research_lab_pickup_or_console(UObject* actor) {
    const auto id=read<int>(actor,L"m_UniqueId",808);
    if(id==32||id==33||id==37)
        return native_kind(actor,AreaKind::FieldItem)&&path(actor).starts_with(chapter_six_lab::gd_path);
    if(id==28||id==29||id==30)
        return native_kind(actor,AreaKind::Switch)&&path(actor).starts_with(chapter_six_lab::gd_path);
    return false;
}
bool warp_effect_visible(UObject* actor,bool second=false) {
    auto* effect=second?object_value(actor,L"m_pNiagaraComponent2",1032):
        object_value(actor,L"m_pNiagaraComponent",1024);
    if(!is(effect,L"/Script/Niagara.NiagaraComponent"))return false;
    Getter owner(effect,L"GetOwner",8,0,8);
    if(!owner||path(owner.function())!=L"/Script/Engine.ActorComponent:GetOwner"||owner.call<UObject*>(effect)!=actor)return false;
    const auto visible=byte_value(effect,L"bVisible",416),hidden=byte_value(effect,L"bHiddenInGame",417);
    return visible&&hidden&&masked(*visible,0x20)&&!masked(*hidden,0x08)&&
        is(object_value(effect,L"Asset",1312),L"/Script/Niagara.NiagaraSystem");
}
std::optional<MysidiaWarpEnd> mysidia_warp_end(UObject* actor,UObject* pawn) {
    if(!mysidia_warp(actor)||!story_actor_available(pawn,actor)||
       byte_value(actor,L"m_MapInteractType",833)!=4||byte_call(actor,L"GetGimmickCondition")!=0)return {};
    auto* first=object_value(actor,L"m_pWarpPointBoxComponent",984);
    auto* second=object_value(actor,L"m_pWarpPointBoxComponent2",992);
    const auto from=owned_navigation_box(actor,first),to=owned_navigation_box(actor,second);
    const auto origin=navigation_position(pawn);
    if(!from||!to||!origin||std::abs(from->center.z-to->center.z)<3000.)return {};
    // The two pads share their map position and are on separate terraces.
    const bool other=std::abs(to->center.z-origin->z)<std::abs(from->center.z-origin->z);
    const auto permitted=byte_value(actor,L"m_CanWarpPoint",1044);
    const int side=other?2:1;
    if(!permitted||!chapter_six_lab::warp_allowed(*permitted,side))return {};
    const auto overlap=overlap_facts(pawn,actor,other?second:first);
    if(!overlap||!enabled_overlap(*overlap))return {};
    auto* mesh=other?object_value(actor,L"m_pWarpPointStaticMeshComponent2",1016):
        object_value(actor,L"m_pWarpPointStaticMeshComponent",1008);
    if(!warp_effect_visible(actor,other)&&visible_owned_mesh(actor,mesh)!=true)return {};
    return MysidiaWarpEnd{other?*to:*from,side,other?from->center.z>to->center.z:to->center.z>from->center.z};
}
std::optional<bool> story_door_box_open(UObject* actor) {
    if(bool_call(actor,L"GetActorEnableCollision")!=true)return {};
    auto* box=object_value(actor,L"m_pRootBoxComponent",760);
    if(!owned_navigation_box(actor,box)||bool_call(box,L"GetGenerateOverlapEvents")!=true)return {};
    const auto mode=byte_call(box,L"GetCollisionEnabled");
    if(mode==0)return true;
    if(mode==1)return false;
    return {};
}
}
std::optional<bool> olderion_inn_door_open(UObject* actor) {
    return olderion_inn_door(actor)?story_door_box_open(actor):std::nullopt;
}
std::optional<bool> burning_castle_door_open(UObject* actor) {
    if(!native_kind(actor,AreaKind::Door))return {};
    constexpr std::wstring_view level=L"/Game/Map/Dng/01Gra/01Gra_42/GD/Dng_01Gra_42_01_GD.Dng_01Gra_42_01_GD:PersistentLevel.";
    const auto name=path(actor);
    if(!name.starts_with(level))return {};
    const auto suffix=std::wstring_view(name).substr(level.size());
    const auto id=read<int>(actor,L"m_UniqueId",808);
    if((id==31&&suffix==L"BP_MapGimmick_Object_Door_C_1")||
       (id==23&&suffix==L"BP_MapGimmick_Object_Door_Child007_C_0"))return story_door_box_open(actor);
    return {};
}
std::optional<bool> kol_inn_door_open(UObject* actor) {
    if(!native_kind(actor,AreaKind::Door)||read<int>(actor,L"m_UniqueId",808)!=8||
       path(actor)!=L"/Game/Map/Town/02Lan/02Lan_20/GD/Town_02Lan_20_01_GD.Town_02Lan_20_01_GD:"
          L"PersistentLevel.BP_MapGimmick_Object_Door_Single002_C_5")return {};
    return story_door_box_open(actor);
}
std::optional<bool> siren_tower_door_open(UObject* actor) {
    return siren_tower_door(actor)?story_door_box_open(actor):std::nullopt;
}
bool is_siren_tower_lift(UObject* actor) {
    return siren_lift(actor)!=nullptr;
}
std::optional<ActivationBox> capture_siren_lift_entry(UObject* pawn,UObject* actor) {
    const auto* lift=siren_lift(actor);
    if(!lift||!field_object_available(actor_facts(actor,actor_world(pawn)))||
       byte_call(actor,L"GetGimmickCondition")!=0||
       visible_owned_mesh(actor,object_value(actor,L"m_pStaticMeshComponent",1144))!=true)return {};
    auto* entry=blueprint_object_value(actor,L"m_EntryBox",area_kind_info(AreaKind::AutomaticLift).native_size);
    if(!entry)return {};
    const auto overlap=overlap_facts(pawn,actor,entry);
    if(!overlap||!enabled_overlap(*overlap))return {};
    const auto box=owned_navigation_box(actor,entry);
    if(!box||!siren_lift_docked(*lift,box->center))return {};
    return box;
}
std::optional<bool> chapter_four_story_door_open(UObject* actor) {
    return chapter_four_story_door(actor)?story_door_box_open(actor):std::nullopt;
}
std::optional<bool> gaberada_elder_door_open(UObject* actor) {
    if(!native_kind(actor,AreaKind::Door)||read<int>(actor,L"m_UniqueId",808)!=1||
       path(actor)!=L"/Game/Map/Town/08Gro/08Gro_01/GD/Town_08Gro_01_01_GD.Town_08Gro_01_01_GD:"
           L"PersistentLevel.BP_MapGimmick_Object_Door_Single002_C_0")return {};
    return story_door_box_open(actor);
}
std::optional<bool> research_lab_door_open(UObject* actor) {
    if(!native_kind(actor,AreaKind::Door))return {};
    if(!research_lab_actor(actor,40,L"BP_MapGimmick_Object_Door_Single_Slid_001_C_3")&&
       !research_lab_actor(actor,41,L"BP_MapGimmick_Object_Door_Slid_C_4")&&
       !research_lab_actor(actor,42,L"BP_MapGimmick_Object_Door_Slid_C_6")&&
       !research_lab_actor(actor,43,L"BP_MapGimmick_Object_Door_Slid_C_8"))return {};
    return story_door_box_open(actor);
}
std::optional<bool> research_lab_rope_lowered(UObject* pawn,UObject* rope,UObject* ladder) {
    if(!research_lab_rope(rope)||!research_lab_actor(ladder,20,L"BP_MapGimmickLadder_C_2")||
       actor_world(rope)!=actor_world(pawn)||actor_world(ladder)!=actor_world(pawn)||
       !native_kind(ladder,AreaKind::Ladder)||blueprint_object_value(rope,L"m_Ladder",1192)!=ladder)return {};
    auto* blocker=object_value(rope,L"m_pBlockingBox",1184);
    if(!owned_navigation_box(rope,blocker))return {};
    const auto collision=byte_call(blocker,L"GetCollisionEnabled");
    if(!collision||*collision>5)return {};
    if(*collision==0)return true;
    return story_actor_available(pawn,rope)&&byte_call(rope,L"GetGimmickCondition")==0&&
        byte_value(rope,L"m_MapInteractType",833)==4?std::optional(false):std::nullopt;
}
std::optional<ActivationBox> capture_research_lab_lift_entry(UObject* pawn,UObject* actor) {
    if(!native_shape(actor,L"/Script/mucha.CPP_MapGimmick_Floor_Elevator",1504)||
       !research_lab_actor(actor,47,L"BP_MapGimmick_Floor_Elevator001_C_3")||
       !story_actor_available(pawn,actor)||byte_call(actor,L"GetGimmickCondition")!=0||
       visible_owned_mesh(actor,object_value(actor,L"m_pStaticMeshComponent",1144))!=true)return {};
    auto* entry=blueprint_object_value(actor,L"m_EntryBox",1504);
    if(!entry)return {};
    const auto overlap=overlap_facts(pawn,actor,entry);
    if(!overlap||!enabled_overlap(*overlap))return {};
    const auto box=owned_navigation_box(actor,entry);
    const auto position=navigation_position(pawn);
    if(!box||!position||std::hypot(box->center.x+160.,box->center.y-19500.)>25.||
       std::abs(box->center.z-position->z)>150.||
       (std::abs(box->center.z-3180.)>25.&&std::abs(box->center.z-180.)>25.))return {};
    return box;
}
std::optional<ActivationBox> capture_research_lab_doorway(UObject* pawn,UObject* actor) {
    if(!native_kind(actor,AreaKind::TriggerDoor)||
       (!research_lab_actor(actor,48,L"BP_MapGimmick_TriggerDoor_Slid_002_C_4")&&
        !research_lab_actor(actor,49,L"BP_MapGimmick_TriggerDoor_Slid_002_C_1"))||
       !story_actor_available(pawn,actor)||byte_call(actor,L"GetGimmickCondition")!=0||
       door_mesh_visible(actor,trigger_door_meshes(actor))!=true)return {};
    auto* box=object_value(actor,L"m_pTriggerBox",984);
    if(!box)return {};
    const auto overlap=overlap_facts(pawn,actor,box);
    if(!overlap||!enabled_overlap(*overlap))return {};
    return capture_activation_box(actor);
}
std::optional<ActivationBox> capture_ladder_bottom_box(UObject* actor) {
    if(!native_kind(actor,AreaKind::Ladder))return {};
    return queryable_interaction_box(actor,object_value(actor,L"m_pBottomBoxComponent",1000));
}
bool is_town_ladder(UObject* actor) {
    if(!native_kind(actor,AreaKind::Ladder))return false;
    const auto name=path(actor);
    const auto id=read<int>(actor,L"m_UniqueId",808);
    if(id==0&&name==L"/Game/Map/Town/01Gra/01Gra_22/GD/Town_01Gra_22_01_GD.Town_01Gra_22_01_GD:"
        L"PersistentLevel.BP_MapGimmickLadder_C_1")return true;
    constexpr std::wstring_view level=L"/Game/Map/Town/03Kol/03Kol_20/GD/Town_03Kol_20_01_GD.Town_03Kol_20_01_GD:"
        L"PersistentLevel.";
    if(!name.starts_with(level))return false;
    const auto instance=std::wstring_view(name).substr(level.size());
    return (id==2&&instance==L"BP_MapGimmick_Object_Ladderwood02_C_0")||
           (id==3&&instance==L"BP_MapGimmick_Object_Ladderwood02_C_1");
}

std::optional<ActivationBox> capture_interaction_box(UObject* actor,UObject* pawn) {
    const auto player_overlap=[&](UObject* component)->std::optional<ActivationBox> {
        if(!component||actor_world(actor)!=actor_world(pawn))return {};
        const auto overlap=overlap_facts(pawn,actor,component);
        return overlap&&enabled_overlap(*overlap)?owned_navigation_box(actor,component):std::nullopt;
    };
    if(const auto* site=research_lab_warp(actor)) {
        const auto permitted=byte_value(actor,L"m_CanWarpPoint",1044);
        if(!permitted||!chapter_six_lab::warp_allowed(*permitted,1)||
           byte_value(actor,L"m_MapInteractType",833)!=4||byte_call(actor,L"GetGimmickCondition")!=0||
           !story_actor_available(pawn,actor)||
           (!warp_effect_visible(actor)&&
            visible_owned_mesh(actor,object_value(actor,L"m_pWarpPointStaticMeshComponent",1008))!=true))return {};
        const auto box=player_overlap(object_value(actor,L"m_pWarpPointBoxComponent",984));
        if(!box||std::hypot(box->center.x-site->source.x,box->center.y-site->source.y)>40.||
           std::abs(box->center.z-site->source.z)>45.)return {};
        return box;
    }
    if(mysidia_warp(actor)) {
        const auto end=mysidia_warp_end(actor,pawn);
        return end?std::optional{end->box}:std::nullopt;
    }
    if(is_town_ladder(actor)&&actor_world(actor)==actor_world(pawn)&&byte_call(actor,L"GetGimmickCondition")==0) {
        const auto origin=navigation_position(pawn);
        auto* low=object_value(actor,L"m_pBottomBoxComponent",1000);
        auto* high=object_value(actor,L"m_pTopBoxComponent",992);
        const auto bottom=owned_navigation_box(actor,low,0.,true);
        const auto top=owned_navigation_box(actor,high,0.,true);
        if(!origin||!bottom||!top||top->center.z<=bottom->center.z)return {};
        const bool upper=std::abs(top->center.z-origin->z)<std::abs(bottom->center.z-origin->z);
        return interaction_box_enabled(actor,upper?high:low)==true?(upper?top:bottom):std::nullopt;
    }
    if(research_lab_rope(actor))
        return player_overlap(object_value(actor,L"m_pRootBoxComponent",760));
    if(native_kind(actor,AreaKind::Person))
        return player_overlap(object_value(actor,L"m_pEventTriggerBox",2448));
    if(native_kind(actor,AreaKind::SavePoint))
        return player_overlap(object_value(actor,L"m_pRootBoxComponent",760));
    if(native_kind(actor,AreaKind::TreasureChest)||mitra_passage_door(actor))
        return owned_navigation_box(actor,object_value(actor,L"m_pRootBoxComponent",760));
    if(native_kind(actor,AreaKind::Door))
        return player_overlap(object_value(actor,L"m_pRootBoxComponent",760));
    if(native_kind(actor,AreaKind::FieldItem)||native_kind(actor,AreaKind::Switch)||native_kind(actor,AreaKind::MovableObject))
        return queryable_interaction_box(actor,object_value(actor,L"m_pRootBoxComponent",760));
    if(native_kind(actor,AreaKind::Elevator))
        return player_overlap(object_value(actor,L"m_pActiveBoxComponent",1504));
    if(const auto* town=town_interaction(actor))
        return byte_value(actor,L"m_MapInteractType",833)==town->interaction?
            player_overlap(object_value(actor,L"m_pRootBoxComponent",760)):std::nullopt;
    if(story_check_trigger(actor)||streetcar_timetable(actor)||olderion_inn_door(actor)||chapter_four_story_door(actor)||
       factory_shortcut_ladder(actor))
        return player_overlap(object_value(actor,L"m_pRootBoxComponent",760));
    if(is(actor,L"/Script/mucha.CPP_MapGimmick_Object_Raft")&&byte_value(actor,L"m_MapInteractType",833)==15)
        return player_overlap(object_value(actor,L"m_pInteractBoxComponent",1016));
    if(native_kind(actor,AreaKind::ElevatorCall)||
       (native_kind(actor,AreaKind::Book)&&(library_book(actor)||placed_pot(actor)))||mobliz_lever_memo(actor))
        return player_overlap(object_value(actor,L"m_pRootBoxComponent",760));
    if(mitra_mailbox(actor)) {
        const auto mode=mailbox_collision(actor);
        if(mode&&query_collision(*mode)&&byte_value(actor,L"m_MapInteractType",833)==27)
            return owned_navigation_box(actor,object_value(actor,L"m_pRootBoxComponent",760));
    }
    return {};
}
bool requires_checked_interaction_box(UObject* actor) {
    return town_interaction(actor)||mysidia_warp(actor)||is_town_ladder(actor)||research_lab_warp(actor)||research_lab_rope(actor)||
        research_lab_pickup_or_console(actor)||
        native_kind(actor,AreaKind::TreasureChest)||
        native_kind(actor,AreaKind::Door)||native_kind(actor,AreaKind::ElevatorCall)||native_kind(actor,AreaKind::Elevator)||
        (native_kind(actor,AreaKind::Book)&&(library_book(actor)||placed_pot(actor)))||mobliz_lever_memo(actor)||
        story_check_trigger(actor)||streetcar_timetable(actor)||olderion_inn_door(actor)||chapter_four_story_door(actor)||
        factory_shortcut_ladder(actor)||
        is(actor,L"/Script/mucha.CPP_MapGimmick_Object_Raft");
}

std::optional<RaftBoardingPoint> capture_raft_boarding_point(UObject* actor,UObject* pawn) {
    constexpr auto boat=L"/Game/Map/Dng/05Old/05Old_40/GD/Dng_05Old_40_01_GD.Dng_05Old_40_01_GD:"
        L"PersistentLevel.BP_MapGimmick_Raft001_C_1";
    if(!is(actor,L"/Script/mucha.CPP_MapGimmick_Object_Raft")||path(actor)!=boat||
        read<int>(actor,L"m_UniqueId",808)!=44||!capture_interaction_box(actor,pawn))return {};
    const auto location=tracked_actor_position(actor);
    if(!location||std::hypot(location->x-635,location->y-10860)>45)return {};
    // The boarding prompt reaches the bank once the moored boat rises with
    // the water. Its interaction box sits beyond the walking surface.
    return RaftBoardingPoint{{510,10805,2.15},location->z>=20.};
}

std::optional<StoryExitRoute> capture_story_exit_route(UObject* pawn,UObject* actor) {
    auto* world=actor_world(pawn);
    if(!world||actor_world(actor)!=world)return {};
    MapContext map(world);
    ExitFacts facts;
    if(native_kind(actor,AreaKind::Exit))facts=exit_facts(actor,map,pawn);
    else if(native_shape(actor,L"/Script/mucha.CPP_MapLandmarkPoint",816)) {
        if(story_point_verdict(story_facts(actor,world)).state!=Availability::Available)return {};
        const auto link=landmark_link(actor,map,pawn);
        if(link.linked!=true||actor_world(link.actor)!=world)return {};
        actor=link.actor;facts=link.transition;
    } else return {};
    if(exit_verdict(facts,true).state!=Availability::Available)return {};
    const auto box=capture_activation_box(actor);
    if(!box)return {};
    const auto& destination=facts.transition.transition;
    auto named=destination.event_only?std::string{}:transition_label(map,actor,destination);
    return StoryExitRoute{actor,box->center,named.empty()?tr("Story passage"):std::move(named),
        exit_route_key(destination),destination.map_id,destination.point_id,destination.event_only};
}
const char* mitra_passage_door(UObject* actor) {
    const auto* door=mitra_door(actor);
    return door&&door->requires_route?door->label:nullptr;
}
std::optional<WalkingSection> capture_walking_section(UObject* actor,bool include_links,std::optional<Vec3> feet) {
    auto* world=actor_world(actor);
    const auto position=world?(feet?feet:navigation_position(actor)):std::nullopt;
    if(!position)return {};
    std::vector<UObject*> volumes;
    UObjectGlobals::FindAllOf(L"CPP_Map_NavMeshBoundsVolume",volumes);
    UObject* selected=nullptr;
    WalkingSection section;
    for(auto* volume:volumes) {
        if(!native_shape(volume,L"/Script/mucha.CPP_Map_NavMeshBoundsVolume",800)||actor_world(volume)!=world)continue;
        const auto id=read<int>(volume,L"m_AreaId",752);
        const auto bounds=owned_navigation_box(volume,object_value(volume,L"m_pBoxComponent",744));
        if(!id||*id<0||!bounds)return {};
        const auto nearest=closest_activation_point(*bounds,*position);
        if(!nearest)return {};
        if(nearest->x!=position->x||nearest->y!=position->y||nearest->z!=position->z)continue;
        if(selected)return {};
        selected=volume;
        section.id=*id;
        section.bounds=*bounds;
    }
    if(!selected)return {};
    if(!include_links)return section;
    // Local events can still use the verified bounds when links are unreadable.
    Getter links(selected,L"GetTransitionTriggerList",16,0,16);
    if(!links||path(links.function())!=L"/Script/mucha.CPP_Map_NavMeshBoundsVolume:GetTransitionTriggerList")return section;
    auto* property=links.function()->GetReturnProperty();
    if(!property->field_class()||property->field_class()->GetName()!=L"ArrayProperty")return section;
    auto* inner=static_cast<FArrayProperty*>(property)->GetInner();
    if(!inner||inner->GetSize()!=8||!inner->field_class()||inner->field_class()->GetName()!=L"ObjectProperty")return section;
    const auto values=links.call<NativeArray<UObject*>>(selected);
    if(!valid_array(values.data,values.num,values.max,1024))return section;
    std::vector<UObject*> exits;
    for(int i=0;i<values.num;++i) {
        auto* exit=values.data[i];
        if(!native_kind(exit,AreaKind::Exit)||actor_world(exit)!=world)return section;
        exits.push_back(exit);
    }
    section.exits=std::move(exits);
    return section;
}
UObject* active_section_exit(UObject* pawn,const WalkingSection& section,int destination_map,std::string& reason) {
    reason.clear();
    auto* world=actor_world(pawn);
    if(!world||!section.exits) {
        reason=tr("The current Battlefield section's linked exits could not be read.");
        return nullptr;
    }
    const auto current=registry(world);
    if(current.problem) {
        reason=tr("The current area's exit list could not be read.");
        return nullptr;
    }
    MapContext map(world,current.manager);
    std::vector<ExitFacts> facts;
    facts.reserve(section.exits->size());
    for(auto* exit:*section.exits) {
        const auto registered=std::find(current.exits.begin(),current.exits.end(),exit)!=current.exits.end();
        facts.push_back(registered?exit_facts(exit,map,pawn):ExitFacts{});
    }
    const auto match=unique_linked_exit(facts,destination_map);
    if(match.issue)reason=tr(match.issue);
    return match.index?(*section.exits)[*match.index]:nullptr;
}
std::optional<CastleLeg> capture_castle_leg(UObject* pawn) {
    auto* world=actor_world(pawn);
    if(!world)return {};
    const auto current=registry(world);
    if(current.problem||!current.manager)return {};
    auto* manager=current.manager;
    MapContext map(world,manager);
    if(map.current_map()!=2014)return {};
    Getter area(manager,L"GetMapAreaId",4,0,4);
    if(!area||path(area.function())!=L"/Script/mucha.CPP_MapManager:GetMapAreaId")return {};
    const auto position=navigation_position(pawn);
    if(!position)return {};
    const auto spec=castle_leg_spec(2014,area.call<int>(manager),*position);
    if(!spec)return {};
    UObject* selected=nullptr;
    for(auto* actor:spec->doorway?current.gimmicks:current.exits) {
        const bool shape=spec->doorway?native_shape(actor,L"/Script/mucha.CPP_MapGimmick_Trigger_DoorBase",1064):native_kind(actor,AreaKind::Exit);
        if(!shape||actor_world(actor)!=world||read<int>(actor,L"m_UniqueId",spec->doorway?808:1040)!=spec->id)continue;
        if(selected)return {};
        selected=actor;
    }
    if(!selected)return {};
    const auto box=capture_activation_box(selected);
    if(!box)return {};
    CastleLeg result{selected,box->center,tr(spec->label)};
    if(spec->doorway) {
        if(actor_verdict(actor_facts(selected,world)).state!=Availability::Available||byte_call(selected,L"GetGimmickCondition")!=0)return {};
        auto* trigger=castle_inner_trigger(selected);
        if(!trigger)return {};
        const auto overlap=overlap_facts(pawn,selected,trigger);
        if(!overlap||!enabled_overlap(*overlap))return {};
        const auto outer=owned_navigation_box(selected,object_value(selected,L"m_pTriggerBox",984));
        if(outer)result.hall_side=outer->center;
    } else {
        const auto facts=exit_facts(selected,map,pawn);
        if(!exit_available(facts))return {};
        const auto same_map=native_bool(byte_value(selected,L"m_IsSameMapTrasition",744));
        if(spec->point_id>=0) {
            if(same_map!=true||facts.transition.transition.map_id!=2014||read<int>(selected,L"m_PointID",780)!=spec->point_id)return {};
        } else {
            const int destination=facts.transition.transition.map_id;
            if(same_map!=false||(destination!=2011&&destination!=2013))return {};
        }
    }
    return result;
}
std::optional<MitraLeg> capture_mitra_leg(UObject* pawn) {
    auto* world=actor_world(pawn);
    if(!world)return {};
    const auto current=registry(world);
    if(current.problem||!current.manager)return {};
    MapContext map(world,current.manager);
    if(map.current_map()!=2000)return {};
    Getter area(current.manager,L"GetMapAreaId",4,0,4);
    if(!area||path(area.function())!=L"/Script/mucha.CPP_MapManager:GetMapAreaId")return {};
    const auto id=mitra_story_exit_id(area.call<int>(current.manager));
    if(!id)return {};
    UObject* selected=nullptr;
    for(auto* actor:current.exits) {
        if(!native_kind(actor,AreaKind::Exit)||actor_world(actor)!=world||
           read<int>(actor,L"m_UniqueId",1040)!=*id)continue;
        if(selected)return {};
        selected=actor;
    }
    if(!selected)return {};
    const auto facts=exit_facts(selected,map,pawn);
    if(!exit_available(facts))return {};
    const auto* passage=mitra_exit_passage(selected,facts.transition.transition);
    const auto box=passage?capture_activation_box(selected):std::nullopt;
    if(!box)return {};
    return MitraLeg{selected,box->center,passage->label};
}
std::optional<AreaTarget> capture_area_target(UObject* pawn,const std::string& target_id) {
    if(target_id.empty()) {scan_report={};capture_summary="no selection";legacy_reason.clear();return std::nullopt;}
    const auto actor_id=target_id.substr(0,std::min(target_id.find(exit_key_prefix),target_id.find(warp_key_prefix)));
    const auto current=scan(pawn,&actor_id);
    return current_target<AreaTarget>(current,target_id);
}
AreaCaptureReport area_capture_report() {return scan_report;}
bool story_actor_available(UObject* pawn,UObject* actor,bool require_exit) {
    auto* world=actor_world(pawn);
    if(!world)return unreadable_query("The current field character is unavailable.");
    if(!live(actor)||actor==pawn)return answer(inactive(Rule::Removed));
    const auto point=story_facts(actor,world);
    if(!require_exit)return answer(story_point_verdict(point));
    // A claimed exit: a landmark with its linked native exit, or the trigger volume itself.
    const auto layout=unknown("native layout differs from the audited build");
    MapContext map(world);
    switch(class_shape(actor,L"/Script/mucha.CPP_MapLandmarkPoint",816)) {
    case Shape::Exact: {
        if(const auto own=story_point_verdict(point);own.state==Availability::Inactive)return answer(own);
        const auto link=landmark_link(actor,map,pawn);
        return answer(story_landmark_verdict(point,link.linked,link.transition));
    }
    case Shape::Mismatch:return answer(layout);
    case Shape::Absent:break;
    }
    switch(class_shape(actor,kind_path(AreaKind::Exit),area_kind_info(AreaKind::Exit).native_size)) {
    case Shape::Exact:return answer(story_trigger_verdict(exit_facts(actor,map,pawn),point.positioned));
    case Shape::Mismatch:return answer(layout);
    case Shape::Absent:break;
    }
    return answer(inactive(Rule::NotExit));
}
std::string area_actor_unavailable_reason() {return actor_reason;}
bool area_actor_available(UObject* pawn,UObject* actor) {
    auto* world=actor_world(pawn);
    if(!world)return unreadable_query("The current field character is unavailable.");
    if(!live(actor)||actor==pawn)return answer(inactive(Rule::Removed));
    MapContext map(world);
    if(native_kind(actor,AreaKind::Person))return answer(person_verdict(person_facts(actor,world)));
    if(native_kind(actor,AreaKind::Exit))return answer(exit_verdict(exit_facts(actor,map,pawn),true));
    if(native_shape(actor,L"/Script/mucha.CPP_MapLandmarkPoint",816)) {
        LandmarkFacts facts{actor_facts(actor,world)};
        if(actor_verdict(facts.landmark).state!=Availability::Inactive) {
            auto link=landmark_link(actor,map,pawn);facts.linked=link.linked;facts.transition=std::move(link.transition);
        }
        return answer(landmark_verdict(facts,true));
    }
    if(native_shape(actor,L"/Script/mucha.CPP_FieldObject",752))return answer(actor_verdict(actor_facts(actor,world)));
    return answer(inactive(Rule::NotExit));
}
std::string last_area_capture_summary() {return capture_summary;}
std::string area_capture_unavailable_reason() {return legacy_reason;}
}
