#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/BattleInput.hpp"
#include "resonance/Localization.hpp"
#include "resonance/NativeBoundary.hpp"
#include "resonance/NativeMetadata.hpp"
#include "BattleConditionLabels.hpp"
#include "NativeUiPresentation.hpp"
#include "StoryInputValidation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <deque>
#include <optional>
#include <stdexcept>

// Exact-build evidence: battle-input/AUDIT.md (JMAP offsets and signatures,
// decoded MVVM bindings, WBP_BtlUnitDamage bytecode, Ghidra DamageData setter).
// Game thread only. Native objects are resolved again by every call.
namespace resonance {
namespace {
using namespace resonance::unreal;
constexpr std::uint32_t dead_flags=0x8000u|0x10000u|0x40000000u;
constexpr std::uint64_t parm=0x80,out_parm=0x100,return_parm=0x400,returned=parm|out_parm|return_parm;
constexpr wchar_t layout_class[]=L"/Game/UI/Battle/Widget/WBP_BattleLayout.WBP_BattleLayout_C";
constexpr wchar_t sequencer_class[]=L"/Game/UI/Battle/Widget/WBP_BattleSequencerLay.WBP_BattleSequencerLay_C";
constexpr wchar_t party_class[]=L"/Game/UI/Battle/Unit/Statuses/WBP_Btl_UnitStatusList.WBP_Btl_UnitStatusList_C";
constexpr wchar_t party_card_class[]=L"/Game/UI/Battle/Unit/Statuses/WBP_Btl_UnitStatus.WBP_Btl_UnitStatus_C";
constexpr wchar_t condition_list_class[]=L"/Game/UI/Common/Unit/Condition/WBP_UnitStatusConditionList.WBP_UnitStatusConditionList_C";
constexpr wchar_t limit_gauge_class[]=L"/Game/UI/Battle/Unit/Statuses/Limit/Widget/WBP_LimitGauge.WBP_LimitGauge_C";
constexpr wchar_t limit_vm_class[]=L"/Script/mucha.CPP_VM_LimitPoint";
constexpr wchar_t banner_class[]=L"/Game/UI/Battle/SkillMessage/WBP_Btl_SkillMsg.WBP_Btl_SkillMsg_C";
constexpr wchar_t libra_class[]=L"/Game/UI/Battle/Libra/Widget/WBP_Btl_LibraInfoLayout.WBP_Btl_LibraInfoLayout_C";
constexpr wchar_t unit_ui_class[]=L"/Game/UI/Battle/UnitUI/WBP_Btl_UnitUILayout.WBP_Btl_UnitUILayout_C";
constexpr wchar_t enemy_ui_class[]=L"/Game/UI/Battle/UnitUI/EnemyStatus/WBP_Btl_EnemyUI.WBP_Btl_EnemyUI_C";
constexpr wchar_t target_cursor_class[]=L"/Game/UI/Battle/UnitUI/Select/WBP_Btl_UnitSelect.WBP_Btl_UnitSelect_C";
constexpr wchar_t target_name_class[]=L"/Game/UI/Battle/UnitUI/Select/WBP_Btl_UnitSelectName.WBP_Btl_UnitSelectName_C";
constexpr wchar_t break_gauge_class[]=L"/Game/UI/Battle/UnitUI/Break/Widget/WBP_BreakPointGauge.WBP_BreakPointGauge_C";
constexpr wchar_t prediction_widget[]=L"/Game/UI/Battle/Skill/Resist/Predict/Widget/WBP_BtlSkillResistPredict.WBP_BtlSkillResistPredict_C";
constexpr wchar_t prediction_appearance[]=L"/Game/UI/Battle/Skill/Resist/Predict/Widget/WBP_BtlSkillResistPredictAppearance.WBP_BtlSkillResistPredictAppearance_C";
constexpr wchar_t prediction_model[]=L"/Script/mucha.CPP_VM_BtlSkillResistPredict";
constexpr wchar_t damage_class[]=L"/Game/UI/Battle/UnitUI/Damage/Widget/WBP_BtlUnitDamage.WBP_BtlUnitDamage_C";
constexpr wchar_t popup_class[]=L"/Game/UI/Battle/UnitUI/Damage/Widget/WBP_BtlUnitDamageProp.WBP_BtlUnitDamageProp_C";
constexpr wchar_t identify_class[]=L"/Game/UI/Battle/UnitUI/EnemyStatus/Identification/Widget/WBP_Btl_Identification.WBP_Btl_Identification_C";
constexpr wchar_t identify_default[]=L"/Game/UI/Battle/UnitUI/EnemyStatus/Identification/Widget/WBP_Btl_Identification.Default__WBP_Btl_Identification_C";
constexpr wchar_t title_class[]=L"/Game/UI/Battle/UnitUI/Damage/Widget/WBP_BtlUnitDamageTitleText.WBP_BtlUnitDamageTitleText_C";
constexpr wchar_t overkill_class[]=L"/Game/UI/Battle/UnitUI/Damage/Widget/WBP_BtlUnitDamage_Overkill.WBP_BtlUnitDamage_Overkill_C";
constexpr wchar_t total_class[]=L"/Game/UI/Battle/UnitUI/TotalDamage/Widget/WBP_BtlUnitTotalDamage.WBP_BtlUnitTotalDamage_C";
constexpr wchar_t unit_vm_class[]=L"/Script/mucha.CPP_VM_BtlUnitParam";

bool live(UObject* o) {
    return o&&UObjectGlobals::IsValidObjectForFindXOf(o)&&!(static_cast<std::uint32_t>(o->GetObjectFlags())&(0x10u|0x20u|dead_flags));
}
std::wstring path(UObject* o) {
    if(!o)return {};auto name=o->GetFullName();const auto space=name.find(L' ');
    return space==std::wstring::npos?name:name.substr(space+1);
}
bool is(UObject* o,std::wstring_view type) {
    if(!o)return false;int depth=0;
    for(auto* c=static_cast<UStruct*>(o->GetClassPrivate());c&&depth++<64;c=c->GetSuperStruct())if(path(c)==type)return true;
    return false;
}
std::string utf8(std::wstring_view text) {
    if(text.empty())return {};
    const auto size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(size<1)return {};
    std::string result(static_cast<std::size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);
    return result;
}
// Rendered rich text: markup tags removed, whitespace collapsed, controls dropped.
std::string clean(const std::wstring& value) {
    std::wstring result;bool tag=false,space=false;
    for(const auto c:value) {
        if(result.size()>=2048)break;
        if(c==L'<'){tag=true;continue;}if(tag){if(c==L'>')tag=false;continue;}
        if(iswspace(c)){space=!result.empty();continue;}if(c<32||c==127)continue;
        if(space){result+=L' ';space=false;}result+=c;
    }
    return utf8(result);
}
std::string text_at(const void* text){return text?clean(static_cast<const FText*>(text)->ToString()):std::string{};}
template<class T> T pod(const std::byte* data,std::size_t offset){T value{};std::memcpy(&value,data+offset,sizeof value);return value;}
template<class T> struct Array {T* data{};int num{},max{};};
bool same(const FName& a,const FName& b){return a.ComparisonIndex==b.ComparisonIndex&&a.Number==b.Number;}
bool none(const FName& name){return !name.ComparisonIndex&&!name.Number;}
std::wstring name_text(FName name){return none(name)?std::wstring{}:name.ToString();}

// owner/name/offset/size/kind are the exact-build reflection of a field; Blueprint
// members (offset -1) are absent from the title-screen profile and are trusted
// only for their reflected kind, size and bounds plus the caller's type checks.
struct Spec {const wchar_t* owner;const wchar_t* name;int offset,size;const wchar_t* kind;};
FProperty* verified_property(UObject* o,const Spec& f) {
    if(!live(o)||!is(o,f.owner))return nullptr;
    auto* p=o->GetPropertyByNameInChain(f.name);
    if(!p||p->GetSize()!=f.size||p->GetOffset_Internal()<0||(f.offset>=0&&p->GetOffset_Internal()!=f.offset))return nullptr;
    auto* kind=p->field_class();auto* cls=o->GetClassPrivate();
    if(!kind||kind->GetName()!=f.kind||!cls||p->GetOffset_Internal()>cls->GetPropertiesSize()-f.size)return nullptr;
    return p;
}
void* field(UObject* o,const Spec& f) {
    return verified_property(o,f)?o->GetValuePtrByPropertyNameInChain(f.name):nullptr;
}
template<class T> std::optional<T> read(UObject* o,const Spec& f) {
    if(static_cast<int>(sizeof(T))!=f.size)return {};
    auto* p=field(o,f);if(!p)return {};T value{};std::memcpy(&value,p,sizeof value);return value;
}
std::optional<bool> flag(UObject* o,const Spec& f){const auto v=read<unsigned char>(o,f);return v?std::optional<bool>(*v!=0):std::nullopt;}
std::optional<bool> bit(UObject* o,const Spec& f,unsigned char mask){const auto v=read<unsigned char>(o,f);return v?std::optional<bool>((*v&mask)!=0):std::nullopt;}
// nullopt: unreadable; nullptr: a verified empty reference.
std::optional<UObject*> reference(UObject* o,const Spec& f,const wchar_t* type) {
    const auto value=read<UObject*>(o,f);if(!value)return {};
    if(!*value)return static_cast<UObject*>(nullptr);
    return live(*value)&&(!type||is(*value,type))?value:std::nullopt;
}
UObject* object(UObject* o,const Spec& f,const wchar_t* type=nullptr){const auto v=reference(o,f,type);return v?*v:nullptr;}

constexpr Spec during_battle{L"/Script/mucha.CPP_UI_BattleSubsystem",L"bDuringBattle",128,1,L"BoolProperty"};
constexpr Spec battle_widget{L"/Script/mucha.CPP_UI_BattleSubsystem",L"BtlWidget",64,8,L"WeakObjectProperty"};
constexpr Spec unit_manager{L"/Script/mucha.CPP_UI_BattleSubsystem",L"BtlUnitManager",120,8,L"ObjectProperty"};
constexpr Spec battle_display{L"/Script/mucha.CPP_VM_Battle",L"bIsDisplayBattleWidget",128,1,L"BoolProperty"};
constexpr Spec battle_event{L"/Script/mucha.CPP_VM_Battle",L"bIsEvent",129,1,L"BoolProperty"};
constexpr Spec battle_actor{L"/Script/mucha.CPP_VM_Battle",L"CurrentActionUnitUniqueId",120,4,L"IntProperty"};
constexpr Spec battle_skill{L"/Script/mucha.CPP_VM_Battle",L"FocusSkillId",124,4,L"IntProperty"};
constexpr Spec unit_id{unit_vm_class,L"UniqueID",408,4,L"IntProperty"};
constexpr Spec unit_select{unit_vm_class,L"TargetSelectState",442,1,L"EnumProperty"};
constexpr Spec unit_visible{unit_vm_class,L"bIsVisible",443,1,L"BoolProperty"};
constexpr Spec prediction_target{prediction_model,L"TargetUnitParamVM",112,8,L"ObjectProperty"};
constexpr Spec prediction_skill{prediction_model,L"SkillId",120,4,L"IntProperty"};
constexpr Spec prediction_display{prediction_model,L"bDisplay",368,1,L"BoolProperty"};
constexpr Spec prediction_data{prediction_model,L"PredictProperty",128,240,L"StructProperty"};
constexpr Spec hit_points{L"/Script/mucha.CPP_VM_UnitParam",L"HitPointVM",144,8,L"ObjectProperty"};
constexpr Spec magic_points{L"/Script/mucha.CPP_VM_UnitParam",L"MagicPointVM",152,8,L"ObjectProperty"};
constexpr Spec limit_points{L"/Script/mucha.CPP_VM_UnitParam",L"LimitPointVM",160,8,L"ObjectProperty"};
constexpr Spec limit_current_level{limit_vm_class,L"CurrentLevel",292,4,L"IntProperty"};
constexpr Spec limit_max_level{limit_vm_class,L"MaxLevel",288,4,L"IntProperty"};
constexpr Spec condition_effects{condition_list_class,L"CondisionEffectList",-1,16,L"ArrayProperty"};
constexpr Spec condition_display_data{condition_list_class,L"StatusConditionDA",-1,8,L"ObjectProperty"};
constexpr Spec point_current{L"/Script/mucha.CPP_VM_Point",L"CurrentPoint",184,4,L"IntProperty"};
constexpr Spec point_max{L"/Script/mucha.CPP_VM_Point",L"MaxPoint",188,4,L"IntProperty"};
constexpr Spec party_ids{L"/Script/mucha.CPP_VM_BtlUnitStatuses",L"DisplayUnitUniqueIdList",104,16,L"ArrayProperty"};
constexpr Spec libra_entries{L"/Script/mucha.CPP_VM_BtlLibra",L"DisplayEntryTagList",120,16,L"ArrayProperty"};
constexpr Spec libra_display{L"/Script/mucha.CPP_VM_BtlLibra",L"bIsDisplay",136,1,L"BoolProperty"};
constexpr Spec damage_unit{L"/Script/mucha.CPP_VM_BtlUnitDamage",L"UniqueID",120,4,L"IntProperty"};
constexpr Spec popup_data{L"/Script/mucha.CPP_VM_BtlUnitDamageProp",L"DamageData",112,64,L"StructProperty"};
constexpr Spec popup_title{L"/Script/mucha.CPP_VM_BtlUnitDamageProp",L"TitleProperty",176,232,L"StructProperty"};
constexpr Spec popup_number{L"/Script/mucha.CPP_VM_BtlUnitDamageProp",L"NumberProperty",408,208,L"StructProperty"};
constexpr Spec popup_display{L"/Script/mucha.CPP_VM_BtlUnitDamageProp",L"DisplayNumberText",968,16,L"TextProperty"};
constexpr Spec popup_additional{L"/Script/mucha.CPP_VM_BtlUnitDamageProp",L"bNotifyAdditionalParam",984,1,L"BoolProperty"};
constexpr Spec identify_data{identify_class,L"UnitIdentifyDA",840,8,L"ObjectProperty"};
constexpr Spec total_unit{L"/Script/mucha.CPP_BtlUIShowTotalDamageDelVal",L"UnitUniqueId",48,4,L"IntProperty"};
constexpr Spec total_value{L"/Script/mucha.CPP_BtlUIShowTotalDamageDelVal",L"totalDamage",52,4,L"IntProperty"};
constexpr Spec widget_slot{L"/Script/UMG.Widget",L"Slot",48,8,L"ObjectProperty"};
constexpr Spec widget_visibility{L"/Script/UMG.Widget",L"Visibility",220,1,L"EnumProperty"};
constexpr Spec widget_opacity{L"/Script/UMG.Widget",L"RenderOpacity",224,4,L"FloatProperty"};
constexpr Spec slot_parent{L"/Script/UMG.PanelSlot",L"Parent",40,8,L"ObjectProperty"};
constexpr Spec slot_content{L"/Script/UMG.PanelSlot",L"Content",48,8,L"ObjectProperty"};
constexpr Spec panel_slots{L"/Script/UMG.PanelWidget",L"Slots",360,16,L"ArrayProperty"};
constexpr Spec user_tree{L"/Script/UMG.UserWidget",L"WidgetTree",728,8,L"ObjectProperty"};
constexpr Spec user_color{L"/Script/UMG.UserWidget",L"ColorAndOpacity",416,16,L"StructProperty"};
constexpr Spec tree_root{L"/Script/UMG.WidgetTree",L"RootWidget",48,8,L"ObjectProperty"};
constexpr Spec text_block{L"/Script/UMG.TextBlock",L"Text",392,16,L"TextProperty"};
constexpr Spec text_color{L"/Script/UMG.TextBlock",L"ColorAndOpacity",424,20,L"StructProperty"};
constexpr Spec rich_text{L"/Script/UMG.RichTextBlock",L"Text",392,16,L"TextProperty"};
constexpr Spec level_world{L"/Script/Engine.Level",L"OwningWorld",192,8,L"ObjectProperty"};
constexpr Spec controller_player{L"/Script/Engine.PlayerController",L"Player",840,8,L"ObjectProperty"};
constexpr Spec player_controller{L"/Script/Engine.Player",L"PlayerController",48,8,L"ObjectProperty"};
constexpr Spec unit_parameter{unit_vm_class,L"BtlUnitParam",416,8,L"ObjectProperty"};
constexpr Spec actor_parameter{L"/Script/mucha.CPP_BtlUnit",L"m_pUnitParameter",2104,8,L"ObjectProperty"};
constexpr Spec actor_unit_ui{L"/Script/mucha.CPP_BtlUnit",L"m_pUnitUI",2112,8,L"ObjectProperty"};
constexpr Spec component_space{L"/Script/UMG.WidgetComponent",L"Space",1408,1,L"EnumProperty"};
constexpr Spec component_on_screen{L"/Script/UMG.WidgetComponent",L"bAddedToScreen",1608,1,L"BoolProperty"};
// Packed bitfield bools (masks from the exact-build profile): bHidden 0x80, bHiddenInGame 0x08.
constexpr Spec actor_hidden{L"/Script/Engine.Actor",L"bHidden",88,1,L"BoolProperty"};
constexpr Spec component_hidden{L"/Script/Engine.SceneComponent",L"bHiddenInGame",417,1,L"BoolProperty"};
constexpr Spec view_constructed{L"/Script/ModelViewViewModel.MVVMView",L"bConstructed",106,1,L"BoolProperty"};
constexpr Spec view_bindings{L"/Script/ModelViewViewModel.MVVMView",L"bBindingsInitialized",108,1,L"BoolProperty"};
constexpr Spec switcher_index{L"/Script/UMG.WidgetSwitcher",L"ActiveWidgetIndex",384,4,L"IntProperty"};
// Battle result screen (exact-build JMAP; staging semantics from Ghidra:
// GetIsFinishStaging/Staging/Skip/FinishStaging implementations at
// 145173e50/14518e6c0/14518e2d0/14516f490 use StagingList[CurrentIndex]).
constexpr wchar_t result_layout_class[]=L"/Game/UI/Battle/Result/Widget/WBP_Btl_ResultLayout.WBP_Btl_ResultLayout_C";
constexpr wchar_t result_page_class[]=L"/Script/mucha.CPP_WBP_Btl_Result_PageListBase";
constexpr Spec result_pages{L"/Script/mucha.CPP_WBP_Btl_ResultLayout",L"WBP_BtlResultPage_Layout",1432,8,L"ObjectProperty"};
constexpr Spec result_page_index{L"/Script/mucha.CPP_WBP_Btl_Result_PageList",L"CurrentPageIndex",1392,4,L"IntProperty"};
constexpr Spec page_staging{result_page_class,L"StagingManager",1400,8,L"ObjectProperty"};
constexpr Spec staging_steps{L"/Script/mucha.CPP_Btl_Result_StagingManager",L"StagingList",40,16,L"ArrayProperty"};
constexpr Spec staging_index{L"/Script/mucha.CPP_Btl_Result_StagingManager",L"CurrentIndex",56,4,L"IntProperty"};
constexpr Spec step_finished{L"/Script/mucha.CPP_Btl_Result_StagingBase",L"IsFinishStaging",245,1,L"BoolProperty"};
constexpr Spec step_playing{L"/Script/mucha.CPP_Btl_Result_StagingBase",L"IsPlayingStaging",246,1,L"BoolProperty"};
constexpr wchar_t reward_page[]=L"/Game/UI/Battle/Result/Widget/Page/Reward/WBP_BtlResultPage_RewardWindow.WBP_BtlResultPage_RewardWindow_C";
constexpr wchar_t reward_list_class[]=L"/Game/UI/Battle/Result/Widget/Page/Reward/WBP_BtlResultPage_RewardList.WBP_BtlResultPage_RewardList_C";
constexpr wchar_t reward_text_class[]=L"/Game/UI/Battle/Result/Widget/Parts/WBP_Btl_Result_Text.WBP_Btl_Result_Text_C";
constexpr Spec reward_data{reward_page,L"m_ResultData",-1,192,L"StructProperty"};
constexpr Spec reward_current{reward_text_class,L"m_CurrentValue",-1,8,L"DoubleProperty"};
constexpr Spec reward_counting{reward_text_class,L"m_IsStartChangeValue",-1,1,L"BoolProperty"};
constexpr wchar_t growth_page[]=L"/Game/UI/Battle/Result/Widget/Page/Growth/WBP_BtlResultPage_GrowthWindow.WBP_BtlResultPage_GrowthWindow_C";
constexpr wchar_t level_up_page[]=L"/Game/UI/Battle/Result/Widget/Page/LevelUp/WBP_BtlResultPage_LevelUpWindow.WBP_BtlResultPage_LevelUpWindow_C";
constexpr Spec level_up_details{level_up_page,L"WidgetSwitcher_Detail",-1,8,L"ObjectProperty"};
constexpr Spec level_up_unit{level_up_page,L"m_UnitDetailIndex",-1,4,L"IntProperty"};
constexpr Spec level_up_vision{level_up_page,L"m_VisioinDetailIndex",-1,4,L"IntProperty"};

UScriptStruct* struct_of(FProperty* p,const wchar_t* expected,int size) {
    if(!p||!p->field_class()||p->field_class()->GetName()!=L"StructProperty")return nullptr;
    UScriptStruct* result{};auto& handle=static_cast<FStructProperty*>(p)->GetStruct();
    std::memcpy(&result,std::addressof(handle),sizeof result);
    return native_metadata(result,NativeMetadataKind::ScriptStruct)&&path(result)==expected&&result->GetPropertiesSize()==size?result:nullptr;
}
struct Member {const wchar_t* name;int offset,size;const wchar_t* kind;};
bool members(UStruct* type,std::initializer_list<Member> list) {
    if(!type)return false;
    for(const auto& m:list) {
        FName key(m.name,FNAME_Find);if(none(key))return false;
        auto* p=type->FindProperty(key);
        if(!p||p->GetOffset_Internal()!=m.offset||p->GetSize()!=m.size||!p->field_class()||p->field_class()->GetName()!=m.kind)return false;
    }
    return true;
}
// BtlUnitDamageTitle/NumberProperty inherit bEnable from BtlUnitDamageElemPropertyBase.
bool element_base(UScriptStruct* type) {
    auto* base=type?type->GetSuperStruct():nullptr;
    return native_metadata(base,NativeMetadataKind::ScriptStruct)&&path(base)==L"/Script/mucha.BtlUnitDamageElemPropertyBase"&&
        base->GetPropertiesSize()==208&&members(base,{{L"bEnable",0,1,L"BoolProperty"}});
}

struct Param {
    const wchar_t* name;int offset,size;const wchar_t* kind;std::uint64_t flags{parm};
    const wchar_t* structure{};int inner{};const wchar_t* inner_structure{};
};
bool param_shape(FProperty* p,const Param& spec) {
    if(!p||p->GetOffset_Internal()!=spec.offset||p->GetSize()!=spec.size||!p->HasAnyPropertyFlags(parm))return false;
    if(((spec.flags&out_parm)!=0)!=p->HasAnyPropertyFlags(out_parm)||((spec.flags&return_parm)!=0)!=p->HasAnyPropertyFlags(return_parm))return false;
    if(!p->field_class()||p->field_class()->GetName()!=spec.kind)return false;
    if(spec.structure&&!struct_of(p,spec.structure,spec.size))return false;
    if(spec.inner) {
        auto* inner=static_cast<FArrayProperty*>(p)->GetInner();
        if(!inner||inner->GetSize()!=spec.inner||(spec.inner_structure&&!struct_of(inner,spec.inner_structure,spec.inner)))return false;
    }
    return true;
}
// Native getters and lookups only: exact path, FUNC_Native, audited allocation,
// every listed parameter verified and constructed/destroyed by its own property.
class Call {
    UFunction* function_{};
    alignas(16) std::array<std::byte,512> data_{};
    std::array<std::optional<story_input_detail::ScopedNativeValue<FProperty>>,4> values_{};
public:
    Call(UObject* receiver,const wchar_t* name,const wchar_t* expected,int allocation,int return_offset,std::initializer_list<Param> params) {
        if(!live(receiver)||params.size()>values_.size())return;
        auto* function=receiver->GetFunctionByNameInChain(name);
        if(!function||path(function)!=expected||!function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400)))return;
        int end=0;for(const auto& spec:params)end=std::max(end,spec.offset+spec.size);
        if(!native_call_extent(function->GetParmsSize(),function->GetPropertiesSize(),allocation,function->GetReturnValueOffset(),
            return_offset,end,static_cast<int>(data_.size())))return;
        std::size_t index=0;
        for(const auto& spec:params) {
            FName key(spec.name,FNAME_Find);if(none(key))return;
            auto* p=function->FindProperty(key);
            if(!param_shape(p,spec)||((spec.flags&return_parm)&&p!=function->GetReturnProperty()))return;
            values_[index++].emplace(*p,data_.data()+spec.offset);
        }
        if(return_offset<0&&function->GetReturnProperty())return;
        function_=function;
    }
    Call(const Call&)=delete;Call& operator=(const Call&)=delete;
    explicit operator bool()const{return function_!=nullptr;}
    void invoke(UObject* receiver){receiver->ProcessEvent(function_,data_.data());}
    template<class T> void put(std::size_t offset,const T& value){std::memcpy(data_.data()+offset,&value,sizeof value);}
    template<class T> T get(std::size_t offset)const{return pod<T>(data_.data(),offset);}
    const std::byte* at(std::size_t offset)const{return data_.data()+offset;}
};
template<class T> std::optional<T> getter(UObject* o,const wchar_t* name,const wchar_t* expected,const wchar_t* kind) {
    Call call(o,name,expected,sizeof(T),0,{{L"ReturnValue",0,static_cast<int>(sizeof(T)),kind,returned}});
    if(!call)return {};call.invoke(o);return call.get<T>(0);
}
UObject* get_object(UObject* o,const wchar_t* name,const wchar_t* expected,const wchar_t* type) {
    const auto value=getter<UObject*>(o,name,expected,L"ObjectProperty");
    return value&&live(*value)&&(!type||is(*value,type))?*value:nullptr;
}
std::optional<std::string> get_text(UObject* o,const wchar_t* name,const wchar_t* expected) {
    Call call(o,name,expected,16,0,{{L"ReturnValue",0,16,L"TextProperty",returned}});
    if(!call)return {};call.invoke(o);return text_at(call.at(0));
}
// The widget's MVVM view extension (UserWidget::GetExtension(MVVMView)).
UObject* view_of(UObject* widget) {
    auto* view_class=UObjectGlobals::StaticFindObject(L"/Script/ModelViewViewModel.MVVMView");
    if(!native_metadata(view_class,NativeMetadataKind::Class)||path(view_class)!=L"/Script/ModelViewViewModel.MVVMView")return nullptr;
    Call extension(widget,L"GetExtension",L"/Script/UMG.UserWidget:GetExtension",16,8,
        {{L"ExtensionType",0,8,L"ClassProperty"},{L"ReturnValue",8,8,L"ObjectProperty",returned}});
    if(!extension)return nullptr;extension.put(0,view_class);extension.invoke(widget);auto* view=extension.get<UObject*>(8);
    return live(view)&&is(view,L"/Script/ModelViewViewModel.MVVMView")&&view->GetOuterPrivate()==widget?view:nullptr;
}
// The widget's own view-model property (set by MVVMView::SetViewModel), else the view.
UObject* viewmodel(UObject* widget,const wchar_t* owner,const wchar_t* name,const wchar_t* type) {
    if(const auto value=reference(widget,{owner,name,-1,8,L"ObjectProperty"},type);value&&*value)return *value;
    auto* view=view_of(widget);
    Call call(view,L"GetViewModel",L"/Script/ModelViewViewModel.MVVMView:GetViewModel",24,8,
        {{L"ViewModelName",0,8,L"NameProperty"},{L"ReturnValue",8,16,L"InterfaceProperty",returned}});
    FName key(name,FNAME_Find);if(!call||none(key))return nullptr;
    call.put(0,key);call.invoke(view);auto* result=call.get<UObject*>(8);
    return live(result)&&call.get<void*>(16)&&is(result,type)?result:nullptr;
}
// A designer widget of owner's own tree (named Blueprint member).
UObject* member(UObject* owner,const wchar_t* owner_class,const wchar_t* name,const wchar_t* type) {
    auto* tree=object(owner,user_tree,L"/Script/UMG.WidgetTree");
    auto* result=object(owner,{owner_class,name,-1,8,L"ObjectProperty"},type);
    return tree&&tree->GetOuterPrivate()==owner&&result&&result->GetOuterPrivate()==tree?result:nullptr;
}
// The only designer widget of type in owner's own tree, through its panels.
// Cooked widgets with bIsVariable=false (WBP_Btl_CharaState) have no member.
UObject* designer_of_type(UObject* owner,const wchar_t* type) {
    auto* tree=object(owner,user_tree,L"/Script/UMG.WidgetTree");
    const auto root=tree&&tree->GetOuterPrivate()==owner?reference(tree,tree_root,L"/Script/UMG.Widget"):std::nullopt;
    if(!root||!*root)return nullptr;
    UObject* found=nullptr;std::vector<UObject*> pending{*root};
    for(int visited=0;!pending.empty();++visited) {
        auto* widget=pending.back();pending.pop_back();
        if(visited>=256)return nullptr;
        if(widget->GetOuterPrivate()!=tree)continue;
        if(is(widget,type)){if(found)return nullptr;found=widget;continue;}
        if(!is(widget,L"/Script/UMG.PanelWidget"))continue;
        const auto slots=read<Array<UObject*>>(widget,panel_slots);
        if(!slots||!story_input_detail::valid_native_array(slots->data,slots->num,slots->max,64))return nullptr;
        for(int i=0;i<slots->num;++i)if(auto* child=object(slots->data[i],slot_content,L"/Script/UMG.Widget"))pending.push_back(child);
    }
    return found;
}
// Visible, HitTestInvisible or SelfHitTestInvisible, with opacity and alpha.
bool shown_self(UObject* widget) {
    const auto visibility=read<unsigned char>(widget,widget_visibility);const auto opacity=read<float>(widget,widget_opacity);
    if(!visibility||!opacity||(*visibility!=0&&*visibility!=3&&*visibility!=4)||!std::isfinite(*opacity)||*opacity<=0)return false;
    if(!is(widget,L"/Script/UMG.UserWidget"))return true;
    auto* color=static_cast<const std::byte*>(field(widget,user_color));const auto alpha=color?pod<float>(color,12):0.f;
    return color&&std::isfinite(alpha)&&alpha>0;
}
// Walks presented ancestors (panel slots, then the owning user widget of a tree
// root) up to stop, else to the first widget with neither. nullptr if anything
// on the way is collapsed, hidden, transparent or unreadable. With switchers,
// a WidgetSwitcher presents only its ActiveWidgetIndex slot; depth counts steps.
UObject* visible_top(UObject* widget,UObject* stop=nullptr,bool switchers=false,int* depth=nullptr) {
    std::array<UObject*,64> seen{};std::size_t count=0;
    for(auto* current=widget;live(current)&&is(current,L"/Script/UMG.Widget");) {
        if(count==seen.size()||std::find(seen.begin(),seen.begin()+count,current)!=seen.begin()+count)return nullptr;
        seen[count++]=current;
        if(!shown_self(current))return nullptr;
        if(current==stop){if(depth)*depth=static_cast<int>(count);return current;}
        const auto slot=reference(current,widget_slot,L"/Script/UMG.PanelSlot");if(!slot)return nullptr;
        if(*slot) {
            auto* parent=object(*slot,slot_parent,L"/Script/UMG.PanelWidget");if(!parent)return nullptr;
            if(switchers&&is(parent,L"/Script/UMG.WidgetSwitcher")) {
                const auto active=read<int>(parent,switcher_index);const auto slots=read<Array<UObject*>>(parent,panel_slots);
                if(!active||!slots||!story_input_detail::valid_native_array(slots->data,slots->num,slots->max,64)||
                   *active<0||*active>=slots->num||slots->data[*active]!=*slot)return nullptr;
            }
            current=parent;continue;
        }
        auto* tree=current->GetOuterPrivate();
        if(live(tree)&&is(tree,L"/Script/UMG.WidgetTree")&&object(tree,tree_root)==current) {
            auto* owner=tree->GetOuterPrivate();
            if(!live(owner)||!is(owner,L"/Script/UMG.UserWidget")||object(owner,user_tree)!=tree)return nullptr;
            current=owner;continue;
        }
        if(depth)*depth=static_cast<int>(count);
        return current;
    }
    return nullptr;
}
bool visible_under(UObject* widget,UObject* root){return widget&&root&&visible_top(widget,root)==root;}
bool reward_counts_settled(UObject* page,UObject* root) {
    // RewardList::SetValue binds the page's base and bonus components to
    // EXP and GILL. Result_Text keeps its own count-up state per row.
    auto* data_type=struct_of(verified_property(page,reward_data),L"/Script/mucha.BTL_RESULT_DATA",192);
    if(!members(data_type,{{L"unitDataList",16,16,L"ArrayProperty"},{L"baseExperience",64,4,L"IntProperty"},
        {L"bonusExperience",68,4,L"IntProperty"},{L"baseMoney",76,4,L"IntProperty"},
        {L"bonusMoney",84,4,L"IntProperty"},{L"baseAp",92,4,L"IntProperty"},{L"bonusAp",96,4,L"IntProperty"}}))return false;
    const auto* data=static_cast<const std::byte*>(field(page,reward_data));
    if(!data||pod<int>(data,24)<=0)return false;
    auto* list=member(page,reward_page,L"WBP_BtlResultPage_RewardList",reward_list_class);
    if(!list||visible_top(list,root,true)!=root)return false;
    struct Row {const wchar_t* name;std::size_t base,bonus;};
    for(const Row row: {Row{L"EXP",64,68},Row{L"GILL",76,84},Row{L"SP",92,96}}) {
        auto* widget=member(list,reward_list_class,row.name,reward_text_class);
        if(!widget)return false;
        if(visible_top(widget,root,true)!=root)continue;
        auto* value=member(widget,reward_text_class,L"Text_Value",L"/Script/UMG.TextBlock");
        if(!value)return false;
        if(visible_top(value,root,true)!=root)continue;
        const auto current=read<double>(widget,reward_current);
        const auto counting=flag(widget,reward_counting);
        const auto base=pod<int>(data,row.base),bonus=pod<int>(data,row.bonus);
        if(!current||!counting||*counting||base<0||bonus<0||std::ceil(*current)!=static_cast<double>(base)+bonus)
            return false;
    }
    return true;
}
// Texts shown below root, in slot order; collapsed subtrees show nothing.
// false if any part of the walk cannot be validated.
bool presented_texts(UObject* root,std::vector<std::string>& out,int depth=0) {
    if(depth>12||!live(root)||!is(root,L"/Script/UMG.Widget"))return false;
    if(out.size()>=16||!shown_self(root))return true;
    if(is(root,L"/Script/UMG.TextBlock")||is(root,L"/Script/UMG.RichTextBlock")) {
        auto value=text_at(field(root,is(root,L"/Script/UMG.TextBlock")?text_block:rich_text));
        if(!value.empty())out.push_back(std::move(value));
        return true;
    }
    if(is(root,L"/Script/UMG.UserWidget")) {
        auto* tree=object(root,user_tree,L"/Script/UMG.WidgetTree");
        const auto top=tree&&tree->GetOuterPrivate()==root?reference(tree,tree_root,L"/Script/UMG.Widget"):std::nullopt;
        return top&&(!*top||presented_texts(*top,out,depth+1));
    }
    if(!is(root,L"/Script/UMG.PanelWidget"))return true;
    const auto slots=read<Array<UObject*>>(root,panel_slots);
    if(!slots||!story_input_detail::valid_native_array(slots->data,slots->num,slots->max,64))return false;
    for(int i=0;i<slots->num;++i)if(auto* child=object(slots->data[i],slot_content,L"/Script/UMG.Widget");child&&!presented_texts(child,out,depth+1))return false;
    return true;
}
UObject* world_of(UObject* o) {
    for(int depth=0;depth<16&&live(o);++depth,o=o->GetOuterPrivate()) {
        if(is(o,L"/Script/Engine.World"))return o;
        if(is(o,L"/Script/Engine.Level")){auto* w=object(o,level_world,L"/Script/Engine.World");return w;}
    }
    return nullptr;
}
std::vector<UObject*> instances(const wchar_t* type) {
    std::vector<UObject*> result;UObjectGlobals::FindAllOf(type,result);
    std::erase_if(result,[](auto* o){return !live(o);});return result;
}
BattleSide side_of(int relation){return relation==1?BattleSide::Ally:relation==2?BattleSide::Enemy:BattleSide::Unknown;}
UObject* unit_vm(UObject* manager,int id) {
    Call call(manager,L"GetUnitParamVM",L"/Script/mucha.CPP_UI_BtlUnitManager:GetUnitParamVM",16,8,
        {{L"InUniqueId",0,4,L"IntProperty",parm|out_parm},{L"ReturnValue",8,8,L"ObjectProperty",returned}});
    if(!call)return nullptr;call.put(0,id);call.invoke(manager);auto* vm=call.get<UObject*>(8);
    return live(vm)&&is(vm,unit_vm_class)?vm:nullptr;
}
// The identification letter widgets show FindIdentifyText(GetDuplicationId()).
class Letters {
    UObject* data_{};bool resolved_{};
public:
    std::string find(int index) {
        if(index<0)return {};
        if(!resolved_) {
            resolved_=true;auto* defaults=UObjectGlobals::StaticFindObject(identify_default);
            const auto flags=defaults?static_cast<std::uint32_t>(defaults->GetObjectFlags()):0u;
            if(defaults&&(flags&0x10u)&&!(flags&dead_flags)&&path(defaults)==identify_default&&path(defaults->GetClassPrivate())==identify_class) {
                auto* p=defaults->GetPropertyByNameInChain(identify_data.name);
                if(p&&p->GetOffset_Internal()==identify_data.offset&&p->GetSize()==8&&p->field_class()&&p->field_class()->GetName()==identify_data.kind) {
                    UObject* da{};std::memcpy(&da,defaults->GetValuePtrByPropertyNameInChain(identify_data.name),sizeof da);
                    if(live(da)&&is(da,L"/Script/mucha.CPP_DA_Btl_UnitIdentify"))data_=da;
                }
            }
        }
        Call call(data_,L"FindIdentifyText",L"/Script/mucha.CPP_DA_Btl_UnitIdentify:FindIdentifyText",32,24,
            {{L"InIndex",0,4,L"IntProperty"},{L"OutText",8,16,L"TextProperty",parm|out_parm},{L"ReturnValue",24,1,L"BoolProperty",returned}});
        if(!call)return {};call.put(0,index);call.invoke(data_);
        return call.get<unsigned char>(24)?text_at(call.at(8)):std::string{};
    }
};
// Exactly what the HUD/target/timeline widgets bind: GetRelation (1 ally,
// 2 enemy), GetShortName (mystery names stay as shown), the duplicate letter.
struct Identity {BattleSide side{};std::string name,letter;int level{-1};bool known{};};
Identity identity(UObject* vm,Letters& letters,bool level) {
    Identity id;
    const auto relation=getter<unsigned char>(vm,L"GetRelation",L"/Script/mucha.CPP_VM_BtlUnitParam:GetRelation",L"EnumProperty");
    auto name=get_text(vm,L"GetShortName",L"/Script/mucha.CPP_VM_UnitParam:GetShortName");
    const auto duplicate=getter<int>(vm,L"GetDuplicationId",L"/Script/mucha.CPP_VM_BtlUnitParam:GetDuplicationId",L"IntProperty");
    if(relation)id.side=side_of(*relation);
    id.known=relation&&name;if(name)id.name=std::move(*name);
    if(duplicate)id.letter=letters.find(*duplicate);
    if(level)id.level=getter<int>(vm,L"GetLevel",L"/Script/mucha.CPP_VM_UnitParam:GetLevel",L"IntProperty").value_or(-1);
    return id;
}
std::optional<int> point(UObject* vm,const Spec& owner,const Spec& value) {
    auto* points=object(vm,owner,L"/Script/mucha.CPP_VM_Point");return points?read<int>(points,value):std::nullopt;
}
// Status shortcuts sample the party card bound to the verified, presented HUD row.
// Each card is a child of the current battle's party list, including pooled cards.
struct PartyCard {UObject* model;UObject* widget;};
std::vector<PartyCard> party_cards(UObject* party) {
    std::vector<PartyCard> result;
    for(auto* widget:instances(L"WBP_Btl_UnitStatus_C")) {
        if(visible_top(widget,party,true)!=party)continue;
        if(auto* vm=viewmodel(widget,party_card_class,L"CPP_VM_BtlUnitParam",unit_vm_class))result.push_back({vm,widget});
    }
    return result;
}
UObject* party_card(const std::vector<PartyCard>& cards,UObject* unit) {
    UObject* result=nullptr;
    for(const auto& card:cards)if(card.model==unit) {
        if(result)return nullptr;
        result=card.widget;
    }
    return result;
}
// The icon widget's native display property has an FGameplayTag at 0 and
// bIsDisplay at 8. This is the same data asset the icon Blueprint queries.
std::optional<bool> condition_displayed(UObject* data,unsigned char condition) {
    Call call(data,L"GetStatusConditionProperty",L"/Script/mucha.CPP_DA_UI_StatusCondition:GetStatusConditionProperty",20,4,
        {{L"InType",0,1,L"EnumProperty"},
         {L"ReturnValue",4,16,L"StructProperty",returned,L"/Script/mucha.UIStatusConditionProperty"}});
    if(!call)return {};
    call.put(0,condition);
    call.invoke(data);
    return !none(call.get<FName>(4))&&call.get<unsigned char>(12)!=0;
}
std::optional<BattleConditions> party_conditions(UObject* card) {
    auto* widget=member(card,party_card_class,L"WBP_UnitStatusConditionList",condition_list_class);
    auto* property=verified_property(widget,condition_effects);
    auto* inner=property?static_cast<FArrayProperty*>(property)->GetInner():nullptr;
    if(!inner||inner->GetSize()!=sizeof(UObject*)||!inner->field_class()||inner->field_class()->GetName()!=L"ObjectProperty")return {};
    const auto effects=read<Array<UObject*>>(widget,condition_effects);
    if(!effects||!story_input_detail::valid_native_array(effects->data,effects->num,effects->max,256))return {};
    BattleConditions output;
    if(effects->num==0)return output;
    if(visible_top(widget,card,true)!=card)return {};
    auto* data=object(widget,condition_display_data,L"/Script/mucha.CPP_DA_UI_StatusCondition");
    if(!data)return {};
    // The game rotates this list through visible icon slots. Use the full
    // displayed set so a request doesn't miss icons on another part of the cycle.
    for(int i=0;i<effects->num;++i) {
        auto* effect=effects->data[i];
        if(!live(effect)||!is(effect,L"/Script/mucha.CPP_ContEffect"))return {};
        const auto condition=getter<unsigned char>(effect,L"GetStatusCondition",L"/Script/mucha.CPP_ContEffect:GetStatusCondition",L"EnumProperty");
        if(!condition)return {};
        const auto displayed=condition_displayed(data,*condition);
        if(!displayed)return {};
        if(!*displayed)continue;
        const auto* label=battle_condition_detail::lookup(*condition);
        if(!label)return {};
        auto& names=label->harmful?output.debuffs:output.buffs;
        auto name=tr(label->label);
        if(std::find(names.begin(),names.end(),name)==names.end())names.push_back(std::move(name));
    }
    return output;
}
BattlePartyStatus party_status(UObject* card,UObject* vm) {
    BattlePartyStatus output;
    if(!card)return output;
    if(auto* view=view_of(card);flag(view,view_constructed).value_or(false)&&flag(view,view_bindings).value_or(false))
        output.effects=party_conditions(card);
    auto* gauge=member(card,party_card_class,L"WBP_LimitGauge",limit_gauge_class);
    if(visible_top(gauge,card,true)!=card)return output;
    auto* limit=object(vm,limit_points,limit_vm_class);
    if(!limit||viewmodel(gauge,limit_gauge_class,L"CPP_VM_LimitPoint",limit_vm_class)!=limit)return output;
    const auto fraction=getter<float>(limit,L"GetPercent",L"/Script/mucha.CPP_VM_Point:GetPercent",L"FloatProperty");
    if(fraction&&std::isfinite(*fraction)&&*fraction>=0.f&&*fraction<=1.f)
        output.limit_percent=static_cast<int>(std::lround(*fraction*100.f));
    const auto has_level=getter<unsigned char>(limit,L"HasLevel",L"/Script/mucha.CPP_VM_LimitPoint:HasLevel",L"BoolProperty");
    if(has_level&&*has_level) {
        const auto current=read<int>(limit,limit_current_level),max=read<int>(limit,limit_max_level);
        if(current&&max&&*current>0&&*current<=*max)output.limit_level=*current;
    }
    return output;
}

