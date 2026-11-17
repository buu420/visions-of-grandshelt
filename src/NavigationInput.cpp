#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include "resonance/NavigationInput.hpp"
#include "resonance/InputContextPolicy.hpp"
#include "resonance/NativeBoundary.hpp"
#include "resonance/ControllerInput.hpp"
#include "resonance/ControllerBindings.hpp"
#include "resonance/Speech.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

// Signatures are from the exact-build JMAP and Ghidra pass recorded in
// D:\FFRSAccess\evidence\nav-key-audit-20261031\RUNTIME-INPUT-ADAPTER.md and
// NAVIGATION-INPUT-HELPER.md. Each call checks parms size and return offset first.
namespace resonance {
namespace {
using namespace resonance::unreal;

std::string narrow(const std::wstring& text) {
    if(text.empty()) return {};
    int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);
    return result;
}
std::wstring path(UObject* o) {
    if(!o) return {};
    auto name=o->GetFullName(); const auto pos=name.find(L' ');
    return pos==std::wstring::npos?name:name.substr(pos+1);
}
constexpr std::uint32_t excluded_flags=0x10u|0x20u|0x8000u|0x10000u; // CDO, archetype, garbage/unreachable.
bool live(UObject* o) {
    return o && UObjectGlobals::IsValidObjectForFindXOf(o) && (static_cast<std::uint32_t>(o->GetObjectFlags())&excluded_flags)==0;
}
bool is(UObject* o,const wchar_t* type) {
    if(!o) return false;
    for(auto* c=static_cast<UStruct*>(o->GetClassPrivate());c;c=c->GetSuperStruct()) if(path(c)==type) return true;
    return false;
}
std::vector<UObject*> instances(const wchar_t* type) {
    std::vector<UObject*> all; UObjectGlobals::FindAllOf(type,all);
    std::erase_if(all,[](UObject* o){return !live(o);}); return all;
}
// Reflected field pointer only when the live property matches the exact-build offset/size.
void* reflected(UObject* o,const wchar_t* name,int offset,int size) {
    if(!live(o)) return nullptr;
    auto* p=o->GetPropertyByNameInChain(name);
    if(!p||p->GetOffset_Internal()!=offset||p->GetSize()!=size) return nullptr;
    return o->GetValuePtrByPropertyNameInChain(name);
}
UObject* reflected_object(UObject* o,const wchar_t* name,int offset) {
    auto* p=reflected(o,name,offset,8); if(!p) return nullptr;
    UObject* value=nullptr; std::memcpy(&value,p,sizeof value); return live(value)?value:nullptr;
}
UObject* single(const wchar_t* type) { auto all=instances(type); return all.size()==1?all.front():nullptr; }
// CPP_BFL_UI_Input::IsBlocked body (FUN_1451dd8c0): GameWidgetManagerSubsystem
// .m_GameWidgetInputManager(@0x90).InputBlockSet(@0x30): ArrayNum @+0x38 - NumFreeIndices @+0x64 > 0.
InputFact ui_blocked() {
    auto* manager=reflected_object(single(L"CPP_GameWidgetManagerSubsystem"),L"m_GameWidgetInputManager",0x90);
    if(!manager||!reflected(manager,L"InputBlockSet",0x30,80)) return InputFact::Unknown;
    int num=0,free_count=0;
    std::memcpy(&num,reinterpret_cast<std::byte*>(manager)+0x38,4);
    std::memcpy(&free_count,reinterpret_cast<std::byte*>(manager)+0x64,4);
    if(num<0||free_count<0||free_count>num||num>4096) return InputFact::Unknown;
    return num-free_count>0?InputFact::Yes:InputFact::No;
}
// CPP_BFL_TalkEvent::IsPlayingEvent body (FUN_1451a9450): TalkEventSubsystem
// .m_pEventManager(@0x58) present and native byte +0x150 == 0. No manager -> not playing.
InputFact talk_event() {
    auto* subsystem=single(L"CPP_TalkEventSubsystem"); if(!subsystem) return InputFact::Unknown;
    if(!reflected(subsystem,L"m_pEventManager",0x58,8)) return InputFact::Unknown;
    auto* manager=reflected_object(subsystem,L"m_pEventManager",0x58); if(!manager) return InputFact::No;
    unsigned char flag=1; std::memcpy(&flag,reinterpret_cast<std::byte*>(manager)+0x150,1);
    return flag==0?InputFact::Yes:InputFact::No;
}
UObject* world_of(UObject* o) {
    for(int depth=0;live(o)&&depth<16;++depth,o=o->GetOuterPrivate()) {
        if(is(o,L"/Script/Engine.World")) return o;
        // Streamed levels have their own outer package. OwningWorld is the
        // verified ULevel property, so package ancestry is not a world test.
        if(is(o,L"/Script/Engine.Level")) {
            auto* world=reflected_object(o,L"OwningWorld",192);
            if(world&&is(world,L"/Script/Engine.World")) return world;
        }
    }
    return nullptr;
}

// FKey: FName KeyName @0, TSharedPtr<FKeyDetails> {object @8, controller @16}.
// Release mirrors the game's FKey array destructor FUN_1412194e0.
void release_key_details(std::byte* key) {
    void* controller=nullptr; std::memcpy(&controller,key+16,sizeof controller);
    if(!controller) return;
    auto* counts=static_cast<volatile long*>(controller);
    if(_InterlockedDecrement(counts+2)==0) {
        (*reinterpret_cast<void(***)(void*)>(controller))[0](controller);
        if(_InterlockedDecrement(counts+3)==0) (*reinterpret_cast<void(***)(void*,unsigned)>(controller))[1](controller,1);
    }
    std::memset(key+8,0,16);
}
bool find_name(const wchar_t* text,FName& out) {
    out=FName(text,FNAME_Find);
    return out.ComparisonIndex!=0||out.Number!=0;
}

class Call {
    UFunction* fn_{};
    FProperty* result_{};
    int result_offset_{};
    std::vector<std::max_align_t> storage_;
public:
    Call(UObject* object,const wchar_t* name,int parms,int return_offset) {
        if(!object||!UObjectGlobals::IsValidObjectForFindXOf(object)) return;
        auto* fn=object->GetFunctionByNameInChain(name);
        if(!fn)return;
        auto* result=fn->GetReturnProperty();
        if(!result||result->GetOffset_Internal()!=return_offset||
           !result->HasAnyPropertyFlags(0x80)||!result->HasAnyPropertyFlags(0x400)||
           !native_call_extent(fn->GetParmsSize(),fn->GetPropertiesSize(),parms,
               fn->GetReturnValueOffset(),return_offset,return_offset+result->GetSize(),1024))return;
        const int bytes=std::max<int>(parms,fn->GetPropertiesSize());
        if(bytes<=0||bytes>1024) return;
        storage_.assign((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t)+1,std::max_align_t{});
        std::memset(storage_.data(),0,storage_.size()*sizeof(std::max_align_t));
        fn_=fn; result_=fn->GetReturnProperty();
        if(result_) {
            result_offset_=result_->GetOffset_Internal();
            if(result_offset_<0||result_offset_+result_->GetSize()>bytes) {fn_=nullptr;result_=nullptr;return;}
            result_->InitializeValue(data()+result_offset_);
        }
    }
    ~Call() { if(result_) result_->DestroyValue(data()+result_offset_); }
    Call(const Call&)=delete; Call& operator=(const Call&)=delete;
    explicit operator bool() const { return fn_!=nullptr; }
    std::byte* data() { return reinterpret_cast<std::byte*>(storage_.data()); }
    FProperty* parameter(const wchar_t* name) { return fn_?fn_->FindProperty(FName(name,FNAME_Find)):nullptr; }
    void invoke(UObject* o) { o->ProcessEvent(fn_,storage_.data()); }
    template<class T> T get(size_t offset) { T value{}; std::memcpy(&value,data()+offset,sizeof(T)); return value; }
    template<class T> void put(size_t offset,const T& value) { std::memcpy(data()+offset,&value,sizeof(T)); }
};

InputFact fact(bool value) { return value?InputFact::Yes:InputFact::No; }
InputFact get_bool(UObject* o,const wchar_t* name) {
    Call c(o,name,1,0); if(!c) return InputFact::Unknown;
    c.invoke(o); return fact(c.get<unsigned char>(0)!=0);
}
UObject* get_object(UObject* o,const wchar_t* name) {
    Call c(o,name,8,0); if(!c) return nullptr;
    c.invoke(o); auto* result=c.get<UObject*>(0); return live(result)?result:nullptr;
}

template<class T> struct NativeArray { T* data; int num; int max; };

// GetMappedKeysInRow reads every configured slot of a movement mapping from
// the live profile. OutKeys is a caller-owned array of 24-byte FKeys.
bool mapped_movement_down(UObject* profile,UObject* controller) {
    if(!is(profile,L"/Script/EnhancedInput.EnhancedPlayerMappableKeyProfile"))return false;
    Call mapping(profile,L"GetMappedKeysInRow",32,24);
    Call down(controller,L"IsInputKeyDown",32,24);
    auto* out=mapping.parameter(L"OutKeys");
    if(!down||!out||!out->field_class()||out->field_class()->GetName()!=L"ArrayProperty"||
       out->GetOffset_Internal()!=8||out->GetSize()!=16||
       !out->HasAnyPropertyFlags(0x80)||!out->HasAnyPropertyFlags(0x100))return false;
    auto* key_property=static_cast<FArrayProperty*>(out)->GetInner();
    if(!key_property||key_property->GetSize()!=24)return false;
    for(const auto* direction:{L"MapUnit_MoveForward",L"MapUnit_MoveBackwards",L"MapUnit_MoveLeft",L"MapUnit_MoveRight",
            L"Map_UnitMove_Forward",L"Map_UnitMove_Backwards",L"Map_UnitMove_Left",L"Map_UnitMove_Right"}) {
        FName name;
        if(!find_name(direction,name))continue;
        out->InitializeValue(mapping.data()+8);
        mapping.put(0,name);
        mapping.invoke(profile);
        const auto keys=mapping.get<NativeArray<std::byte>>(8);
        bool held=false;
        if(keys.data&&keys.num>0&&keys.num<=64&&keys.max>=keys.num) {
            for(int i=0;i<keys.num&&!held;++i) {
                FName key;
                std::memcpy(&key,keys.data+i*24,sizeof key);
                if(!key.ComparisonIndex&&!key.Number)continue;
                down.put(0,key);
                down.invoke(controller);
                held=down.get<unsigned char>(24)!=0;
                release_key_details(down.data());
            }
        }
        out->DestroyValue(mapping.data()+8);
        if(held)return true;
    }
    return false;
}

// GetMappingNamesForKey: parms 48, InKey @0, OutMappingNames TArray<FName> @24, int @40.
std::optional<std::vector<std::string>> mapping_names(UObject* profile,const wchar_t* key) {
    FName name; if(!find_name(key,name)) return {};
    Call c(profile,L"GetMappingNamesForKey",48,40); if(!c) return {};
    c.put(0,name); c.invoke(profile);
    auto names=c.get<NativeArray<FName>>(24);
    std::optional<std::vector<std::string>> result;
    if(names.num>=0&&names.num<=256&&names.max>=names.num&&(names.num==0||names.data)) {
        result.emplace();
        for(int i=0;i<names.num;++i) result->push_back(narrow(names.data[i].ToString()));
    }
    if(names.data) FMemory::Free(names.data);
    std::memset(c.data()+24,0,16);
    release_key_details(c.data());
    return result;
}

// GetAllIMCStackList: parms 16, return TArray<MUCHA_INPUT_MAPPING_CONTEXT_DATA> @0;
// element 32 bytes: parent u8 @8, children TArray<u8> @16. DestroyValue frees all.
std::optional<std::vector<InputContext>> context_stack(UObject* manager) {
    Call c(manager,L"GetAllIMCStackList",16,0); if(!c) return {};
    c.invoke(manager);
    auto items=c.get<NativeArray<std::byte>>(0);
    if(items.num<0||items.num>64||items.max<items.num||(items.num&&!items.data)) return {};
    std::vector<InputContext> result;
    for(int i=0;i<items.num;++i) {
        const auto* element=items.data+i*32;
        InputContext context; std::memcpy(&context.parent,element+8,1);
        NativeArray<std::uint8_t> children{}; std::memcpy(&children,element+16,sizeof children);
        if(children.num<0||children.num>64||children.max<children.num||(children.num&&!children.data)) return {};
        context.children.assign(children.data,children.data+children.num);
        result.push_back(std::move(context));
    }
    return result;
}

}