void append(std::string& to,const std::string& text) {
    if(text.empty())return;
    if(!to.empty()){
        const char end=to.back();
        const bool complete=end=='.'||end=='!'||end=='?'||
            to.ends_with("。")||to.ends_with("！")||to.ends_with("？");
        to+=(complete?" ":". ");
    }
    to+=text;
}
// The command flow mounts Select Resonance inside the battle layout. The
// cooked WBP_FinishCmd widgets and native FinishCmdEntryData establish which
// displayed skill, direction glyph and cancel guide belong to this prompt.
constexpr wchar_t command_class[]=L"/Game/UI/Battle/Command/WBP_BtlCmdLayout.WBP_BtlCmdLayout_C";
constexpr wchar_t finish_layout[]=L"/Game/UI/Battle/Command/Finish/Widget/WBP_FinishCmdLayout.WBP_FinishCmdLayout_C";
constexpr wchar_t finish_title[]=L"/Game/UI/Battle/Command/Finish/Widget/WBP_FinishCmdTitle.WBP_FinishCmdTitle_C";
constexpr wchar_t finish_menu[]=L"/Game/UI/Battle/Command/Finish/Widget/WBP_FinishCmdMenu.WBP_FinishCmdMenu_C";
constexpr wchar_t finish_entry[]=L"/Game/UI/Battle/Command/Finish/Widget/WBP_FinishCmdEntry.WBP_FinishCmdEntry_C";
constexpr wchar_t finish_key[]=L"/Game/UI/Battle/Command/Finish/Widget/WBP_FinishCmdKey.WBP_FinishCmdKey_C";
constexpr wchar_t finish_vm[]=L"/Script/mucha.CPP_VM_FinishCmdEntry";
constexpr wchar_t skill_entry[]=L"/Game/UI/Common/Skill/Widget/WBP_SkillEntryBase.WBP_SkillEntryBase_C";
constexpr wchar_t action_entry[]=L"/Game/UI/Common/Action/Widget/WBP_ActionEntryBase.WBP_ActionEntryBase_C";
constexpr wchar_t tag_guide[]=L"/Game/UI/MainMenu/Common/Key/KeyGuide/WBP_TagKeyGuide.WBP_TagKeyGuide_C";
constexpr wchar_t key_guide[]=L"/Game/UI/MainMenu/Common/Key/KeyGuide/WBP_KeyGuide.WBP_KeyGuide_C";
constexpr wchar_t key_button[]=L"/Game/UI/Common/Key/Mapping/Widget/WBP_KeyMappingButton.WBP_KeyMappingButton_C";
constexpr wchar_t key_icon[]=L"/Game/UI/Common/KeyIcon/WBP_KeyIcon.WBP_KeyIcon_C";
constexpr wchar_t action_icon[]=L"/Script/EnhancedUI.CPP_EU_WBP_CurrentDeviceKeyIcon";
constexpr wchar_t mapping_icon[]=L"/Script/EnhancedUI.CPP_EU_WBP_KeyMappingIcon";
constexpr Spec entry_data{finish_vm,L"EntryData",104,32,L"StructProperty"};
constexpr Spec mapping_name{mapping_icon,L"MappingName",424,8,L"NameProperty"};
constexpr Spec world_instance{L"/Script/Engine.World",L"OwningGameInstance",552,8,L"ObjectProperty"};
constexpr Spec prompt_input{L"/Script/EnhancedUI.CPP_EU_WBP_Activatable",L"IsCanInput",1345,1,L"BoolProperty"};

UObject* panel_child(UObject* panel,UObject* slot,const wchar_t* type) {
    if(object(slot,slot_parent,L"/Script/UMG.PanelWidget")!=panel)return nullptr;
    auto* child=object(slot,slot_content,type);
    return child&&object(child,widget_slot,L"/Script/UMG.PanelSlot")==slot&&shown_self(child)?child:nullptr;
}
std::string rendered_label(UObject* text,UObject* root) {
    if(!text||visible_top(text,root,true)!=root)return {};
    return text_at(field(text,is(text,L"/Script/UMG.RichTextBlock")?rich_text:text_block));
}
std::string binding_label(UObject* glyph) {
    std::string result;
    for(const auto& label:displayed_key_labels(glyph)) {
        result=result.empty()?label:trf("{0} plus {1}",{result,label});
    }
    return result;
}
std::string finish_cancel(UObject* title,UObject* root) {
    auto* tag=member(title,finish_title,L"WBP_TagKeyGuide",tag_guide);
    auto* guide=member(tag,tag_guide,L"WBP_KeyGuide",key_guide);
    auto* label=member(guide,key_guide,L"Text_Name",L"/Script/UMG.TextBlock");
    auto result=rendered_label(label,root);
    if(result.empty())return result;
    std::string binding;
    for(auto* wrapper:instances(L"WBP_KeyIcon_C")) {
        if(visible_top(wrapper,guide,true)!=guide)continue;
        auto* glyph=member(wrapper,key_icon,L"CPP_EU_WBP_CurrentDeviceKeyIcon_77",action_icon);
        if(!glyph||visible_top(glyph,guide,true)!=guide)continue;
        const auto name=binding_label(glyph);
        if(name.empty())continue;
        binding=binding.empty()?name:trf("{0} or {1}",{binding,name});
    }
    return binding.empty()?result:result+", "+binding;
}
std::vector<std::pair<FName,std::string>> finish_glyphs(UObject* menu,UObject* root) {
    auto* panel=member(menu,finish_menu,L"Overlay_Key",L"/Script/UMG.Overlay");
    const auto slots=read<Array<UObject*>>(panel,panel_slots);
    if(!slots||!story_input_detail::valid_native_array(slots->data,slots->num,slots->max,8))return {};
    std::vector<std::pair<FName,std::string>> result;
    for(int i=0;i<slots->num;++i) {
        auto* glyph=panel_child(panel,slots->data[i],finish_key);
        auto* button=member(glyph,finish_key,L"WBP_KeyMappingButton",key_button);
        auto* icon=member(button,key_button,L"CPP_EU_WBP_KeyMappingIcon_66",mapping_icon);
        if(!icon||visible_top(icon,root,true)!=root)continue;
        if(const auto name=read<FName>(icon,mapping_name);name&&!none(*name))
            result.emplace_back(*name,binding_label(icon));
    }
    return result;
}
UObject* finish_tooltip_target(UObject* gi) {
    constexpr wchar_t vm_type[]=L"/Script/mucha.CPP_VM_Tooltip";
    for(auto* subsystem:instances(L"CPP_TooltipSubsystem")) {
        if(subsystem->GetOuterPrivate()!=gi)continue;
        auto* model=get_object(subsystem,L"GetTooltipVM",L"/Script/mucha.CPP_TooltipSubsystem:GetTooltipVM",vm_type);
        if(!flag(model,{vm_type,L"bDisplay",144,1,L"BoolProperty"}).value_or(false))return nullptr;
        return get_object(model,L"GetTargetWidget",L"/Script/mucha.CPP_VM_Tooltip:GetTargetWidget",L"/Script/UMG.Widget");
    }
    return nullptr;
}
std::string finish_tooltip_text(UObject* gi) {
    auto* layer=active_layout_layer(gi,L"UI.Layer.Tooltip");
    if(!layer||!is(layer,L"/Game/UI/Common/Info/Tooltip/Widget/WBP_TooltipLayout.WBP_TooltipLayout_C"))return {};
    constexpr wchar_t tooltip_skill[]=L"/Game/UI/Common/Info/Tooltip/Widget/WBP_Tooltip_Skill.WBP_Tooltip_Skill_C";
    for(auto* skill:instances(L"WBP_Tooltip_Skill_C")) {
        if(visible_top(skill,layer,true)!=layer)continue;
        auto* description=member(skill,tooltip_skill,L"Text_Description",L"/Script/UMG.RichTextBlock");
        return rendered_label(description,layer);
    }
    return {};
}
std::optional<BattlePrompt> finish_prompt(UObject* layout,UObject* world) {
    auto* command=member(layout,layout_class,L"WBP_BtlCmdLayout",command_class);
    auto* mount=member(command,command_class,L"Overlay_Content",L"/Script/UMG.Overlay");
    auto* root=object(command,{command_class,L"ContentWidget",-1,8,L"ObjectProperty"},finish_layout);
    if(!root||!mount||visible_top(root,mount,true)!=mount||visible_top(mount,layout,true)!=layout||
       !flag(root,prompt_input).value_or(false)||
       !getter<unsigned char>(root,L"IsActivated",L"/Script/CommonUI.CommonActivatableWidget:IsActivated",L"BoolProperty").value_or(0))
        return {};
    auto* title=member(root,finish_layout,L"WBP_FinishCmdTitle",finish_title);
    auto* menu=member(root,finish_layout,L"WBP_FinishCmdMenu",finish_menu);
    const auto heading=rendered_label(designer_of_type(title,L"/Script/UMG.TextBlock"),root);
    if(heading.empty())return {};
    BattlePrompt prompt{utf8(path(root)),heading,{}};
    auto* gi=object(world,world_instance,L"/Script/Engine.GameInstance");
    auto* tooltip=gi?finish_tooltip_target(gi):nullptr;
    const auto glyphs=finish_glyphs(menu,root);
    auto* panel=member(menu,finish_menu,L"Overlay_Entry",L"/Script/UMG.Overlay");
    const auto slots=read<Array<UObject*>>(panel,panel_slots);
    constexpr std::array<const char*,4> positions={"left choice","right choice","top choice","bottom choice"};
    std::array<std::string,4> choices;
    bool incomplete=!slots||!story_input_detail::valid_native_array(slots->data,slots->num,slots->max,8);
    if(!incomplete)for(int i=0;i<slots->num;++i) {
        auto* entry=panel_child(panel,slots->data[i],finish_entry);
        if(!entry)continue;
        auto* model=viewmodel(entry,finish_entry,L"VM_FinishCmdEntry",finish_vm);
        auto* data_type=struct_of(verified_property(model,entry_data),L"/Script/mucha.FinishCmdEntryData",32);
        auto* data=static_cast<const std::byte*>(field(model,entry_data));
        if(!data||!members(data_type,{{L"bEnable",0,1,L"BoolProperty"},{L"SkillId",12,4,L"IntProperty"},
           {L"DirectionProperty",16,16,L"StructProperty"}})) {incomplete=true;continue;}
        if(!pod<unsigned char>(data,0))continue;
        auto* direction=data_type->FindProperty(FName(L"DirectionProperty",FNAME_Find));
        if(!members(struct_of(direction,L"/Script/mucha.FinishCmdDirProperty",16),
           {{L"RelatedNavigation",0,1,L"EnumProperty"},{L"NavigationKeyMappingName",4,8,L"NameProperty"}})) {
            incomplete=true;continue;
        }
        const auto index=pod<unsigned char>(data,16);
        auto* skill=member(entry,finish_entry,L"PathButton",skill_entry);
        auto* action=member(skill,skill_entry,L"WBP_ActionEntryBase",action_entry);
        auto* label=member(action,action_entry,L"Text_Name",L"/Script/UMG.TextBlock");
        const auto ability=rendered_label(label,root);
        const auto current=read<int>(skill,{skill_entry,L"SkillId",-1,4,L"IntProperty"});
        if(index>=choices.size()||ability.empty()||!current||*current!=pod<int>(data,12)) {incomplete=true;continue;}
        const auto name=pod<FName>(data,20);
        const auto glyph=std::find_if(glyphs.begin(),glyphs.end(),[&](const auto& key){return same(key.first,name);});
        const auto binding=glyph==glyphs.end()||glyph->second.empty()?tr(positions[index]):glyph->second;
        choices[index]=ability+", "+binding;
        prompt.id+="|"+std::to_string(index)+":"+ability;
        if(tooltip&&tooltip==action) {
            const auto detail=finish_tooltip_text(gi);
            if(!detail.empty())prompt.selection=ability+". "+detail;
        }
    }
    for(const int i:{2,0,3,1})append(prompt.text,choices[i]);
    if(incomplete)append(prompt.text,tr("Some choices could not be read"));
    if(std::any_of(choices.begin(),choices.end(),[](const auto& choice){return !choice.empty();}))
        append(prompt.text,tr("Press Confirm to use the highlighted Resonance"));
    append(prompt.text,finish_cancel(title,root));
    return prompt;
}
// Number style of DA_UI_BtlUnitDamage (font material per BaseDamageType): heal,
// MP and limit styles are visibly distinct from ordinary damage numbers.
std::string_view category(std::wstring_view type) {
    constexpr std::wstring_view prefix=L"UI.Battle.Unit.Damage.Type.";
    if(!type.starts_with(prefix))return {};type.remove_prefix(prefix.size());
    if(type==L"Increase.HP"||type==L"Resistance.Absorb")return "{0} HP recovered";
    if(type==L"Increase.MP")return "{0} MP recovered";
    if(type==L"Decrease.MP")return "{0} MP damage";
    if(type==L"Increase.LB")return "{0} limit gauge gained";
    if(type==L"Decrease.LB")return "{0} limit gauge lost";
    if(type==L"Decrease.HP"||type.starts_with(L"Resistance.")||type==L"Overkill")return "{0} damage";
    return {};
}
// Top to bottom as WBP_BtlUnitDamageProp stacks them: NumberText, the title
// box (main title, then SetAdditionalTitleText's titles), then Overkill.
std::string popup_text(const std::string& number,const std::vector<std::string>& below,std::wstring_view type) {
    std::string result=number;const auto word=category(type);
    if(!word.empty()&&std::any_of(number.begin(),number.end(),[](unsigned char c){return c>='0'&&c<='9';}))result=trf(word,{number});
    for(const auto& text:below)append(result,text);
    return result;
}
struct NameKey {
    std::uint32_t index{},number{};
    bool operator==(const NameKey&)const=default;
};
NameKey key_of(UObject* o){const auto& n=o->GetNamePrivate();return {n.ComparisonIndex,n.Number};}
struct Popup {UObject* widget{};UObject* model{};UObject* receiver{};};
// A pop-up made by WBP_BtlUnitDamage::SetDamageData: CreateWidget(World) gives
// it a game-instance outer, so ownership is its slot in the receiver's
// CanvasPanel_47 plus its view model, spawned with the receiver as outer.
std::optional<Popup> popup_parts(UObject* widget) {
    if(!live(widget)||!is(widget,popup_class))return {};
    auto* slot=object(widget,widget_slot,L"/Script/UMG.PanelSlot");
    auto* canvas=object(slot,slot_parent,L"/Script/UMG.CanvasPanel");
    if(!canvas||object(slot,slot_content)!=widget)return {};
    auto* tree=canvas->GetOuterPrivate();
    auto* receiver=live(tree)&&is(tree,L"/Script/UMG.WidgetTree")?tree->GetOuterPrivate():nullptr;
    if(!live(receiver)||!is(receiver,damage_class)||member(receiver,damage_class,L"CanvasPanel_47",L"/Script/UMG.CanvasPanel")!=canvas)return {};
    auto* model=viewmodel(widget,popup_class,L"VM_BtlUnitDamageProp",L"/Script/mucha.CPP_VM_BtlUnitDamageProp");
    if(!model||model->GetOuterPrivate()!=receiver)return {};
    return Popup{widget,model,receiver};
}
struct DamageShape {UScriptStruct* data{},*title{},*number{};};
std::optional<DamageShape> damage_shape(UObject* model) {
    auto* data=struct_of(model?model->GetPropertyByNameInChain(L"DamageData"):nullptr,L"/Script/mucha.UIBtlUnitDamageData",64);
    auto* title=struct_of(model?model->GetPropertyByNameInChain(L"TitleProperty"):nullptr,L"/Script/mucha.BtlUnitDamageTitleProperty",232);
    auto* number=struct_of(model?model->GetPropertyByNameInChain(L"NumberProperty"):nullptr,L"/Script/mucha.BtlUnitDamageNumberProperty",208);
    if(!data||!title||!number||!element_base(title)||!element_base(number)||
       !members(data,{{L"UnitUniqueId",0,4,L"IntProperty"},{L"BaseDamageType",4,8,L"StructProperty"},{L"VariationValue",52,4,L"IntProperty"}})||
       !members(title,{{L"Text",208,16,L"TextProperty"}}))return {};
    return DamageShape{data,title,number};
}
std::optional<std::vector<std::string>> additional_titles(UObject* model) {
    Call call(model,L"GetAdditionalParam",L"/Script/mucha.CPP_VM_BtlUnitDamageProp:GetAdditionalParam",32,-1,
        {{L"OutProperties",0,16,L"ArrayProperty",parm|out_parm,nullptr,232,L"/Script/mucha.BtlUnitDamageTitleProperty"},
         {L"OutDatas",16,16,L"ArrayProperty",parm|out_parm,nullptr,176,L"/Script/mucha.BtlUnitDamageText"}});
    if(!call)return {};call.invoke(model);
    const auto list=call.get<Array<std::byte>>(0);
    if(!story_input_detail::valid_native_array(list.data,list.num,list.max,8))return {};
    std::vector<std::string> titles;
    for(int i=0;i<list.num;++i) {
        const auto* entry=list.data+static_cast<std::size_t>(i)*232;
        if(!pod<unsigned char>(entry,0))continue;
        auto title=text_at(entry+208);if(!title.empty())titles.push_back(std::move(title));
    }
    return titles;
}
enum class Readiness {Ready,Waiting,Silent,Unreadable};
struct PopupRead {Readiness state{Readiness::Unreadable};std::string text;};
// A local player of world: its PlayerController is local, in world, and
// points back to it (PlayerController.Player / Player.PlayerController).
bool local_player(UObject* player,UObject* world) {
    if(!world||!live(player)||!is(player,L"/Script/Engine.LocalPlayer"))return false;
    auto* controller=object(player,player_controller,L"/Script/Engine.PlayerController");
    return controller&&world_of(controller)==world&&object(controller,controller_player)==player&&
        getter<unsigned char>(controller,L"IsLocalPlayerController",L"/Script/Engine.Controller:IsLocalPlayerController",L"BoolProperty").value_or(0);
}
// The recipient's own installed unit UI, positively: the battle's manager VM
// for id -> BtlUnitParam -> GetOwnerBtlUnit() in the battle world whose
// m_pUnitParameter is that parameter -> its m_pUnitUI WidgetComponent (owned by
// it) -> GetUserWidgetObject(). shown is the component's own presentation:
// screen space needs bAddedToScreen, IsVisible and the local owner player;
// world space needs IsWidgetVisible, IsVisible and no hidden actor/component.
// Neither proves camera framing or occlusion. No HP/alive/action gates.
enum class Ownership {Verified,Missing,Unverified};
struct UnitUi {Ownership state{Ownership::Unverified};UObject* vm{};UObject* root{};bool shown{};};
UnitUi unit_ui_of(UObject* subsystem,int id) {
    auto* manager=object(subsystem,unit_manager,L"/Script/mucha.CPP_UI_BtlUnitManager");auto* world=world_of(subsystem);
    if(!manager||!world)return {};
    auto* vm=unit_vm(manager,id);if(!vm)return {Ownership::Missing};
    auto* parameter=object(vm,unit_parameter,L"/Script/mucha.CPP_BtlUnitParameter");
    auto* actor=parameter?get_object(parameter,L"GetOwnerBtlUnit",L"/Script/mucha.CPP_BtlUnitParameter:GetOwnerBtlUnit",L"/Script/mucha.CPP_BtlUnit"):nullptr;
    auto* component=actor?object(actor,actor_unit_ui,L"/Script/UMG.WidgetComponent"):nullptr;
    if(!actor||world_of(actor)!=world||object(actor,actor_parameter)!=parameter||!component||
       get_object(component,L"GetOwner",L"/Script/Engine.ActorComponent:GetOwner",L"/Script/Engine.Actor")!=actor)return {};
    auto* root=get_object(component,L"GetUserWidgetObject",L"/Script/UMG.WidgetComponent:GetUserWidgetObject",unit_ui_class);
    const auto space=read<unsigned char>(component,component_space);
    const auto visible=getter<unsigned char>(component,L"IsVisible",L"/Script/Engine.SceneComponent:IsVisible",L"BoolProperty");
    if(!root||!space||!visible)return {};
    UnitUi result{Ownership::Verified,vm,root};
    if(*space==1) {
        const auto added=flag(component,component_on_screen);
        auto* owner=get_object(component,L"GetOwnerPlayer",L"/Script/UMG.WidgetComponent:GetOwnerPlayer",L"/Script/Engine.LocalPlayer");
        if(!added)return {};
        result.shown=*added&&*visible&&local_player(owner,world);
    } else if(*space==0) {
        const auto widget=getter<unsigned char>(component,L"IsWidgetVisible",L"/Script/UMG.WidgetComponent:IsWidgetVisible",L"BoolProperty");
        const auto actor_off=bit(actor,actor_hidden,0x80),component_off=bit(component,component_hidden,0x08);
        if(!widget||!actor_off||!component_off)return {};
        result.shown=*widget&&*visible&&!*actor_off&&!*component_off;
    } else return {};
    return result;
}
UObject* shown_unit_widget(const std::vector<UObject*>& widgets,UObject* root) {
    UObject* shown=nullptr;
    for(auto* candidate:widgets)if(visible_top(candidate,root,true)==root) {
        if(shown)return nullptr;
        shown=candidate;
    }
    return shown;
}
std::string target_health(UObject* cursor,UObject* vm,UObject* root) {
    if(viewmodel(cursor,target_cursor_class,L"CPP_VM_BtlUnitParam",unit_vm_class)!=vm)return {};
    auto* name=member(cursor,target_cursor_class,L"WBP_Btl_UnitSelectName",target_name_class);
    auto* label=member(name,target_name_class,L"Text_Name",L"/Script/UMG.TextBlock");
    if(visible_top(label,root,true)!=root)return {};
    auto* shape=struct_of(verified_property(label,text_color),L"/Script/SlateCore.SlateColor",20);
    if(!members(shape,{{L"SpecifiedColor",0,16,L"StructProperty"},{L"ColorUseRule",16,1,L"EnumProperty"}}))return {};
    const auto* color=static_cast<const std::byte*>(field(label,text_color));
    if(!color||pod<unsigned char>(color,16)!=0||!(pod<float>(color,12)>0))return {};
    const auto matches=[&](float g,float b) {
        return std::abs(pod<float>(color,0)-1.f)<.003f&&
            std::abs(pod<float>(color,4)-g)<.003f&&std::abs(pod<float>(color,8)-b)<.003f;
    };
    // DA_PointRatio's yellow and HP Dying red, as rendered on this target's name.
    if(matches(.654823f,0.f))return tr("Weakened");
    if(matches(.026042f,.054156f))return tr("Critically weakened");
    return {};
}
void target_indicators(UObject* subsystem,UObject* vm,BattleUnit& row,int skill,
                       const std::vector<UObject*>& enemy_widgets,const std::vector<UObject*>& cursors) {
    const auto ui=unit_ui_of(subsystem,row.id);
    if(ui.state!=Ownership::Verified||!ui.shown||ui.vm!=vm)return;
    row.health_indicator=target_health(shown_unit_widget(cursors,ui.root),vm,ui.root);
    auto* enemy=shown_unit_widget(enemy_widgets,ui.root);
    if(!enemy)return;
    auto* gauge=member(enemy,enemy_ui_class,L"WBP_BreakPointGauge",break_gauge_class);
    auto* broken=member(gauge,break_gauge_class,L"Overlay_Break",L"/Script/UMG.Overlay");
    std::vector<std::string> stagger;
    if(visible_top(broken,ui.root,true)==ui.root&&presented_texts(broken,stagger)&&stagger.size()==1)
        row.stagger=std::move(stagger.front());

    auto* predict=member(enemy,enemy_ui_class,L"WBP_BtlSkillResistPredict",prediction_widget);
    auto* appearance=member(predict,prediction_widget,L"WBP_BtlSkillResistPredictAppearance",prediction_appearance);
    auto* label=member(appearance,prediction_appearance,L"Text_EffectName",L"/Script/UMG.TextBlock");
    if(visible_top(label,ui.root,true)!=ui.root)return;
    auto* model=viewmodel(predict,prediction_widget,L"VM_BtlSkillResistPredict",prediction_model);
    if(skill<=0||object(model,prediction_target,unit_vm_class)!=vm||!flag(model,prediction_display).value_or(false)||
       read<int>(model,prediction_skill)!=skill)return;
    // Match the visible label to the current widget's per-skill property.
    // The engine decides when an affinity is known and displays its prediction.
    auto* shape=struct_of(verified_property(model,prediction_data),L"/Script/mucha.UISkillResistPredictProperty",240);
    if(!members(shape,{{L"bDisplay",0,1,L"BoolProperty"},{L"TextData",16,192,L"StructProperty"}}))return;
    auto* text_shape=struct_of(shape->FindProperty(FName(L"TextData",FNAME_Find)),L"/Script/mucha.TextData",192);
    if(!members(text_shape,{{L"Text",0,16,L"TextProperty"}}))return;
    const auto* data=static_cast<const std::byte*>(field(model,prediction_data));
    if(!data||!pod<unsigned char>(data,0))return;
    auto shown=text_at(field(label,text_block));
    if(!shown.empty()&&shown==text_at(data+16))row.target_prediction=std::move(shown);
}
// Copies the pop-up's rendered texts once MVVM has pushed its view model into
// them (NumberText <- DisplayNumberText, title <- TitleProperty, visibilities
// <- bEnable, extra titles <- GetAdditionalParam). Waiting until every shown
// part matches its source, so designer defaults ("12000", "Type") never leak.
PopupRead read_popup(const Popup& p,const UnitUi& ui) {
    const auto shape=damage_shape(p.model);if(!shape||ui.state!=Ownership::Verified)return {};
    auto* view=view_of(p.widget);
    const auto constructed=flag(view,view_constructed),bindings=flag(view,view_bindings);
    if(!constructed||!bindings)return {};
    if(!*constructed||!*bindings||!ui.shown)return {Readiness::Waiting};
    auto* top=visible_top(p.widget);
    if(!top)return {Readiness::Waiting};
    if(top!=ui.root)return {};
    auto* data=static_cast<const std::byte*>(field(p.model,popup_data));
    auto* title=static_cast<const std::byte*>(field(p.model,popup_title));
    auto* number=static_cast<const std::byte*>(field(p.model,popup_number));
    auto* display=field(p.model,popup_display);const auto additional=flag(p.model,popup_additional);
    auto* number_widget=member(p.widget,popup_class,L"NumberText",L"/Script/UMG.TextBlock");
    auto* main=member(p.widget,popup_class,L"WBP_BtlUnitDamageTitleText",title_class);
    auto* main_widget=main?member(main,title_class,L"TitleText",L"/Script/UMG.TextBlock"):nullptr;
    auto* box=member(p.widget,popup_class,L"VerticalBox_TitleText",L"/Script/UMG.PanelWidget");
    auto* overkill=member(p.widget,popup_class,L"WBP_BtlUnitDamage_Overkill",overkill_class);
    auto* number_text=field(number_widget,text_block);auto* main_text=field(main_widget,text_block);
    const auto box_slots=read<Array<UObject*>>(box,panel_slots);
    if(!data||!title||!number||!display||!additional||!number_text||!main_text||!overkill||
       !box_slots||!story_input_detail::valid_native_array(box_slots->data,box_slots->num,box_slots->max,32))return {};
    const bool number_shown=visible_under(number_widget,p.widget),title_shown=visible_under(main_widget,p.widget);
    if(number_shown!=(pod<unsigned char>(number,0)!=0)||title_shown!=(pod<unsigned char>(title,0)!=0))return {Readiness::Waiting};
    std::string shown;std::vector<std::string> below;
    if(number_shown&&(shown=text_at(number_text))!=text_at(display))return {Readiness::Waiting};
    if(title_shown) {
        auto value=text_at(main_text);if(value!=text_at(title+208))return {Readiness::Waiting};
        if(!value.empty())below.push_back(std::move(value));
    }
    std::vector<std::string> extras;
    for(int i=0;i<box_slots->num;++i) {
        auto* child=object(box_slots->data[i],slot_content,L"/Script/UMG.Widget");
        if(!child||child==main||!is(child,title_class))continue;
        auto* extra=member(child,title_class,L"TitleText",L"/Script/UMG.TextBlock");
        if(!extra||!visible_under(extra,p.widget))continue;
        auto value=text_at(field(extra,text_block));if(!value.empty())extras.push_back(std::move(value));
    }
    if(*additional) {
        const auto expected=additional_titles(p.model);if(!expected)return {};
        if(*expected!=extras)return {Readiness::Waiting};
    } else if(!extras.empty())return {Readiness::Waiting};
    below.insert(below.end(),extras.begin(),extras.end());
    if(visible_under(overkill,p.widget)&&!presented_texts(overkill,below))return {};
    PopupRead result;result.text=popup_text(shown,below,name_text(pod<FName>(data,4)));
    result.state=result.text.empty()?Readiness::Silent:Readiness::Ready;
    return result;
}
// The current-world battle of the latest capture, resolved again.
UObject* current_battle(const std::string& world) {
    UObject* subsystem=nullptr;
    for(auto* candidate:instances(L"CPP_UI_BattleSubsystem")) {
        auto* owner=world_of(candidate);
        if(!owner||(!world.empty()&&utf8(path(owner))!=world)||!flag(candidate,during_battle).value_or(false))continue;
        if(subsystem)return nullptr;
        subsystem=candidate;
    }
    return subsystem;
}
// The recipient's displayed identity at presentation time (it may vanish later).
void recipient(const UnitUi& ui,BattleEvent& event) {
    Letters letters;auto id=identity(ui.vm,letters,false);
    event.side=id.side;event.name=std::move(id.name);event.letter=std::move(id.letter);
}
// The single pointer parameter of a Blueprint event, verified by reflection.
UObject* object_param(UFunction* function,void* params,const wchar_t* name) {
    FName key(name,FNAME_Find);auto* p=none(key)?nullptr:function->FindProperty(key);
    if(!params||!p||p->GetOffset_Internal()!=0||p->GetSize()!=8||!p->HasAnyPropertyFlags(parm)||p->HasAnyPropertyFlags(out_parm|return_parm)||
       !p->field_class()||p->field_class()->GetName()!=L"ObjectProperty"||function->GetParmsSize()!=8||
       function->GetReturnValueOffset()!=0xffff||function->GetReturnProperty())return nullptr;
    UObject* value{};std::memcpy(&value,params,sizeof value);return live(value)?value:nullptr;
}
enum class Surface {None,Layout,Sequencer};
}