bool battle_review_binding_available(const wchar_t* key) {
    auto* subsystem=single(L"CPP_PlayerMappableKeysSubsystem");
    auto* profile=subsystem?get_object(subsystem,L"GetCurrentKeyProfile"):nullptr;
    const auto names=profile?mapping_names(profile,key):std::nullopt;
    if(!names)return false;
    return std::none_of(names->begin(),names->end(),[](const std::string& name) {
        return name.starts_with("Battle")||name.starts_with("UIBattle")||name.starts_with("UI_Battle")||
            name.starts_with("UICommon")||name.starts_with("UI_Common");
    });
}

NativeNavigationInput capture_navigation_input(UObject* pawn,UObject* active_widget,bool controller_gate_only) {
    NativeNavigationInput r;
    bool complete=true;
    auto deny=[&](const char* why){ if(r.reason.empty()) r.reason=why; };
    auto need=[&](InputFact f){ if(f==InputFact::Unknown) complete=false; return f; };

    // Local controlled map unit.
    UObject* controller=nullptr;
    if(live(pawn)&&is(pawn,L"/Script/mucha.CPP_MapUnit_Playable")) {
        controller=get_object(pawn,L"GetController");
        const bool player=controller&&is(controller,L"/Script/Engine.PlayerController");
        const auto local=player?get_bool(controller,L"IsLocalPlayerController"):InputFact::Unknown;
        const auto locally=get_bool(pawn,L"IsLocallyControlled");
        r.local_player_pawn=(local==InputFact::Yes&&locally==InputFact::Yes)?InputFact::Yes:
            (local==InputFact::Unknown||locally==InputFact::Unknown)?InputFact::Unknown:InputFact::No;
        r.unit_input_enabled=get_bool(pawn,L"IsEnableUnitInput");
        r.paused=get_bool(pawn,L"IsPaused");
        r.move_input_ignored=get_bool(pawn,L"IsMoveInputIgnored");
        r.native_move_input=get_bool(pawn,L"IsInputAction");
    }
    need(r.local_player_pawn); need(r.unit_input_enabled); need(r.paused); need(r.move_input_ignored);
    if(r.local_player_pawn!=InputFact::Yes) deny("no local controlled map unit");
    if(r.unit_input_enabled!=InputFact::Yes) deny("unit input disabled");
    if(r.paused!=InputFact::No) deny("map unit paused");
    if(r.move_input_ignored!=InputFact::No) deny("move input ignored");

    // Current-world input manager and its applied context stack.
    UObject* manager=nullptr;
    if(auto* world=world_of(pawn)) for(auto* m:instances(L"CPP_InputManager")) if(world_of(m)==world) {manager=m;break;}
    r.input_manager_enabled=manager?get_bool(manager,L"IsEnableInput"):InputFact::Unknown;
    need(r.input_manager_enabled);
    if(r.input_manager_enabled!=InputFact::Yes) deny("input manager disabled or not in pawn world");
    if(auto stack=manager?context_stack(manager):std::nullopt) {
        r.contexts=std::move(*stack);
        const auto policy=evaluate_input_contexts(r.contexts);
        if(!policy.known)complete=false;
        r.map_unit_context=policy.map_unit_context;r.blocking_context=policy.blocking_context;
        if(policy.text_entry==InputFact::Yes)r.text_entry=InputFact::Yes;
    } else complete=false;
    if(r.map_unit_context!=InputFact::Yes) deny("MapUnit input context absent");
    if(r.blocking_context!=InputFact::No) deny("menu/battle/event/text/map context present");

    // Native UI block tokens and talk events (verified static-library bodies).
    r.ui_blocked=need(ui_blocked());
    r.talk_event=need(talk_event());
    if(r.ui_blocked!=InputFact::No) deny("UI input blocked");
    if(r.talk_event!=InputFact::No) deny("talk event playing");

    // Options key selector capture.
    {
        bool any=false,failed=false;
        for(auto* s:instances(L"CPP_CommonMenu_InputKeySelector")) {
            const auto f=get_bool(s,L"GetIsSelectingKey");
            if(f==InputFact::Unknown) failed=true; else any|=f==InputFact::Yes;
        }
        r.selecting_key=any?InputFact::Yes:failed?InputFact::Unknown:InputFact::No;
    }
    // Focused native editable text (UWidget::HasKeyboardFocus, parms 1).
    if(r.text_entry!=InputFact::Yes) {
        bool any=false,failed=false;
        for(const wchar_t* type:{L"EditableText",L"EditableTextBox",L"MultiLineEditableText",L"MultiLineEditableTextBox"})
            for(auto* w:instances(type)) {
                const auto f=get_bool(w,L"HasKeyboardFocus");
                if(f==InputFact::Unknown) failed=true; else any|=f==InputFact::Yes;
            }
        r.text_entry=any?InputFact::Yes:(failed||r.contexts.empty())?InputFact::Unknown:InputFact::No;
    }
    need(r.selecting_key); need(r.text_entry);
    if(r.selecting_key!=InputFact::No) deny("key selector capturing or unknown");
    if(r.text_entry!=InputFact::No) deny("text entry active or unknown");
    if(active_widget) deny("active widget supplied");

    // Candidate command-key bindings in the current key profile.
    UObject* profile=nullptr;
    if(auto* s=single(L"CPP_PlayerMappableKeysSubsystem"))profile=get_object(s,L"GetCurrentKeyProfile");
    const auto modifier=controller_modifier();
    const std::wstring_view controller_key=modifier==ControllerModifier::RightShoulder?L"Gamepad_RightShoulder":
        modifier==ControllerModifier::RightTrigger?L"Gamepad_RightTrigger":L"Gamepad_LeftTrigger";
    const std::wstring_view controller_axis=modifier==ControllerModifier::RightShoulder?L"":
        modifier==ControllerModifier::RightTrigger?L"Gamepad_RightTriggerAxis":L"Gamepad_LeftTriggerAxis";
    for(const wchar_t* key:{L"Home",L"End",L"PageUp",L"PageDown",L"Delete",L"LeftShift",L"RightShift",L"Gamepad_LeftTrigger",L"Gamepad_LeftTriggerAxis",L"Gamepad_RightTrigger",L"Gamepad_RightTriggerAxis",L"Gamepad_RightShoulder"}) {
        if(controller_gate_only&&std::wstring_view(key)!=controller_key&&std::wstring_view(key)!=controller_axis)continue;
        KeyBindingFact k; k.key=narrow(key);
        if(auto names=profile?mapping_names(profile,key):std::nullopt) {
            k.mappings=*names;
            const bool page_up=std::wstring_view(key)==L"PageUp", page_down=std::wstring_view(key)==L"PageDown";
            const bool other=std::any_of(names->begin(),names->end(),[&](const std::string& n){
                return !((page_up&&n=="UICommon_ScrollUp")||(page_down&&n=="UICommon_ScrollDown")); });
            k.conflict=fact(other);
        }
        r.keys.push_back(std::move(k));
    }
    // Log the modifier's native mapping names and verdict once per change, from the
    // same facts the guard evaluates, so a refused binding names its native action.
    {
        const auto key_name=narrow(std::wstring(controller_key)),axis_name=narrow(std::wstring(controller_axis));
        const KeyBindingFact* button=nullptr;const KeyBindingFact* axis=nullptr;
        for(const auto& k:r.keys){if(k.key==key_name)button=&k;if(!axis_name.empty()&&k.key==axis_name)axis=&k;}
        const bool button_known=button&&button->conflict!=InputFact::Unknown;
        const auto names=button?std::span<const std::string>(button->mappings):std::span<const std::string>{};
        const auto decision=controller_binding_decision(modifier,button_known,names,axis&&axis->conflict!=InputFact::Unknown,
            axis?std::span<const std::string>(axis->mappings):std::span<const std::string>{});
        static std::string last;
        auto line=controller_binding_summary(modifier,key_name,button_known,names,decision);
        if(line!=last){last=std::move(line);log("CONTROLLER binding "+last);}
    }

    r.known=complete;
    if(!complete) deny("native query unavailable");
    r.field_control=complete&&r.reason.empty();
    // Some digital movement has already been consumed by the post-tick read
    // of IsInputAction. Check only keys in the live field movement mappings.
    if(!controller_gate_only&&r.field_control&&r.native_move_input!=InputFact::Yes&&mapped_movement_down(profile,controller))
        r.native_move_input=InputFact::Yes;
    if(r.field_control) {
        auto page=[&](const char* name){ for(const auto& k:r.keys) if(k.key==name) return k.conflict==InputFact::No; return false; };
        r.page_keys_borrowable=page("PageUp")&&page("PageDown");
    }
    return r;
}
}