struct BattleInput::State {
    bool busy{};
    FName display_damage{},show_damage{},begin_display{};
    std::uint32_t name_watermark{};
    // FNAME_Find never adds to the pool; a name not yet published cannot be called.
    bool resolve_names() {
        if(none(display_damage))display_damage=FName(L"HandleDisplayDamage",FNAME_Find);
        if(none(show_damage))show_damage=FName(L"HandleShowDamage",FNAME_Find);
        if(none(begin_display))begin_display=FName(L"BeginDisplay",FNAME_Find);
        return !none(display_damage)&&!none(show_damage)&&!none(begin_display);
    }
    std::string world,subsystem,context,active_context;
    bool in_battle{};
    std::uint64_t generation{},next_sequence{};
    struct Pending {NameKey widget,model;std::string context;BattleEvent event;int attempts{};bool shown{};};
    std::deque<BattleEvent> ready;
    std::vector<Pending> pending;
    std::deque<std::pair<NameKey,NameKey>> presented;   // (pop-up widget, its view model)
    std::string damage_issue,summary;
    std::uint64_t popups{},dropped{};
    void drop_session() {ready.clear();pending.clear();presented.clear();damage_issue.clear();}
    void end_session() {if(in_battle)drop_session();in_battle=false;context.clear();active_context.clear();}
    void begin_session(const std::string& next_world,const std::string& next_subsystem) {
        if(in_battle&&world==next_world&&subsystem==next_subsystem)return;
        drop_session();in_battle=true;world=next_world;subsystem=next_subsystem;
        context="battle:"+world+"|"+subsystem+"#"+std::to_string(++generation);
    }
    bool spoken(NameKey widget,NameKey model)const{return std::find(presented.begin(),presented.end(),std::pair{widget,model})!=presented.end();}
    bool seen(NameKey widget,NameKey model)const {
        return spoken(widget,model)||std::any_of(pending.begin(),pending.end(),[&](const auto& p){return p.widget==widget&&p.model==model;});
    }
    void remember(NameKey widget,NameKey model){presented.emplace_back(widget,model);while(presented.size()>512)presented.pop_front();}
    void lost(bool shown) {
        ++dropped;
        if(shown)damage_issue="Some displayed battle results could not be read.";
    }
    void deliver(BattleEvent event) {
        event.sequence=++next_sequence;event.presented=true;ready.push_back(std::move(event));++popups;
        while(ready.size()>128){ready.pop_front();lost(true);}
    }
};
namespace {
struct Busy {
    bool& flag;
    explicit Busy(bool& value):flag(value){flag=true;}
    ~Busy(){flag=false;}
};
// The battle subsystem of the top widget's owning player's world, else of the
// latest session's world (top may be null while loading). owner_world is set
// only when the local player's world itself was resolved.
struct Located {UObject* controller{};UObject* owner_world{};UObject* world{};UObject* subsystem{};std::optional<bool> during;bool ambiguous{};};
Located locate(UObject* top,const std::string& last_world) {
    Located result;
    if(live(top)&&is(top,L"/Script/UMG.Widget")) {
        result.controller=get_object(top,L"GetOwningPlayer",L"/Script/UMG.Widget:GetOwningPlayer",L"/Script/Engine.PlayerController");
        auto* world=world_of(result.controller);
        if(local_player(object(result.controller,controller_player),world))result.owner_world=world;
    }
    for(auto* candidate:instances(L"CPP_UI_BattleSubsystem")) {
        auto* world=world_of(candidate);if(!world)continue;
        if(result.owner_world?world!=result.owner_world:(last_world.empty()||utf8(path(world))!=last_world))continue;
        if(result.subsystem){result.subsystem=nullptr;result.ambiguous=true;break;}
        result.subsystem=candidate;result.world=world;
    }
    if(result.subsystem)result.during=flag(result.subsystem,during_battle);
    return result;
}
}

BattleInput::BattleInput():state_(std::make_unique<State>()) {}
BattleInput::~BattleInput()=default;
void BattleInput::reset() {
    auto& s=*state_;s.drop_session();s.in_battle=false;s.context.clear();s.active_context.clear();s.world.clear();s.subsystem.clear();
}
std::string BattleInput::diagnostic() const {return state_->summary;}
// Reads only reflected properties; no UFunction is invoked.
BattleResultPage BattleInput::result_page(UObject* top) {
    BattleResultPage result;
    try {
        if(!live(top)||!is(top,result_layout_class)||!is(top,L"/Script/mucha.CPP_WBP_Btl_ResultLayout"))return result;
        auto* pages=object(top,result_pages,L"/Script/mucha.CPP_WBP_Btl_Result_PageList");
        const auto index=read<int>(pages,result_page_index);
        if(!pages||!index){result.diagnostic="RESULT page list unreadable";return result;}
        // Only a page window is a distinct presentation. The reward item
        // list also derives from PageListBase and has its own staging state.
        UObject* page=nullptr;int deepest=0,shown=0;
        for(auto* candidate:instances(L"CPP_WBP_Btl_Result_PageListBase")) {
            int depth=0;
            if((!is(candidate,reward_page)&&!is(candidate,growth_page)&&!is(candidate,level_up_page))||
               visible_top(candidate,top,true,&depth)!=top)continue;
            ++shown;if(depth>deepest){deepest=depth;page=candidate;}
        }
        result.diagnostic="RESULT index="+std::to_string(*index)+" shown="+std::to_string(shown);
        if(!page)return result;
        result.page=std::to_string(*index)+':'+utf8(name_text(page->GetNamePrivate()));
        if(is(page,level_up_page)) {
            auto* detail=object(page,level_up_details,L"/Script/UMG.WidgetSwitcher");
            const auto view=read<int>(detail,switcher_index);
            if(view&&(*view==0||*view==1)) {
                const auto row=read<int>(page,*view==0?level_up_unit:level_up_vision);
                if(row&&*row>=0)result.page+=':'+std::to_string(*view)+':'+std::to_string(*row);
            }
        }
        result.diagnostic+=" page="+result.page;
        auto* manager=object(page,page_staging,L"/Script/mucha.CPP_Btl_Result_StagingManager");
        const auto steps=read<Array<UObject*>>(manager,staging_steps);
        const auto current=read<int>(manager,staging_index);
        if(steps&&current&&story_input_detail::valid_native_array(steps->data,steps->num,steps->max,64)) {
            if(!steps->num||*current==steps->num)result.staging=false;
            else if(*current>=0&&*current<steps->num) {
                auto* step=steps->data[*current];
                // IsPlayStaging stays false during the native count-up. Inspect
                // the actual manager and retain staging between adjacent steps.
                if(*current<steps->num-1||flag(step,step_playing).value_or(false))result.staging=true;
                else if(const auto finished=flag(step,step_finished))result.staging=!*finished;
            }
            result.diagnostic+=" step="+std::to_string(*current)+"/"+std::to_string(steps->num);
        }
        if(is(page,reward_page))result.counting_rewards=!reward_counts_settled(page,top);
        result.diagnostic+=" staging="+(result.staging?std::to_string(*result.staging):std::string("unknown"));
        if(is(page,reward_page))result.diagnostic+=" reward_counts="+std::string(result.counting_rewards?"pending":"finished");
    } catch(const std::exception&) {result={};result.diagnostic="RESULT page could not be read";}
    return result;
}

std::vector<BattleEvent> BattleInput::drain_events() {
    auto& s=*state_;std::vector<BattleEvent> result;
    if(s.active_context.empty())return result;
    for(auto& event:s.ready)if(event.context==s.active_context)result.push_back(std::move(event));
    s.ready.clear();
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.sequence<b.sequence;});
    return result;
}

namespace {
// WBP_BtlUnitDamage::HandleDisplayDamage(UIBtlUnitDamageData DamageData)
// synchronously runs SetDamageData, which creates one pop-up when the base type
// is a valid tag (bytecode). The pop-up shows the view model's
// DisplayNumberText, computed by the native DamageData setter (Ghidra).
template<class State> void display_damage(State& s,UObject* receiver,UFunction* function,void* params) {
    if(!live(receiver)||!is(receiver,damage_class)||path(function)!=std::wstring(damage_class)+L":HandleDisplayDamage")return;
    FName data_name(L"DamageData",FNAME_Find);
    auto* parameter=none(data_name)?nullptr:function->FindProperty(data_name);
    auto* data_type=struct_of(parameter,L"/Script/mucha.UIBtlUnitDamageData",64);
    if(!params||!parameter||parameter->GetOffset_Internal()!=0||parameter->GetSize()!=64||!parameter->HasAnyPropertyFlags(parm)||
       parameter->HasAnyPropertyFlags(out_parm|return_parm)||!parameter->field_class()||parameter->field_class()->GetName()!=L"StructProperty"||
       !data_type||function->GetParmsSize()!=64||function->GetReturnValueOffset()!=0xffff||function->GetReturnProperty()||
       !members(data_type,{{L"UnitUniqueId",0,4,L"IntProperty"},{L"BaseDamageType",4,8,L"StructProperty"},{L"VariationValue",52,4,L"IntProperty"}})) {
        s.damage_issue="Damage numbers could not be read.";return;
    }
    const auto* bytes=static_cast<const std::byte*>(params);
    const auto unit=pod<int>(bytes,0);const auto type=pod<FName>(bytes,4);const auto variation=pod<int>(bytes,52);
    if(none(type))return;
    auto* subsystem=current_battle(s.world);
    if(!subsystem){s.damage_issue="Damage numbers could not be matched to the current battle.";return;}
    s.begin_session(utf8(path(world_of(subsystem))),utf8(path(subsystem)));
    auto* damage_vm=viewmodel(receiver,damage_class,L"VM_BtlUnitDamage",L"/Script/mucha.CPP_VM_BtlUnitDamage");
    if(read<int>(damage_vm,damage_unit)!=unit){s.damage_issue="Damage numbers could not be matched to a unit.";return;}
    auto* canvas=member(receiver,damage_class,L"CanvasPanel_47",L"/Script/UMG.CanvasPanel");
    const auto slots=read<Array<UObject*>>(canvas,panel_slots);
    if(!slots||!story_input_detail::valid_native_array(slots->data,slots->num,slots->max,256)||!slots->num) {
        s.damage_issue="Damage numbers could not be read.";return;
    }
    auto* widget=object(slots->data[slots->num-1],slot_content);
    const auto parts=popup_parts(widget);
    const auto shape=parts?damage_shape(parts->model):std::nullopt;
    auto* data=parts?static_cast<const std::byte*>(field(parts->model,popup_data)):nullptr;
    // The newest canvas child must be this call's pop-up: its view model holds
    // exactly this DamageData. An already captured presentation adds nothing.
    if(parts&&s.seen(key_of(parts->widget),key_of(parts->model)))return;
    if(!parts||parts->receiver!=receiver||!shape||!data||
       pod<int>(data,0)!=unit||!same(pod<FName>(data,4),type)||pod<int>(data,52)!=variation) {
        s.damage_issue="A damage number could not be read.";return;
    }
    const auto ui=unit_ui_of(subsystem,unit);
    if(ui.state!=Ownership::Verified) {
        s.damage_issue=ui.state==Ownership::Missing?"A damage number's target could not be verified.":"A damage number could not be matched to its unit.";
        return;
    }
    BattleEvent event;event.context=s.context;event.unit=unit;recipient(ui,event);
    typename State::Pending pending{key_of(parts->widget),key_of(parts->model),s.context,std::move(event)};
    pending.shown=ui.shown&&visible_top(parts->widget)==ui.root;
    const auto popup=read_popup(*parts,ui);
    if(popup.state==Readiness::Ready){s.remember(pending.widget,pending.model);pending.event.text=popup.text;s.deliver(std::move(pending.event));}
    else if(popup.state==Readiness::Waiting){
        if(s.pending.size()>=32){
            const auto& old=s.pending.front();s.lost(old.shown);s.remember(old.widget,old.model);s.pending.erase(s.pending.begin());
        }
        s.pending.push_back(std::move(pending));
    }
    else if(popup.state==Readiness::Silent)s.remember(pending.widget,pending.model);
    else s.damage_issue="A damage number could not be read.";
}
// WBP_BtlUnitTotalDamage::HandleShowDamage(pValue): only a
// CPP_BtlUIShowTotalDamageDelVal for its own UniqueId shows; SetValue makes the
// number visible only above zero, then the widget turns visible (bytecode).
template<class State> void show_total(State& s,UObject* receiver,UFunction* function,void* params) {
    if(!live(receiver)||!is(receiver,total_class)||path(function)!=std::wstring(total_class)+L":HandleShowDamage")return;
    auto* value=object_param(function,params,L"pValue");
    if(!value||!is(value,L"/Script/mucha.CPP_BtlUIShowTotalDamageDelVal"))return;
    const auto unit=read<int>(value,total_unit),total=read<int>(value,total_value);
    const auto own=read<int>(receiver,{total_class,L"UniqueId",-1,4,L"IntProperty"});
    if(!unit||!total||!own){s.damage_issue="Total damage could not be read.";return;}
    if(*unit!=*own||*total<=0)return;
    auto* tree=receiver->GetOuterPrivate();
    auto* owner=live(tree)&&is(tree,L"/Script/UMG.WidgetTree")?tree->GetOuterPrivate():nullptr;
    auto* subsystem=current_battle(s.world);
    if(!owner||!is(owner,damage_class)||member(owner,damage_class,L"WBP_BtlUnitTotalDamage",total_class)!=receiver||!subsystem||
       read<int>(viewmodel(owner,damage_class,L"VM_BtlUnitDamage",L"/Script/mucha.CPP_VM_BtlUnitDamage"),damage_unit)!=*unit) {
        s.damage_issue="Total damage could not be matched to a unit.";return;
    }
    s.begin_session(utf8(path(world_of(subsystem))),utf8(path(subsystem)));
    auto* number=member(receiver,total_class,L"Text_Num",L"/Script/UMG.TextBlock");
    auto* titles=member(receiver,total_class,L"CommonVisualAttachment_Total",L"/Script/UMG.PanelWidget");
    auto* number_text=field(number,text_block);
    if(!number_text||!titles){s.damage_issue="Total damage could not be read.";return;}
    const auto ui=unit_ui_of(subsystem,*unit);
    if(ui.state!=Ownership::Verified){s.damage_issue="Total damage could not be matched to its unit.";return;}
    if(!ui.shown||visible_top(receiver)!=ui.root||!visible_under(number,receiver))return;
    // SetCurrentValue formats totalDamage; accept only exactly those digits.
    const auto shown=text_at(number_text);std::string digits;
    for(const char c:shown){if(c=='.')break;if(c>='0'&&c<='9')digits+=c;}
    std::vector<std::string> words;
    if(digits!=std::to_string(*total)||(visible_under(titles,receiver)&&!presented_texts(titles,words))) {
        s.damage_issue="Total damage could not be read.";return;
    }
    std::string text;for(const auto& word:words)text+=(text.empty()?"":" ")+word;
    BattleEvent event;event.context=s.context;event.unit=*unit;recipient(ui,event);
    event.text=text.empty()?shown:text+" "+shown;s.deliver(std::move(event));
}
// WBP_BtlUnitDamage_Overkill::BeginDisplay (bound to the overkill view model's
// OnDisplayBegan) shows it. While its pop-up is still unread, that pop-up's own
// read includes it; after the pop-up was spoken it becomes its own event.
template<class State> void show_overkill(State& s,UObject* receiver,UFunction* function) {
    if(!live(receiver)||!is(receiver,overkill_class)||path(function)!=std::wstring(overkill_class)+L":BeginDisplay")return;
    if(function->GetParmsSize()!=0||function->GetReturnValueOffset()!=0xffff||function->GetReturnProperty()) {
        s.damage_issue="An overkill could not be read.";return;
    }
    auto* tree=receiver->GetOuterPrivate();
    auto* popup=live(tree)&&is(tree,L"/Script/UMG.WidgetTree")?tree->GetOuterPrivate():nullptr;
    if(!popup||!is(popup,popup_class)||member(popup,popup_class,L"WBP_BtlUnitDamage_Overkill",overkill_class)!=receiver)return;
    const auto parts=popup_parts(popup);
    if(!parts||!s.spoken(key_of(parts->widget),key_of(parts->model)))return;
    auto* data=static_cast<const std::byte*>(field(parts->model,popup_data));auto* subsystem=current_battle(s.world);
    if(!data||!subsystem){s.damage_issue="An overkill could not be read.";return;}
    const auto ui=unit_ui_of(subsystem,pod<int>(data,0));
    if(ui.state!=Ownership::Verified){s.damage_issue="An overkill could not be matched to its unit.";return;}
    if(!ui.shown||visible_top(receiver)!=ui.root)return;
    std::vector<std::string> words;
    if(!presented_texts(receiver,words)){s.damage_issue="An overkill could not be read.";return;}
    if(words.empty())return;
    s.begin_session(utf8(path(world_of(subsystem))),utf8(path(subsystem)));
    BattleEvent event;event.context=s.context;event.unit=pod<int>(data,0);recipient(ui,event);
    for(const auto& word:words)append(event.text,word);
    s.deliver(std::move(event));
}
}

void BattleInput::on_event(UObject* receiver,UFunction* function,void* params) {
    auto& s=*state_;
    if(s.busy||!receiver||!function)return;
    const auto& name=function->GetNamePrivate();
    // Name-pool indices grow with publication: retry only for a newer name.
    if((none(s.display_damage)||none(s.show_damage)||none(s.begin_display))&&name.ComparisonIndex>s.name_watermark&&!s.resolve_names())
        s.name_watermark=name.ComparisonIndex;
    const bool damage=!none(s.display_damage)&&same(name,s.display_damage),total=!none(s.show_damage)&&same(name,s.show_damage),
        overkill=!none(s.begin_display)&&same(name,s.begin_display);
    if(!damage&&!total&&!overkill)return;
    Busy busy(s.busy);
    try {
        if(damage)display_damage(s,receiver,function,params);
        else if(total)show_total(s,receiver,function,params);
        else show_overkill(s,receiver,function);
    } catch(const std::exception&) {s.damage_issue="Damage numbers could not be read.";}
}

BattleFrame BattleInput::capture(UObject* top,bool include_party_status) {
    auto& s=*state_;BattleFrame frame;
    if(s.busy)return frame;
    Busy busy(s.busy);
    std::string issue;
    Surface surface=Surface::None;bool subsystem_layout=false;
    try {
        s.resolve_names();
        const auto battle=locate(top,s.world);
        if(!battle.subsystem||!battle.during||!*battle.during) {
            // A verified finished battle, or a resolved player world without a
            // battle, ends the session; an unresolved moment only pauses it.
            if((battle.during&&!*battle.during)||(battle.owner_world&&!battle.subsystem&&!battle.ambiguous))s.end_session();
            if(battle.subsystem&&!battle.during)frame.unavailable_reason=tr("Battle state could not be read.");
            s.active_context.clear();
            s.summary="BATTLE inactive subsystem="+std::to_string(battle.subsystem!=nullptr)+" during="+
                (battle.during?std::to_string(*battle.during):std::string("unknown"))+" session="+s.context;
            return frame;
        }
        s.begin_session(utf8(path(battle.world)),utf8(path(battle.subsystem)));
        frame.context=s.context;
        // top comes from the reader's in-viewport layer stack and its local
        // owning player resolved the battle's world. The retest log shows the
        // layout instance replaced mid-battle, so BtlWidget is diagnostic only;
        // the top layout must itself be visible (its container is no panel).
        const auto* weak=static_cast<const FWeakObjectPtr*>(field(battle.subsystem,battle_widget));
        subsystem_layout=weak&&top&&weak->Get()==top;
        auto* layout=battle.owner_world&&live(top)&&is(top,layout_class)?top:nullptr;
        if(layout&&visible_top(layout,layout)==layout)surface=Surface::Layout;
        else if(battle.owner_world&&live(top)&&is(top,sequencer_class)&&is(top,L"/Script/mucha.CPP_WBP_BattleSequencer"))
            surface=Surface::Sequencer;
        frame.active=surface!=Surface::None;
        if(surface==Surface::Layout) {
            frame.prompt=finish_prompt(layout,battle.world);
            auto* model=viewmodel(layout,layout_class,L"CPP_VM_Battle",L"/Script/mucha.CPP_VM_Battle");
            const auto displayed=flag(model,battle_display),event=flag(model,battle_event);
            auto* manager=object(battle.subsystem,unit_manager,L"/Script/mucha.CPP_UI_BtlUnitManager");
            if(!displayed||!event||!manager)issue="Battle status could not be read.";
            else if(*displayed&&!*event) {
                // Units the battle's own manager maps to these view models.
                struct Unit {UObject* vm;int id;bool visible;};
                std::vector<Unit> units;
                for(auto* vm:instances(L"CPP_VM_BtlUnitParam")) {
                    const auto id=read<int>(vm,unit_id);
                    if(id&&unit_vm(manager,*id)==vm)units.push_back({vm,*id,flag(vm,unit_visible).value_or(false)});
                }
                const auto find=[&](int id){return std::find_if(units.begin(),units.end(),[&](const auto& u){return u.id==id;});};
                Letters letters;
                std::vector<int> order;
                auto* party=designer_of_type(layout,party_class);
                auto* party_model=party?viewmodel(party,party_class,L"VM_BtlUnitStatuses",L"/Script/mucha.CPP_VM_BtlUnitStatuses"):nullptr;
                const auto ids=read<Array<int>>(party_model,party_ids);
                std::vector<int> shown;
                if(visible_under(party,layout)) {
                    if(!ids||!story_input_detail::valid_native_array(ids->data,ids->num,ids->max,16))issue="Party status could not be read.";
                    else for(int i=0;i<ids->num;++i)if(find(ids->data[i])!=units.end()){shown.push_back(ids->data[i]);order.push_back(ids->data[i]);}
                }
                const auto cards=include_party_status&&!shown.empty()&&!frame.prompt?party_cards(party):std::vector<PartyCard>{};
                const auto actor=read<int>(model,battle_actor);
                if(actor&&find(*actor)!=units.end()){frame.actor=*actor;order.push_back(*actor);}
                // Target cursors are shown where IsTarget(); the finger cursor's
                // state is TargetSelectState (eSkillTargetSelectStateType Main=1).
                std::vector<int> main_targets;
                for(const auto& unit:units)
                    if(unit.visible&&getter<unsigned char>(unit.vm,L"IsTarget",L"/Script/mucha.CPP_VM_BtlUnitParam:IsTarget",L"BoolProperty").value_or(0)) {
                        frame.targets.push_back(unit.id);order.push_back(unit.id);
                        if(read<unsigned char>(unit.vm,unit_select)==1)main_targets.push_back(unit.id);
                    }
                std::sort(frame.targets.begin(),frame.targets.end());
                frame.choosing_target=!frame.targets.empty();
                if(main_targets.size()==1)frame.focused_target=main_targets.front();
                auto* libra_layout=member(layout,layout_class,L"WBP_Btl_LibraInfoLayout",libra_class);
                auto* libra=libra_layout?viewmodel(libra_layout,libra_class,L"VM_BtlLibra",L"/Script/mucha.CPP_VM_BtlLibra"):nullptr;
                if(visible_under(libra_layout,layout)&&flag(libra,libra_display).value_or(false)) {
                    auto* target=get_object(libra,L"GetTargetUnitParamVM",L"/Script/mucha.CPP_VM_BtlLibra:GetTargetUnitParamVM",unit_vm_class);
                    const auto id=read<int>(target,unit_id);
                    if(!id||find(*id)==units.end()||find(*id)->vm!=target)issue="Libra target could not be read.";
                    else {
                        BattleLibra entry;entry.unit=*id;order.push_back(*id);
                        // HP is a displayed Libra entry only once analysed (DA_BtlLibra).
                        FName hp_tag(L"UI.Battle.Libra.Entry.Style.HP",FNAME_Find);
                        const auto tags=read<Array<FName>>(libra,libra_entries);
                        const bool hp_entry=tags&&!none(hp_tag)&&story_input_detail::valid_native_array(tags->data,tags->num,tags->max,64)&&
                            std::any_of(tags->data,tags->data+tags->num,[&](const FName& tag){return same(tag,hp_tag);});
                        const auto analysed=getter<unsigned char>(libra,L"IsAnalyzed",L"/Script/mucha.CPP_VM_BtlLibra:IsAnalyzed",L"BoolProperty");
                        if(!analysed)issue="Libra details could not be read.";
                        else if(*analysed&&hp_entry){entry.hp=point(target,hit_points,point_current);entry.hp_max=point(target,hit_points,point_max);}
                        frame.libra=std::move(entry);
                    }
                }
                // Every unit standing visibly on the field is presented by its own
                // overhead UI and turn-order entry; identity only, no hidden stats.
                std::vector<int> visible;for(const auto& unit:units)if(unit.visible)visible.push_back(unit.id);
                std::sort(visible.begin(),visible.end());order.insert(order.end(),visible.begin(),visible.end());
                const auto enemy_widgets=frame.choosing_target?instances(L"WBP_Btl_EnemyUI_C"):std::vector<UObject*>{};
                const auto cursors=frame.choosing_target?instances(L"WBP_Btl_UnitSelect_C"):std::vector<UObject*>{};
                const auto selected_skill=read<int>(model,battle_skill).value_or(0);
                std::vector<int> added;
                for(const int id:order) {
                    if(std::find(added.begin(),added.end(),id)!=added.end())continue;
                    added.push_back(id);
                    // Level is shown only by the target name label and the Libra panel.
                    const bool level=std::find(frame.targets.begin(),frame.targets.end(),id)!=frame.targets.end()||(frame.libra&&frame.libra->unit==id);
                    const auto unit=find(id);auto who=identity(unit->vm,letters,level);
                    if(!who.known&&issue.empty())issue="Some battle names could not be read.";
                    BattleUnit row;row.id=id;row.side=who.side;row.name=std::move(who.name);row.letter=std::move(who.letter);row.level=who.level;
                    if(row.side==BattleSide::Ally&&std::find(shown.begin(),shown.end(),id)!=shown.end()) {
                        row.hp=point(unit->vm,hit_points,point_current);row.mp=point(unit->vm,magic_points,point_current);
                        if(include_party_status)row.party_status=party_status(party_card(cards,unit->vm),unit->vm);
                        if((!row.hp||!row.mp)&&issue.empty())issue="Party status could not be read.";
                    }
                    if(row.side==BattleSide::Enemy&&std::binary_search(frame.targets.begin(),frame.targets.end(),id))
                        target_indicators(battle.subsystem,unit->vm,row,selected_skill,enemy_widgets,cursors);
                    frame.units.push_back(std::move(row));
                }
                auto* banner=member(layout,layout_class,L"WBP_Btl_SkillMsg",banner_class);
                auto* banner_text=banner?member(banner,banner_class,L"Text",L"/Script/UMG.RichTextBlock"):nullptr;
                if(visible_under(banner_text,layout))frame.banner=text_at(field(banner_text,rich_text));
            }
        }
        // Pop-ups that were created but not yet verifiably shown: the widget is
        // found again by its unique name and every ownership link is re-proved.
        if(!s.pending.empty()) {
            const auto widgets=instances(L"WBP_BtlUnitDamageProp_C");
            for(auto entry=s.pending.begin();entry!=s.pending.end();) {
                const auto found=std::find_if(widgets.begin(),widgets.end(),[&](auto* w){return key_of(w)==entry->widget;});
                const auto parts=found==widgets.end()?std::nullopt:popup_parts(*found);
                PopupRead popup{Readiness::Silent};
                if(parts&&key_of(parts->model)==entry->model) {
                    const auto ui=unit_ui_of(battle.subsystem,entry->event.unit);
                    entry->shown=entry->shown||(ui.state==Ownership::Verified&&ui.shown&&visible_top(parts->widget)==ui.root);
                    if(ui.state==Ownership::Missing)s.damage_issue="A damage number's target could not be verified.";
                    else popup=read_popup(*parts,ui);
                }
                if(popup.state==Readiness::Ready&&entry->context==s.context) {
                    s.remember(entry->widget,entry->model);entry->event.text=popup.text;s.deliver(std::move(entry->event));
                    entry=s.pending.erase(entry);continue;
                }
                if(popup.state==Readiness::Waiting&&++entry->attempts<120){++entry;continue;}
                if(popup.state==Readiness::Waiting||popup.state==Readiness::Unreadable||!parts||key_of(parts->model)!=entry->model)
                    s.lost(entry->shown);
                if(popup.state==Readiness::Unreadable)s.damage_issue="A damage number could not be read.";
                s.remember(entry->widget,entry->model);entry=s.pending.erase(entry);
            }
        }
    } catch(const std::exception&) {issue="Battle information could not be read.";}
    if(!issue.empty())frame.unavailable_reason=tr(issue);
    if(!s.damage_issue.empty()) {
        append(frame.unavailable_reason,tr(s.damage_issue));
        append(issue,s.damage_issue);
    }
    s.active_context=frame.active?s.context:std::string{};
    s.summary="BATTLE active="+std::to_string(frame.active)+" surface="+
        (surface==Surface::Layout?"layout":surface==Surface::Sequencer?"sequencer":"none")+" btlwidget_top="+std::to_string(subsystem_layout)+" context="+frame.context+
        " units="+std::to_string(frame.units.size())+" actor="+std::to_string(frame.actor)+" targets="+std::to_string(frame.targets.size())+
        " libra="+std::to_string(frame.libra?frame.libra->unit:-1)+" libra_hp="+std::to_string(frame.libra&&frame.libra->hp)+
        " party_hp="+std::to_string(std::count_if(frame.units.begin(),frame.units.end(),[](const auto& u){return u.hp.has_value();}))+
        " banner="+std::to_string(!frame.banner.empty())+" prompt="+std::to_string(frame.prompt.has_value())+
        " pending="+std::to_string(s.pending.size())+" queued="+std::to_string(s.ready.size())+" popups="+std::to_string(s.popups)+
        " dropped="+std::to_string(s.dropped)+" issue="+issue;
    return frame;
}
}
