#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/UnrealReader.hpp"
#include "resonance/NavigationInput.hpp"
#include "resonance/StoryInput.hpp"
#include "resonance/AreaInput.hpp"
#include "TravelCatalog.hpp"
#include "resonance/ActivationGoal.hpp"
#include "CrystalTowerSpiral.hpp"
#include "resonance/NarrationInput.hpp"
#include "resonance/TutorialInput.hpp"
#include "resonance/NativeBoundary.hpp"
#include "resonance/NativeMetadata.hpp"
#include "resonance/DirectNavigation.hpp"
#include "resonance/NativeWalking.hpp"
#include "resonance/ControllerInput.hpp"
#include "resonance/ControllerBindings.hpp"
#include "resonance/EquipmentInput.hpp"
#include "resonance/Localization.hpp"
#include "resonance/DialogueSource.hpp"
#include "resonance/WorldPlaceNames.hpp"
#include "NativeUiPresentation.hpp"
#include "BuildProfile.hpp"
#include "ChapterThreeBoats.hpp"
#include "ChapterThreeWaterShrine.hpp"
#include "ChapterFourStory.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>

namespace resonance {
namespace { thread_local std::string stage; }
const std::string& reader_stage() { return stage; }
namespace {
using namespace resonance::unreal;
std::string utf8(const std::wstring& text) {
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
bool valid(UObject* o) {
    return o && UObjectGlobals::IsValidObjectForFindXOf(o) &&
        (static_cast<std::uint32_t>(o->GetObjectFlags()) & (0x10u|0x20u|0x8000u|0x10000u))==0;
}
std::vector<UObject*> find(const wchar_t* type,bool defaults=false) {
    std::vector<UObject*> all; UObjectGlobals::FindAllOf(type,all);
    std::erase_if(all,[&](auto* o){return !o || (!defaults&&!valid(o));}); return all;
}
UObject* first(const wchar_t* type) { auto a=find(type); return a.empty()?nullptr:a.front(); }
bool is(UObject* o,const wchar_t* type) {
    if(!o) return false;
    for(auto* c=static_cast<UStruct*>(o->GetClassPrivate());c;c=c->GetSuperStruct()) if(path(c)==type) return true;
    return false;
}
const profile::Property* field_descriptor(const std::wstring& owner,const wchar_t* name) {
    static const auto index=[] {
        std::unordered_map<std::wstring,const profile::Property*> result;
        for(const auto& p:profile::properties) result.emplace(std::wstring(p.owner)+L":"+p.name,&p);
        return result;
    }();
    auto item=index.find(owner+L":"+name);return item==index.end()?nullptr:item->second;
}
const profile::Property* descriptor(UObject* o,const wchar_t* name) {
    if(!o) return nullptr;
    for(auto* c=static_cast<UStruct*>(o->GetClassPrivate());c;c=c->GetSuperStruct()) if(auto* p=field_descriptor(path(c),name)) return p;
    return nullptr;
}
void* property(UObject* o,const wchar_t* name,const wchar_t* type,int size=0) {
    if(!valid(o)) return nullptr;
    const auto* d=descriptor(o,name);
    if(!d || (type && std::wstring_view(d->type)!=type) || (size && d->size!=size)) return nullptr;
    auto* p=o->GetPropertyByNameInChain(name);
    if(!p || p->GetOffset_Internal()!=d->offset || p->GetSize()!=d->size) return nullptr;
    return o->GetValuePtrByPropertyNameInChain(name);
}
template<class T> std::optional<T> read(UObject* o,const wchar_t* name,const wchar_t* type=nullptr) {
    auto* p=property(o,name,type,sizeof(T)); if(!p) return {};
    T value; std::memcpy(&value,p,sizeof(T)); return value;
}
UObject* object(UObject* o,const wchar_t* name) {
    const auto result=read<UObject*>(o,name,L"ObjectProperty");
    return result && valid(*result)?*result:nullptr;
}
bool boolean(UObject* o,const wchar_t* name,bool fallback=false) {
    auto* p=property(o,name,L"BoolProperty",1); const auto* d=o?descriptor(o,name):nullptr;
    return p&&d?(*static_cast<unsigned char*>(p)&d->mask)!=0:fallback;
}
template<class T> struct Array { T* data; int num; int max; };
template<class T> std::vector<T> array(UObject* o,const wchar_t* name) {
    const auto a=read<Array<T>>(o,name,L"ArrayProperty");
    if(!a || a->num<0 || a->num>4096 || a->max<a->num || (a->num&&!a->data)) return {};
    return a->num?std::vector<T>(a->data,a->data+a->num):std::vector<T>{};
}
std::wstring text(UObject* o,const wchar_t* name) {
    auto* p=property(o,name,L"TextProperty",16);
    return p?static_cast<FText*>(p)->ToString():std::wstring{};
}
void* struct_field(UObject* o,const wchar_t* parent,const wchar_t* member,const wchar_t* type,int bytes) {
    const auto* container=descriptor(o,parent);if(!container||std::wstring_view(container->type)!=L"StructProperty") return nullptr;
    const auto* child=field_descriptor(container->struct_type,member);
    if(!child||std::wstring_view(child->type)!=type||child->size!=bytes||child->offset<0||child->offset+bytes>container->size) return nullptr;
    auto* data=property(o,parent,L"StructProperty",container->size);
    return data?static_cast<char*>(data)+child->offset:nullptr;
}
std::wstring struct_text(UObject* o,const wchar_t* parent,const wchar_t* member) {
    auto* p=struct_field(o,parent,member,L"TextProperty",16);return p?static_cast<FText*>(p)->ToString():std::wstring{};
}
std::vector<std::wstring> text_array(UObject* o,const wchar_t* name) {
    const auto* d=descriptor(o,name);
    if(!d||std::wstring_view(d->inner_type)!=L"TextProperty"||d->inner_size!=16) return {};
    auto a=read<Array<std::byte>>(o,name,L"ArrayProperty");
    if(!a||a->num<0||a->num>4096||a->max<a->num||(a->num&&!a->data)) return {};
    std::vector<std::wstring> result;
    for(int i=0;i<a->num;++i) result.push_back(reinterpret_cast<FText*>(a->data+i*16)->ToString());
    return result;
}
std::wstring string_value(UObject* o,const wchar_t* name) {
    auto a=read<Array<wchar_t>>(o,name,L"StrProperty");
    if(!a||a->num<=0||a->num>8192||a->max<a->num||!a->data) return {};
    return std::wstring(a->data,a->data+a->num-(a->data[a->num-1]==L'\0'?1:0));
}
class Call {
    UFunction* fn_{};
    FProperty* result_property_{};
    int result_offset_{};
    std::vector<std::max_align_t> storage_;
public:
    Call(UObject* object,const wchar_t* name,int size,int result,int void_extent=0,bool static_library=false) {
        stage="prepare getter "+utf8(name);
        if(static_library) {
            if(!object||!(static_cast<std::uint32_t>(object->GetObjectFlags())&0x10u))return;
        } else if(!valid(object))return;
        auto* fn=object->GetFunctionByNameInChain(name);
        if(!fn||(static_library&&!fn->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x2000))))return;
        auto* returned=fn->GetReturnProperty();
        if((result>=0&&(!returned||returned->GetOffset_Internal()!=result||
           !returned->HasAnyPropertyFlags(0x80)||!returned->HasAnyPropertyFlags(0x400)))||
           (result<0&&returned))return;
        const int consumed=result>=0?result+returned->GetSize():(void_extent?void_extent:size);
        if(!getter_call_extent(fn->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400)),fn->GetParmsSize(),fn->GetPropertiesSize(),size,
            fn->GetReturnValueOffset(),result,consumed,4096))return;
        const auto bytes=std::max<int>(size,fn->GetPropertiesSize());
        if(bytes<0||bytes>4096) return;
        storage_.resize((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t)+1);
        // UFunction parameter blocks are not script-struct instances. Only the
        // return property needs construction here; all supported input/out args are POD.
        fn_=fn; result_property_=fn->GetReturnProperty();
        if(result_property_) {
            result_offset_=result_property_->GetOffset_Internal();
            if(result_offset_<0||result_offset_+result_property_->GetSize()>bytes) {fn_=nullptr;result_property_=nullptr;return;}
            stage="initialize return "+utf8(path(fn));
            result_property_->InitializeValue(reinterpret_cast<char*>(storage_.data())+result_offset_);
        }
    }
    ~Call() { if(result_property_) {stage="destroy return "+utf8(path(fn_));result_property_->DestroyValue(reinterpret_cast<char*>(storage_.data())+result_offset_);} }
    Call(const Call&)=delete;
    Call& operator=(const Call&)=delete;
    explicit operator bool() const { return fn_!=nullptr; }
    template<class T> void put(size_t offset,T value) { std::memcpy(reinterpret_cast<char*>(storage_.data())+offset,&value,sizeof(T)); }
    template<class T> T get(size_t offset) const { T result{}; std::memcpy(&result,reinterpret_cast<const char*>(storage_.data())+offset,sizeof(T)); return result; }
    void invoke(UObject* o) { stage="ProcessEvent "+utf8(path(fn_));o->ProcessEvent(fn_,storage_.data()); }
    const FText& get_native_text(size_t offset) const {
        return *reinterpret_cast<const FText*>(reinterpret_cast<const char*>(storage_.data())+offset);
    }
    std::wstring get_text(size_t offset) const { return get_native_text(offset).ToString(); }
};
template<class T> std::optional<T> getter(UObject* o,const wchar_t* name,int size=sizeof(T)) {
    Call c(o,name,size,0); if(!c) return {}; c.invoke(o); return c.get<T>(0);
}
UObject* get_object(UObject* o,const wchar_t* name) { auto p=getter<UObject*>(o,name); return p&&valid(*p)?*p:nullptr; }
bool get_bool(UObject* o,const wchar_t* name,bool fallback=false) { return getter<bool>(o,name).value_or(fallback); }
std::wstring get_text(UObject* o,const wchar_t* name=L"GetText") { Call c(o,name,16,0); if(!c) return {}; c.invoke(o); return c.get_text(0); }
std::optional<Vec3> controlled_agent_position(UObject* pawn) {
    auto* root=object(pawn,L"RootComponent");
    auto* movement=object(pawn,L"CharacterMovement");
    auto* updated=object(movement,L"UpdatedComponent");
    // Native Pawn dispatch uses CharacterMovement; field/walking flags do not
    // prove that its UpdatedComponent is attached during a transition.
    if(!root||!is(root,L"/Script/Engine.SceneComponent")||
       !movement||!is(movement,L"/Script/Engine.CharacterMovementComponent")||
       !updated||updated!=root||get_object(movement,L"GetOwner")!=pawn)return {};
    return getter<Vec3>(pawn,L"GetNavAgentLocation");
}
bool visible(UObject* widget) {
    if(!valid(widget)) return false;
    auto v=read<unsigned char>(widget,L"Visibility");
    if(!v || *v==1||*v==2) return false;
    const auto opacity=read<float>(widget,L"RenderOpacity");
    return !opacity || *opacity>.01f;
}
std::vector<UObject*> children(UObject* widget) {
    std::vector<UObject*> result;
    if(auto* tree=object(widget,L"WidgetTree")) if(auto* root=object(tree,L"RootWidget")) result.push_back(root);
    const auto slots=array<UObject*>(widget,L"Slots");
    const bool switcher=is(widget,L"/Script/UMG.WidgetSwitcher");
    const int selected=switcher?read<int>(widget,L"ActiveWidgetIndex",L"IntProperty").value_or(-1):-1;
    for(auto index:presented_children(static_cast<int>(slots.size()),switcher,selected)) {
        if(auto* content=object(slots[index],L"Content")) result.push_back(content);
    }
    // CommonUI keeps its current entry outside UPanelWidget::Slots. Inspect
    // that entry, rather than cached inactive widgets in its pool.
    if(is(widget,L"/Script/CommonUI.CommonActivatableWidgetContainerBase")) {
        if(auto* active=get_object(widget,L"GetActiveWidget");
           active&&std::find(result.begin(),result.end(),active)==result.end()) result.push_back(active);
    }
    // Virtualized list rows are Slate-generated widgets, outside WidgetTree/Slots.
    // Query the list's native displayed entries so its focused row's popup is in scope.
    if(is(widget,L"/Script/UMG.ListViewBase")) {
        Call entries(widget,L"GetDisplayedEntryWidgets",16,0);
        if(entries) {
            entries.invoke(widget);auto live=entries.get<Array<UObject*>>(0);
            if(live.num>=0&&live.num<=4096&&live.max>=live.num&&(!live.num||live.data)) for(int i=0;i<live.num;++i) {
                if(valid(live.data[i])&&std::find(result.begin(),result.end(),live.data[i])==result.end()) result.push_back(live.data[i]);
            }
        }
    }
    return result;
}
void walk(UObject* root,const std::function<void(UObject*)>& action,int depth=0) {
    if(depth>32||!visible(root)) return;
    action(root); for(auto* child:children(root)) walk(child,action,depth+1);
}
std::string clean(std::wstring input) {
    std::wstring output; bool tag=false,space=false;
    for(auto c:input) {
        if(c==L'<') {tag=true;continue;} if(c==L'>'&&tag) {tag=false;continue;} if(tag) continue;
        if(iswspace(c)) {if(!output.empty()) space=true;continue;}
        if(space) {output+=L' ';space=false;} output+=c;
    }
    return utf8(output);
}
bool battle_result_layout(UObject* top) {
    return is(top,L"/Game/UI/Battle/Result/Widget/WBP_Btl_ResultLayout.WBP_Btl_ResultLayout_C");
}
bool timer_layout(UObject* widget) {
    return is(widget,L"/Game/UI/System/Timer/Widget/WBP_TimerLayout.WBP_TimerLayout_C");
}
std::vector<std::string> widget_lines(UObject* root,bool omit_menus,bool omit_dialogue,bool omit_timer,UObject* skip=nullptr) {
    const bool results=battle_result_layout(root);
    std::vector<std::string> pieces,wallet;
    std::set<UObject*> visited,combined;
    std::function<void(UObject*,int,UObject*)> collect=[&](UObject* child,int depth,UObject* wallet_value) {
        if(child==skip||depth>(results?64:32)||!visible(child)||(omit_timer&&timer_layout(child))||
           (omit_menus&&is(child,L"/Script/EnhancedUI.CPP_WBP_MenuBase"))||
           (omit_dialogue&&(is(child,L"/Script/mucha.CPP_WBP_Talk")||is(child,L"/Script/mucha.CPP_WBP_TextWindow")))) return;
        if(results&&(visited.size()>=4096||!visited.insert(child).second))return;
        if(results&&is(child,L"/Game/UI/Common/Player/Money/Widget/WBP_Money.WBP_Money_C"))
            wallet_value=object(child,L"Text_Value");
        if(results&&is(child,L"/Game/UI/Battle/Result/Widget/Parts/WBP_Btl_Result_Text.WBP_Btl_Result_Text_C")) {
            UObject* label_widget=nullptr;
            UObject* number_widget=nullptr;
            walk(child,[&](auto* widget) {
                const auto name=widget->GetNamePrivate().ToString();
                if(name==L"CommonTextBlock_288")label_widget=widget;
                else if(name==L"Text_Value")number_widget=widget;
            });
            if(label_widget&&number_widget) {
                auto label=clean(text(label_widget,L"Text"));
                auto number=clean(text(number_widget,L"Text"));
                if(!label.empty()&&!number.empty()) {
                    pieces.push_back(std::move(label)+" "+number);
                    combined.insert(label_widget);combined.insert(number_widget);
                }
            }
        }
        const bool gradual=is(child,L"/Script/mucha.CPP_GradualTextBlock");
        auto value=presented_text({clean(text(child,L"Text")),true,gradual,!gradual||get_bool(child,L"IsTextFinished")});
        if(value&&!combined.contains(child)&&(results||std::find(pieces.begin(),pieces.end(),*value)==pieces.end())) {
            if(child==wallet_value)wallet.push_back(trf("Gil on hand {0}",{*value}));
            else pieces.push_back(std::move(*value));
        }
        for(auto* next:children(child)) collect(next,depth+1,wallet_value);
    };
    collect(root,0,nullptr);
    pieces.insert(pieces.end(),std::make_move_iterator(wallet.begin()),std::make_move_iterator(wallet.end()));
    return pieces;
}
std::string widget_text(UObject* root,bool omit_menus=false,bool omit_dialogue=false,bool omit_timer=false,UObject* skip=nullptr) {
    std::string result;
    for(const auto& s:widget_lines(root,omit_menus,omit_dialogue,omit_timer,skip)) {
        if(result.size()+s.size()>8192) break;
        if(!result.empty()) {const char c=result.back();result+=(c=='.'||c=='?'||c=='!'?" ":". ");}
        result+=s;
    }
    return result;
}
std::string displayed_timer() {
    UObject* viewport=nullptr;
    for(auto* subsystem:find(L"CPP_EU_GameLayoutSubsystem")) {
        auto* root=object(subsystem,L"PrimaryGameLayout");
        if(!visible(root)||!get_bool(root,L"IsInViewport"))continue;
        if(viewport&&viewport!=root)return {};
        viewport=root;
    }
    std::set<std::string> labels;
    walk(viewport,[&](UObject* widget) {
        if(!timer_layout(widget))return;
        auto value=widget_text(widget);
        const auto colon=value.find(':');
        if(colon>=1&&colon<=3&&value.size()==colon+3&&value[colon+1]>='0'&&value[colon+1]<='5'&&
           std::all_of(value.begin(),value.begin()+colon,[](char c){return c>='0'&&c<='9';})&&
           value.back()>='0'&&value.back()<='9')labels.insert(std::move(value));
    });
    return labels.size()==1?trf("Time remaining. {0}",{*labels.begin()}):std::string{};
}
UObject* top_widget() {
    for(auto* subsystem:find(L"CPP_EU_GameLayoutSubsystem")) {
        auto* layout=object(subsystem,L"PrimaryGameLayout");
        if(!get_bool(layout,L"IsInViewport")) continue;
        auto* layer=object(layout,L"CurrentInputTargetLayer");
        if(!layer) layer=get_object(layout,L"GetTopLayer");
        if(auto* active=get_object(layer,L"GetActiveWidget")) return active;
        return layout;
    }
    // Startup system dialogs can be added straight to the viewport before the
    // EnhancedUI layout exists. Native viewport membership excludes asset templates.
    UObject* result=nullptr;int highest=-2147483647;
    for(auto* widget:find(L"UserWidget")) {
        if(!visible(widget)||!get_bool(widget,L"IsInViewport")) continue;
        const auto order=getter<int>(widget,L"GetZOrderInViewport").value_or(0);
        if(!result||order>=highest) {result=widget;highest=order;}
    }
    return result;
}
bool executable(void* pointer) {
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(pointer,&memory,sizeof(memory))) return false;
    return memory.AllocationBase==GetModuleHandleW(nullptr) && (memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
// The three indices are verified in Ghidra from this exact executable's interface thunks.
template<class R,class... Args> std::optional<R> interface_call(UObject* o,int offset,int slot,Args... args) {
    if(!is(o,L"/Script/EnhancedUI.CPP_WBP_MenuBase")) return {};
    // Validate the native base layout before touching its interface subobject.
    if(!property(o,L"FirstFocusItemIndex",L"IntProperty",4) || !property(o,L"SelectedItemList",L"ArrayProperty",16)) return {};
    auto* address=reinterpret_cast<char*>(o)+offset;
    auto** table=*reinterpret_cast<void***>(address);
    if(!table || !executable(table[slot])) return {};
    return reinterpret_cast<R(*)(void*,Args...)>(table[slot])(address,args...);
}
struct Selection { UObject* entry{}; int index{-1},count{}; };
Selection selection(UObject* menu) {
    // Native CPP_BI_MenuBase is at +1048, input state interface at +1072.
    stage="menu CanReceiveInput "+utf8(path(menu));
    if(!interface_call<bool>(menu,1072,2).value_or(false)) return {};
    stage="menu focused widget "+utf8(path(menu));
    auto entry=interface_call<UObject*,unsigned char>(menu,1048,7,0);
    if(!entry||!valid(*entry)||!visible(*entry)) return {};
    stage="menu focused index "+utf8(path(menu));
    auto index=interface_call<int,unsigned char>(menu,1048,6,0);
    if(!index||*index<0) return {};
    stage="menu count "+utf8(path(menu));
    auto count=interface_call<int>(menu,1048,2);
    if(!entry||!valid(*entry)||!index||!count||*index<0||*index>=*count) return {};
    return {*entry,*index,*count};
}
std::vector<UObject*> menu_items(UObject* menu) {
    if(!is(menu,L"/Script/EnhancedUI.CPP_WBP_MenuBase")||!property(menu,L"FirstFocusItemIndex",L"IntProperty",4)) return {};
    auto* address=reinterpret_cast<char*>(menu)+1048;
    auto** table=*reinterpret_cast<void***>(address);
    if(!table||!executable(table[3])) return {};
    // Exact-build Ghidra execGetItemList calls interface slot 3 with a caller-owned TArray return buffer.
    Array<UObject*> list{};
    reinterpret_cast<void*(*)(void*,void*)>(table[3])(address,&list);
    std::vector<UObject*> result;
    if(list.num>=0&&list.num<=4096&&list.max>=list.num&&(!list.num||list.data)) {
        if(list.num) result.assign(list.data,list.data+list.num);
    }
    if(list.data) FMemory::Free(list.data);
    return result;
}
bool list_widget_matches_selection(UObject* menu,const Selection& selected) {
    auto* list=object(menu,L"ListView");
    if(!is(list,L"/Script/UMG.ListView")) return false;
    Call item(list,L"GetItemAt",16,8);
    if(!item) return false;
    item.put(0,selected.index);
    item.invoke(list);
    auto* expected=item.get<UObject*>(8);
    if(!valid(expected)) return false;

    // ListView can report the new index while the selected Slate entry still
    // displays its previous object. Resolve the binding of that rendered entry.
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/UMG.Default__UserObjectListEntryLibrary");
    if(!is(library,L"/Script/UMG.UserObjectListEntryLibrary")||
       !(static_cast<std::uint32_t>(library->GetObjectFlags())&0x10u)) return false;
    auto* fn=library->GetFunctionByNameInChain(L"GetListItemObject");
    if(!fn||path(fn)!=L"/Script/UMG.UserObjectListEntryLibrary:GetListItemObject"||
       !fn->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400))||fn->GetParmsSize()!=24||
       fn->GetPropertiesSize()!=24||fn->GetReturnValueOffset()!=16) return false;
    auto* arg=fn->FindProperty(FName(L"UserObjectListEntry",FNAME_Find));
    auto* result=fn->GetReturnProperty();
    if(!arg||!arg->HasAnyPropertyFlags(0x80)||arg->GetOffset_Internal()!=0||arg->GetSize()!=16||
       !arg->field_class()||arg->field_class()->GetName()!=L"InterfaceProperty"||
       !result||result->GetOffset_Internal()!=16||result->GetSize()!=8||
       !result->field_class()||result->field_class()->GetName()!=L"ObjectProperty") return false;
    // The shipped getter uses the UObject from FScriptInterface. Some rows
    // implement this interface in Blueprint and have no native interface pointer.
    struct Parameters { UObject* entry; void* interface_address; UObject* item; };
    static_assert(sizeof(Parameters)==24 && offsetof(Parameters,item)==16);
    Parameters params{selected.entry,nullptr,nullptr};
    stage="resolve focused ListView entry binding";
    library->ProcessEvent(fn,&params);
    return params.item==expected;
}
bool menu_widget_matches_selection(UObject* menu,const Selection& selected) {
    if(is(menu,L"/Script/mucha.CPP_WBP_ListViewMenuBase"))
        return list_widget_matches_selection(menu,selected);
    if(is(menu,L"/Script/EnhancedUI.CPP_EU_WBP_DirectionGridMenu")||
       is(menu,L"/Script/EnhancedUI.CPP_EU_WBP_PanelMenu")) {
        const auto items=menu_items(menu);
        if(items.size()==static_cast<std::size_t>(selected.count))
            return items[selected.index]==selected.entry;
    }
    return true;
}
std::optional<MenuSnapshot> option_snapshot(UObject* entry,const Selection& selection,UObject* menu,const std::set<UObject*>& active) {
    if(!is(entry,L"/Script/mucha.CPP_OP_WBP_BaseMenu_EntryBase")) return {};
    OptionObservation option;
    const wchar_t* info=L"EntryInfo";
    for(const auto* candidate:{L"In Info",L"m_EntryInfo",L"EntryInfo",L"Entry"}) {
        auto label=clean(struct_text(entry,candidate,L"DisplayText"));
        if(!label.empty()) {option.label=std::move(label);info=candidate;break;}
    }
    if(option.label.empty()) return MenuSnapshot{utf8(path(menu)),tr("Option text unavailable"),"",selection.index,selection.count,true,true,false};
    if(is(menu,L"/Script/mucha.CPP_OP_List")) {
        const auto data=array<UObject*>(menu,L"NavigateObjects");
        option.binding_matches=false;
        if(selection.index>=0&&selection.index<static_cast<int>(data.size())) {
            auto* actual_tag=struct_field(entry,info,L"EntryTag",L"StructProperty",8);
            auto* expected_tag=struct_field(data[selection.index],L"Items",L"EntryTag",L"StructProperty",8);
            const auto expected_label=clean(struct_text(data[selection.index],L"Items",L"DisplayText"));
            option.binding_matches=actual_tag&&expected_tag&&std::memcmp(actual_tag,expected_tag,8)==0&&expected_label==option.label;
        }
        if(!option.binding_matches) return option_menu(option,utf8(path(menu)),selection.index,selection.count);
    }
    option.enabled=get_bool(entry,L"GetIsEnabled",true);
    auto* info_enabled=struct_field(entry,info,L"Enabled",L"BoolProperty",1);
    if(info_enabled) option.enabled=option.enabled&&*static_cast<bool*>(info_enabled);
    if(is(entry,L"/Script/mucha.CPP_OP_RadioButton")) {
        auto* group=object(entry,L"WBP_OP_RaduiButtonList");
        option.selected=getter<int>(group,L"GetSelectIndex").value_or(-1);
        auto* buttons=object(group,L"WBP_DirectionLayoutMenu");
        for(auto* button:menu_items(buttons)) option.alternatives.push_back(widget_text(button));
    } else if(is(entry,L"/Script/mucha.CPP_CommonMenu_ComboBox")) {
        option.selected=getter<int>(entry,L"GetSelectItem").value_or(-1);
        for(const auto& value:text_array(entry,L"m_ComboBoxItemList")) option.alternatives.push_back(clean(value));
        auto* popup=object(entry,L"WBP_OP_ComboboxList");
        option.popup_open=popup&&active.contains(popup);
        auto* discrete=object(entry,L"Text_DiscreteEntry");
        if(discrete&&active.contains(discrete)) option.presented_value=clean(text(discrete,L"Text"));
        const auto override=clean(string_value(entry,L"m_OverrideSelectText"));
        if(!override.empty()) walk(entry,[&](auto* w){if(clean(text(w,L"Text"))==override) option.presented_value=override;});
        if(option.alternatives.empty()) {
            Call names(entry,L"GetComboBoxItemList",16,0);
            if(names) {
                names.invoke(entry);auto values=names.get<Array<std::byte>>(0);
                if(values.num>=0&&values.num<=4096&&values.max>=values.num&&(!values.num||values.data)) for(int i=0;i<values.num;++i) option.alternatives.push_back(clean(reinterpret_cast<FText*>(values.data+i*16)->ToString()));
            }
        }
    } else if(is(entry,L"/Script/mucha.CPP_WBP_OP_Slider")) {
        option.presented_value=clean(text(object(entry,L"Slider_ValueText"),L"Text"));
    } else if(descriptor(entry,L"currentScroll")&&descriptor(entry,L"Display Text")) {
        option.selected=read<int>(entry,L"currentScroll",L"IntProperty").value_or(-1);
        for(const auto& value:text_array(entry,L"Display Text")) option.alternatives.push_back(clean(value));
    } else if(auto* check=object(entry,L"CheckBox_49")) {
        auto state=getter<unsigned char>(check,L"GetCheckedState");
        if(state&&*state<=2) option.presented_value=tr(*state==0?"Not checked":(*state==1?"Checked":"Mixed"));
    } else {
        option.selection_expected=false;
    }
    auto snapshot=option_menu(option,utf8(path(menu)),selection.index,selection.count);
    stage="option "+option.label+" selected="+std::to_string(option.selected)+" choices="+std::to_string(option.alternatives.size());
    static std::unordered_map<std::string,std::string> diagnostics;
    const auto identity=utf8(path(entry));
    const auto details=stage+" value="+snapshot.value+" popup="+std::to_string(option.popup_open)+" enabled="+std::to_string(option.enabled)+" combo_enabled="+std::to_string(boolean(entry,L"m_EnableComboBox",true));
    if(diagnostics[identity]!=details) {if(diagnostics.size()>128) diagnostics.clear();diagnostics[identity]=details;log("OPTION "+details);}
    return snapshot;
}
bool battle_layout(UObject* top) {
    return is(top,L"/Game/UI/Battle/Widget/WBP_BattleLayout.WBP_BattleLayout_C");
}
bool presented_within(UObject* widget,UObject* root) {
    if(!widget)return false;
    bool found=false;
    walk(root,[&](auto* child){found|=child==widget;});
    return found;
}
template<class T> std::optional<T> blueprint_member(UObject* widget,const wchar_t* owner,
    const wchar_t* name,const wchar_t* kind) {
    if(!valid(widget)||!is(widget,owner))return {};
    auto* prop=widget->GetPropertyByNameInChain(name);
    const auto offset=prop?prop->GetOffset_Internal():-1;
    if(!prop||!prop->field_class()||prop->field_class()->GetName()!=kind||
       prop->GetSize()!=sizeof(T)||offset<0||offset>widget->GetClassPrivate()->GetPropertiesSize()-static_cast<int>(sizeof(T)))return {};
    T result{};
    std::memcpy(&result,reinterpret_cast<const std::byte*>(widget)+offset,sizeof result);
    return result;
}
UObject* blueprint_object(UObject* widget,const wchar_t* owner,const wchar_t* name) {
    const auto member=blueprint_member<UObject*>(widget,owner,name,L"ObjectProperty");
    return member&&valid(*member)?*member:nullptr;
}
void append_menu_text(std::string& result,const std::string& next) {
    if(next.empty())return;
    if(!result.empty()) {
        const char last=result.back();
        result+=last=='.'||last=='?'||last=='!'||result.ends_with("。")||
            result.ends_with("？")||result.ends_with("！")?" ":". ";
    }
    result+=next;
}
constexpr auto main_menu_class=L"/Game/UI/MainMenu/Widget/WBP_MainMenuLayout.WBP_MainMenuLayout_C";
constexpr auto journal_class=L"/Game/UI/MainMenu/Journal/Widget/WBP_JournalLayout.WBP_JournalLayout_C";
constexpr auto journal_row=L"/Game/UI/MainMenu/Journal/Widget/WBP_JournalEntry.WBP_JournalEntry_C";
constexpr auto journal_info=L"/Game/UI/MainMenu/Journal/Widget/WBP_Journal_QuestInfo.WBP_Journal_QuestInfo_C";
constexpr auto scroll_class=L"/Game/UI/Common/Scroll/Widget/WBP_CommonScrollBox.WBP_CommonScrollBox_C";
using PaintGeometry=std::array<std::byte,56>;
struct PaintPoint {double x{},y{};};
struct TextClip {double top{},bottom{};};
bool copy_layout_memory(const void* source,void* destination,std::size_t size) {
    SIZE_T copied{};
    return source&&ReadProcessMemory(GetCurrentProcess(),source,destination,size,&copied)&&copied==size;
}
template<class T> std::optional<T> layout_field(const void* base,std::size_t offset) {
    if(!base)return {};
    T value{};
    if(!copy_layout_memory(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(base)+offset),&value,sizeof value))return {};
    return value;
}
template<class T> std::vector<T> layout_array(const void* base,std::size_t offset,int limit) {
    const auto values=layout_field<Array<T>>(base,offset);
    if(!values||values->num<=0||values->num>limit||values->max<values->num||values->max>limit*4)return {};
    std::vector<T> copy(values->num);
    if(!copy_layout_memory(values->data,copy.data(),copy.size()*sizeof(T)))return {};
    return copy;
}
std::optional<TextClip> journal_clip(UObject* summary,UObject* scroll) {
    auto* slate=UObjectGlobals::StaticFindObject(L"/Script/UMG.Default__SlateBlueprintLibrary");
    auto geometry=getter<PaintGeometry>(summary,L"GetPaintSpaceGeometry");
    auto viewport=getter<PaintGeometry>(scroll,L"GetPaintSpaceGeometry");
    if(!slate||!geometry||!viewport)return {};
    Call size(slate,L"GetLocalSize",72,56,0,true);
    Call to_absolute(slate,L"LocalToAbsolute",88,72,0,true);
    Call to_text(slate,L"AbsoluteToLocal",88,72,0,true);
    if(!size||!to_absolute||!to_text)return {};
    size.put(0,*viewport);
    size.invoke(slate);
    const auto extent=size.get<PaintPoint>(56);
    if(!std::isfinite(extent.x)||!std::isfinite(extent.y)||extent.x<=1||extent.y<=1)return {};
    to_absolute.put(0,*viewport);
    to_text.put(0,*geometry);
    const auto point=[&](double y) {
        to_absolute.put(56,PaintPoint{0,y});
        to_absolute.invoke(slate);
        to_text.put(56,to_absolute.get<PaintPoint>(72));
        to_text.invoke(slate);
        return to_text.get<PaintPoint>(72);
    };
    const auto top=point(0),bottom=point(extent.y);
    if(!std::isfinite(top.y)||!std::isfinite(bottom.y)||bottom.y-top.y<=1||
       !std::isfinite(top.x)||!std::isfinite(bottom.x)||std::abs(bottom.x-top.x)>1)return {};
    return TextClip{top.y,bottom.y};
}
struct SlateTextLine {
    std::byte blocks_and_highlights[0x30];
    double x,y,width,height;
    float text_height,justification_width;
    int begin,end;
    unsigned char direction;
    int model;
};
struct SlateTextModel {
    const void* string;
    std::byte layout_data[0x68];
};
static_assert(sizeof(SlateTextLine)==0x68&&offsetof(SlateTextLine,model)==0x64);
static_assert(sizeof(SlateTextModel)==0x70&&sizeof(wchar_t)==2);

std::string journal_visible_passage(UObject* summary,UObject* scroll) {
    // Build 24250053. URichTextBlock::RefreshTextLayout (0x14363de50)
    // follows MyRichTextBlock and its TextLayoutCache. FSlateTextLayout's
    // own layout pass (0x141950b20) supplies the model and wrapped line arrays.
    if(!property(summary,L"Text",L"TextProperty",16)||summary->GetClassPrivate()->GetPropertiesSize()<0x7b0)return {};
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto rich=layout_field<const void*>(summary,0x7a0).value_or(nullptr);
    if(!image||layout_field<std::uintptr_t>(rich,0)!=image+0x7880748)return {};
    const auto cache=layout_field<const void*>(rich,0x208).value_or(nullptr);
    const auto layout=layout_field<const void*>(cache,0).value_or(nullptr);
    if(layout_field<std::uintptr_t>(layout,0)!=image+0x78753a0||
       (layout_field<unsigned char>(layout,0x90).value_or(1)&1))return {};
    const auto layout_scale=layout_field<float>(layout,0x94).value_or(0);
    const auto render_scale=layout_field<float>(rich,0x600).value_or(0);
    if(!std::isfinite(layout_scale)||layout_scale<=0||!std::isfinite(render_scale)||render_scale<=0)return {};
    const auto clip=journal_clip(summary,scroll);
    if(!clip)return {};
    const auto models=layout_array<SlateTextModel>(layout,0x20,512);
    const auto lines=layout_array<SlateTextLine>(layout,0x30,2048);
    if(models.empty()||lines.empty())return {};

    // Verify Slate has painted the currently bound text. The UMG property
    // can already hold a different quest while Slate still has the old lines.
    std::wstring document;
    std::vector<std::wstring> strings;
    for(const auto& model:models) {
        auto chars=layout_array<wchar_t>(model.string,0,32768);
        if(!chars.empty()&&chars.back()==L'\0')chars.pop_back();
        if(document.size()+chars.size()>32768)return {};
        strings.emplace_back(chars.begin(),chars.end());
        if(!document.empty())document+=L'\n';
        document+=strings.back();
    }
    if(document.empty()||clean(document)!=clean(text(summary,L"Text")))return {};

    std::wstring page;
    int previous=-1,previous_model=-1,previous_end=0;
    const double to_local=static_cast<double>(render_scale)/layout_scale;
    for(std::size_t i=0;i<lines.size();++i) {
        const auto& line=lines[i];
        if(line.model<0||static_cast<std::size_t>(line.model)>=strings.size())return {};
        const auto& source=strings[line.model];
        if(line.begin<0||line.end<line.begin||static_cast<std::size_t>(line.end)>source.size()||
           !std::isfinite(line.y)||!std::isfinite(line.text_height)||line.text_height<0)return {};
        const double baseline=(line.y+line.text_height*.5)*to_local;
        if(baseline<clip->top||baseline>=clip->bottom||line.begin==line.end)continue;
        int begin=line.begin;
        if(previous+1==static_cast<int>(i)&&previous_model==line.model&&previous_end<=begin)
            begin=previous_end;
        else if(!page.empty())page+=L'\n';
        page.append(source,begin,line.end-begin);
        previous=static_cast<int>(i);previous_model=line.model;previous_end=line.end;
    }
    return clean(std::move(page));
}
std::string journal_tab(UObject* journal,UObject* model) {
    constexpr auto tab_class=L"/Game/UI/MainMenu/Journal/Tab/WBP_JournalTabEntry.WBP_JournalTabEntry_C";
    const auto category=read<unsigned char>(model,L"DisplayQuestCategory",L"EnumProperty");
    if(!category)return {};
    std::string label;
    walk(blueprint_object(journal,journal_class,L"WBP_JournalTab"),[&](auto* widget){
        if(blueprint_member<unsigned char>(widget,tab_class,L"QuestCategory",L"EnumProperty")!=category)return;
        auto* name=blueprint_object(widget,tab_class,L"Text_Category");
        if(presented_within(name,widget))label=clean(text(name,L"Text"));
    });
    return label;
}
void journal_presentation(UObject* journal,UObject* model,UObject* row,const JournalScrollInput& input,MenuSnapshot& snapshot) {
    if(!is(row,journal_row))return;
    auto* row_model=blueprint_object(row,journal_row,L"VM_QuestData");
    const auto quest=read<int>(row_model,L"QuestId",L"IntProperty");
    if(!quest||*quest<=0||quest!=read<int>(model,L"FocusQuestId",L"IntProperty"))return;
    auto* info=blueprint_object(journal,journal_class,L"WBP_Journal_QuestInfo");
    if(!presented_within(info,journal)||
       quest!=read<int>(blueprint_object(info,journal_info,L"VM_QuestData"),L"QuestId",L"IntProperty"))return;
    auto* summary=blueprint_object(info,journal_info,L"Text_Summary");
    if(auto detail=widget_text(info,false,false,false,summary);!detail.empty())snapshot.details=std::move(detail);
    auto* owner=blueprint_object(info,journal_info,L"WBP_CommonScrollBox");
    auto* scroll=blueprint_object(owner,scroll_class,L"CPP_MuchaScrollBox");
    if(!is(scroll,L"/Script/UMG.ScrollBox")||!presented_within(scroll,info)||
       !is(summary,L"/Script/UMG.RichTextBlock")||!presented_within(summary,scroll))return;
    const auto offset=getter<float>(scroll,L"GetScrollOffset");
    if(!offset||!std::isfinite(*offset))return;
    auto& page=*snapshot.journal;
    page.offset=*offset;
    if(journal==input.layout&&*quest==input.quest)page.scroll_input=input.at;
    page.text=journal_visible_passage(summary,scroll);
}
std::string recovery_recipient(UObject* selector,UObject* row) {
    constexpr auto selector_class=L"/Game/UI/MainMenu/Party/Widget/WBP_PartySelecter.WBP_PartySelecter_C";
    constexpr auto unit_row=L"/Game/UI/Common/UnitList/Widget/Entry/WBP_UnitDataEntry.WBP_UnitDataEntry_C";
    constexpr auto unit_icon=L"/Game/UI/Common/UnitList/Widget/Entry/Parts/Icon/WBP_UnitList_UnitIcon.WBP_UnitList_UnitIcon_C";
    if(!presented_within(row,selector))return {};
    if(blueprint_member<unsigned char>(selector,selector_class,L"bAllSelect",L"BoolProperty").value_or(0))
        return tr("All allies");
    const auto selected=blueprint_member<int>(selector,selector_class,L"FocusUnitId",L"IntProperty");
    auto* menu=blueprint_object(selector,selector_class,L"WBP_PathMenu");
    const auto focus=read<int>(menu,L"FocusUnitId",L"IntProperty");
    if(!selected||!focus||*selected!=*focus||*focus<=0)return {};
    std::string label;
    walk(row,[&](auto* widget){
        const auto id=blueprint_member<int>(widget,unit_row,L"m_UnitID",L"IntProperty");
        if(id!=selected)return;
        auto* portrait=blueprint_object(widget,unit_row,L"WBP_UnitList_UnitIcon");
        auto* model=blueprint_object(widget,unit_row,L"CPP_VM_UnitParam");
        if(blueprint_member<int>(portrait,unit_icon,L"UnitId",L"IntProperty")!=id||
           read<int>(model,L"UnitId",L"IntProperty")!=id||!presented_within(portrait,widget))return;
        // The native GetUnitName returns the name from the unit parameter
        // associated with this portrait, including its game localization.
        auto* unit=object(model,L"UnitParam");
        if(is(unit,L"/Script/mucha.CPP_UnitParameter"))label=clean(text(unit,L"UnitName"));
    });
    return label;
}
std::string equipment_parameter_name(UObject* brush) {
    constexpr std::wstring_view prefix=L"UI.Common.Unit.Status.Icon.";
    const auto tag=read<FName>(brush,L"BrushTag",L"StructProperty");
    if(!tag)return {};
    const auto name=tag->ToString();
    if(!name.starts_with(prefix))return {};
    const auto suffix=std::wstring_view(name).substr(prefix.size());
    if(suffix==L"AP")return "AP";
    // DA_UI_UnitParamType maps the paint brush on each rendered row to
    // the game's localized stat name in ST_UI_Unit.
    constexpr std::wstring_view known[]={L"Attack",L"Defence",L"Intelligence",L"Mind",L"Agility",
        L"PhysicalAttack",L"PhysicalDefense",L"MagicalAttack",L"MagicalDefense",L"Divine"};
    if(std::find(std::begin(known),std::end(known),suffix)==std::end(known))return {};
    auto key=L"UI_UnitParameter"+std::wstring(suffix);
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/Engine.Default__KismetTextLibrary");
    auto* fn=library?library->GetFunctionByNameInChain(L"TextFromStringTable"):nullptr;
    if(path(fn)!=L"/Script/Engine.KismetTextLibrary:TextFromStringTable")return {};
    const FName table(L"/Game/Datatable/Strings/UI/UI_Common/ST_UI_Unit.ST_UI_Unit",FNAME_Find);
    if(!table.ComparisonIndex)return {};
    const auto parameter=[&](const wchar_t* field,const wchar_t* kind,int offset,int size) {
        auto* p=fn->FindProperty(FName(field,FNAME_Find));
        return p&&p->field_class()&&p->field_class()->GetName()==kind&&
            p->HasAnyPropertyFlags(0x80)&&!p->HasAnyPropertyFlags(0x100)&&
            p->GetOffset_Internal()==offset&&p->GetSize()==size;
    };
    if(!parameter(L"TableId",L"NameProperty",0,8)||!parameter(L"Key",L"StrProperty",8,16))return {};
    Call lookup(library,L"TextFromStringTable",40,24,0,true);
    if(!lookup)return {};
    const auto length=static_cast<int>(key.size())+1;
    lookup.put(0,table);
    lookup.put(8,Array<wchar_t>{key.data(),length,length});
    lookup.invoke(library);
    const auto label=clean(lookup.get_text(24));
    return label.starts_with("MISSING STRING TABLE ENTRY")||label.starts_with("<MISSING")?std::string{}:label;
}
std::optional<std::string> selected_tooltip(UObject* instance,UObject* entry) {
    if(!instance)return {};
    constexpr auto layout_class=L"/Game/UI/Common/Info/Tooltip/Widget/WBP_TooltipLayout.WBP_TooltipLayout_C";
    constexpr auto tip_class=L"/Game/UI/Common/Info/Tooltip/Widget/WBP_Tooltip.WBP_Tooltip_C";
    constexpr auto action_entry=L"/Game/UI/Common/Action/Widget/WBP_ActionEntryBase.WBP_ActionEntryBase_C";
    auto* layer=active_layout_layer(instance,L"UI.Layer.Tooltip");
    auto* vm=blueprint_object(layer,layout_class,L"VM_Tooltip");
    if(!is(vm,L"/Script/mucha.CPP_VM_Tooltip"))return {};
    if(!boolean(vm,L"IsDisplayTooltip",true))return std::string{};
    if(!boolean(vm,L"bDisplay"))return {};
    auto* anchor=get_object(vm,L"GetTargetWidget");
    if(!presented_within(anchor,entry))return {};
    if(is(anchor,action_entry)) {
        const auto id=[](UObject* owner){return static_cast<const int*>(struct_field(owner,L"ActionKey",L"ID",L"IntProperty",4));};
        const auto category=[](UObject* owner){return static_cast<const unsigned char*>(struct_field(owner,L"ActionKey",L"Category",L"EnumProperty",1));};
        const auto* expected_id=id(anchor);
        const auto* actual_id=id(vm);
        const auto* expected_category=category(anchor);
        const auto* actual_category=category(vm);
        if(!expected_id||!actual_id||!expected_category||!actual_category||
           *expected_id!=*actual_id||*expected_category!=*actual_category)return {};
    }
    auto* tip=blueprint_object(layer,layout_class,L"WBP_Tooltip");
    if(!is(tip,tip_class)||!presented_within(tip,layer))return {};
    auto* slot=blueprint_object(tip,tip_class,L"CPP_CommonContentSlot_81");
    if(!presented_within(slot,tip))return {};

    constexpr auto gear_row=L"/Game/UI/Common/Info/Item/Equipment/WBP_EquipParamEntry.WBP_EquipParamEntry_C";
    std::string description,stats;
    walk(slot,[&](auto* widget){
        if(is(widget,gear_row)) {
            auto* icon=object(widget,L"WBP_UnitParamIcon");
            auto* brush=object(icon,L"CPP_WBP_GameplayTagBrush_89");
            auto* number=object(widget,L"CommonNumericTextBlock_53");
            if(!presented_within(brush,widget)||!presented_within(number,widget))return;
            const auto label=equipment_parameter_name(brush);
            const auto value=clean(get_text(number));
            if(!label.empty()&&!value.empty())append_menu_text(stats,label+" "+value);
        } else {
            auto* label=object(widget,L"Text_Description");
            if(presented_within(label,widget))append_menu_text(description,clean(text(label,L"Text")));
        }
    });
    append_menu_text(description,stats);
    return description.empty()?std::nullopt:std::optional<std::string>{std::move(description)};
}
std::string menu_guides(UObject* top,UObject* instance) {
    constexpr auto key_guide=L"/Game/UI/MainMenu/Common/Key/KeyGuide/WBP_KeyGuide.WBP_KeyGuide_C";
    auto* layer=instance?active_layout_layer(instance,L"UI.Layer.ActionGuide"):nullptr;
    UObject* owner=nullptr;
    if(visible(layer))for(auto* subsystem:find(L"CPP_ActionGuideSubsystem")) {
        if(subsystem->GetOuterPrivate()!=instance)continue;
        auto* weak=static_cast<const FWeakObjectPtr*>(property(subsystem,L"GuideOwner",L"WeakObjectProperty",8));
        auto* model=object(subsystem,L"ActionGuideVM");
        if(weak&&boolean(model,L"bDisplay"))owner=weak->Get();
        break;
    }
    std::vector<std::string> hints;
    const auto collect=[&](UObject* root){
        walk(root,[&](auto* widget){
            if(!is(widget,key_guide)||!get_bool(widget,L"GetIsEnabled"))return;
            auto* text_widget=object(widget,L"Text_Name");
            if(!presented_within(text_widget,widget))return;
            auto label=clean(text(text_widget,L"Text"));
            if(label.empty())return;
            std::string bindings;
            walk(widget,[&](auto* glyph){
                if(!is(glyph,L"/Script/EnhancedUI.CPP_EU_WBP_CurrentDeviceKeyIcon")&&
                   !is(glyph,L"/Script/EnhancedUI.CPP_EU_WBP_KeyMappingIcon"))return;
                std::string chord;
                for(const auto& key:displayed_key_labels(glyph))
                    chord=chord.empty()?key:trf("{0} plus {1}",{chord,key});
                if(!chord.empty())bindings=bindings.empty()?chord:trf("{0} or {1}",{bindings,chord});
            });
            if(bindings.empty())return;
            label+=", "+bindings;
            if(std::find(hints.begin(),hints.end(),label)==hints.end())hints.push_back(std::move(label));
        });
    };
    collect(top);
    if(presented_within(owner,top))collect(layer);
    std::string result;
    for(const auto& hint:hints)append_menu_text(result,hint);
    return result;
}
std::optional<MenuSnapshot> menu_snapshot(UObject* top,UObject* instance,
    const JournalScrollInput& journal_input,bool read_screen_text=true) {
    // FindAllOf also sees Blueprint WidgetTree templates with no live Slate/list data.
    // Never invoke menu interfaces outside the current viewport's active widget tree.
    if(!top) return {};
    std::set<UObject*> active;bool dialogue_window=false;
    UObject* equipment_scene=nullptr;
    UObject* journal=nullptr;
    UObject* recovery=nullptr;
    const bool main_menu=is(top,main_menu_class);
    constexpr auto action_layout=L"/Game/UI/MainMenu/Action/Widget/WBP_ActionLayoutBase.WBP_ActionLayoutBase_C";
    walk(top,[&](auto* w){
        active.insert(w);
        if(is(w,L"/Script/mucha.CPP_WBP_TextWindow")&&visible(object(w,L"m_pTextBlock"))) dialogue_window=true;
        if((main_menu||w==top)&&
           is(w,L"/Game/UI/MainMenu/ActionEquip/WBP_ActionEquipmentLayout.WBP_ActionEquipmentLayout_C")) equipment_scene=w;
        if(main_menu||w==top) {
            if(is(w,journal_class))journal=w;
            if(is(w,action_layout))recovery=w;
        }
    });
    auto* journal_model=blueprint_object(journal,journal_class,L"VM_Journal");
    const auto tab=journal_tab(journal,journal_model);
    const auto scene=main_menu?getter<FName>(top,L"GetCurrentScene"):std::nullopt;
    const bool journal_scene=journal||(scene&&scene->ToString()==L"UI.MainMenu.Scene.Journal");
    const auto mark_journal=[journal_scene](MenuSnapshot m) {
        if(journal_scene)m.journal.emplace();
        return m;
    };
    auto* recipient_selector=blueprint_object(recovery,action_layout,L"WBP_TargetUnitList");
    const bool options_context=is(top,L"/Script/mucha.CPP_OP_Layout");
    UObject* target=top;
    for(int n=0;n<12&&target;++n) {
        auto* next=get_object(target,L"BP_GetInputTargetWidget");
        if(!next) next=object(target,L"PathMenu");
        if(!next) next=object(target,L"PathButton");
        if(!next||next==target) break; target=next;
    }
    auto menus=find(L"CPP_WBP_MenuBase");
    std::stable_sort(menus.begin(),menus.end(),[&](auto* a,auto* b){return a==target && b!=target;});
    for(auto* menu:menus) {
        if(!visible(menu) || (!active.contains(menu) && menu!=target)) continue;
        auto s=selection(menu); if(!s.entry || !visible(s.entry)) continue;
        const bool ready=!boolean(top,L"IsOpeningProcess")&&!boolean(top,L"IsClosingProcess");
        if(!menu_widget_matches_selection(menu,s))
            return mark_journal({utf8(path(menu)),"","",s.index,s.count,true,false,true});
        if(options_context) {
            UObject* setting=nullptr;
            walk(s.entry,[&](auto* widget){if(!setting&&is(widget,L"/Script/mucha.CPP_OP_WBP_BaseMenu_EntryBase")) setting=widget;});
            if(auto option=option_snapshot(setting,s,menu,active)) {
                option->ready=option->ready&&ready;
                return option;
            }
        }
        auto label=widget_text(s.entry);
        if(equipment_scene) {
            if(auto equipment=equipment_row_snapshot(equipment_scene,menu,s.entry,s.index,s.count,label)) {
                equipment->ready=equipment->ready&&ready;
                if(equipment->ready)equipment->details=selected_tooltip(instance,s.entry);
                return equipment;
            }
        }
        if(presented_within(menu,recipient_selector)) {
            const auto recipient=recovery_recipient(recipient_selector,s.entry);
            if(recipient.empty())return MenuSnapshot{utf8(path(menu)),"","",s.index,s.count,true,false,true};
            label=recipient;
        }
        if(label.empty()) continue;
        auto snapshot=mark_journal({utf8(path(menu)),label,"",s.index,s.count,true,ready,get_bool(s.entry,L"GetIsEnabled",true)});
        if(ready&&journal) {
            if(!tab.empty()&&is(s.entry,journal_row))snapshot.label=tab+". "+snapshot.label;
            journal_presentation(journal,journal_model,s.entry,journal_input,snapshot);
        } else if(ready)snapshot.details=selected_tooltip(instance,s.entry);
        return snapshot;
    }
    if(options_context) return MenuSnapshot{utf8(path(top)),"","",-1,0,true,false,true};
    if(journal_scene) {
        const auto quests=read<Array<std::byte>>(journal_model,L"DisplayQuestDataList",L"ArrayProperty");
        const bool empty=quests&&quests->num==0&&quests->max>=0&&!boolean(journal_model,L"bIsInitializing",true);
        return mark_journal({utf8(path(journal)),tab,"",-1,0,true,
            empty&&!tab.empty()&&!boolean(top,L"IsOpeningProcess")&&!boolean(top,L"IsClosingProcess"),true});
    }
    // Battle HUD, timeline, skill banner and target panel have separate roles.
    // Their combined tree text is not a menu selection.
    if(read_screen_text&&!battle_layout(top)&&!dialogue_window && visible(top) && !boolean(top,L"IsOpeningProcess") && !boolean(top,L"IsClosingProcess")) {
        auto screen=widget_text(top,false,false,true);
        auto snapshot=screen_text_menu({utf8(path(top)),std::move(screen),true,true,
            is(top,L"/Game/UI/Field/Widget/WBP_FieldLayout.WBP_FieldLayout_C"),false});
        if(snapshot)apply_tutorial_controls(*snapshot,capture_tutorial_controls(top));
        return snapshot;
    }
    return {};
}
struct NativeFieldShape {const wchar_t* name;int offset,size;};
bool native_struct_shape(UStruct* type,std::initializer_list<NativeFieldShape> fields) {
    if(!type)return false;
    for(const auto& expected:fields) {
        FName name(expected.name,FNAME_Find);
        if(!name.ComparisonIndex&&!name.Number)return false;
        auto* found=type->FindProperty(name);
        if(!found||found->GetOffset_Internal()!=expected.offset||found->GetSize()!=expected.size)return false;
    }
    return true;
}
UStruct* native_class(UObject* object,const wchar_t* expected,int size) {
    if(!valid(object))return nullptr;
    int depth=0;
    for(auto* type=static_cast<UStruct*>(object->GetClassPrivate());type&&depth++<64;type=type->GetSuperStruct())
        if(path(type)==expected)return type->GetPropertiesSize()==size?type:nullptr;
    return nullptr;
}
DialogueVoice dialogue_voice(UObject* talk,UObject* data) {
    // Exact-build VOICE-MAPPING.md: native reflect145217280 calls PlayVoice,
    // then sets AutoProceed+0x30. A successful TalkVoice ID survives natural
    // finish, and SetCurrentTalkData resets it before the next line. No global
    // "voice busy" test, metadata guess or mutating native call is used here.
    if(!native_class(talk,L"/Script/mucha.CPP_WBP_Talk",1616)||
       !native_class(data,L"/Script/mucha.CPP_TalkData",120)||object(talk,L"m_pCurrentTalkData")!=data)
        return DialogueVoice::Unknown;
    auto* voice=object(talk,L"m_pVoice");auto* advance=object(talk,L"m_pAutoProceed");
    if(!native_class(voice,L"/Script/mucha.CPP_TalkVoice",48)||
       !native_class(advance,L"/Script/mucha.CPP_TalkAutoProceed",120))return DialogueVoice::Unknown;
    auto* found=UObjectGlobals::StaticFindObject(L"/Script/mucha.TalkData");
    if(!native_metadata(found,NativeMetadataKind::ScriptStruct)||path(found)!=L"/Script/mucha.TalkData")return DialogueVoice::Unknown;
    auto* shape=static_cast<UScriptStruct*>(found);
    if(shape->GetPropertiesSize()!=80||!native_struct_shape(shape,{{L"Voice",32,40},{L"IsVoiceValid",73,1}}))
        return DialogueVoice::Unknown;
    unsigned char active{},aborted{},reflected{},voice_valid{};int id=-1;
    std::memcpy(&active,reinterpret_cast<std::byte*>(talk)+0x5d8,1);
    std::memcpy(&aborted,reinterpret_cast<std::byte*>(talk)+0x5d9,1);
    std::memcpy(&reflected,reinterpret_cast<std::byte*>(advance)+0x30,1);
    std::memcpy(&voice_valid,reinterpret_cast<std::byte*>(data)+0x71,1);
    std::memcpy(&id,reinterpret_cast<std::byte*>(voice)+0x28,sizeof id);
    if(active>1||aborted>1||reflected>1||voice_valid>1)return DialogueVoice::Unknown;
    // IsVoiceValid is only a conservative designer-unvoiced veto; it never
    // establishes voice presence. Unknown/pre-reflect/inactive state keeps
    // already-readable text accessible rather than adding a speech wait.
    return assigned_dialogue_voice({true,active==1&&aborted==0,reflected==1,voice_valid==1,id});
}
std::optional<DialogueSnapshot> dialogue_snapshot(UObject* top,const std::unordered_map<std::string,std::string>& speakers,
        const std::unordered_map<std::string,unsigned long long>& lines,bool read_subtitles) {
    if(!top) return {};
    std::vector<UObject*> talks;
    walk(top,[&](auto* widget){if(is(widget,L"/Script/mucha.CPP_WBP_Talk")) talks.push_back(widget);});
    for(auto* talk:talks) {
        if(!visible(talk) || boolean(talk,L"IsClosingProcess") || boolean(talk,L"IsOpeningProcess")) continue;
        auto* data=object(talk,L"m_pCurrentTalkData");
        auto* manager=object(talk,L"WindowManager");
        auto* window=object(manager,L"m_pWindow");
        auto* block=object(window,L"m_pTextBlock");
        if(!visible(window)||!visible(block)||!data) continue;
        // The gradual block stores full unrevealed text. Wait for its native finish state.
        if(!get_bool(block,L"IsTextFinished")) continue;
        auto rendered=clean(text(block,L"Text"));
        Call caption(data,L"GetText",16,0);
        if(!caption) continue;
        caption.invoke(data);
        auto line=clean(caption.get_text(0));
        if(line.empty() || rendered.empty() || line!=rendered) continue;
        const auto id=utf8(path(window));
        std::string speaker;
        if(auto it=speakers.find(id);it!=speakers.end()) {
            // Native speaker IDs can identify unrevealed people. Only accept the
            // localized name if an actually visible text node also contains it.
            walk(window,[&](auto* widget){if(clean(text(widget,L"Text"))==it->second) speaker=it->second;});
        }
        const auto sequence=lines.find(id);
        const auto identity=utf8(path(data))+":"+std::to_string(sequence==lines.end()?0:sequence->second);
        const auto voice=dialogue_voice(talk,data);
        std::string source_reason;
        auto source_event=!read_subtitles?std::string{}:
            capture_dialogue_source_event(caption.get_native_text(0),&source_reason);
        static std::unordered_map<std::string,DialogueVoice> diagnostics;
        if(auto prior=diagnostics.find(identity);prior==diagnostics.end()||prior->second!=voice) {
            if(diagnostics.size()>128)diagnostics.clear();
            diagnostics[identity]=voice;
            log("DIALOGUE_VOICE "+identity+" assignment="+(voice==DialogueVoice::Voiced?"voiced":voice==DialogueVoice::Unvoiced?"unvoiced":"unknown")+
                (!source_event.empty()?" source="+source_event:
                    source_reason.empty()?std::string{}:" source_unavailable="+source_reason));
        }
        return DialogueSnapshot{identity,speaker,line,true,true,voice,std::move(source_event),read_subtitles};
    }
    return {};
}
UObject* player() {
    UObject* result=nullptr;
    for(auto* controller:find(L"PlayerController")) {
        if(!get_bool(controller,L"IsLocalPlayerController"))continue;
        auto* pawn=get_object(controller,L"K2_GetPawn");
        if(!is(pawn,L"/Script/mucha.CPP_MapUnit_Playable")||object(pawn,L"Controller")!=controller||
           !get_bool(pawn,L"IsPlayerControlled")||!get_bool(pawn,L"IsLocallyControlled"))continue;
        if(result)return nullptr; // Multiple local pawns are ambiguous for this mod.
        result=pawn;
    }
    return result;
}
UObject* actor_world(UObject* actor);
std::optional<WalkingAxes> walking_input_axes(UObject* pawn,std::string* reason=nullptr) {
    stage="capture native walking input axes";
    if(reason)reason->clear();
    const auto report=[](const std::string& diagnostic) {
        static std::string previous;
        if(diagnostic!=previous){previous=diagnostic;log("NAV basis "+diagnostic);}
    };
    const auto fail=[&](const char* message)->std::optional<WalkingAxes> {
        if(reason)*reason=message;
        report(std::string("unavailable=")+message);return {};
    };
    // Two native non-UPROPERTY flags are used by the real movement methods.
    // Validate their declaring base layouts before reading either byte. The
    // caller is admitted only after the exact executable/loader hash check.
    bool playable_layout=false,unit_layout=false;
    if(!valid(pawn))return fail("The current walking character is unavailable.");
    for(auto* c=static_cast<UStruct*>(pawn->GetClassPrivate());c;c=c->GetSuperStruct()) {
        const auto type=path(c);
        if(type==L"/Script/mucha.CPP_MapUnit_Playable")
            playable_layout=native_metadata(c,NativeMetadataKind::Class)&&c->GetPropertiesSize()==3072;
        if(type==L"/Script/mucha.CPP_MapUnit")
            unit_layout=native_metadata(c,NativeMetadataKind::Class)&&c->GetPropertiesSize()==2432;
    }
    if(!playable_layout||!unit_layout)return fail("The native walking-character layout could not be verified.");
    const auto flag=[&](const wchar_t* name)->std::optional<bool> {
        const auto value=read<unsigned char>(pawn,name,L"BoolProperty");
        const auto* d=descriptor(pawn,name);
        if(!value||!d)return {};
        return (*value&d->mask)!=0;
    };
    unsigned char force_internal{},vertical{};
    std::memcpy(&force_internal,reinterpret_cast<const std::byte*>(pawn)+0xbb8,1);
    std::memcpy(&vertical,reinterpret_cast<const std::byte*>(pawn)+0x8b8,1);
    const WalkingBasisFlags flags{flag(L"m_MapCameraInternalRotation"),flag(L"m_MapCameraExternalRotaion"),
        force_internal!=0,vertical!=0};
    const auto source=walking_yaw_source(flags);
    if(!source)return fail(vertical?"Ordinary walking directions are unavailable during vertical travel.":
        "The native walking-camera flags could not be read.");
    auto* controller=object(pawn,L"Controller");
    if(!is(controller,L"/Script/Engine.PlayerController")||!get_bool(controller,L"IsLocalPlayerController")||
       get_object(controller,L"K2_GetPawn")!=pawn)return fail("The local walking controller could not be verified.");
    std::optional<Vec3> rotation;
    std::string name;
    if(*source==WalkingYawSource::UnitCamera) {
        auto* unit_camera=object(pawn,L"m_pCamera");
        if(!is(unit_camera,L"/Script/CinematicCamera.CineCameraComponent")||
           get_object(unit_camera,L"GetOwner")!=pawn)return fail("The character's walking camera could not be verified.");
        rotation=getter<Vec3>(unit_camera,L"K2_GetComponentRotation");name="unit_camera";
    } else if(*source==WalkingYawSource::ViewTarget) {
        const auto view=getter<UObject*>(controller,L"GetViewTarget");
        if(!view)return fail("The native walking view target could not be read."); // Distinct from native null fallback.
        if(*view&&!valid(*view))return fail("The non-null walking view target could not be verified.");
        const bool garbage=*view&&(static_cast<std::uint32_t>((*view)->GetObjectFlags())&0x40000000u)!=0;
        if(!*view||garbage) {
            rotation=getter<Vec3>(controller,L"GetControlRotation");
            name=garbage?"control_garbage_view_target":"control_no_view_target";
        } else {
            if(!is(*view,L"/Script/Engine.Actor")||actor_world(*view)!=actor_world(pawn))
                return fail("The walking view target is outside the current area.");
            const auto root=read<UObject*>(*view,L"RootComponent",L"ObjectProperty");
            if(!root)return fail("The walking view target's root could not be read.");
            if(!*root) {rotation=Vec3{};name="view_target_no_root";} // Native ZeroRotator branch.
            else {
                if(!valid(*root)||!is(*root,L"/Script/Engine.SceneComponent")||get_object(*root,L"GetOwner")!=*view)
                    return fail("The walking view target's root could not be verified.");
                rotation=getter<Vec3>(*root,L"K2_GetComponentRotation");name="view_target";
            }
        }
    } else {rotation=getter<Vec3>(controller,L"GetControlRotation");name="control";}
    if(!rotation||!std::isfinite(rotation->x)||!std::isfinite(rotation->y)||!std::isfinite(rotation->z))
        return fail("The native walking-control rotation could not be read.");
    const auto axes=walking_axes(rotation->y);
    const auto diagnostic="source="+name+" yaw="+std::to_string(rotation->y)+
        " internal="+std::to_string(*flags.internal)+" external="+std::to_string(*flags.external)+
        " force_internal="+std::to_string(force_internal)+" vertical="+std::to_string(vertical);
    report(diagnostic);
    return axes;
}
constexpr std::size_t story_category=5;
constexpr std::size_t destination_category=6;
constexpr std::array<const char*,7> target_categories={
    "All","People","Objects","Exits, doors and ladders","Save points","Story","Destinations"};
struct Target { TargetSnapshot snapshot; UObject* actor; double distance;std::size_t category;std::wstring type;std::optional<Vec3> position;bool story{};std::string story_detail; };
std::optional<StorySpatialContext> story_spatial_context(UObject* pawn) {
    auto* world=actor_world(pawn);
    const auto origin=tracked_actor_position(pawn);
    const auto axes=walking_input_axes(pawn);
    if(!world||!origin||!axes)return {};
    std::optional<float> scale;
    for(auto* setting:find(L"WorldSettings"))if(actor_world(setting)==world) {
        const auto value=read<float>(setting,L"WorldToMeters");
        if(!value||!std::isfinite(*value)||*value<=0||(scale&&std::abs(*scale-*value)>.001f))return {};
        scale=value;
    }
    if(!scale)return {};
    return StorySpatialContext{*origin,axes->right,axes->up,*scale};
}
std::string story_target_id(const StorySnapshot& snapshot,const StoryDestination& destination) {
    return "story:"+std::to_string(snapshot.objective_id.size())+":"+snapshot.objective_id+":"+destination.id;
}
bool story_target(const std::string& id) { return id.starts_with("story:"); }
std::vector<Target> collect_story_targets(UObject* pawn,const StorySnapshot& snapshot) {
    if(!valid(pawn)||snapshot.world!=utf8(path(actor_world(pawn)))||snapshot.player!=utf8(path(pawn)))return {};
    const auto origin=getter<Vec3>(pawn,L"K2_GetActorLocation");
    const auto spatial=story_spatial_context(pawn);
    std::vector<Target> result;
    for(const auto& destination:story_destinations(snapshot)) {
        const bool routeable=story_route_available(snapshot,destination);
        const double distance=origin&&routeable?std::hypot(destination.position->x-origin->x,destination.position->y-origin->y):0.;
        auto description=describe_story_destination(story_target_id(snapshot,destination),snapshot,destination,spatial);
        result.push_back({std::move(description.target),nullptr,distance,story_category,destination.actor_type,
            routeable?destination.position:std::nullopt,true,std::move(description.detail)});
    }
    return result;
}
bool finite(Vec3 value) {return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);}
std::string coordinates(Vec3 point) {
    return std::to_string(point.x)+","+std::to_string(point.y)+","+std::to_string(point.z);
}
UObject* actor_world(UObject* actor) {
    if(!valid(actor))return nullptr;
    auto* level=actor->GetOuterPrivate();
    if(!valid(level)||!is(level,L"/Script/Engine.Level"))return nullptr;
    auto* world=object(level,L"OwningWorld");
    return valid(world)&&is(world,L"/Script/Engine.World")?world:nullptr;
}
UObject* nav_library(UObject* pawn) {
    auto* world=actor_world(pawn);if(!world)return nullptr;
    if(auto* current=object(world,L"NavigationSystem");
       current&&current->GetOuterPrivate()==world&&is(current,L"/Script/NavigationSystem.NavigationSystemV1"))
        return current;
    UObject* result=nullptr;
    for(auto* system:find(L"NavigationSystemV1"))if(system->GetOuterPrivate()==world) {
        if(result)return nullptr;result=system;
    }
    return result;
}
bool capsule_trace_library(UObject* object) {
    if(!object||path(object)!=L"/Script/Engine.Default__KismetSystemLibrary"||
       path(object->GetClassPrivate())!=L"/Script/Engine.KismetSystemLibrary")return false;
    const auto flags=static_cast<std::uint32_t>(object->GetObjectFlags());
    return (flags&0x10u)!=0&&(flags&(0x8000u|0x10000u|0x40000000u))==0;
}
template<std::size_t N> bool nav_call(UObject* library,const wchar_t* name,int result,std::array<std::byte,N>& params) {
    const bool capsule_trace=std::wstring_view(name)==L"CapsuleTraceSingleByProfile";
    if(capsule_trace?!capsule_trace_library(library):!valid(library))return false;
    auto* function=library->GetFunctionByNameInChain(name);
    if(!function)return false;
    if(capsule_trace&&(path(function)!=L"/Script/Engine.KismetSystemLibrary:CapsuleTraceSingleByProfile"||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400))||
       !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x2000))))return false;
    auto* returned=function->GetReturnProperty();
    if(!returned||returned->GetOffset_Internal()!=result||!returned->HasAnyPropertyFlags(0x80)||
       !returned->HasAnyPropertyFlags(0x400)||
       !native_call_extent(function->GetParmsSize(),function->GetPropertiesSize(),static_cast<int>(N),
           function->GetReturnValueOffset(),result,result+returned->GetSize(),static_cast<int>(N)))return false;
    stage="native navigation "+utf8(name);library->ProcessEvent(function,params.data());return true;
}
template<class T,std::size_t N> void nav_put(std::array<std::byte,N>& params,std::size_t offset,const T& value) {
    std::memcpy(params.data()+offset,&value,sizeof(T));
}
template<class T,std::size_t N> T nav_get(const std::array<std::byte,N>& params,std::size_t offset) {
    T value{};std::memcpy(&value,params.data()+offset,sizeof(T));return value;
}
struct NavigationProjection {
    bool on_mesh{};
    Vec3 position;
};
std::optional<NavigationProjection> project_navigation(UObject* pawn,UObject* system,UObject* mesh,
        Vec3 point,Vec3 extent={}) {
    alignas(16) std::array<std::byte,104> params{};
    nav_put(params,0,pawn);
    nav_put(params,8,point);
    nav_put(params,56,mesh);
    nav_put(params,72,extent);
    if(!nav_call(system,L"K2_ProjectPointToNavigation",96,params))return {};
    if(!nav_get<bool>(params,96))return NavigationProjection{};
    const auto surface=nav_get<Vec3>(params,32);
    if(!finite(surface))return {};
    return NavigationProjection{true,surface};
}
direct_nav::Api direct_navigation_api() {
    return direct_nav::exact_build_api([](const void* weak)->void* {
        return static_cast<const FWeakObjectPtr*>(weak)->Get();
    });
}
bool current_walking_mesh(UObject* pawn,UObject* system,UObject* mesh) {
    auto* world=actor_world(pawn);
    if(!world||!valid(mesh)||!is(mesh,L"/Script/NavigationSystem.RecastNavMesh")||actor_world(mesh)!=world)return false;
    const auto registered=array<UObject*>(system,L"NavDataSet");
    return std::count(registered.begin(),registered.end(),mesh)==1&&nav_library(pawn)==system;
}
UObject* registered_default_surface(UObject* pawn,UObject* system) {
    UObject* alternate=nullptr;
    for(auto* mesh:array<UObject*>(system,L"NavDataSet")) {
        if(!current_walking_mesh(pawn,system,mesh)||
           !path(mesh).ends_with(L".RecastNavMesh-Default"))continue;
        if(alternate)return nullptr;
        alternate=mesh;
    }
    return alternate;
}
UObject* default_walking_surface(UObject* pawn,UObject* system,UObject* preferred,Vec3 start) {
    const auto primary=project_navigation(pawn,system,preferred,start);
    if(!primary||primary->on_mesh)return nullptr;
    auto* alternate=registered_default_surface(pawn,system);
    if(!alternate||alternate==preferred)return nullptr;
    const auto projected=project_navigation(pawn,system,alternate,start);
    return projected&&projected->on_mesh?alternate:nullptr;
}
bool world_map(UObject* world) {
    return path(world)==L"/Game/Map/Wld/Wld_PL.Wld_PL";
}
bool earth_shrine_exterior(UObject* world) {
    return path(world)==L"/Game/Map/Dng/01Gra/01Gra_43/Dng_01Gra_43_PL.Dng_01Gra_43_PL";
}
bool earth_shrine_entrance(UObject* target) {
    return path(target)==L"/Game/Map/Dng/01Gra/01Gra_43/GD/Dng_01Gra_43_GD.Dng_01Gra_43_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_0"&&read<int>(target,L"m_UniqueId")==0;
}
bool crystal_tower_spiral(UObject* world) {
    return path(world)==L"/Game/Map/Dng/10Cry/10Cry_41/Dng_10Cry_41_PL.Dng_10Cry_41_PL";
}
bool tower_chamber_return(UObject* target) {
    return is(target,L"/Script/mucha.CPP_MapTransitionTrigger")&&read<int>(target,L"m_UniqueId")==7&&
        path(target)==L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
            L"PersistentLevel.BP_MapTransitionTrigger_C_13";
}
enum class TowerSpiralExit { None, Summit, RestStop };
TowerSpiralExit tower_spiral_exit(UObject* actor) {
    if(!is(actor,L"/Script/mucha.CPP_MapTransitionTrigger")||!crystal_tower_spiral(actor_world(actor)))
        return TowerSpiralExit::None;
    constexpr std::wstring_view prefix=L"/Game/Map/Dng/10Cry/10Cry_41/GD/Dng_10Cry_41_01_GD."
        L"Dng_10Cry_41_01_GD:PersistentLevel.BP_MapTransitionTrigger_C_";
    const auto identity=path(actor);
    if(!identity.starts_with(prefix))return TowerSpiralExit::None;
    const auto suffix=std::wstring_view(identity).substr(prefix.size());
    const auto id=read<int>(actor,L"m_UniqueId");
    if(id==1&&suffix==L"1")return TowerSpiralExit::Summit;
    if(id==3&&suffix==L"3")return TowerSpiralExit::RestStop;
    return TowerSpiralExit::None;
}
bool swept_default_mesh(UObject* world,std::wstring_view mesh) {
    // At the Earth Shrine the Default bake has a connected path around the
    // merchant. A straight capsule graph can miss that turn from the talk box.
    return mesh.ends_with(L".RecastNavMesh-Default")&&
        (world_map(world)||crystal_tower_spiral(world)||earth_shrine_exterior(world));
}
bool olderion_south_exit(UObject* target) {
    return path(target)==L"/Game/Map/Town/05Old/05Old_01/GD/Town_05Old_01_01_GD.Town_05Old_01_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_1";
}
bool wandering_crystal_gate(UObject* target) {
    return path(target)==L"/Game/Map/Dng/09Pha/09Pha_43/GD/Dng_09Pha_43_01_GD.Dng_09Pha_43_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_1"&&read<int>(target,L"m_UniqueId")==3;
}
bool dilmagia_shipyard_gate(UObject* target) {
    return path(target)==L"/Game/Map/Town/04Dir/04Dir_01/GD/Town_04Dir_01_01_GD.Town_04Dir_01_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_1";
}
bool waterfall_first_passage(UObject* target) {
    return path(target)==L"/Game/Map/Dng/05Old/05Old_43/GD/Dng_05Old_43_01_GD.Dng_05Old_43_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_5";
}
bool waterfall_world(UObject* pawn) {
    return path(actor_world(pawn))==L"/Game/Map/Dng/05Old/05Old_43/Dng_05Old_43_PL.Dng_05Old_43_PL";
}
std::optional<int> waterfall_ladder_id(UObject* target) {
    if(!is(target,L"/Script/mucha.CPP_MapGimmickLadder")||!waterfall_world(target))return {};
    return read<int>(target,L"m_UniqueId");
}
std::string_view cardinal_motion(Vec3 motion,const std::optional<WalkingAxes>& axes) {
    const auto length=std::hypot(motion.x,motion.y);
    if(!axes||!(length>0.))return {};
    const auto right=(axes->right.x*motion.x+axes->right.y*motion.y)/length;
    const auto up=(axes->up.x*motion.x+axes->up.y*motion.y)/length;
    if(up>.9)return "Up";
    if(up<-.9)return "Down";
    if(right>.9)return "Right";
    if(right<-.9)return "Left";
    return {};
}
std::string ladder_instruction(bool down,Vec3 mount,const std::optional<WalkingAxes>& axes) {
    const auto hold=tr(down?"hold Down to climb down.":"hold Up to climb.");
    const auto direction=cardinal_motion(mount,axes);
    if(direction==(down?"Down":"Up"))return tr(down?"Hold Down to climb down.":"Hold Up to climb.");
    if(!direction.empty())return trf("Move {0} onto the ladder, then {1}",{tr(direction),hold});
    return trf("Move toward the ladder, then {0}",{hold});
}
std::optional<std::string> live_ladder_instruction(UObject* ladder,Vec3 goal,const std::optional<WalkingAxes>& axes) {
    const auto position=[&](const wchar_t* field)->std::optional<Vec3> {
        auto* component=object(ladder,field);
        if(!is(component,L"/Script/Engine.SceneComponent")||get_object(component,L"GetOwner")!=ladder)return {};
        const auto point=getter<Vec3>(component,L"K2_GetComponentLocation");
        return point&&finite(*point)?point:std::nullopt;
    };
    const auto landing=position(L"m_TopLandingPoint"),top=position(L"m_pTopBoxComponent"),bottom=position(L"m_pBottomBoxComponent");
    if(!landing||!top||!bottom)return {};
    const auto matches=[&](Vec3 point){return std::hypot(point.x-goal.x,point.y-goal.y,point.z-goal.z)<5.;};
    if(matches(*landing))return ladder_instruction(true,{top->x-landing->x,top->y-landing->y,0},axes);
    if(matches(*bottom))return ladder_instruction(false,{landing->x-top->x,landing->y-top->y,0},axes);
    return {};
}
struct CapsuleSweep {
    static constexpr float floor_clearance=2.15f;

    UObject* pawn;
    UObject* library;
    FName profile;
    float radius;
    float half_height;
    bool complex;
    float step_height;
    float floor_z;
    UObject* landing_bumper{};

    struct Hit { bool blocking; bool penetrating; Vec3 location; Vec3 normal; };
    struct Obstacle {Vec3 point,normal;float fraction;UObject* component;};
    std::optional<Hit> trace(Vec3 from,Vec3 to,Obstacle* obstacle=nullptr) const {
        // The game disembarks into the edge of its raft-only bumper. The
        // ordinary pawn can move away from it. Exclude that one actor from the
        // egress probe while still checking all other collision and floor.
        auto* ignored=from.x>=2298.&&from.x<=2321.&&to.x+.001>=from.x&&
            std::abs(from.y-11501.4)<=18.&&std::abs(to.y-11501.4)<=18.?landing_bumper:nullptr;
        from.z+=half_height;
        to.z+=half_height;
        alignas(16) std::array<std::byte,0x198> params{};
        nav_put(params,0,pawn);
        nav_put(params,0x8,from);
        nav_put(params,0x20,to);
        nav_put(params,0x38,radius);
        nav_put(params,0x3c,half_height);
        nav_put(params,0x40,profile);
        nav_put(params,0x48,complex);
        if(ignored) {
            auto* function=library->GetFunctionByNameInChain(L"CapsuleTraceSingleByProfile");
            if(!native_struct_shape(function,{{L"ActorsToIgnore",0x50,16}}))return {};
            nav_put(params,0x50,Array<UObject*>{&ignored,1,1});
        }
        nav_put(params,0x168,true); // Ignore only the controlled pawn.
        nav_put(params,0x6c,-1.f); // FHitResult::Time sentinel.
        if(!nav_call(library,L"CapsuleTraceSingleByProfile",0x190,params))return {};
        const auto close=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<.001;};
        if(!close(nav_get<Vec3>(params,0xd8),from)||!close(nav_get<Vec3>(params,0xf0),to))return {};
        const auto time=nav_get<float>(params,0x6c);
        const auto flags=nav_get<unsigned char>(params,0x115);
        const bool blocked=nav_get<bool>(params,0x190);
        if(!blocked) {
            // A false return without a written no-hit result is not clearance.
            if(time!=1.f||(flags&3))return {};
            return Hit{false,false,{},{}};
        }
        const auto location=nav_get<Vec3>(params,0x78),normal=nav_get<Vec3>(params,0xc0);
        if(!(flags&1)||!std::isfinite(time)||time<0||time>1||!finite(location)||!finite(normal))return {};
        if(obstacle) {
            constexpr std::size_t hit=0x68;
            *obstacle={nav_get<Vec3>(params,hit+40),nav_get<Vec3>(params,hit+88),time,
                reinterpret_cast<const FWeakObjectPtr*>(params.data()+hit+216)->Get()};
        }
        return Hit{true,(flags&2)!=0,location,normal};
    }
    std::optional<Vec3> movement_floor(Vec3 point) const {
        auto* movement=object(pawn,L"CharacterMovement");
        if(!movement||path(movement->GetClassPrivate())!=L"/Script/Engine.CharacterMovementComponent"||
           object(movement,L"UpdatedComponent")!=object(pawn,L"CapsuleComponent"))return {};
        auto* function=movement->GetFunctionByNameInChain(L"K2_ComputeFloorDist");
        constexpr std::size_t result=0x28,hit=result+0x10;
        alignas(16) std::array<std::byte,0x138> params{};
        if(!function||path(function)!=L"/Script/Engine.CharacterMovementComponent:K2_ComputeFloorDist"||
           !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400))||
           function->GetParmsSize()!=params.size()||function->GetPropertiesSize()>params.size()||
           !native_struct_shape(function,{{L"CapsuleLocation",0,24},{L"LineDistance",0x18,4},
               {L"SweepDistance",0x1c,4},{L"SweepRadius",0x20,4},{L"FloorResult",result,0x110}}))return {};
        const auto center=Vec3{point.x,point.y,point.z+half_height+step_height};
        // The trace hits the floor before the capsule settles at its usual
        // clearance. Measure the full step from the settled feet.
        const auto depth=2*step_height+floor_clearance;
        nav_put(params,0,center);
        nav_put(params,0x18,depth);
        nav_put(params,0x1c,depth);
        nav_put(params,0x20,radius);
        stage="native movement floor query";
        movement->ProcessEvent(function,params.data());
        const auto flags=nav_get<unsigned char>(params,result);
        const auto hit_flags=nav_get<unsigned char>(params,hit+0xad);
        const auto distance=nav_get<float>(params,result+((flags&4)?8:4));
        if((flags&3)!=3||(hit_flags&3)!=1||!std::isfinite(distance)||distance<0||distance>depth)return {};
        const auto z=center.z-half_height-distance+floor_clearance;
        if(!std::isfinite(z)||std::abs(z-point.z)>step_height)return {};
        return Vec3{point.x,point.y,z};
    }
    struct Contact {Vec3 from,to,normal;bool penetrating;};
    GroundProbe ground_probe(std::optional<Contact>* contact=nullptr) const {
        return {
            [this,contact](Vec3 from,Vec3 to)->std::optional<bool> {
                const auto hit=trace(from,to);
                if(!hit)return {};
                if(!hit->blocking)return false;
                if(contact) {
                    // A native step can cross a crease that a straight sweep
                    // between two supported poses touches. Check all three
                    // parts of the step with the full live capsule.
                    const auto height=std::min(from.z,to.z)+step_height;
                    auto raised_from=from,raised_to=to;
                    raised_from.z=raised_to.z=height;
                    const auto clear=[&](Vec3 a,Vec3 b) {
                        const auto leg=trace(a,b);
                        return leg&&!leg->blocking;
                    };
                    if(height>std::max(from.z,to.z)&&clear(from,raised_from)&&
                       clear(raised_from,raised_to)&&clear(raised_to,to))return false;
                    *contact=Contact{from,to,hit->normal,hit->penetrating};
                }
                return true;
            },
            [this,contact](Vec3 point)->std::optional<Vec3> {
                const auto hit=trace({point.x,point.y,point.z+step_height},
                    {point.x,point.y,point.z-step_height-floor_clearance});
                if(hit&&hit->blocking&&!hit->penetrating&&hit->normal.z>=floor_z) {
                    auto feet=hit->location;
                    feet.z+=floor_clearance-half_height;
                    if(std::abs(feet.z-point.z)<=step_height)return feet;
                }
                return contact?movement_floor(point):std::nullopt;
            }
        };
    }
};
UObject* cave_disembark_bumper(UObject* pawn) {
    if(path(actor_world(pawn))!=L"/Game/Map/Dng/05Old/05Old_40/Dng_05Old_40_PL.Dng_05Old_40_PL")return nullptr;
    const auto pose=getter<Vec3>(pawn,L"K2_GetActorLocation");
    if(!pose||pose->x<2298.||pose->x>2321.||std::abs(pose->y-11501.4)>18.||std::abs(pose->z-102.15)>10.)
        return nullptr;
    constexpr auto id=L"/Game/Map/Dng/05Old/05Old_40/GD/Dng_05Old_40_01_GD.Dng_05Old_40_01_GD:"
        L"PersistentLevel.BP_MapGimmick_BlockingBox_Raft_C_4";
    auto* actor=UObjectGlobals::StaticFindObject(id);
    if(!valid(actor)||path(actor)!=id||actor_world(actor)!=actor_world(pawn)||read<int>(actor,L"m_UniqueId")!=26)return nullptr;
    auto* root=object(actor,L"RootComponent");
    if(!is(root,L"/Script/Engine.BoxComponent")||get_object(root,L"GetOwner")!=actor)return nullptr;
    const auto center=getter<Vec3>(root,L"K2_GetComponentLocation"),half=getter<Vec3>(root,L"GetScaledBoxExtent");
    if(!center||!half||std::hypot(center->x-2254.,center->y-11491.)>.1||
       std::abs(half->x-10.)>.1||std::abs(half->y-120.)>.1)return nullptr;
    return actor;
}
std::optional<CapsuleSweep> capture_capsule_sweep(UObject* pawn,bool rowing=false) {
    auto* capsule=object(pawn,L"CapsuleComponent");
    if(!capsule||capsule!=object(pawn,L"RootComponent")||get_object(capsule,L"GetOwner")!=pawn||
       !is(capsule,L"/Script/Engine.CapsuleComponent")||!property(capsule,L"bTraceComplexOnMove",L"BoolProperty",1))return {};
    const auto radius=getter<float>(capsule,L"GetScaledCapsuleRadius");
    const auto half=getter<float>(capsule,L"GetScaledCapsuleHalfHeight");
    const auto rotation=getter<Vec3>(capsule,L"K2_GetComponentRotation");
    const auto enabled=getter<unsigned char>(capsule,L"GetCollisionEnabled");
    const auto object_type=getter<unsigned char>(capsule,L"GetCollisionObjectType");
    auto profile=getter<FName>(capsule,L"GetCollisionProfileName");
    if(!radius||!half||!std::isfinite(*radius)||!std::isfinite(*half)||*radius<=0||*half<*radius||
       !rotation||!finite(*rotation)||std::abs(rotation->x)>.1||std::abs(rotation->z)>.1||
       !enabled||(*enabled!=1&&*enabled!=3&&*enabled!=5)||!object_type||!profile)return {};
    Call get_response(capsule,L"GetCollisionResponseToChannel",2,1);
    if(!get_response)return {};
    std::array<unsigned char,32> live_responses{};
    for(std::size_t i=0;i<live_responses.size();++i) {
        get_response.put(0,static_cast<unsigned char>(i));
        get_response.invoke(capsule);
        live_responses[i]=get_response.get<unsigned char>(1);
        if(live_responses[i]>2)return {};
    }
    // SweepSingleByProfile needs a registered name. The save-point interaction
    // can leave the capsule labelled Custom with the exact Pawn filter.
    // Check the actual channel and every live response before using either name.
    using Resolve=bool (__fastcall*)(std::uint64_t,std::uint32_t*,unsigned char*);
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto resolve=reinterpret_cast<Resolve>(base+0x3a7c540);
    const auto matches_capsule=[&](FName candidate) {
        static_assert(sizeof(FName)==8);
        std::uint64_t name{};
        std::memcpy(&name,&candidate,sizeof(name));
        std::uint32_t channel=~0u;
        std::array<unsigned char,32> responses{};
        return name&&resolve(name,&channel,responses.data())&&channel==*object_type&&responses==live_responses;
    };
    if(!matches_capsule(*profile)) {
        const FName pawn_profile(L"Pawn",FNAME_Find);
        if(!matches_capsule(pawn_profile))return {};
        profile=pawn_profile;
    }
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/Engine.Default__KismetSystemLibrary");
    if(!capsule_trace_library(library))return {};
    auto* movement=object(pawn,L"CharacterMovement");
    const auto step=read<float>(movement,L"MaxStepHeight",L"FloatProperty");
    const auto floor_z=read<float>(movement,L"WalkableFloorZ",L"FloatProperty");
    if(!step||!std::isfinite(*step)||(rowing?*step!=0:*step<=0)||
       !floor_z||!std::isfinite(*floor_z)||*floor_z<=0||*floor_z>1)return {};
    return CapsuleSweep{pawn,library,*profile,*radius,*half,boolean(capsule,L"bTraceComplexOnMove"),*step,*floor_z,
        rowing?nullptr:cave_disembark_bumper(pawn)};
}
std::optional<Vec3> capsule_lane(const CapsuleSweep& sweep,UObject* system,UObject* mesh,Vec3 from,Vec3 to,double scale) {
    const auto extent=Vec3{scale*.02,scale*.02,3.*sweep.step_height};
    const auto on_surface=[&](Vec3 feet)->std::optional<Vec3> {
        const auto sample=project_navigation(sweep.pawn,system,mesh,feet,extent);
        if(!sample||!sample->on_mesh||
           std::hypot(sample->position.x-feet.x,sample->position.y-feet.y)>scale*.02)return {};
        return sample->position;
    };
    const auto start=on_surface(from),end=on_surface(to);
    if(!start||!end)return {};
    if(std::hypot(end->x-start->x,end->y-start->y)>scale*.001) {
        const auto blocked=direct_nav::raycast(mesh,*start,*end);
        if(!blocked||*blocked)return {};
    }
    const auto feet=checked_walking_leg(from,to,scale,sweep.floor_z,sweep.ground_probe());
    const auto actual_end=feet?on_surface(*feet):std::nullopt;
    if(!actual_end||std::abs(actual_end->z-end->z)>sweep.step_height)return {};
    return feet;
}
bool capsule_floor_on_mesh(const CapsuleSweep& sweep,UObject* system,UObject* mesh,Vec3 floor,double scale) {
    const Vec3 extent{scale*.02,scale*.02,sweep.step_height};
    const auto projected=project_navigation(sweep.pawn,system,mesh,floor,extent);
    return projected&&projected->on_mesh&&std::abs(projected->position.z-floor.z)<=extent.z&&
        std::hypot(projected->position.x-floor.x,projected->position.y-floor.y)<=extent.x;
}
std::optional<direct_nav::Path> route_around_start_contact(UObject* pawn,UObject* system,UObject* mesh,
        Vec3 feet,const direct_nav::Path& native,double scale,std::size_t& rejoin) {
    const auto sweep=capture_capsule_sweep(pawn);
    if(!sweep||native.points.size()<2)return {};
    const auto clear_first_metres=[&](Vec3 from,const direct_nav::Path& route,
            std::optional<CapsuleSweep::Contact>* contact=nullptr) {
        const auto probe=sweep->ground_probe(contact);
        auto remaining=2.*sweep->radius;
        for(std::size_t i=1;i<route.points.size()&&remaining>0.;++i) {
            const auto next=route.points[i];
            const auto length=std::hypot(next.x-from.x,next.y-from.y);
            if(length<scale*.01)continue;
            const auto fraction=std::min(1.,remaining/length);
            const Vec3 end{from.x+(next.x-from.x)*fraction,from.y+(next.y-from.y)*fraction,from.z};
            const auto floor=checked_walking_leg(from,end,scale,sweep->floor_z,probe);
            if(!floor)return false;
            from=*floor;
            remaining-=length;
        }
        return true;
    };
    std::optional<CapsuleSweep::Contact> contact;
    if(clear_first_metres(feet,native,&contact)||!contact||contact->penetrating||std::abs(contact->normal.z)>.1)
        return {};
    const auto normal=std::hypot(contact->normal.x,contact->normal.y);
    if(normal<.9)return {};
    const Vec3 away{contact->normal.x/normal,contact->normal.y/normal,0};
    // When a speaker's capsule obstructs the first baked turn, find a clear
    // standing point beside it and ask the game again from that position.
    const std::array headings{away,Vec3{away.x-away.y,away.y+away.x,0},
        Vec3{away.x+away.y,away.y-away.x,0},Vec3{-1,0,0},Vec3{1,0,0},Vec3{0,-1,0},Vec3{0,1,0}};
    const auto goal=native.points.back();
    const auto probe=sweep->ground_probe();
    const auto join=[&](std::span<const Vec3> prefix)->std::optional<direct_nav::Path> {
        const auto landing=prefix.back();
        if(!capsule_floor_on_mesh(*sweep,system,mesh,landing,scale))return {};
        auto onward=direct_nav::find_path(direct_navigation_api(),pawn,system,mesh,landing,goal);
        if(!onward.valid||onward.points.size()<2||
           std::hypot(onward.points.front().x-landing.x,onward.points.front().y-landing.y)>scale*.05||
           std::hypot(onward.points.back().x-goal.x,onward.points.back().y-goal.y)>scale*.05||
           std::abs(onward.points.back().z-goal.z)>sweep->step_height)return {};
        onward.points.back()=goal;
        if(!clear_first_metres(landing,onward))return {};
        onward.points.front()=landing;
        onward.points.insert(onward.points.begin(),prefix.begin(),prefix.end()-1);
        rejoin=prefix.size()-1;
        return onward;
    };
    if(feet.x>=-5250.&&feet.x<=-4780.&&std::abs(feet.y)<=600.&&std::abs(feet.z+298.)<45.) {
        // The Shrine merchants stand on either side of the entrance lane.
        // Back away and cross behind them before connecting to the middle.
        const Vec3 retreat{-5360.,feet.y,feet.z},aisle{-5360.,0.,feet.z};
        const auto turn=checked_walking_leg(feet,retreat,scale,sweep->floor_z,probe);
        const auto middle=turn?checked_walking_leg(*turn,aisle,scale,sweep->floor_z,probe):std::nullopt;
        if(middle&&std::hypot(middle->x-turn->x,middle->y-turn->y)>scale*.01)
            if(auto route=join(std::array{feet,*turn,*middle}))return route;
    }
    for(const auto radius:{1.5,3.,5.,6.5})for(const auto heading:headings) {
        const auto step=radius*sweep->radius/std::hypot(heading.x,heading.y);
        const Vec3 point{feet.x+step*heading.x,feet.y+step*heading.y,feet.z};
        const auto landing=checked_walking_leg(feet,point,scale,sweep->floor_z,probe);
        if(landing)if(auto route=join(std::array{feet,*landing}))return route;
    }
    return {};
}
std::optional<Vec3> swept_capsule_lane(const CapsuleSweep& sweep,UObject* system,UObject* mesh,Vec3 from,Vec3 to,double scale) {
    if(!current_walking_mesh(sweep.pawn,system,mesh)||!swept_default_mesh(actor_world(sweep.pawn),path(mesh)))return {};
    const auto mesh_slope=read<float>(mesh,L"AgentMaxSlope",L"FloatProperty");
    if(!mesh_slope||!std::isfinite(*mesh_slope)||*mesh_slope<0||*mesh_slope>=90)return {};
    const auto floor_z=std::max<double>(sweep.floor_z,std::cos(*mesh_slope*3.14159265358979323846/180.));
    std::optional<CapsuleSweep::Contact> contact;
    const auto live=sweep.ground_probe(&contact);
    const GroundProbe probe{live.blocked,live.project,sweep.step_height/scale};
    const auto start=probe.project(from);
    if(!start||std::abs(start->z-from.z)>sweep.step_height)return {};
    const auto approach=probe.blocked(from,*start);
    if(!approach||*approach)return {};
    return checked_walking_leg(*start,to,scale,floor_z,probe);
}
bool inside_box(const ActivationBox& box,Vec3 point) {
    const auto nearest=closest_activation_point(box,point);
    return nearest&&nearest->x==point.x&&nearest->y==point.y&&nearest->z==point.z;
}
constexpr ActivationBox mitra_weapon_stair{{1910,4150,394},{620,210,180},0};
bool olderion_plaza_floor(UObject* world,Vec3 feet) {
    constexpr ActivationBox plaza{{540,-1690,122},{590,590,55},0};
    return path(world)==L"/Game/Map/Town/05Old/05Old_01/Town_05Old_01_PL.Town_05Old_01_PL"&&
        inside_box(plaza,feet);
}
bool zoldaad_swept_walkway(UObject* world) {
    const auto name=path(world);
    return name==L"/Game/Map/Dng/06Zol/06Zol_41/Dng_06Zol_41_PL.Dng_06Zol_41_PL"||
        name==L"/Game/Map/Dng/06Zol/06Zol_42/Dng_06Zol_42_PL.Dng_06Zol_42_PL"||
        name==L"/Game/Map/Dng/06Zol/06Zol_44/Dng_06Zol_44_PL.Dng_06Zol_44_PL";
}
bool olderion_tree_local(UObject* target) {
    return path(actor_world(target))==L"/Game/Map/Town/05Old/05Old_01/Town_05Old_01_PL.Town_05Old_01_PL"&&
        is(target,L"/Script/mucha.CPP_MapUnit_NPC")&&read<int>(target,L"m_UniqueId")==18;
}
bool olderion_bridge_soldier(UObject* target) {
    return path(target)==L"/Game/Map/Town/05Old/05Old_01/EV/Town_05Old_01_01_EV.Town_05Old_01_01_EV:"
        L"PersistentLevel.BP_MapUnit_NPC_C_4"&&read<int>(target,L"m_UniqueId")==21;
}
bool olderion_arsha_trigger(UObject* target) {
    return path(target)==L"/Game/Map/Town/05Old/05Old_01/EV/Town_05Old_01_01_EV.Town_05Old_01_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_EventTrigger_1"&&read<int>(target,L"m_UniqueId")==0;
}
bool olderion_harbor_check(UObject* target) {
    return path(target)==L"/Game/Map/Wld/05Old/EV/Wld_05Old_EV.Wld_05Old_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1"&&read<int>(target,L"m_UniqueId")==191;
}
bool wardens_cape_trigger(UObject* target) {
    return path(target)==L"/Game/Map/Wld/05Old/EV/Wld_05Old_EV.Wld_05Old_EV:"
        L"PersistentLevel.CPP_MapGimmick_EventTrigger_1"&&read<int>(target,L"m_UniqueId")==20;
}
std::optional<int> old_town_story_trigger(UObject* target) {
    constexpr std::wstring_view level=
        L"/Game/Map/Dng/05Old/05Old_41/EV/Dng_05Old_41_01_EV.Dng_05Old_41_01_EV:PersistentLevel.";
    const auto name=path(target);
    if(!name.starts_with(level))return {};
    const auto id=read<int>(target,L"m_UniqueId");
    const auto actor=std::wstring_view(name).substr(level.size());
    if((id==2&&actor==L"BP_MapGimmick_EventTrigger_C_2")||
       (id==3&&actor==L"BP_MapGimmick_EventTrigger_C_3")||
       (id==4&&actor==L"BP_MapGimmick_EventTrigger_C_4"))return id;
    return {};
}
bool water_shrine_bridge_exit(UObject* target) {
    return path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_28"&&
        read<int>(target,L"m_UniqueId")==26;
}
bool amore_north_entrance(UObject* target) {
    return path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_26"&&
        read<int>(target,L"m_UniqueId")==25;
}
bool amore_south_exit(UObject* target) {
    return path(target)==L"/Game/Map/Town/05Old/05Old_21/GD/Town_05Old_21_01_GD.Town_05Old_21_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_0"&&read<int>(target,L"m_UniqueId")==15;
}
bool olderion_world_entrance(UObject* target) {
    return path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_21"&&
        read<int>(target,L"m_UniqueId")==19;
}
bool mozul_world_entrance(UObject* target) {
    return path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_35"&&
        read<int>(target,L"m_UniqueId")==31;
}
std::optional<Vec3> zoldaad_world_approach(UObject* target) {
    constexpr std::wstring_view prefix=L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_";
    const auto name=path(target);
    if(!name.starts_with(prefix))return {};
    const auto suffix=std::wstring_view(name).substr(prefix.size());
    const auto id=read<int>(target,L"m_UniqueId");
    if(id==32&&suffix==L"36")return Vec3{-6693,7525,66.4};
    if(id==33&&suffix==L"38")return Vec3{-8872,13344,43.3};
    if(id==34&&suffix==L"37")return Vec3{-10182,18423,769.74};
    return {};
}
bool zoldaad_tunnel_exit(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_42/GD/Dng_06Zol_42_01_GD.Dng_06Zol_42_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_3"&&read<int>(target,L"m_UniqueId")==2;
}
bool zoldaad_factory_hall_gate(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_41/GD/Dng_06Zol_41_01_GD.Dng_06Zol_41_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_8"&&read<int>(target,L"m_UniqueId")==5;
}
bool zoldaad_factory_pedestal_conveyor(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_41/GD/Dng_06Zol_41_01_GD.Dng_06Zol_41_01_GD:"
        L"PersistentLevel.BP_MapGimmickJumpPoint_Walk_001_C_36"&&read<int>(target,L"m_UniqueId")==13;
}
bool zoldaad_factory_gimmick(UObject* target,int id,const wchar_t* type) {
    return is(target,type)&&read<int>(target,L"m_UniqueId")==id&&
        path(actor_world(target))==L"/Game/Map/Dng/06Zol/06Zol_41/Dng_06Zol_41_PL.Dng_06Zol_41_PL";
}
bool zoldaad_factory_maze_pedestal(UObject* target) {
    return zoldaad_factory_gimmick(target,54,L"/Script/mucha.CPP_MapGimmick_Switch");
}
bool zoldaad_factory_upper_ladder(UObject* target,Vec3 goal) {
    return goal.z>2000.&&zoldaad_factory_gimmick(target,30,L"/Script/mucha.CPP_MapGimmickLadder");
}
bool zoldaad_factory_west_ladder(UObject* target,Vec3 goal) {
    return goal.z>600.&&zoldaad_factory_gimmick(target,31,L"/Script/mucha.CPP_MapGimmickLadder");
}
bool zoldaad_factory_west_pedestal(UObject* target,int id) {
    return zoldaad_factory_gimmick(target,id,L"/Script/mucha.CPP_MapGimmick_Switch");
}
bool zoldaad_factory_generator(UObject* target) {
    return zoldaad_factory_gimmick(target,28,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
}
bool zoldaad_factory_bridge_return(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_41/GD/Dng_06Zol_41_01_GD.Dng_06Zol_41_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_10"&&read<int>(target,L"m_UniqueId")==0;
}
bool zoldaad_dungeon_castle_exit(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_43/GD/Dng_06Zol_43_01_GD.Dng_06Zol_43_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_23"&&read<int>(target,L"m_UniqueId")==13;
}
bool zoldaad_throne_approach(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_44/EV/Dng_06Zol_44_01_EV.Dng_06Zol_44_01_EV:"
        L"PersistentLevel.BP_MapGimmick_EventTrigger_C_2"&&read<int>(target,L"m_UniqueId")==9;
}
bool zoldaad_roof_exit(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_44/GD/Dng_06Zol_44_01_GD.Dng_06Zol_44_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_5"&&read<int>(target,L"m_UniqueId")==5;
}
bool zoldaad_captain_approach(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_44/EV/Dng_06Zol_44_01_EV.Dng_06Zol_44_01_EV:"
        L"PersistentLevel.CPP_MapGimmick_EventTrigger_1"&&read<int>(target,L"m_UniqueId")==6;
}
bool zoldaad_captain_exit(UObject* target) {
    return path(target)==L"/Game/Map/Dng/06Zol/06Zol_44/GD/Dng_06Zol_44_01_GD.Dng_06Zol_44_01_GD:"
        L"PersistentLevel.BP_MapGimmick_TriggerDoor003_C_3"&&read<int>(target,L"m_UniqueId")==19;
}
struct SlidingDoorApproach {
    Vec3 approach,entry;
    ActivationBox corridor;
    const char* failure;
};
std::optional<SlidingDoorApproach> sliding_door_approach(UObject* target) {
    if(dilmagia_shipyard_gate(target))return SlidingDoorApproach{{11170,0,2.15},{11273,0,2.15},
        {{11050,0,2.15},{440,210,100},0},"The Shipyard gate approach could not be measured."};
    if(zoldaad_factory_hall_gate(target))return SlidingDoorApproach{{3255,1900,2.15},{3680,1900,2.15},
        {{3480,1900,2.15},{290,245,100},0},"The factory door approach could not be measured."};
    if(zoldaad_captain_approach(target))return SlidingDoorApproach{chapter_four::captain_hall_landing,{30334,5587,1182.15},
        {{29870,5600,1182.15},{630,265,140},0},"The captain's door approach could not be measured."};
    return {};
}
bool waterfall_south_entrance(UObject* target) {
    return path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_22"&&
        read<int>(target,L"m_UniqueId")==20;
}
constexpr Vec3 waterfall_south_road{2020,-418.15,-24.85};
bool cave_world_south_entrance(UObject* target) {
    return path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_25"&&
        read<int>(target,L"m_UniqueId")==22;
}
bool cave_world_north_entrance(UObject* target) {
    return path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_27"&&
        read<int>(target,L"m_UniqueId")==23;
}
bool water_shrine_world(UObject* actor) {
    return path(actor_world(actor))==L"/Game/Map/Dng/05Old/05Old_44/Dng_05Old_44_PL.Dng_05Old_44_PL";
}
std::optional<int> water_shrine_dry_passage(UObject* target) {
    if(!is(target,L"/Script/mucha.CPP_MapTransitionTrigger")||!water_shrine_world(target))return {};
    const auto id=read<int>(target,L"m_UniqueId");
    return id&&(*id==4||*id==10||*id==11||*id==15||*id==16||*id==17)?id:std::nullopt;
}
bool shrine_crate_push_goal(UObject* target,Vec3 goal) {
    if(!is(target,L"/Script/mucha.CPP_MapGimmick_PushObject")||read<int>(target,L"m_UniqueId")!=45||
       !water_shrine_world(target)||read<unsigned char>(target,L"m_IsInterctToMove")!=0)return false;
    const auto crate=tracked_actor_position(target);
    const auto push=crate?chapter_three::shrine_crate_push_side(*crate):std::nullopt;
    return push&&std::hypot(push->x-goal.x,push->y-goal.y,push->z-goal.z)<=5.;
}
bool shrine_water_switch(UObject* target,int id) {
    return is(target,L"/Script/mucha.CPP_MapGimmick_Switch")&&read<int>(target,L"m_UniqueId")==id&&
        water_shrine_world(target);
}
bool shrine_basin_boat(UObject* target) {
    return is(target,L"/Script/mucha.CPP_MapGimmick_Object_Raft")&&read<int>(target,L"m_UniqueId")==70&&
        water_shrine_world(target);
}
bool shrine_tower_ladder_base(UObject* target,Vec3 destination) {
    return is(target,L"/Script/mucha.CPP_MapGimmickLadder")&&read<int>(target,L"m_UniqueId")==76&&
        water_shrine_world(target)&&std::hypot(destination.x+740.,destination.y-12260.,destination.z-145.)<=5.;
}
bool shrine_lower_ladder_top(UObject* target,Vec3 destination) {
    return is(target,L"/Script/mucha.CPP_MapGimmickLadder")&&read<int>(target,L"m_UniqueId")==1&&
        water_shrine_world(target)&&std::hypot(destination.x+475.,destination.y-31050.,destination.z+2600.)<=5.;
}
bool zoldaad_capital_street(UObject* actor) {
    return path(actor_world(actor))==L"/Game/Map/Town/06Zol/06Zol_01/Town_06Zol_01_PL.Town_06Zol_01_PL";
}
std::optional<ActivationBox> story_walkway(UObject* target,Vec3 feet) {
    // These approaches have walkable collision beyond the registered mesh.
    // Scope the live sweep to the traversed parts of each map.
    const auto target_path=path(target);
    if(target_path==L"/Game/Map/Wld/11Adr/EV/Wld_11Adr_EV.Wld_11Adr_EV:"
        L"PersistentLevel.BP_MapUnit_NPC_Field_C_1"&&read<int>(target,L"m_UniqueId")==8) {
        const ActivationBox crater_floor{{-21525,20345,-185},{420,390,140},0};
        if(inside_box(crater_floor,feet))return crater_floor;
    }
    if(waterfall_first_passage(target)) {
        const ActivationBox walkway{{950,5950,15},{1700,1250,160},0};
        if(inside_box(walkway,feet))return walkway;
    }
    if(target_path==L"/Game/Map/Dng/05Old/05Old_41/EV/Dng_05Old_41_01_EV.Dng_05Old_41_01_EV:"
        L"PersistentLevel.BP_MapGimmick_EventTrigger_C_1") {
        const ActivationBox stairs{{0,-2100,-300},{650,2600,430},0};
        if(inside_box(stairs,feet))return stairs;
    }
    if(zoldaad_capital_street(target)) {
        // The only baked polygons in this map belong to the inn interior.
        const ActivationBox street{{1740,-1050,30},{2570,2150,240},0};
        if(inside_box(street,feet))return street;
    }
    if(is(target,L"/Script/mucha.CPP_MapTransitionTrigger")&&
       path(actor_world(target))==L"/Game/Map/Dng/06Zol/06Zol_45/Dng_06Zol_45_PL.Dng_06Zol_45_PL") {
        const ActivationBox gate_road{{950,0,160},{2300,490,330},0};
        if(inside_box(gate_road,feet))return gate_road;
    }
    return {};
}
enum class PassageMap {Other,Mitra,Shrine};
PassageMap passage_map(UObject* pawn) {
    const auto world=path(actor_world(pawn));
    if(world==L"/Game/Map/Town/01Gra/01Gra_20/Town_01Gra_20_PL.Town_01Gra_20_PL")return PassageMap::Mitra;
    if(world==L"/Game/Map/Dng/01Gra/01Gra_40/Dng_01Gra_40_PL.Dng_01Gra_40_PL")return PassageMap::Shrine;
    return PassageMap::Other;
}
bool separate_walking_room(PassageMap map,int from,int to) {
    if(from==to)return false;
    if(map==PassageMap::Shrine)return from==4||to==4;
    const auto indoor=[](int id){return id>=2&&id<=9;};
    return map==PassageMap::Mitra&&(indoor(from)||indoor(to));
}
std::optional<ActivationBox> inn_floor(UObject* pawn) {
    if(passage_map(pawn)!=PassageMap::Mitra)return {};
    const auto section=capture_walking_section(pawn,false);
    // The shipped inn ground floor has no Recast polygons on either mesh.
    return section&&section->id==2?std::optional{section->bounds}:std::nullopt;
}
bool mobliz_world(UObject* pawn) {
    return path(actor_world(pawn))==L"/Game/Map/Dng/04Dir/04Dir_41/Dng_04Dir_41_PL.Dng_04Dir_41_PL";
}
bool wind_shrine_world(UObject* pawn) {
    return path(actor_world(pawn))==L"/Game/Map/Dng/04Dir/04Dir_42/Dng_04Dir_42_PL.Dng_04Dir_42_PL";
}
std::optional<ActivationBox> wind_boarding_floor(UObject* pawn,UObject* target,Vec3 feet) {
    if(!wind_shrine_world(pawn)||actor_world(target)!=actor_world(pawn)||
       !is(target,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact"))return {};
    const auto id=read<int>(target,L"m_UniqueId");
    if(!id)return {};
    const auto dock=getter<Vec3>(target,L"K2_GetActorLocation");
    if(!dock||!finite(*dock))return {};
    ActivationBox corridor;
    Vec3 expected;
    switch(*id) {
    case 68:
        expected={8000,41330,1992};
        corridor={{10450,41900,2150},{3950,2300,760},0};
        break;
    case 70:
        expected={11630,38600,1592};
        corridor={{12050,37750,1480},{2250,1500,405},0};
        break;
    case 73:
        expected={10400,40200,1203};
        corridor={{10020,40200,1250},{1400,1050,280},0};
        break;
    case 74:
        expected={14024,43404,dock->z>4000?4802.:2804.};
        corridor={{14024,42890,expected.z+8},{760,1220,260},0};
        break;
    case 41:
        expected={17585,40201,5602};
        corridor={{13900,41500,5220},{4100,2450,620},0};
        break;
    default:return {};
    }
    if(std::hypot(dock->x-expected.x,dock->y-expected.y)>80||std::abs(dock->z-expected.z)>45)return {};
    // The walkway and the docked platform can be separate Recast islands.
    // Keep the physical boarding corridor on the surveyed deck only.
    return inside_box(corridor,feet)?std::optional{corridor}:std::nullopt;
}
std::optional<ActivationBox> mobliz_escape_floor(UObject* pawn,UObject* target,Vec3 feet) {
    if(!target||!mobliz_world(pawn)||actor_world(target)!=actor_world(pawn))return {};
    const auto id=read<int>(target,L"m_UniqueId");
    if(!id)return {};
    const bool event=is(target,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
    const bool lift=is(target,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact");
    const bool pedestal=is(target,L"/Script/mucha.CPP_MapGimmick_CallSwitch");
    const bool lever=is(target,L"/Script/mucha.CPP_MapGimmick_Switch");
    const bool gate=is(target,L"/Script/mucha.CPP_MapTransitionTrigger");
    const bool memo=*id==8&&requires_checked_interaction_box(target);
    // Both Recast meshes have no polygons on the timed escape's middle and
    // upper walkways. The lower deck and upper floor meet at ordinary stairs.
    // Coordinates bound the candidate search, not whether an edge is safe.
    if((event&&(*id==46||*id==48))||(lift&&*id==73)||(pedestal&&*id==14)||
       (lever&&*id>=63&&*id<=67)||memo) {
        const ActivationBox decks{{3150,12400,1250},{5000,4950,560},0};
        if(inside_box(decks,feet))return decks;
    }
    if((gate&&*id==4)||(event&&*id==47)) {
        const ActivationBox factory{{-550,10950,0},{3100,3600,400},0};
        if(inside_box(factory,feet))return factory;
    }
    if((gate&&*id==0)||(event&&*id==49)) {
        const ActivationBox entrance{{1290,-2590,0},{2900,3650,400},0};
        if(inside_box(entrance,feet))return entrance;
    }
    return {};
}
constexpr ActivationBox laboratory_west_passage{{670.,-6540.,17.},{280.,270.,90.},0};

bool leaves_laboratory_wall_contact(const CapsuleSweep& sweep,const CapsuleSweep::Contact& contact) {
    if(!contact.penetrating||std::abs(contact.normal.z)>.1||!inside_box(laboratory_west_passage,contact.from)||
       path(actor_world(sweep.pawn))!=L"/Game/Map/Dng/08Gro/08Gro_40/Dng_08Gro_40_PL.Dng_08Gro_40_PL")return false;
    const auto feet=controlled_agent_position(sweep.pawn);
    if(!feet||std::hypot(feet->x-contact.from.x,feet->y-contact.from.y,feet->z-contact.from.z)>.01)return false;
    const auto dx=contact.to.x-contact.from.x,dy=contact.to.y-contact.from.y;
    const auto distance=std::hypot(dx,dy);
    if(distance<=0||distance>sweep.radius||dx*contact.normal.x+dy*contact.normal.y<distance*.6)return false;
    const auto standing=sweep.trace(contact.to,contact.to);
    if(!standing||standing->blocking)return false;
    // The original rounded pose can overlap the wall by a few centimetres.
    // The same capsule must be clear from the new pose back to that contact.
    CapsuleSweep::Obstacle leaving{},returning{};
    const auto forward=sweep.trace(contact.from,contact.to,&leaving);
    if(!forward||!forward->penetrating||!leaving.component)return false;
    const auto reverse=sweep.trace(contact.to,contact.from,&returning);
    return reverse&&!reverse->penetrating&&(!reverse->blocking||
        (returning.component==leaving.component&&distance*(1.-returning.fraction)<=sweep.radius*.125));
}

std::optional<Vec3> room_lane(const CapsuleSweep& sweep,const ActivationBox& bounds,Vec3 from,Vec3 to,double scale,
        std::size_t* remaining_probes=nullptr,std::string* diagnostic=nullptr,std::vector<Vec3>* lane_turns=nullptr) {
    if(!inside_box(bounds,from)||!inside_box(bounds,{to.x,to.y,from.z}))return {};
    if(remaining_probes&&2.*std::ceil(std::hypot(to.x-from.x,to.y-from.y)/(.2*scale))>*remaining_probes)return {};
    std::optional<CapsuleSweep::Contact> contact;
    const auto live=sweep.ground_probe(&contact);
    const auto permit=[&] {
        if(!remaining_probes)return true;
        if(!*remaining_probes)return false;
        --*remaining_probes;
        return true;
    };
    const GroundProbe local{
        [&](Vec3 a,Vec3 b)->std::optional<bool>{
            auto blocked=permit()?live.blocked(a,b):std::nullopt;
            if(blocked==true&&contact&&leaves_laboratory_wall_contact(sweep,*contact))blocked=false;
            if(diagnostic&&(!blocked||*blocked))
                *diagnostic="sweep "+coordinates(a)+" to "+coordinates(b)+
                    (contact?" normal="+coordinates(contact->normal)+" penetrating="+std::to_string(contact->penetrating):"");
            return blocked;
        },
        [&](Vec3 point)->std::optional<Vec3>{
            const auto floor=permit()?live.project(point):std::nullopt;
            if(diagnostic)*diagnostic="floor "+coordinates(point)+" result="+(floor?coordinates(*floor):"unavailable");
            return floor&&inside_box(bounds,*floor)?floor:std::nullopt;
        },live.swept_floor_tolerance};
    const auto direct=checked_walking_leg(from,to,scale,sweep.floor_z,local);
    if(direct||!lane_turns||!contact||contact->penetrating||std::abs(contact->normal.z)>.1)return direct;
    // All earlier samples passed. Search from the last verified feet so
    // the local corridor only has to go around the obstruction.
    const auto start=contact->from;
    const auto length=std::hypot(to.x-start.x,to.y-start.y)/scale;
    if(length<=.01)return {};
    PathSnapshot leg{{start,to},{},{},scale,true,false};
    const auto normal=contact->normal;
    auto bypass=checked_walking_corridor(leg,{1,0.},start,normal,sweep.radius,sweep.floor_z,
        local,length,length-.01);
    if(!bypass||bypass->rejoin+1!=bypass->path.points.size())return {};
    const auto floor=local.project(to);
    if(!floor)return {};
    const auto& points=bypass->path.points;
    if(std::hypot(start.x-from.x,start.y-from.y)>scale*.01)lane_turns->push_back(start);
    lane_turns->insert(lane_turns->end(),points.begin()+1,points.end()-1);
    return floor;
}
UObject* capsule_route_mesh(UObject* pawn,UObject* system,std::wstring_view mesh_name) {
    auto* mesh=registered_default_surface(pawn,system);
    return mesh&&path(mesh)==mesh_name?mesh:nullptr;
}
bool renew_capsule_lane(UObject* pawn,std::wstring_view mesh_name,Vec3 feet,
        std::pair<Vec3,Vec3> leg,double scale,const std::optional<ActivationBox>& room) {
    const auto& [a,b]=leg;
    const auto length=std::hypot(b.x-a.x,b.y-a.y);
    if(length<=0)return false;
    const auto tx=(b.x-a.x)/length,ty=(b.y-a.y)/length;
    const auto offset=std::abs((feet.x-a.x)*ty-(feet.y-a.y)*tx);
    const auto left=std::hypot(b.x-feet.x,b.y-feet.y);
    if(offset>scale*.35)return false;
    if(left<=scale*.01)return true;
    const auto part=std::min(left,scale*1.2)/left;
    const Vec3 ahead{feet.x+(b.x-feet.x)*part,feet.y+(b.y-feet.y)*part,feet.z+(b.z-feet.z)*part};
    if(room) {
        const auto sweep=capture_capsule_sweep(pawn);
        return sweep&&room_lane(*sweep,*room,feet,ahead,scale).has_value();
    }
    auto* system=nav_library(pawn);
    auto* mesh=capsule_route_mesh(pawn,system,mesh_name);
    const auto sweep=mesh?capture_capsule_sweep(pawn):std::nullopt;
    return sweep&&capsule_lane(*sweep,system,mesh,feet,ahead,scale).has_value();
}
std::optional<double> walking_tick_bound(UObject* pawn) {
    auto* world=actor_world(pawn);
    auto* level=object(world,L"PersistentLevel");
    auto* settings=object(level,L"WorldSettings");
    if(!is(settings,L"/Script/Engine.WorldSettings")||settings->GetOuterPrivate()!=level||actor_world(settings)!=world)
        return {};
    const auto time=read<float>(settings,L"TimeDilation",L"FloatProperty");
    const auto cinematic=read<float>(settings,L"CinematicTimeDilation",L"FloatProperty");
    const auto demo=read<float>(settings,L"DemoPlayTimeDilation",L"FloatProperty");
    const auto actor=read<float>(pawn,L"CustomTimeDilation",L"FloatProperty");
    if(time!=1.f||cinematic!=1.f||demo!=1.f||actor!=1.f) {
        log("NAV capsule frame clock world="+std::to_string(time.value_or(-1))+
            " cinematic="+std::to_string(cinematic.value_or(-1))+" demo="+std::to_string(demo.value_or(-1))+
            " actor="+std::to_string(actor.value_or(-1)));
        return {};
    }
    // FixupDeltaSeconds in the supported executable (0x14441ad00)
    // clamps world time to the effective-dilation-scaled frame interval.
    const auto bound=read<float>(settings,L"MaxUndilatedFrameTime",L"FloatProperty");
    const auto minimum=read<float>(settings,L"MinUndilatedFrameTime",L"FloatProperty");
    if(!bound||!std::isfinite(*bound)||*bound<=0||!minimum||!std::isfinite(*minimum)||
       *minimum<0||*minimum>*bound)return {};
    return *bound;
}
UObject* walking_mesh(UObject* pawn,UObject* system,Vec3 start);
UObject* swept_walking_mesh(UObject* pawn,UObject* system,Vec3 feet,std::wstring_view route_mesh) {
    if(swept_default_mesh(actor_world(pawn),route_mesh)) {
        // A local capsule-checked lane can run beside the baked world edge.
        // Keep the exact registered mesh which supplied its native rejoin.
        auto* alternate=registered_default_surface(pawn,system);
        return alternate&&path(alternate)==route_mesh?alternate:nullptr;
    }
    auto* preferred=walking_mesh(pawn,system,feet);
    return preferred&&path(preferred)==route_mesh?preferred:nullptr;
}
std::optional<NativeWalkingPulse> prepare_walking_pulse(UObject* pawn,std::wstring_view mesh_name,Vec3 feet,Vec3 direction,
        Vec3 waypoint,double scale,std::optional<double> checked_metres,std::wstring_view detour_mesh,
        const std::optional<ActivationBox>& room,bool room_pivot) {
    auto* movement=object(pawn,L"CharacterMovement");
    auto* movement_slot=property(pawn,L"CharacterMovement",L"ObjectProperty",sizeof(UObject*));
    auto* input_slot=property(pawn,L"m_InputActionValue",L"StructProperty",2*sizeof(double));
    auto* max_speed=property(movement,L"MaxWalkSpeed",L"FloatProperty",sizeof(float));
    auto* velocity_slot=property(movement,L"Velocity",L"StructProperty",sizeof(Vec3));
    auto* movement_mode=property(movement,L"MovementMode",nullptr,sizeof(unsigned char));
    auto* accumulated_input=property(pawn,L"ControlInputVector",L"StructProperty",sizeof(Vec3));
    auto* root_slot=property(pawn,L"RootComponent",L"ObjectProperty",sizeof(UObject*));
    auto* root=object(pawn,L"RootComponent");
    const auto position=is(root,L"/Script/Engine.SceneComponent")?getter<Vec3>(root,L"K2_GetComponentLocation"):std::nullopt;
    const auto threshold=read<float>(pawn,L"m_MovementInputThreshold",L"FloatProperty");
    const auto axes=walking_input_axes(pawn);
    const auto stick=axes?walking_stick(direction,*axes):std::nullopt;
    const auto interval=getter<float>(movement,L"GetComponentTickInterval");
    const auto impulse=read<Vec3>(movement,L"PendingImpulseToApply",L"StructProperty");
    const auto force=read<Vec3>(movement,L"PendingForceToApply",L"StructProperty");
    const auto launch=read<Vec3>(movement,L"PendingLaunchVelocity",L"StructProperty");
    const auto input=getter<Vec3>(pawn,L"GetPendingMovementInputVector");
    const auto gravity=getter<Vec3>(movement,L"GetGravityDirection");
    const auto zero=[](const std::optional<Vec3>& v){return v&&v->x==0&&v->y==0&&v->z==0;};
    const auto fail=[](const std::string& reason)->std::optional<NativeWalkingPulse> {
        log("NAV native walking: "+reason);return {};
    };
    if(!movement_slot||!input_slot||!max_speed||!velocity_slot||!movement_mode||!accumulated_input||!root_slot||!position||
       !finite(*position)||std::hypot(position->x-feet.x,position->y-feet.y)>scale*.001||
       !stick||!threshold||!std::isfinite(*threshold)||*threshold<0||*threshold>=1.f)
        return fail("native movement input layout unavailable");
    // Playable Movement reads the root transform's translation at +0x200.
    // Match that layout to the live reflected position before retaining it.
    const auto* position_slot=reinterpret_cast<const std::byte*>(root)+0x200;
    Vec3 root_position;
    std::memcpy(&root_position,position_slot,sizeof(root_position));
    if(!finite(root_position)||std::hypot(root_position.x-position->x,root_position.y-position->y,
       root_position.z-position->z)>scale*.001)return fail("native root translation unavailable");
    if(!interval||*interval!=0||!get_bool(movement,L"IsComponentTickEnabled"))return fail("movement tick unavailable");
    if(get_bool(pawn,L"HasAnyRootMotion",true))return fail("root motion unavailable or active");
    if(boolean(movement,L"bForceMaxAccel",true)||boolean(movement,L"bHasRequestedVelocity",true)||
       boolean(movement,L"bUseRVOAvoidance",true))return fail("native acceleration override");
    if(!zero(impulse)||!zero(force)||!zero(launch))return fail("pending native force unavailable or active");
    if(!gravity||gravity->x!=0||gravity->y!=0||gravity->z!=-1)return fail("gravity direction unavailable or changed");
    if(!input||!finite(*input))return fail("pending input unavailable");
    if(!zero(input)&&!owns_native_walking_input(pawn,*input))return NativeWalkingPulse{};
    const auto velocity=read<Vec3>(movement,L"Velocity",L"StructProperty");
    const auto acceleration=getter<float>(movement,L"GetMaxAcceleration");
    const auto braking=getter<float>(movement,L"GetMaxBrakingDeceleration");
    const auto tick=walking_tick_bound(pawn);
    const auto positive=[](const auto& v){return v&&std::isfinite(*v)&&*v>0;};
    if(!velocity||!finite(*velocity)||!positive(acceleration)||!positive(braking)||!positive(tick))
        return fail("kinematics velocity="+std::to_string(velocity.has_value())+
            " acceleration="+std::to_string(acceleration.value_or(-1))+" braking="+std::to_string(braking.value_or(-1))+
            " tick="+std::to_string(tick.value_or(-1)));
    const auto speed=std::hypot(velocity->x,velocity->y);
    const auto side_speed=std::abs(velocity->x*direction.y-velocity->y*direction.x);
    // Let the previous leg's velocity settle before proving a new heading.
    // A probe for the new leg during this coast can cut across the corner.
    if(speed>scale*.001&&((velocity->x*direction.x+velocity->y*direction.y)<=0||
       side_speed>scale*.001))return NativeWalkingPulse{};
    const auto left=std::hypot(waypoint.x-feet.x,waypoint.y-feet.y);
    if(!std::isfinite(left))return NativeWalkingPulse{};
    const auto verified=checked_metres?std::min(left,*checked_metres*scale):left;
    auto lane=verified;
    // A held detour turn can otherwise stop short forever: braking reserves a
    // fraction of the remaining approach, then the input dead zone takes over.
    // Find a little runway through the pivot using the live feet and capsule.
    // The route follower still independently verifies the new heading.
    if((room_pivot||!detour_mesh.empty())&&left>scale*.0001&&left<=scale*.04&&speed<=scale*.001) {
        auto* system=room_pivot?nullptr:nav_library(pawn);
        auto* mesh=system?swept_walking_mesh(pawn,system,feet,detour_mesh):nullptr;
        const auto sweep=mesh||room_pivot?capture_capsule_sweep(pawn):std::nullopt;
        const auto check=swept_default_mesh(actor_world(pawn),detour_mesh)?swept_capsule_lane:capsule_lane;
        if(sweep)for(const double extension:{.05,.03,.015}) {
            const auto distance=left+scale*extension;
            const Vec3 beyond{feet.x+direction.x*distance,feet.y+direction.y*distance,waypoint.z};
            if(distance-std::min(scale*.04,distance*.2)>left+scale*.001&&
               (room_pivot?room_lane(*sweep,*room,feet,beyond,scale):check(*sweep,system,mesh,feet,beyond,scale))) {
                lane=distance;
                log("NAV detour pivot runway from="+coordinates(feet)+" pivot="+coordinates(waypoint)+
                    " clearance_cm="+std::to_string(distance));
                break;
            }
        }
    }
    if(left<=scale*.01&&lane<=verified)return NativeWalkingPulse{};
    if(!mesh_name.empty()||room) {
        auto* system=room?nullptr:nav_library(pawn);
        auto* mesh=room?nullptr:capsule_route_mesh(pawn,system,mesh_name);
        const auto sweep=mesh||room?capture_capsule_sweep(pawn):std::nullopt;
        if(!sweep)return fail("current mesh or capsule profile unavailable");
        const auto jogging=boolean(pawn,L"m_IsDash");
        const auto authored=read<float>(pawn,jogging?L"m_MaxJogSpeed":L"m_MaxRunningSpeed",L"FloatProperty");
        const auto retained=read<float>(movement,L"MaxWalkSpeed",L"FloatProperty");
        if(!positive(retained))return fail("native maximum speed unavailable");
        const double preferred=std::max(*retained,positive(authored)?*authored:0.f)*(jogging?1.:.8);
        const auto stopping_distance=[&](double velocity) {
            return velocity*(*tick+velocity/(2.* *braking));
        };
        const auto runway=lane;
        const auto short_lane=std::min(runway,scale*2.);
        // The previously queued step can consume part of this lane before
        // the native walking call. Its actual distance is subtracted there.
        const auto previous_step=(speed>scale*.001||!zero(input))?std::max(speed,preferred)* *tick:0.;
        const auto desired_lane=previous_step+stopping_distance(preferred)+scale*.05;
        lane=std::min(runway,std::clamp(desired_lane,scale*2.,scale*12.));
        // The world route already used the selected mesh's live capsule
        // proof. Sweep its steering chord with the same support and step rules.
        const bool world_lane=swept_default_mesh(actor_world(pawn),mesh_name);
        const auto check=world_lane?swept_capsule_lane:capsule_lane;
        const auto clear=[&](double distance) {
            const auto z=feet.z+(waypoint.z-feet.z)*std::min(1.,distance/left);
            const Vec3 ahead{feet.x+direction.x*distance,feet.y+direction.y*distance,z};
            return room?room_lane(*sweep,*room,feet,ahead,scale).has_value():
                check(*sweep,system,mesh,feet,ahead,scale).has_value();
        };
        if(!clear(lane)) {
            if(lane<=short_lane||!clear(short_lane)) {
                // Let a world pawn settle before the follower proves the
                // turn again or replans from its new standing position.
                if(world_lane||(room&&(mobliz_world(pawn)||waterfall_world(pawn))))return NativeWalkingPulse{};
                return fail("the steering lane is blocked from="+coordinates(feet)+" toward="+coordinates(waypoint));
            }
            lane=short_lane;
        }
        if(stopping_distance(speed)>lane)return NativeWalkingPulse{};
    }
    std::optional<double> min_analog_speed;
    if(path(movement->GetClassPrivate())==L"/Script/Engine.CharacterMovementComponent") {
        const auto minimum=getter<float>(movement,L"GetMinAnalogSpeed");
        if(minimum&&std::isfinite(*minimum)&&*minimum>=0)min_analog_speed=*minimum;
    }
    return NativeWalkingPulse{pawn,input_slot,movement_slot,movement,max_speed,velocity_slot,movement_mode,accumulated_input,
        root_slot,root,position_slot,root_position,direction,*stick,lane-std::min(scale*.04,lane*.2),
        scale*.01,*acceleration,*braking,*tick,min_analog_speed};
}
UObject* walking_mesh(UObject* pawn,UObject* system,Vec3 start) {
    if(!valid(system)||path(system->GetClassPrivate())!=L"/Script/NavigationSystem.NavigationSystemV1")return nullptr;
    auto* movement=object(pawn,L"CharacterMovement");
    auto* props=property(movement,L"NavAgentProps",L"StructProperty",56);
    auto* props_field=movement?movement->GetPropertyByNameInChain(L"NavAgentProps"):nullptr;
    if(!props||!props_field)return nullptr;
    auto& props_handle=static_cast<FStructProperty*>(props_field)->GetStruct();UScriptStruct* props_type{};
    std::memcpy(&props_type,std::addressof(props_handle),sizeof props_type);
    if(!native_metadata(props_type,NativeMetadataKind::ScriptStruct)||path(props_type)!=L"/Script/Engine.NavAgentProperties"||
       props_type->GetPropertiesSize()!=56)return nullptr;
    const auto api=direct_navigation_api();
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto table=*reinterpret_cast<void***>(system);
    if(table[0x350/8]!=reinterpret_cast<void*>(api.select)||table[0x358/8]!=reinterpret_cast<void*>(base+0x4a26ca0))return nullptr;
    stage="native navigation agent mesh selection";
    auto* mesh=static_cast<UObject*>(direct_nav::select_mesh(api,system,props,start));
    float agent_radius{},agent_height{};
    std::memcpy(&agent_radius,static_cast<std::byte*>(props)+4,4);
    std::memcpy(&agent_height,static_cast<std::byte*>(props)+8,4);
    static std::string last_selection;
    const auto selection="world="+utf8(path(actor_world(pawn)))+" selected="+(valid(mesh)?utf8(path(mesh)):"unavailable")+
        " agent_radius="+std::to_string(agent_radius)+" agent_height="+std::to_string(agent_height)+
        " main="+(object(system,L"MainNavData")?"present":"unset");
    if(selection!=last_selection){last_selection=selection;log("NAV selection "+selection);}
    if(!current_walking_mesh(pawn,system,mesh))return nullptr;
    auto* capsule=object(pawn,L"CapsuleComponent");
    if(!capsule||!is(capsule,L"/Script/Engine.CapsuleComponent")||get_object(capsule,L"GetOwner")!=pawn)return nullptr;
    const auto radius=getter<float>(capsule,L"GetScaledCapsuleRadius"),half=getter<float>(capsule,L"GetScaledCapsuleHalfHeight");
    const auto mesh_radius=read<float>(mesh,L"AgentRadius",L"FloatProperty"),mesh_height=read<float>(mesh,L"AgentHeight",L"FloatProperty");
    auto* config=property(mesh,L"NavDataConfig",L"StructProperty",136);
    auto* field=mesh->GetPropertyByNameInChain(L"NavDataConfig");
    if(!radius||!half||!mesh_radius||!mesh_height||!config||!field)return nullptr;
    auto& handle=static_cast<FStructProperty*>(field)->GetStruct();UScriptStruct* type{};
    std::memcpy(&type,std::addressof(handle),sizeof type);
    if(!native_metadata(type,NativeMetadataKind::ScriptStruct)||path(type)!=L"/Script/Engine.NavDataConfig"||type->GetPropertiesSize()!=136)return nullptr;
    const auto member=[&](const wchar_t* name,int offset) {
        for(auto* current=static_cast<UStruct*>(type);current;current=current->GetSuperStruct()) {
            auto* child=current->FindProperty(FName(name,FNAME_Find));
            if(child)return child->GetOffset_Internal()==offset&&child->GetSize()==4;
        }
        return false;
    };
    if(!member(L"AgentRadius",4)||!member(L"AgentHeight",8))return nullptr;
    float config_radius{},config_height{};
    std::memcpy(&config_radius,static_cast<std::byte*>(config)+4,4);std::memcpy(&config_height,static_cast<std::byte*>(config)+8,4);
    const bool fits=navmesh_supports_capsule(config_radius,config_height,*mesh_radius,*mesh_height,*radius,*half);
    static std::string previous;
    const auto diagnostic="selected="+utf8(path(mesh))+" main="+(object(system,L"MainNavData")?"present":"unset")+
        " radius="+std::to_string(*mesh_radius)+" height="+std::to_string(*mesh_height)+
        " capsule_radius="+std::to_string(*radius)+" capsule_half_height="+std::to_string(*half)+" compatible="+std::to_string(fits);
    if(diagnostic!=previous){previous=diagnostic;log("NAV mesh "+diagnostic);}
    return fits?mesh:nullptr;
}
std::optional<WalkingGroundPolicy> capture_ground_policy(UObject* pawn,double scale) {
    auto* movement=object(pawn,L"CharacterMovement");
    const auto speed=read<float>(movement,L"MaxWalkSpeed",L"FloatProperty");
    const auto velocity=read<Vec3>(movement,L"Velocity",L"StructProperty");
    const auto braking=read<float>(movement,L"BrakingDecelerationWalking",L"FloatProperty");
    const auto friction=read<float>(movement,L"GroundFriction",L"FloatProperty");
    const auto braking_friction=read<float>(movement,L"BrakingFriction",L"FloatProperty");
    const auto factor=read<float>(movement,L"BrakingFrictionFactor",L"FloatProperty");
    if(!speed||!velocity||!finite(*velocity)||!braking||!friction||!braking_friction||!factor||
       !std::isfinite(*friction)||*friction<0||!std::isfinite(*braking_friction)||*braking_friction<0||
       !std::isfinite(*factor)||*factor<0)return {};
    return walking_ground_policy(*speed,std::hypot(velocity->x,velocity->y,velocity->z),*braking,scale);
}
constexpr ActivationBox gaberada_elder_ramp{{1370.,-990.,27.},{1120.,795.,220.},0};
constexpr ActivationBox gaberada_upper_ramp{{4780.,-3635.,289.},{730.,275.,248.},0};
constexpr ActivationBox gronoa_lower_stair_turn{{-2505.,-878.,-4190.},{155.,183.,100.},0};
enum class GroundProof { MeshFirst, CapsuleSweep, SweptTurn, CapsulePrefix };
std::optional<GroundCoverage> ordinary_ground_path(UObject* pawn,UObject* library,UObject* mesh,
        PathSnapshot& path,std::uint64_t path_id,const GroundBudget& budget,std::string& reason,std::size_t& bypass_rejoin,
        GroundProof proof=GroundProof::MeshFirst,std::size_t prefix_legs=0) {
    const auto started=std::chrono::steady_clock::now();
    const auto scale=path.world_to_metres;
    reason.clear();
    const auto fail=[&](const char* message)->std::optional<GroundCoverage>{reason=message;return {};};
    if(!std::isfinite(scale)||scale<=0||!path_id)return fail("The walking-ground inspection could not be prepared.");
    auto* world=actor_world(pawn);
    if(!world||!current_walking_mesh(pawn,library,mesh))return fail("The walking area changed before its ground check.");
    const bool gronoa=resonance::path(world)==L"/Game/Map/Dng/08Gro/08Gro_42/Dng_08Gro_42_PL.Dng_08Gro_42_PL";
    const auto character_slope=read<float>(object(pawn,L"CharacterMovement"),L"WalkableFloorAngle",L"FloatProperty");
    const auto floor_z=read<float>(object(pawn,L"CharacterMovement"),L"WalkableFloorZ",L"FloatProperty");
    const auto mesh_slope=read<float>(mesh,L"AgentMaxSlope",L"FloatProperty");
    const auto slope_known=[](const auto& value){return value&&std::isfinite(*value)&&*value>=0&&*value<90;};
    if(!slope_known(character_slope)||!slope_known(mesh_slope)||!floor_z||!std::isfinite(*floor_z)||
       *floor_z<0||*floor_z>1||std::abs(std::cos(*character_slope*3.14159265358979323846/180)-*floor_z)>.001)
        return fail("The character and walking mesh slope limits could not be verified.");
    auto native_budget=budget;
    native_budget.walkable_slope_degrees=std::min(*character_slope,*mesh_slope);
    static std::string previous_slopes;
    const auto slopes="character="+std::to_string(*character_slope)+" mesh="+std::to_string(*mesh_slope)+
        " effective="+std::to_string(*native_budget.walkable_slope_degrees);
    if(slopes!=previous_slopes){previous_slopes=slopes;log("NAV slopes "+slopes);}
    const Vec3 extent{scale*.02,scale*.02,scale*.10};
    GroundProbe probe;
    std::optional<std::array<Vec3,3>> obstruction;
    constexpr std::size_t max_corner_rechecks=8;
    direct_nav::SurfaceRayRetry corner_retry{scale*.025,0};
    std::size_t verified_corners=0;
    probe.blocked=[&](Vec3 a,Vec3 b)->std::optional<bool> {
        if(!valid(mesh))return {};
        stage="native selected-mesh ground ray";
        Vec3 hit;
        const auto retries=corner_retry.remaining;
        const auto blocked=direct_nav::raycast(mesh,a,b,&hit,&corner_retry);
        if(retries!=corner_retry.remaining&&blocked&&!*blocked)++verified_corners;
        if(blocked&&*blocked)obstruction=std::array{a,b,hit};
        return blocked;
    };
    probe.project=[&](Vec3 point)->std::optional<Vec3> {
        stage="native selected-mesh ground projection";
        const auto surface=project_navigation(pawn,library,mesh,point,extent);
        return surface&&surface->on_mesh?std::optional<Vec3>{surface->position}:std::nullopt;
    };
    const auto local_budget=ground_window_budget(path,native_budget);
    if(!local_budget)return fail("The ground ahead could not be inspected.");
    const bool rechecking_bypass=bypass_rejoin!=0;
    const bool capsule_only=proof!=GroundProof::MeshFirst;
    const bool world_capsule=swept_default_mesh(world,resonance::path(mesh));
    const bool corridor_capsule=world_capsule||(capsule_only&&zoldaad_swept_walkway(world));
    const bool mitra=resonance::path(world)==L"/Game/Map/Town/01Gra/01Gra_20/Town_01Gra_20_PL.Town_01Gra_20_PL";
    const bool gaberada=capsule_only&&
        resonance::path(world)==L"/Game/Map/Town/08Gro/08Gro_01/Town_08Gro_01_PL.Town_08Gro_01_PL";
    const bool crystal_tower=resonance::path(world)==
        L"/Game/Map/Dng/10Cry/10Cry_40/Dng_10Cry_40_PL.Dng_10Cry_40_PL";
    // Use the native capsule and floor traces where the Crystal Tower bake
    // misses a ramp surface or cuts the inside edge of the return passage.
    constexpr ActivationBox lower_chamber_ramp{{-270.,-90860.,1000.},{370.,175.,175.},0.};
    constexpr ActivationBox lower_chamber_return_ramp{{-1625.,-90857.,950.},{240.,220.,130.},0.};
    constexpr ActivationBox middle_reach_stairs{{1080.,-297660.,2880.},{905.,330.,435.},0.};
    constexpr ActivationBox middle_reach_lower_turn{{-3250.,-297045.,975.},{260.,200.,145.},0.};
    const auto middle_reach_turn_floor=[&](Vec3 feet) {
        return crystal_tower&&inside_box(middle_reach_lower_turn,feet);
    };
    const auto tower_ramp_floor=[&](Vec3 feet) {
        return middle_reach_turn_floor(feet)||(crystal_tower&&(inside_box(lower_chamber_ramp,feet)||
            inside_box(lower_chamber_return_ramp,feet)||inside_box(middle_reach_stairs,feet)));
    };
    const auto gaberada_floor=[&](Vec3 feet) {
        return gaberada&&(inside_box(gaberada_elder_ramp,feet)||inside_box(gaberada_upper_ramp,feet));
    };
    const auto on_swept_prefix=[&](Vec3 p) {
        if(proof!=GroundProof::CapsulePrefix||prefix_legs==0||prefix_legs>4||path.points.size()<=prefix_legs+1)return false;
        double total=0.;bool covered=false;
        for(std::size_t i=1;i<=prefix_legs;++i) {
            const auto a=path.points[i-1],b=path.points[i];
            const auto x=b.x-a.x,y=b.y-a.y,length=std::hypot(x,y);
            if(length<=0)return false;
            total+=length;
            const auto along=((p.x-a.x)*x+(p.y-a.y)*y)/length;
            const auto across=std::abs((p.x-a.x)*y-(p.y-a.y)*x)/length;
            covered|=along>=-scale*.02&&along<=length&&across<=scale*.02;
        }
        return covered&&total<=scale*12.;
    };
    auto coverage=rechecking_bypass||capsule_only?std::nullopt:plan_ground_coverage(path,path_id,*local_budget,probe);
    const auto covers_rejoin=[](const GroundCoverage& ground,std::size_t rejoin) {
        return ground.end.segment>rejoin||(ground.end.segment==rejoin&&ground.end.fraction==1.);
    };
    std::size_t initial_calls=0;
    bool used_capsule=false;
    bool bypassed=false;
    std::string capsule_failure;
    const auto report_capsule_failure=[&] {
        static std::string previous;
        if(!capsule_failure.empty()&&capsule_failure!=previous) {
            previous=capsule_failure;
            log("NAV capsule surface limit "+capsule_failure);
        }
    };
    if(rechecking_bypass||capsule_only||(coverage&&(coverage->requires_surface||coverage->limit==GroundLimit::Surface||
        coverage->limit==GroundLimit::Projection||coverage->limit==GroundLimit::Obstruction))) {
        // A native string-pulled chord need not contain terrain breakpoints.
        // A ray starting on a mesh edge can also refuse a continuous floor.
        // Keep the native route and test actual support and capsule clearance
        // along its XY trace. Each supported step must still be on this mesh.
        initial_calls=coverage?coverage->native_calls:0;
        std::optional<GroundCoverage> physical;
        const auto sweep=capture_capsule_sweep(pawn);
        const auto feet=controlled_agent_position(pawn);
        auto origin=path.points.front();
        std::optional<CapsuleSweep::Contact> contact;
        const bool local_ramp=feet&&((mitra&&inside_box(mitra_weapon_stair,*feet))||tower_ramp_floor(*feet));
        if((corridor_capsule||local_ramp)&&sweep&&feet&&
           std::hypot(feet->x-origin.x,feet->y-origin.y)<=scale*(local_ramp?.25:.05)) {
            // The baked path can differ from the road or ramp at the same
            // XY. Anchor each replan to the live floor under the character.
            const auto floor=sweep->ground_probe(&contact).project(*feet);
            if(floor&&std::abs(floor->z-feet->z)<=sweep->step_height) {
                origin=*floor;
                path.points.front()=*floor;
            }
        }
        if(sweep&&feet&&std::hypot(feet->x-origin.x,feet->y-origin.y)<=scale*2&&std::abs(feet->z-origin.z)<=sweep->step_height) {
            const auto live=sweep->ground_probe(&contact);
            const auto approach=checked_walking_leg(*feet,origin,scale,sweep->floor_z,live);
            if(approach&&std::abs(approach->z-origin.z)<=sweep->step_height) {
                std::size_t mesh_projections=0;
                auto last_supported=*approach;
                GroundProbe floor{[&](Vec3 a,Vec3 b)->std::optional<bool> {
                    const auto blocked=live.blocked(a,b);
                    if(blocked&&!*blocked)last_supported=b;
                    return blocked;
                },[&](Vec3 point)->std::optional<Vec3> {
                    const auto support=live.project(point);
                    if(!support) {capsule_failure="floor support missing at "+coordinates(point);return {};}
                    // Sweep the actual surface when the bake differs from
                    // the road or stairs. The route and rejoin are native.
                    if(corridor_capsule||on_swept_prefix(point)||gaberada_floor(*support)||
                       olderion_plaza_floor(world,*support)||tower_ramp_floor(*support)||
                       (mitra&&inside_box(mitra_weapon_stair,*support))||
                       (gronoa&&inside_box(gronoa_lower_stair_turn,last_supported)&&
                           inside_box(gronoa_lower_stair_turn,*support)))return support;
                    ++mesh_projections;
                    if(!capsule_floor_on_mesh(*sweep,library,mesh,*support,scale)) {
                        capsule_failure="supported floor outside native mesh at "+coordinates(*support);return {};
                    }
                    return support;
                },sweep->step_height/scale,[&](Vec3 a,Vec3 b) {
                    return (gaberada_floor(a)&&gaberada_floor(b))||
                        (middle_reach_turn_floor(a)&&middle_reach_turn_floor(b));
                }};
                auto physical_budget=native_budget;physical_budget.follow_surface=true;
                const auto verify=[&](const PathSnapshot& candidate)->std::optional<GroundCoverage> {
                    const auto funded=surface_window_budget(candidate,physical_budget);
                    if(!funded)return {};
                    last_supported=*approach;
                    contact.reset();
                    capsule_failure.clear();
                    auto checked=plan_ground_coverage(candidate,path_id,*funded,floor);
                    if(checked&&checked->limit==GroundLimit::Obstruction&&contact) {
                        capsule_failure="capsule contact at "+coordinates(contact->from)+" to "+coordinates(contact->to)+
                            " normal="+coordinates(contact->normal)+" penetrating="+std::to_string(contact->penetrating);
                    }
                    return checked;
                };
                physical=verify(path);
                const bool plaza_corner=contact&&!contact->penetrating&&olderion_plaza_floor(world,contact->from);
                const bool extend=(proof==GroundProof::CapsuleSweep&&world_capsule)||plaza_corner;
                const unsigned repairs=extend?3:rechecking_bypass||proof==GroundProof::SweptTurn||proof==GroundProof::CapsulePrefix?0:1;
                // Bound speculative collision checks on the game thread.
                // The accepted path still gets the full ground verification.
                std::size_t repair_probes=512,corridor_probes=1024;
                const auto budgeted_floor=[&floor](std::size_t& probes) {
                    const auto spend=[left=&probes]() {
                        if(!*left)return false;
                        --*left;
                        return true;
                    };
                    return GroundProbe{
                        [&floor,spend](Vec3 a,Vec3 b){return spend()?floor.blocked(a,b):std::nullopt;},
                        [&floor,spend](Vec3 point){return spend()?floor.project(point):std::nullopt;},
                        floor.swept_floor_tolerance};
                };
                const auto search=budgeted_floor(repair_probes),corridor=budgeted_floor(corridor_probes);
                for(unsigned attempt=0;attempt<repairs&&physical;++attempt) {
                    if(physical->limit!=GroundLimit::Obstruction&&physical->limit!=GroundLimit::Projection)break;
                    if(bypass_rejoin&&!covers_rejoin(*physical,bypass_rejoin))break;
                    // Keep walking the swept prefix while the next obstacle is
                    // distant. Repeated speculative searches behind a retained
                    // detour can stall input for several frames.
                    if(extend&&rechecking_bypass&&physical->metres>3.5)break;
                    // A checked detour may end immediately before another
                    // wall. Extend it only after its old rejoin is still clear.
                    const auto failure=capsule_failure;
                    const auto progress=extend?horizontal_arc_metres(path,physical->end):std::nullopt;
                    const auto lookahead=progress?physical_budget.horizon_metres-*progress-2.*sweep->radius/scale:0.;
                const auto from=last_supported,normal=contact&&!contact->penetrating?contact->normal:Vec3{};
                    const auto obstructed_metres=contact&&!contact->penetrating?
                        std::hypot(contact->to.x-contact->from.x,contact->to.y-contact->from.y)/scale:
                        physical_budget.spacing_metres;
                    auto bypass=!extend||repair_probes?checked_walking_bypass(path,physical->end,from,normal,
                        sweep->radius,sweep->floor_z,extend?search:floor,lookahead):std::nullopt;
                    const auto verify_rejoin=[&]()->std::optional<GroundCoverage> {
                        if(!bypass||bypass->rejoin>=bypass->path.points.size())return {};
                        if((world_capsule||plaza_corner)&&
                           !capsule_floor_on_mesh(*sweep,library,mesh,bypass->path.points[bypass->rejoin],scale))
                            return {};
                        const auto checked=verify(bypass->path);
                        return checked&&covers_rejoin(*checked,bypass->rejoin)?checked:std::nullopt;
                    };
                    auto repaired=verify_rejoin();
                    bool used_corridor=false;
                    std::optional<Vec3> upstream;
                    if(!repaired&&extend&&corridor_probes&&lookahead>0.) {
                        // A corner immediately beyond a retained rejoin may
                        // need an earlier turn. Re-sweep the replacement from
                        // the live feet before accepting a new native rejoin.
                        auto backtrack_world=(rechecking_bypass?8.:1.)*sweep->radius;
                        auto entry=walking_detour_entry(path,physical->end,backtrack_world,
                            rechecking_bypass?0:bypass_rejoin,corridor);
                        if(!entry&&rechecking_bypass) {
                            backtrack_world=sweep->radius;
                            entry=walking_detour_entry(path,physical->end,backtrack_world,bypass_rejoin,corridor);
                        }
                        if(entry) {
                            const auto backtrack=backtrack_world/scale;
                            bypass=checked_walking_corridor(path,entry->location,entry->feet,normal,
                                sweep->radius,sweep->floor_z,corridor,lookahead+backtrack,backtrack+obstructed_metres);
                            repaired=verify_rejoin();
                            if(repaired)upstream=entry->feet;
                        }
                        if(!repaired&&corridor_probes) {
                            bypass=checked_walking_corridor(path,physical->end,from,normal,
                                sweep->radius,sweep->floor_z,corridor,lookahead);
                            repaired=verify_rejoin();
                        }
                        used_corridor=repaired.has_value();
                    }
                    if(!repaired) {
                        capsule_failure=failure;
                        break;
                    }
                    log("NAV capsule detour cleared "+failure+" rejoin="+
                        coordinates(bypass->path.points[bypass->rejoin])+
                        (used_corridor?" corridor=1":"")+
                        (upstream?" entry="+coordinates(*upstream):""));
                    path=std::move(bypass->path);bypass_rejoin=bypass->rejoin;
                    physical=repaired;bypassed=true;
                    if(!extend)capsule_failure.clear();
                }
                if(physical)physical->native_calls+=mesh_projections;
            } else capsule_failure="initial capsule clearance or height";
        } else capsule_failure="initial capsule or start height";
        if(physical&&!physical->reaches_end&&physical->limit!=GroundLimit::Horizon)
            capsule_failure=std::to_string(static_cast<int>(physical->limit))+" at "+std::to_string(physical->metres)+"m "+capsule_failure;
        if(rechecking_bypass) {
            if(!physical||!covers_rejoin(*physical,bypass_rejoin))
                return fail("The previous walking detour could not be verified from the current position.");
            coverage=physical;used_capsule=true;
        } else if(capsule_only) {
            if(!physical||!physical->swept_ground) {
                report_capsule_failure();
                return {};
            }
            coverage=physical;used_capsule=true;
        } else if(physical&&(bypassed||physical->reaches_end||physical->limit==GroundLimit::Horizon||
           physical->limit==GroundLimit::Obstruction)) {
            coverage=physical;used_capsule=true;
        } else {
            // Preserve the existing fine mesh proof for ground the native
            // mover can step onto, where a straight capsule sweep stops.
            const auto surface=surface_following_budget(native_budget);
            const auto funded=surface?surface_window_budget(path,*surface):std::nullopt;
            if(!funded)return fail("The walking surface inspection could not be prepared.");
            corner_retry.remaining=max_corner_rechecks;
            coverage=plan_ground_coverage(path,path_id,*funded,probe);
            if(coverage)coverage->native_calls+=max_corner_rechecks-corner_retry.remaining;
            if(physical&&(!coverage||physical->metres>coverage->metres)) {coverage=physical;used_capsule=true;}
            else if(physical&&coverage)coverage->swept_ground=physical->swept_ground;
        }
    }
    if(actor_world(pawn)!=world||!current_walking_mesh(pawn,library,mesh))return fail("The walking area changed during its ground check.");
    if(!coverage)return fail("The ground ahead could not be inspected.");
    report_capsule_failure();
    if(!used_capsule&&coverage->limit==GroundLimit::Obstruction&&obstruction) {
        const auto point=[](Vec3 p){return std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z);};
        static std::string previous;
        const auto message="start="+point((*obstruction)[0])+" end="+point((*obstruction)[1])+
            " hit="+point((*obstruction)[2]);
        if(message!=previous){previous=message;log("NAV ground obstruction "+message);}
    }
    const auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
    static unsigned completed{};
    if(++completed==1||completed%20==0||verified_corners||used_capsule)log("NAV ground cost_us="+std::to_string(elapsed)+
        " calls="+std::to_string(coverage->native_calls)+" verified_metres="+std::to_string(coverage->metres)+
        " initial_calls="+std::to_string(initial_calls)+" surface="+std::to_string(coverage->follow_surface)+
        " corners_rechecked="+std::to_string(verified_corners)+" capsule_floor="+std::to_string(used_capsule)+
        " bypass="+std::to_string(bypassed)+" retained_bypass="+std::to_string(rechecking_bypass)+
        " reaches_end="+std::to_string(coverage->reaches_end)+" limit="+std::to_string(static_cast<int>(coverage->limit)));
    return coverage;
}
constexpr const char* hall_door_arrival="At the door to the hall. Walk through the door, then start the next route.";
struct NativeRoute {
    PathSnapshot snapshot;
    std::optional<GroundCoverage> ground;
    std::string ground_reason;
    std::optional<double> activation_arrival_metres;
    bool activation_reached{};
    std::string room_door;
    std::wstring capsule_mesh;
    std::wstring swept_mesh;
    std::size_t bypass_rejoin{};
    std::optional<ActivationBox> capsule_room;
};
std::array<Vec3,12> interaction_side_approaches(const ActivationBox& box,WalkingCapsule capsule,double z,double gap,double spread=.32) {
    const auto angle=box.yaw_degrees*3.14159265358979323846/180.;
    const auto c=std::cos(angle),s=std::sin(angle);
    std::array<Vec3,12> points;
    auto point=points.begin();
    for(const auto side:std::array<Vec3,4>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}}})
        for(const auto along:{0.,-spread,spread}) {
            const auto x=side.x*(box.half_extent.x+gap*capsule.radius)+std::abs(side.y)*along*box.half_extent.x;
            const auto y=side.y*(box.half_extent.y+gap*capsule.radius)+std::abs(side.x)*along*box.half_extent.y;
            *point++={box.center.x+x*c-y*s,box.center.y+x*s+y*c,z};
        }
    return points;
}
struct InteractionRoute { direct_nav::Path path; double arrival_metres; std::size_t lift_prefix{}; };
std::optional<InteractionRoute> interaction_route_on_mesh(const CapsuleSweep& sweep,UObject* system,UObject* mesh,
        Vec3 from,const ActivationBox& box,double z,double scale,bool chest=false,std::optional<Vec3> preferred={},
        bool portal_ramp=false) {
    const auto start=project_navigation(sweep.pawn,system,mesh,from);
    if(!start||!start->on_mesh)return {};
    const WalkingCapsule capsule{sweep.radius,sweep.half_height};
    auto candidates=box_corner_approaches(box,capsule,z);
    for(const auto gap:{.8,.92}) {
        const auto sides=interaction_side_approaches(box,capsule,z,gap);
        candidates.insert(candidates.end(),sides.begin(),sides.end());
    }
    // A nonblocking target can also be approached from inside the trigger.
    candidates.push_back({box.center.x,box.center.y,z});
    const auto distance=[&](Vec3 point){return std::hypot(point.x-from.x,point.y-from.y);};
    std::stable_sort(candidates.begin(),candidates.end(),[&](Vec3 a,Vec3 b){return distance(a)<distance(b);});
    if(chest) {
        // A wall chest can leave only one end of its long interaction box
        // accessible. Try that edge too, with more overlap than a corner.
        for(const auto gap:{.55,.8}) {
            const auto sides=interaction_side_approaches(box,capsule,z,gap,.88);
            candidates.insert(candidates.end(),sides.begin(),sides.end());
        }
    }
    if(preferred)candidates.insert(candidates.begin(),*preferred);
    const auto api=direct_navigation_api();
    std::vector<std::uint64_t> disconnected;
    for(const auto candidate:candidates) {
        const auto native=direct_nav::project_point(mesh,candidate);
        if(native&&(!native->polygon||std::find(disconnected.begin(),disconnected.end(),native->polygon)!=disconnected.end()))continue;
        const auto reflected=native?std::optional<NavigationProjection>{}:
            project_navigation(sweep.pawn,system,mesh,candidate);
        if(!native&&(!reflected||!reflected->on_mesh))continue;
        const auto projected=native?native->position:reflected->position;
        if(!activation_approach(box,capsule,projected,scale))continue;
        auto route=direct_nav::find_path(api,sweep.pawn,system,mesh,from,projected);
        if(route.partial&&native)disconnected.push_back(native->polygon);
        if(!route.valid)continue;
        const auto end=route.points.back(),prior=route.points[route.points.size()-2];
        const auto run=std::hypot(prior.x-end.x,prior.y-end.y);
        if(run<scale*.01)continue;
        // Verify the final metre against the real collision profile,
        // including the target's body.
        const auto t=std::min(1.,scale/run);
        const Vec3 tail{end.x+(prior.x-end.x)*t,end.y+(prior.y-end.y)*t,end.z+(prior.z-end.z)*t};
        auto floor=capsule_lane(sweep,system,mesh,tail,end,scale);
        if(!floor&&portal_ramp) {
            // The visible portal platforms can extend past their baked edge.
            // Keep the connected native route and sweep its final metre on the floor.
            const ActivationBox landing{end,{scale*1.5,scale*1.5,scale*1.5},0};
            const auto support=sweep.ground_probe().project(tail);
            if(support)floor=room_lane(sweep,landing,*support,end,scale);
        }
        if(!floor)continue;
        const auto approach=activation_approach(box,capsule,*floor,scale);
        if(!approach||!current_walking_mesh(sweep.pawn,system,mesh))continue;
        route.points.back()=approach->point;
        return InteractionRoute{std::move(route),approach->arrival_metres};
    }
    return {};
}
std::optional<InteractionRoute> wind_lift_landing_route(const CapsuleSweep& sweep,UObject* library,UObject* mesh,
        Vec3 start,const ActivationBox& target,double target_z,double scale,bool log_query) {
    auto* pawn=sweep.pawn;
    auto* world=actor_world(pawn);
    if(!world||!wind_shrine_world(pawn))return {};
    auto* movement=object(pawn,L"CharacterMovement");
    auto* base_getter=movement?movement->GetFunctionByNameInChain(L"GetMovementBase"):nullptr;
    if(!base_getter||path(base_getter)!=L"/Script/Engine.CharacterMovementComponent:GetMovementBase")return {};
    auto* base=get_object(movement,L"GetMovementBase");
    auto* lift=is(base,L"/Script/Engine.PrimitiveComponent")?get_object(base,L"GetOwner"):nullptr;
    if(!lift||actor_world(lift)!=world||!is(lift,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact"))return {};
    const auto id=read<int>(lift,L"m_UniqueId");
    const auto location=getter<Vec3>(lift,L"K2_GetActorLocation");
    if(!id||!location)return {};
    // Surveyed points just off the elevator docks. They are only proposals:
    // require a continuous capsule sweep to the native mesh and a complete
    // route from there. This never connects across a shaft or to another floor.
    Vec3 dock;
    std::array<Vec3,3> proposals;
    switch(*id) {
    case 68:dock={9115,40200,1196};proposals={{{9600,40200,1203},{9730,40200,1203},{9800,40300,1203}}};break;
    case 69:dock={9486,39291,3192};proposals={{{9930,39730,3203},{10048,39856,3203},{9877,39677,3203}}};break;
    case 72:dock={13600,37000,1206};proposals={{{13080,37000,1212},{12810,37000,1220},{12710,37000,1220}}};break;
    case 70:dock={13683,40110,2392};proposals={{{13720,40620,2410},{13720,40900,2430},{13720,41100,2430}}};break;
    case 74:dock={14024,43404,4802};proposals={{{14024,42860,4803},{14024,42650,4803},{14024,42500,4803}}};break;
    default:return {};
    }
    if(std::hypot(dock.x-location->x,dock.y-location->y)>80||std::abs(dock.z-location->z)>45)return {};
    std::string diagnostic;
    for(const auto proposal:proposals) {
        const auto nav=direct_nav::project_point(mesh,proposal);
        if(!nav||!nav->polygon||std::hypot(nav->position.x-proposal.x,nav->position.y-proposal.y)>scale*.65){diagnostic+='M';continue;}
        const auto join=nav->position;
        const auto run=std::hypot(join.x-start.x,join.y-start.y);
        if(run>scale*11.5||run<scale*.1||std::abs(join.z-start.z)>sweep.step_height){diagnostic+='D';continue;}
        const auto ground=sweep.ground_probe();
        auto physical=checked_walking_leg(start,join,scale,sweep.floor_z,ground);
        std::optional<Vec3> pivot;
        if(!physical) {
            // Riders keep their offset when the lift moves. At the lower
            // landing that can put a crystal between them and the opening.
            // Try the dock center first, with a separate sweep for each leg.
            const Vec3 center{dock.x,dock.y,start.z};
            const auto toward_center=std::hypot(center.x-start.x,center.y-start.y);
            if(toward_center>=scale*.1&&toward_center<=scale*5&&
               toward_center+std::hypot(join.x-center.x,join.y-center.y)<=scale*12.) {
                pivot=checked_walking_leg(start,center,scale,sweep.floor_z,ground);
                if(pivot)physical=checked_walking_leg(*pivot,join,scale,sweep.floor_z,ground);
            }
        }
        if(!physical){diagnostic+='C';continue;}
        if(!capsule_floor_on_mesh(sweep,library,mesh,*physical,scale)){diagnostic+='M';continue;}
        auto onward=interaction_route_on_mesh(sweep,library,mesh,*physical,target,target_z,scale,true);
        if(!onward||onward->path.points.size()<2){diagnostic+='P';continue;}
        const auto next=onward->path.points.front();
        if(std::hypot(next.x-physical->x,next.y-physical->y)>scale*.05||std::abs(next.z-physical->z)>sweep.step_height){diagnostic+='R';continue;}
        if(pivot)onward->path.points.insert(onward->path.points.begin(),*pivot);
        onward->path.points.insert(onward->path.points.begin(),start);
        onward->lift_prefix=pivot?2:1;
        return onward;
    }
    if(log_query) {
        static std::string previous;
        auto current="NAV lift landing rejected id="+std::to_string(*id)+" checks="+diagnostic;
        if(current!=previous){previous=current;log(current);}
    }
    return {};
}
std::optional<NativeRoute> capsule_route(UObject* pawn,UObject* target,UObject* system,UObject* mesh,
        Vec3 start,Vec3 goal,WalkingAxes axes,double scale,const std::optional<ActivationBox>& activation,
        bool ground_check,std::uint64_t path_id,const std::optional<ActivationBox>& room={},bool direct_only=false) {
    const auto interaction=activation?std::nullopt:capture_interaction_box(target,pawn);
    const auto& box=activation?activation:interaction;
    if(room) {
        // A doorway can overlap the edge of this room. Distant actors must
        // never be candidates, even if their sublevels share the same world.
        const auto edge=box?closest_activation_point(*room,box->center):std::nullopt;
        if(!edge||std::hypot(edge->x-box->center.x,edge->y-box->center.y,edge->z-box->center.z)>
            std::max(box->half_extent.x,box->half_extent.y)+scale)return {};
    }
    const auto sweep=capture_capsule_sweep(pawn);
    if(!sweep)return {};
    const WalkingCapsule capsule{sweep->radius,sweep->half_height};
    auto room_bounds=room;
    if(room_bounds&&activation) {
        // The authored room bounds stop just inside a doorway. Include its
        // threshold in the collision checks, while keeping the same floor.
        room_bounds->half_extent.x+=scale;
        room_bounds->half_extent.y+=scale;
    }
    const bool person=is(target,L"/Script/mucha.CPP_MapUnit_NPC");
    if(!interaction&&(person||requires_checked_interaction_box(target)))return {};
    struct Junction {
        Vec3 position;
        bool goal{};
        bool visited{};
        double goal_cost{};
        double distance{std::numeric_limits<double>::infinity()};
        std::vector<Vec3> path;
        std::optional<double> arrival;
    };
    std::vector<Junction> graph;
    graph.push_back({.position=start,.distance=0,.path={start}});
    if(room&&!direct_only&&zoldaad_capital_street(pawn)) {
        // The street has no baked polygons. Use surveyed junctions around
        // the station clock and behind the hotel, with live capsule proof.
        constexpr Vec3 street[]{{-350,100,2.15},{70,-470,2.15},{496,774,2.15},
            {734,827,2.15},{946,-343,2.15},{2290,-333,2.15},{2333,-51,2.15},
            {2175,-1573,2.15},{2795,-1669,2.15},{3394,-1689,2.15},{3653,-1260,2.15},
            {3039,-2436,2.15},{4121,-2574,2.15}};
        for(const auto point:street)if(inside_box(*room,point))graph.push_back({.position=point});
    } else if(room&&!direct_only&&mobliz_world(pawn)) {
        // Taken from the copied-game walk around the railings, stair approach
        // and upper passage. Every edge still needs its own live capsule and
        // walkable floor proof; there are no forced jumps or floor changes.
        for(const auto point:std::array<Vec3,11>{{{6150,13000,800},{5600,13050,800},
            {3815,13050,800},{3815,15500,1600},{3815,16200,1600},{2400,16200,1600},
            {1200,16100,1600},{1150,15200,1600},{1000,13900,1600},{540,13650,1600},{550,13200,1600}}})
            if(inside_box(*room,point))graph.push_back({.position=point});
        if(start.z>1500&&start.z<1800&&std::min(start.x,goal.x)<650&&std::max(start.y,goal.y)>14900) {
            // The upper fence opening and the aisle between the equipment
            // cabinets. The far levers cannot be approached straight from
            // the memo. Try both sides of the aisle against live collision.
            constexpr Vec3 aisle[]{{630,15868,1600},{195,15868,1600},{-330,16000,1600},
                {-445,15848,1600},{-445,15464,1600},{-1085,15465,1600},
                {-470,15840,1600},{-470,15540,1600},{-470,15410,1600},
                {-800,15410,1600},{-1130,15410,1600},{-1220,15410,1600},
                {-1210,15630,1600},{-1210,15795,1600},{-1270,15890,1600}};
            for(const auto point:aisle)
                if(inside_box(*room,point))graph.push_back({.position=point});
        }
        const auto id=read<int>(target,L"m_UniqueId");
        if(id&&(*id==73||*id==14)&&start.z>1500&&start.z<1800) {
            // After restoring power, the way back crosses the broken fence
            // and follows the crate side of the upper deck to the lift dock.
            constexpr Vec3 landing[]{{985,15865,1600},{985,14290,1600},{480,14240,1600},
                {465,13620,1600},{430,12850,1600},{405,12405,1600},
                {385,12190,1600},{440,12020,1600},{665,12000,1600},{865,12000,1630}};
            for(const auto point:landing)
                if(inside_box(*room,point))graph.push_back({.position=point});
        }
        if(id&&*id==4&&start.z>-120&&start.z<200&&
           is(target,L"/Script/mucha.CPP_MapTransitionTrigger")) {
            // Riders keep their offset on the restored lift. At its lower
            // dock they must line up with the aisle before stepping off.
            constexpr Vec3 landing[]{{1400,12000,12},{1200,12000,12},{940,12000,3},
                {685,12000,2},{365,12000,2},{-450,12000,2},{-990,12000,2},{-1990,12000,2}};
            for(const auto point:landing)
                if(inside_box(*room,point))graph.push_back({.position=point});
        }
    } else if(room&&!direct_only&&wind_shrine_world(pawn)) {
        constexpr Vec3 junctions[]{ {13600,37000,1217},{13080,37000,1217},
            {12300,37000,1264},{11120,37140,1600},{10800,37750,1600},{10585,38200,1600},
            {10635,38592,1600},{11000,38592,1600},{14024,42760,2808},{14024,43070,2808},
            {14024,43380,4812},{14024,42860,4803},{14024,41600,4803},{14024,40200,4803},
            {14500,40200,4870},{15750,40200,5240},{17112,40200,5613},{17350,40200,5613},
            {13683,40110,2403},{13720,40620,2400},{13640,41300,2250},{12800,41440,2250},
            {12550,42550,2250},{12200,42980,2250},{11350,43010,2250},{9750,43010,2250},
            {7950,42890,1950},{7950,41820,1992},{8000,41480,1992},
            {9115,40200,1207},{9690,40200,1206},{10090,40200,1206},
            {10400,40200,5214},{10400,41380,5100},{11730,41390,5100},{13100,41390,5100},
            {13930,41400,4803},{14024,42760,4803},{14024,43070,4803} };
        for(const auto point:junctions)
            if(inside_box(*room,point))graph.push_back({.position=point});
    } else if(room&&!direct_only&&waterfall_first_passage(target)) {
        // The entry walkway bends around the water before the passage.
        graph.push_back({.position={2340,5480,2.15}});
        graph.push_back({.position={2340,6310,2.15}});
    } else if(room&&!direct_only&&passage_map(pawn)==PassageMap::Mitra) {
        // Junctions around the counter and innkeeper; the live collision
        // query decides which of these are usable from this position.
        for(const auto point:std::array<Vec3,7>{{{-1240,13350,start.z},{-1010,13260,start.z},
            {-995,13520,start.z},{-740,13530,start.z},{-595,13510,start.z},{-615,13360,start.z},{-555,13350,start.z}}})
            graph.push_back({.position=point});
    } else if(!room&&earth_shrine_exterior(actor_world(pawn))) {
        for(const auto point:std::array<Vec3,6>{{{-4480,-20,-198},{-4300,0,-198},{-3925,490,-198},
            {-4000,600,-198},{-3925,805,-198},{-3925,-490,-198}}})
            graph.push_back({.position=point});
    }
    const auto add_goal=[&](Vec3 point,double cost=0.){graph.push_back({.position=point,.goal=true,.goal_cost=cost});};
    if(interaction) {
        // An NPC's capsule can occupy almost the entire width of the talk
        // box. Its corners leave room to overlap without touching the body.
        if(person)for(const auto point:box_corner_approaches(*interaction,capsule,goal.z))add_goal(point);
        for(const auto gap:{.8,.92})
            for(const auto point:interaction_side_approaches(*interaction,capsule,goal.z,gap))
                add_goal(point,gap>.9?scale*.5:0.);
        if(room)for(const auto point:interaction_side_approaches(*interaction,capsule,start.z,.55,.88))
            add_goal(point);
        if(person)add_goal({interaction->center.x,interaction->center.y,goal.z},scale);
        if(room&&is(target,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact"))
            add_goal({interaction->center.x,interaction->center.y,goal.z},scale*.25);
    } else {
        if(activation) {
            // A standing capsule can activate a transition before its
            // centre reaches the box face. A door may block the face itself.
            const auto run=std::hypot(goal.x-start.x,goal.y-start.y);
            if(run>0) {
                const auto back=std::min(.55*capsule.radius,run*.5)/run;
                add_goal({goal.x+(start.x-goal.x)*back,goal.y+(start.y-goal.y)*back,goal.z});
            }
            const auto angle=activation->yaw_degrees*3.14159265358979323846/180.;
            const auto c=std::cos(angle),s=std::sin(angle);
            for(const auto side:std::array<Vec3,4>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}}}) {
                const auto x=side.x*(activation->half_extent.x+.55*capsule.radius);
                const auto y=side.y*(activation->half_extent.y+.55*capsule.radius);
                add_goal({activation->center.x+x*c-y*s,activation->center.y+x*s+y*c,goal.z});
            }
            if(room)for(const auto point:interaction_side_approaches(*activation,capsule,start.z,.85))add_goal(point);
        }
        add_goal(goal);
    }
    // A collision-only fallback may try several nearby faces of a large box.
    // Share a probe budget across those attempts on the game thread.
    std::size_t remaining_probes=640;
    const auto proof=[&](Vec3 from,Vec3 to) {
        return room_bounds?room_lane(*sweep,*room_bounds,from,to,scale,direct_only?&remaining_probes:nullptr):
            capsule_lane(*sweep,system,mesh,from,to,scale);
    };
    const auto result=[&](Junction& end)->std::optional<NativeRoute> {
        if(end.path.size()<2||(!room&&!current_walking_mesh(pawn,system,mesh)))return {};
        PathSnapshot route{std::move(end.path),axes.right,axes.up,scale,true,false};
        if(!describe_route(route))return {};
        std::optional<GroundCoverage> ground;
        if(ground_check) {
            const RouteLocation last{route.points.size()-1,1};
            const auto metres=route_arc_metres(route,last);
            if(!metres)return {};
            ground=GroundCoverage{.path_id=path_id,.end=last,.end_point=route.points.back(),
                .metres=*metres,.reaches_end=true,.follow_surface=true};
        }
        return NativeRoute{.snapshot=std::move(route),.ground=ground,.activation_arrival_metres=end.arrival,
            .capsule_mesh=room?std::wstring{}:path(mesh),.capsule_room=room_bounds};
    };
    if(room&&box) {
        // Most room routes have a direct approach. Prove the closest of the
        // overlapping candidates first, without sweeping an entire graph.
        auto first=std::find_if(graph.begin(),graph.end(),[](const Junction& n){return n.goal;});
        std::stable_sort(first,graph.end(),[&](const Junction& a,const Junction& b){
            const auto cost=[&](const Junction& n){return std::hypot(n.position.x-start.x,n.position.y-start.y)+n.goal_cost;};
            return cost(a)<cost(b);
        });
        for(auto it=first;it!=graph.end()&&(!direct_only||remaining_probes);++it) {
            const auto floor=proof(start,it->position);
            const auto approach=floor?activation_approach(*box,capsule,*floor,scale):std::nullopt;
            if(approach&&std::hypot(floor->x-start.x,floor->y-start.y)>=scale*.01) {
                it->path={start,*floor};it->arrival=approach->arrival_metres;
                return result(*it);
            }
        }
        if(direct_only)return {};
    }
    while(true) {
        auto best=graph.end();
        for(auto it=graph.begin();it!=graph.end();++it)
            if(!it->visited&&std::isfinite(it->distance)&&(best==graph.end()||it->distance<best->distance))best=it;
        if(best==graph.end())return {};
        if(best->goal)return result(*best);
        best->visited=true;
        for(auto& next:graph) {
            if(next.visited)continue;
            const auto from=best->path.back();
            const auto length=std::hypot(next.position.x-from.x,next.position.y-from.y);
            const auto distance=best->distance+length+next.goal_cost;
            if(length<scale*.01||distance>=next.distance)continue;
            if(room&&best==graph.begin()&&next.goal)continue; // Already checked the direct approaches.
            const auto floor=proof(from,next.position);
            if(!floor)continue;
            if(next.goal&&box) {
                const auto approach=activation_approach(*box,capsule,*floor,scale);
                if(!approach)continue;
                next.arrival=approach->arrival_metres;
            }
            next.path=best->path;next.path.push_back(*floor);next.distance=distance;
        }
    }
}
std::optional<NativeRoute> verified_walking_turns(const CapsuleSweep& sweep,const ActivationBox& walkway,
        Vec3 start,std::span<const Vec3> turns,WalkingAxes axes,double scale,bool ground_check,
        std::uint64_t path_id,double arrival,std::size_t probes=1600,std::string_view diagnostic={},bool repair_corner=false) {
    if(turns.empty()||!inside_box(walkway,start))return {};
    const auto end=turns.back();
    if(std::hypot(start.x-end.x,start.y-end.y)<=scale*arrival&&std::abs(start.z-end.z)<scale*.15)
        return NativeRoute{.activation_reached=true};
    constexpr std::array offsets{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0},
        Vec3{.707,.707,0},Vec3{.707,-.707,0},Vec3{-.707,.707,0},Vec3{-.707,-.707,0}};
    for(std::size_t index=turns.size();index>0&&probes;--index) {
        const auto first=index-1;
        if(index>1&&std::hypot(start.x-turns[first].x,start.y-turns[first].y)>scale*8.)continue;
        std::vector<Vec3> points{start};
        std::size_t previous_leg_start=0;
        std::size_t i=first;
        for(;i<turns.size();++i) {
            std::string detail;
            std::vector<Vec3> lane_turns;
            auto floor=room_lane(sweep,walkway,points.back(),turns[i],scale,&probes,diagnostic.empty()?nullptr:&detail,repair_corner?&lane_turns:nullptr);
            if(repair_corner&&i>first&&!floor) {
                // The inbound leg can end alongside a wall the next leg
                // crosses. Keep an adjusted pivot only if both sweeps pass.
                const auto pivot=points.back();
                for(const double radius:{.08,.16,.24}) {
                    for(const auto offset:offsets) {
                        const Vec3 candidate{pivot.x+scale*radius*offset.x,pivot.y+scale*radius*offset.y,pivot.z};
                        const auto supported=room_lane(sweep,walkway,points[previous_leg_start],candidate,scale,&probes);
                        if(!supported||std::abs(supported->z-pivot.z)>scale*.12)continue;
                        const auto next=room_lane(sweep,walkway,*supported,turns[i],scale,&probes);
                        if(!next||std::abs(next->z-turns[i].z)>scale*.20)continue;
                        points.resize(previous_leg_start+1);
                        points.push_back(*supported);
                        floor=next;
                        lane_turns.clear();
                        break;
                    }
                    if(floor)break;
                }
            }
            if(repair_corner&&i+1<turns.size()&&(!floor||std::abs(floor->z-turns[i].z)>scale*.20)) {
                // A sampled corner can sit on the edge of a railing. Look
                // for nearby footing with the same height and a clear sweep.
                for(const double radius:{.08,.16,.24}) {
                    for(const auto offset:offsets) {
                        const auto& corner=turns[i];
                        const Vec3 candidate{corner.x+scale*radius*offset.x,corner.y+scale*radius*offset.y,corner.z};
                        const auto supported=room_lane(sweep,walkway,points.back(),candidate,scale,&probes);
                        if(supported&&std::abs(supported->z-corner.z)<=scale*.20) {
                            floor=supported;
                            lane_turns.clear();
                            break;
                        }
                    }
                    if(floor&&std::abs(floor->z-turns[i].z)<=scale*.20)break;
                }
            }
            if(!floor||std::abs(floor->z-turns[i].z)>scale*.20) {
                if(!diagnostic.empty())log("NAV "+std::string(diagnostic)+" rejected lane from="+coordinates(points.back())+
                    " to="+coordinates(turns[i])+" support="+(floor?coordinates(*floor):"unavailable")+" "+detail);
                break;
            }
            previous_leg_start=points.size()-1;
            points.insert(points.end(),lane_turns.begin(),lane_turns.end());
            points.push_back(*floor);
        }
        if(i!=turns.size())continue;
        PathSnapshot route{std::move(points),axes.right,axes.up,scale,true,false};
        std::optional<GroundCoverage> ground;
        if(ground_check) {
            const RouteLocation last{route.points.size()-1,1};
            const auto metres=route_arc_metres(route,last);
            if(!metres)return {};
            ground=GroundCoverage{.path_id=path_id,.end=last,.end_point=route.points.back(),.metres=*metres,
                .reaches_end=true,.follow_surface=true};
        }
        return NativeRoute{.snapshot=std::move(route),.ground=ground,.activation_arrival_metres=arrival,
            .capsule_room=walkway};
    }
    return {};
}
std::optional<NativeRoute> olderion_tree_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    constexpr ActivationBox plaza{{6140,-425,400},{2360,845,445},0};
    if(!inside_box(plaza,start))return {};
    constexpr std::array turns{
        Vec3{3900,-850,82.15},Vec3{3900,-1030,122.15},Vec3{4420,-1030,122.15},
        Vec3{4640,-725,122.15},Vec3{4770,-430,122.15},Vec3{4900,0,122.15},
        Vec3{5200,0,122.15},Vec3{5260,-175,122.15},Vec3{6540,-175,722.15},
        Vec3{6650,0,722.15},Vec3{7130,-365,722.15},Vec3{8145,-527,722.15},Vec3{8195,-527,722.15}
    };
    const auto box=capture_interaction_box(target,pawn);
    const auto sweep=box?capture_capsule_sweep(pawn):std::nullopt;
    if(!sweep)return {};
    const auto approach=activation_approach(*box,{sweep->radius,sweep->half_height},turns.back(),scale);
    if(!approach)return {};
    return verified_walking_turns(*sweep,plaza,start,turns,axes,scale,ground_check,path_id,approach->arrival_metres);
}
std::optional<NativeRoute> verified_trigger_turns(UObject* pawn,UObject* target,Vec3 start,
        const ActivationBox& bounds,std::span<const Vec3> turns,WalkingAxes axes,double scale,
        bool ground_check,std::uint64_t path_id,std::size_t probes=1600,std::string_view diagnostic={},bool repair_corner=false) {
    if(!inside_box(bounds,start)||turns.empty())return {};
    const auto box=capture_activation_box(target);
    const auto sweep=box?capture_capsule_sweep(pawn):std::nullopt;
    if(!sweep) {
        if(!diagnostic.empty())log("NAV "+std::string(diagnostic)+" missing activation or player capsule");
        return {};
    }
    const WalkingCapsule capsule{sweep->radius,sweep->half_height};
    const auto at=activation_clearance(*box,capsule,start);
    if(at&&*at>0)return NativeRoute{.activation_reached=true};
    const auto approach=activation_approach(*box,capsule,turns.back(),scale);
    if(!approach) {
        if(!diagnostic.empty())log("NAV "+std::string(diagnostic)+" endpoint outside activation center="+
            coordinates(box->center)+" extent="+coordinates(box->half_extent)+" goal="+coordinates(turns.back()));
        return {};
    }
    return verified_walking_turns(*sweep,bounds,start,turns,axes,scale,ground_check,path_id,approach->arrival_metres,
        probes,diagnostic,repair_corner);
}
std::optional<NativeRoute> mozul_station_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    if(!mozul_world_entrance(target))return {};
    // Stop inside the interaction volume on the open side of the entrance.
    // The baked target is farther in against the structure.
    constexpr ActivationBox approach{{-3720,14382,266},{230,120,60},0};
    constexpr std::array turns{Vec3{-3725,14384,265.65},Vec3{-3758.5,14384,265.65}};
    return verified_trigger_turns(pawn,target,start,approach,turns,axes,scale,ground_check,path_id);
}
std::optional<NativeRoute> earth_shrine_door_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    // The bake can lead diagonally into the door's wall. Sweep the centre of
    // the forecourt to a point that overlaps the live trigger before the wall.
    constexpr ActivationBox forecourt{{-3880,0,-200},{650,1240,130},0};
    constexpr std::array turns{Vec3{-3760,0,-197.85},Vec3{-3282,0,-197.85}};
    return verified_trigger_turns(pawn,target,start,forecourt,turns,axes,scale,ground_check,path_id,640);
}
std::optional<NativeRoute> olderion_south_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    constexpr ActivationBox road{{3240,-3420,420},{5000,3710,450},0};
    // Keep clear of the upper street residents, use the stair centerline and
    // cross the center of the bridge before turning around the lower plaza.
    constexpr std::array turns{
        Vec3{7785,-754,722.15},Vec3{7710,-650,722.15},Vec3{7450,-470,722.15},
        Vec3{7280,-450,722.15},Vec3{7130,-365,722.15},Vec3{6650,0,722.15},
        Vec3{6540,-175,722.15},Vec3{5260,-175,122.15},Vec3{5200,0,122.15},
        Vec3{4900,0,122.15},Vec3{4770,-430,122.15},Vec3{4640,-725,122.15},
        Vec3{4420,-1030,122.15},Vec3{3900,-1030,122.15},
        Vec3{2990,-790,122.15},Vec3{2498,-1282,457.15},Vec3{1869,-1911,457.15},
        Vec3{1395,-2385,122.24},Vec3{1010,-2220,122.15},Vec3{774,-2107,122.15},
        Vec3{580,-2010,122.15},Vec3{390,-1910,122.15},Vec3{310,-1696,122.15},
        Vec3{-260,-700,147.15},Vec3{-690,-240,147.15},Vec3{-730,-220,147.15},
        Vec3{-1600,10,52.15},Vec3{-1660,10,52.15}
    };
    return verified_trigger_turns(pawn,target,start,road,turns,axes,scale,ground_check,path_id,2400);
}
std::optional<direct_nav::Path> join_verified_walking_prefix(UObject* pawn,UObject* system,UObject* mesh,
        std::vector<Vec3> points,Vec3 goal,double scale) {
    if(points.size()<2||!current_walking_mesh(pawn,system,mesh))return {};
    auto onward=direct_nav::find_path(direct_navigation_api(),pawn,system,mesh,points.back(),goal);
    if(!onward.valid||onward.points.empty()||
       std::hypot(onward.points.front().x-points.back().x,onward.points.front().y-points.back().y)>scale*.05)return {};
    points.insert(points.end(),onward.points.begin()+1,onward.points.end());
    onward.points=std::move(points);
    onward.offset_points.clear();
    return onward;
}
std::optional<NativeRoute> dungeon_gate_crossing(UObject* pawn,UObject* target,UObject* library,UObject* mesh,
        Vec3 start,WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    constexpr ActivationBox passage{{1535,-47055,2},{440,170,100},0};
    constexpr Vec3 landing{1847,-47055.25,2.15},exit{4000,-47520,74.04};
    if(start.x>=landing.x-scale*.05||!inside_box(passage,start))return {};
    // Opening the gate updates collision, while the two baked surfaces stay
    // separate. Verify the short crossing to a point on the onward surface.
    const auto sweep=capture_capsule_sweep(pawn);
    const auto box=sweep?capture_activation_box(target):std::nullopt;
    if(!box)return {};
    const auto floor=room_lane(*sweep,passage,start,landing,scale);
    const auto goal=floor?project_navigation(pawn,library,mesh,exit,{scale*.04,scale*.04,scale*.22}):std::nullopt;
    if(!goal||!goal->on_mesh)return {};
    auto native=join_verified_walking_prefix(pawn,library,mesh,{start,*floor},goal->position,scale);
    if(!native)return {};
    const auto approach=activation_approach(*box,{sweep->radius,sweep->half_height},native->points.back(),scale);
    if(!approach)return {};
    PathSnapshot snapshot{std::move(native->points),axes.right,axes.up,scale,true,false};
    if(!describe_route(snapshot))return {};
    std::optional<GroundCoverage> ground;
    std::string reason;
    std::size_t rejoin{};
    if(ground_check) {
        if(const auto policy=capture_ground_policy(pawn,scale))
            ground=ordinary_ground_path(pawn,library,mesh,snapshot,path_id,policy->budget,reason,rejoin,
                GroundProof::CapsulePrefix,1);
        else reason="The character's walking and braking state could not be read.";
    }
    return NativeRoute{.snapshot=std::move(snapshot),.ground=ground,.ground_reason=std::move(reason),
        .activation_arrival_metres=approach->arrival_metres,.swept_mesh=ground&&ground->swept_ground?path(mesh):std::wstring{}};
}
std::optional<direct_nav::Path> surveyed_walking_turn(UObject* pawn,UObject* target,UObject* system,UObject* mesh,
        Vec3 start,Vec3 goal,double scale,std::size_t& prefix_legs,std::optional<double>* arrival=nullptr) {
    const auto name=path(target);
    const auto id=read<int>(target,L"m_UniqueId");
    const bool reizan=id==5&&name==L"/Game/Map/Dng/09Pha/09Pha_40/EV/Dng_09Pha_40_01_EV.Dng_09Pha_40_01_EV:"
        L"PersistentLevel.BP_MapGimmick_EventTrigger_C_1";
    const bool thunderclap=id==9&&name==L"/Game/Map/Dng/04Dir/04Dir_40/EV/Dng_04Dir_40_01_EV.Dng_04Dir_40_01_EV:"
        L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_3";
    const bool tower_chamber=tower_chamber_return(target);
    const bool lower_tower_portal=id==2&&name==L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_4";
    const bool middle_reach=id==12&&name==L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_17";
    const bool enterprise_helm=id==1&&name==L"/Game/Map/Town/00Com/00Com_01/GD/Town_00Com_01_GD.Town_00Com_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_2";
    if(enterprise_helm) {
        // The bake can lead into the port railing from the aft deck. Cross
        // the flat deck at the player's X before joining the native approach.
        constexpr ActivationBox deck{{1200.,0.,16.},{100.,270.,45.},0};
        if(start.y>=180.||!inside_box(deck,start))return {};
        const auto sweep=capture_capsule_sweep(pawn);
        const auto cross=sweep?room_lane(*sweep,deck,start,{start.x,200.,start.z},scale):std::nullopt;
        auto route=cross?join_verified_walking_prefix(pawn,system,mesh,{start,*cross},goal,scale):std::nullopt;
        if(route)prefix_legs=1;
        return route;
    }
    if(!reizan&&!thunderclap&&!tower_chamber&&!middle_reach&&!lower_tower_portal)return {};
    const auto interaction=thunderclap&&arrival?capture_interaction_box(target,pawn):std::nullopt;
    if(thunderclap&&!interaction)return {};
    constexpr ActivationBox lower{{-9285.,8820.,1445.},{335.,610.,180.},0};
    constexpr ActivationBox middle{{-7270.,8310.,3040.},{260.,230.,190.},0};
    constexpr ActivationBox upper{{-6880.,10545.,4010.},{345.,330.,175.},0};
    constexpr std::array low_turn{Vec3{-9241.8,9004.1,1502.15},Vec3{-9241.8,9165.9,1502.15},
        Vec3{-9203.,9225.7,1502.15},Vec3{-9031.,9268.5,1502.15}};
    constexpr std::array mid_turn{Vec3{-7369.,8196.,3002.15},Vec3{-7240.,8196.,3002.15},
        Vec3{-7110.,8215.,3002.15},Vec3{-7108.5,8460.,3079.}};
    constexpr std::array high_turn{Vec3{-7098.8,10660.9,4052.15},Vec3{-6907.,10675.9,4052.15},
        Vec3{-6708.6,10675.9,4052.15},Vec3{-6685.,10300.4,4052.15}};
    // The Thunderclap bake cuts the wall while descending. Keep the
    // capsule on the ramp until the level section opens to the west.
    constexpr ActivationBox descent{{4548.,1645.,655.},{200.,560.,290.},0};
    constexpr std::array ramp_turn{Vec3{4560.,1420.,698.8},Vec3{4560.,1810.,503.8},
        Vec3{4560.,2104.,402.15},Vec3{4485.,2155.,402.15}};
    // Portal 7's bake cuts the lower chamber ramp's inside wall.
    constexpr ActivationBox chamber_turn{{-1390.,-90810.,947.},{490.,240.,120.},0};
    constexpr std::array chamber_ramp{Vec3{-1440.7,-90735.5,881.96},Vec3{-1579.6,-90735.5,895.85},
        Vec3{-1696.,-90896.,956.15}};
    // Both meshes contain the centre of the rising stair strip. Join away
    // from the outer rail and its adjacent lower-floor polygons.
    constexpr ActivationBox middle_approach{{540.,-297550.,2710.},{540.,370.,375.},0.};
    constexpr std::array middle_stairway{Vec3{205.,-297650.,2482.15},Vec3{420.,-297650.,2590.},
        Vec3{950.,-297650.,2942.}};
    // The native route can stall where it crosses the lip of the lower
    // portal walkway. Use the path walked with the controller.
    constexpr ActivationBox portal_walkway{{9150.,16525.,510.},{330.,80.,65.},0.};
    constexpr std::array portal_turn{Vec3{9408.,16525.,502.15}};
    const ActivationBox* lane=nullptr;
    std::span<const Vec3> turns;
    if(tower_chamber&&start.x>-1696.&&inside_box(chamber_turn,start)){lane=&chamber_turn;turns=chamber_ramp;}
    else if(middle_reach&&start.x<920.&&inside_box(middle_approach,start)){lane=&middle_approach;turns=middle_stairway;}
    else if(lower_tower_portal&&start.x<9360.&&inside_box(portal_walkway,start)){lane=&portal_walkway;turns=portal_turn;}
    else if(thunderclap&&start.y<2110.&&inside_box(descent,start)){lane=&descent;turns=ramp_turn;}
    else if(reizan&&inside_box(lower,start)&&start.x<-9095.){lane=&lower;turns=low_turn;}
    else if(reizan&&inside_box(middle,start)&&start.y<8415.){lane=&middle;turns=mid_turn;}
    else if(reizan&&inside_box(upper,start)&&(start.x<-6810.||start.y>10540.)){lane=&upper;turns=high_turn;}
    if(!lane)return {};
    const auto sweep=capture_capsule_sweep(pawn);
    if(!sweep)return {};
    // Join the native route after the inside corner, using the slope and
    // railing clearance from the same live capsule. On the chamber
    // ramp, retain the entrance pivot until the party has passed the wall.
    const std::size_t last_join=tower_chamber&&start.x>chamber_ramp[1].x+8.?
        chamber_ramp.size()-1:turns.size();
    for(std::size_t first=last_join;first>0;--first) {
        std::vector<Vec3> prefix{start};
        for(const auto next:turns.subspan(first-1)) {
            const auto floor=room_lane(*sweep,*lane,prefix.back(),next,scale);
            if(!floor||std::abs(floor->z-next.z)>scale*.22)break;
            prefix.push_back(*floor);
        }
        const auto legs=prefix.size()-1;
        if(legs!=turns.size()-first+1)continue;
        if(interaction) {
            // Keep the interaction solver's swept approach at the far end.
            auto onward=interaction_route_on_mesh(*sweep,system,mesh,prefix.back(),*interaction,goal.z,scale);
            if(!onward||onward->path.points.empty()||
               std::hypot(onward->path.points.front().x-prefix.back().x,
                          onward->path.points.front().y-prefix.back().y)>scale*.05)continue;
            prefix.insert(prefix.end(),onward->path.points.begin()+1,onward->path.points.end());
            onward->path.points=std::move(prefix);
            onward->path.offset_points.clear();
            *arrival=onward->arrival_metres;
            prefix_legs=legs;
            return std::move(onward->path);
        }
        if(auto route=join_verified_walking_prefix(pawn,system,mesh,std::move(prefix),goal,scale)) {
            prefix_legs=legs;
            return route;
        }
    }
    return {};
}

std::optional<direct_nav::Path> cave_world_switchback(UObject* pawn,UObject* target,UObject* system,UObject* mesh,
        Vec3 start,Vec3 goal,double scale) {
    constexpr ActivationBox road{{660,5360,120},{225,220,70},0};
    if(!inside_box(road,start))return {};
    const bool toward_cave=cave_world_south_entrance(target)&&start.x<737.&&start.y>=5180.;
    const bool toward_olderion=olderion_world_entrance(target)&&start.x>=600.&&start.y>5372.;
    if(!toward_cave&&!toward_olderion)return {};
    const auto sweep=capture_capsule_sweep(pawn);
    if(!sweep)return {};
    // The baked funnel cuts across the trees on the inside of this bend.
    constexpr Vec3 foot{605,5362,105.66},outer{605,5502,117.56},top{747,5502,129.74};
    std::vector<Vec3> points{start};
    const auto append=[&](Vec3 turn) {
        if(std::hypot(turn.x-points.back().x,turn.y-points.back().y)<scale*.06)return true;
        const auto floor=room_lane(*sweep,road,points.back(),turn,scale);
        if(!floor)return false;
        points.push_back(*floor);
        return true;
    };
    if(toward_cave) {
        if(start.y<5350.&&!append(foot))return {};
        if(start.y<5494.&&!append(outer))return {};
        if(!append(top))return {};
    } else {
        if(start.x>740.&&start.y<5494.&&!append(top))return {};
        if(start.x>615.&&!append(outer))return {};
        if(!append(foot))return {};
    }
    return join_verified_walking_prefix(pawn,system,mesh,std::move(points),goal,scale);
}
std::optional<direct_nav::Path> factory_desert_bend(UObject* pawn,UObject* target,UObject* system,UObject* mesh,
        Vec3 start,Vec3 goal,double scale) {
    if(path(target)!=L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_38"||
       read<int>(target,L"m_UniqueId")!=33)return {};
    constexpr ActivationBox road{{-10320,18015,690},{180,535,130},0};
    if(!inside_box(road,start)||start.y<17518.)return {};
    const auto sweep=capture_capsule_sweep(pawn);
    if(!sweep)return {};
    // Descend to the open side of the rock before following the native route.
    constexpr std::array turns{Vec3{-10334,18303,690.45},Vec3{-10382,18235,654.28},Vec3{-10382,17510,607.48}};
    std::vector<Vec3> points{start};
    for(const auto turn:turns) {
        if(start.y-turn.y<scale*.06)continue;
        const auto floor=room_lane(*sweep,road,points.back(),turn,scale);
        if(!floor)return {};
        points.push_back(*floor);
    }
    return join_verified_walking_prefix(pawn,system,mesh,std::move(points),goal,scale);
}
std::optional<NativeRoute> cave_lake_walking_route(UObject* pawn,UObject* target,Vec3 start,Vec3 goal,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    constexpr std::wstring_view level=
        L"/Game/Map/Dng/05Old/05Old_40/GD/Dng_05Old_40_01_GD.Dng_05Old_40_01_GD:PersistentLevel.";
    const auto name=path(target);
    if(!name.starts_with(level))return {};
    const auto actor=std::wstring_view(name).substr(level.size());
    const auto id=read<int>(target,L"m_UniqueId");
    constexpr ActivationBox corridor{{1540,-4500,-350},{1810,690,470},0};
    // Recast splits the bottom of the drained ramp. Follow the physical
    // center of the passage with the player's capsule and floor checks.
    constexpr Vec3 turn{-100,-4950,2.15};
    if(id==1&&actor==L"BP_MapTransitionTrigger_C_10") {
        constexpr std::array points{turn,Vec3{3200,-4950,-699.23}};
        return verified_trigger_turns(pawn,target,start,corridor,points,axes,scale,ground_check,path_id);
    }
    if(id==17&&actor==L"BP_MapTransitionTrigger_C_9") {
        constexpr std::array points{turn,Vec3{-100,-3820,2.15}};
        return verified_trigger_turns(pawn,target,start,corridor,points,axes,scale,ground_check,path_id);
    }
    constexpr ActivationBox lower_landing{{920,-9250,100},{400,650,140},0};
    constexpr Vec3 corner{1030,-9750,102.15},foot{1030,-8800,102.15};
    if(id==3&&actor==L"BP_MapTransitionTrigger_C_12") {
        constexpr std::array points{foot,corner,Vec3{620,-9750,102.15}};
        return verified_trigger_turns(pawn,target,start,lower_landing,points,axes,scale,ground_check,path_id);
    }
    if(id==1&&actor==L"BP_MapGimmick_Object_Ladderwood_C_0"&&std::abs(goal.z-102.15)<60.) {
        constexpr std::array points{corner,foot,Vec3{1180,-8800,102.15}};
        const auto sweep=capture_capsule_sweep(pawn);
        if(sweep)return verified_walking_turns(*sweep,lower_landing,start,points,axes,scale,ground_check,path_id,.08);
    }
    return {};
}
std::optional<NativeRoute> old_town_story_route(UObject* pawn,UObject* target,int event,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    // After the party splits, both live bakes can lose the lower streets.
    // These lanes stay on the dry floor and use the active event volume.
    constexpr double street_floor=-657.85;
    if(event==3) {
        if(start.y>-7000&&start.x>300)return {};
        constexpr ActivationBox street{{1200,-7250,street_floor},{1560,1875,110},0};
        constexpr std::array turns{Vec3{78,-8060,street_floor},Vec3{483,-8372,street_floor},
            Vec3{483,-8649,street_floor},Vec3{2625,-8649,street_floor}};
        return verified_trigger_turns(pawn,target,start,street,turns,axes,scale,ground_check,path_id);
    }
    if(event==2) {
        constexpr ActivationBox bridge{{4800,-8070,-625},{2150,820,160},0};
        constexpr std::array turns{Vec3{5159,-8665,-608.14},Vec3{5693,-8357,street_floor},
            Vec3{6720,-7484,street_floor}};
        return verified_trigger_turns(pawn,target,start,bridge,turns,axes,scale,ground_check,path_id);
    }
    constexpr ActivationBox square{{6100,-3560,street_floor},{1250,3860,120},0};
    constexpr std::array turns{Vec3{6635,-5205,street_floor},Vec3{6635,-3701,street_floor},
        Vec3{6439,-3380,street_floor},Vec3{5460,-2859,street_floor},Vec3{5221,-2525,street_floor},
        Vec3{5221,-990,street_floor},Vec3{5460,-990,street_floor}};
    return verified_trigger_turns(pawn,target,start,square,turns,axes,scale,ground_check,path_id);
}
std::optional<NativeRoute> verified_interaction_turns(UObject* pawn,UObject* target,Vec3 start,
        const ActivationBox& bounds,std::span<const Vec3> turns,WalkingAxes axes,double scale,
        bool ground_check,std::uint64_t path_id,std::string_view diagnostic={},bool repair_corner=false) {
    if(!inside_box(bounds,start)||turns.empty())return {};
    const auto box=capture_interaction_box(target,pawn);
    const auto sweep=box?capture_capsule_sweep(pawn):std::nullopt;
    if(!sweep)return {};
    const auto approach=activation_approach(*box,{sweep->radius,sweep->half_height},turns.back(),scale);
    if(!approach) {
        if(!diagnostic.empty())log("NAV "+std::string(diagnostic)+" endpoint outside interaction area: "+coordinates(turns.back()));
        return {};
    }
    return verified_walking_turns(*sweep,bounds,start,turns,axes,scale,ground_check,path_id,
        approach->arrival_metres,1600,diagnostic,repair_corner);
}
std::optional<NativeRoute> sian_ayaka_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id,bool diagnostic) {
    constexpr std::wstring_view courtyard=L"/Game/Map/Town/09Pha/09Pha_01/EV/Town_09Pha_01_01_EV."
        L"Town_09Pha_01_01_EV:PersistentLevel.";
    if(!is(target,L"/Script/mucha.CPP_MapUnit_NPC"))return {};
    const auto name=path(target);
    if(!name.starts_with(courtyard))return {};
    const auto actor=std::wstring_view(name).substr(courtyard.size());
    const auto id=read<int>(target,L"m_UniqueId");
    if(!(id==1&&actor==L"BP_MapUnit_NPC_C_1")&&!(id==3&&actor==L"BP_MapUnit_NPC_C_10"))return {};
    constexpr ActivationBox town{{-150.,-290.,220.},{2620.,2000.,270.},0};
    // The native path cuts the bridge rail and the corner of the second stairs.
    // Stay between the bridge rails, then line up with each stair landing.
    const std::array turns{Vec3{-2050.,0.,2.15},Vec3{-1440.13,-1646.78,2.15},
        Vec3{-1000.,-1650.,2.15},
        Vec3{-613.,-1660.,8.2},Vec3{-211.,-1667.,143.46},Vec3{53.,-1641.,54.53},
        Vec3{228.,-1670.,2.15},Vec3{417.,-1424.,2.15},Vec3{588.,-990.,2.15},
        Vec3{605.,-535.,202.15},Vec3{605.,-210.,202.15},Vec3{690.,-55.,202.15},
        Vec3{690.,0.,202.15},Vec3{1240.,0.,402.15},Vec3{1615.,-30.,402.15},
        id==1?Vec3{1910.,-125.,402.15}:Vec3{1744.,-229.,402.15}};
    return verified_interaction_turns(pawn,target,start,town,turns,axes,scale,ground_check,path_id,
        diagnostic?"Sian upper courtyard":"",true);
}

std::optional<NativeRoute> chapter_seven_walking_route(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id,bool diagnostic) {
    const auto name=path(target);
    const auto id=read<int>(target,L"m_UniqueId");
    const auto follow=[&](const ActivationBox& bounds,std::span<const Vec3> points,const char* label,std::size_t probes) {
        return verified_trigger_turns(pawn,target,start,bounds,points,axes,scale,ground_check,path_id,
            probes,diagnostic?label:"",true);
    };
    if(id==1&&name==L"/Game/Map/Town/09Pha/09Pha_01/GD/Town_09Pha_01_01_GD.Town_09Pha_01_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_2") {
        constexpr ActivationBox town{{-150.,-290.,220.},{2620.,2000.,270.},0};
        constexpr std::array points{Vec3{1240.,0.,402.15},Vec3{690.,0.,202.15},Vec3{690.,-55.,202.15},
            Vec3{605.,-210.,202.15},Vec3{605.,-535.,202.15},Vec3{588.,-990.,2.15},
            Vec3{417.,-1424.,2.15},Vec3{228.,-1670.,2.15},Vec3{53.,-1641.,54.53},
            Vec3{-211.,-1667.,143.46},Vec3{-613.,-1660.,8.2},Vec3{-1000.,-1650.,2.15},
            Vec3{-1440.13,-1646.78,2.15},Vec3{-2050.,0.,2.15},Vec3{-2420.,0.,2.15}};
        return follow(town,points,"Sian main exit",2600);
    }
    if(id==28&&name==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_30") {
        // Follow the bend uphill to the gate, including the short live approach
        // beyond the final polygon of the Default bake.
        constexpr ActivationBox approach{{-4790.,-17490.,115.},{1670.,410.,140.},0};
        constexpr std::array points{Vec3{-3900.,-17435.,38.},Vec3{-4496.,-17480.,39.05},
            Vec3{-5149.,-17534.,35.97},Vec3{-5434.,-17530.,34.37},
            Vec3{-5514.,-17512.,46.},Vec3{-5544.,-17482.,52.2},Vec3{-5584.,-17448.,61.2},
            Vec3{-5700.,-17370.,86.},Vec3{-5763.,-17329.,98.5},Vec3{-5808.,-17320.,107.5},
            Vec3{-5888.,-17320.,122.6},Vec3{-5950.,-17328.,135.},Vec3{-6023.,-17378.,147.55},
            Vec3{-6108.,-17458.,169.77},Vec3{-6165.,-17534.,179.64},Vec3{-6192.,-17534.,181.5},Vec3{-6186.,-17577.,185.85}};
        return follow(approach,points,"Pharm road to Mt. Reizan",2300);
    }

    if(id==5&&name==L"/Game/Map/Town/00Com/00Com_01/EV/Town_00Com_01_EV.Town_00Com_01_EV:"
        L"PersistentLevel.BP_MapUnit_NPC_C_8") {
        // Approach the side of the bridge from the foot of the ramp.
        constexpr ActivationBox bridge{{1100.,300.,150.},{1250.,460.,205.},0};
        constexpr std::array points{Vec3{1150.,0.,16.},Vec3{1150.,424.,16.},Vec3{1242.,424.,16.},
            Vec3{1665.,458.,260.09},Vec3{1895.,458.,272.71},
            Vec3{2013.,525.,272.71},Vec3{2117.,561.,272.71}};
        return verified_interaction_turns(pawn,target,start,bridge,points,axes,scale,ground_check,path_id,
            diagnostic?"Enterprise bridge to Lid":"",true);
    }

    constexpr std::wstring_view waterfall=L"/Game/Map/Dng/05Old/05Old_43/EV/Dng_05Old_43_01_EV."
        L"Dng_05Old_43_01_EV:PersistentLevel.";
    if(name.starts_with(waterfall)) {
        const auto actor=std::wstring_view(name).substr(waterfall.size());
        if(id==4&&actor==L"BP_MapGimmick_InteractEventTrigger_C_2") {
            constexpr ActivationBox lane{{1180.,5480.,2.15},{1350.,365.,90.},0};
            constexpr std::array points{Vec3{1200.,5480.,2.15},Vec3{2270.,5475.,2.15}};
            return verified_interaction_turns(pawn,target,start,lane,points,axes,scale,ground_check,path_id,
                diagnostic?"Waterfall Way water ley line":"");
        }
        if(id==7&&actor==L"BP_MapGimmick_FieldItem_C_1") {
            constexpr ActivationBox lane{{230.,19030.,2.15},{650.,1050.,90.},0};
            constexpr std::array points{Vec3{45.,19045.,2.15},Vec3{180.,18616.,2.15},
                Vec3{385.,18396.,2.15},Vec3{480.,18376.,2.15}};
            return verified_interaction_turns(pawn,target,start,lane,points,axes,scale,ground_check,path_id,
                diagnostic?"Waterfall Way flower":"",true);
        }
    }

    // The floating road is physically walkable above the world navigation bake.
    // The end of each route lies inside the active gate, away from its wall.
    constexpr std::wstring_view pharm=L"/Game/Map/Wld/GD/Wld_09Pha_02_GD.Wld_09Pha_02_GD:PersistentLevel.";
    if(name.starts_with(pharm)) {
        const auto actor=std::wstring_view(name).substr(pharm.size());
        if(id==65&&actor==L"BP_MapTransitionTrigger_C_1") {
            constexpr ActivationBox path_bounds{{-8340.,-15870.,1185.},{270.,620.,190.},0};
            constexpr std::array points{Vec3{-8425.,-15930.,1174.36},Vec3{-8345.,-15640.,1259.07},
                Vec3{-8325.,-15560.,1260.50},Vec3{-8245.,-15370.,1261.66}};
            return follow(path_bounds,points,"Lake Ryusei to Forest Path",1000);
        }
        if(id==64&&actor==L"BP_MapTransitionTrigger_C_0") {
            constexpr ActivationBox path_bounds{{-7715.,-14780.,1260.},{320.,260.,120.},0};
            constexpr std::array points{Vec3{-7670.,-14755.,1274.16},Vec3{-7665.,-14755.,1274.39}};
            return follow(path_bounds,points,"Forest Path to Wandering Lands",880);
        }
    }
    if(id==0&&name==L"/Game/Map/Dng/09Pha/09Pha_41/GD/Dng_09Pha_41_01_GD.Dng_09Pha_41_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_0") {
        // The live bake returns a partial route across the two shallow ramps.
        constexpr ActivationBox trail{{5650.,50500.,5310.},{5900.,1000.,295.},0};
        constexpr std::array points{Vec3{5200.,50120.,5102.27},Vec3{5490.,50124.,5122.},
            Vec3{6048.,50136.,5302.15},Vec3{8682.,50196.,5320.},Vec3{9072.,50204.,5502.15},
            Vec3{9770.,50160.,5502.11},Vec3{11150.,50820.,5502.11},Vec3{11150.,51150.,5502.11}};
        return follow(trail,points,"Wandering Lands outer trail",2600);
    }
    if(wandering_crystal_gate(target)) {
        // The native Default path has a connected gate approach, but its
        // inset corners still skim the upper trail's jagged railings.
        constexpr ActivationBox trail{{13945.,95.,107.},{3740.,1760.,155.},0};
        constexpr std::array points{Vec3{11082.,8.,2.15},Vec3{12190.,-1081.,2.15},
            Vec3{12355.,-1288.,2.15},Vec3{12363.,-1465.,2.15},Vec3{12650.,-1480.,2.15},
            Vec3{12990.,-1430.,2.15},Vec3{15004.,-1430.,154.8},Vec3{15553.,-1430.,202.15},
            Vec3{15729.,-1418.,202.15},Vec3{15950.,-1327.,202.15},Vec3{16188.,-1182.,202.15},
            Vec3{16327.,-1037.,202.15},Vec3{16528.,-840.,202.15},Vec3{16435.,-130.,202.15},
            Vec3{16427.,492.,202.15},Vec3{16377.,610.,202.15},Vec3{16377.,850.,202.15},
            Vec3{16385.,1008.,202.15},
            Vec3{16461.,1104.,202.15},Vec3{16527.,1142.,202.15},Vec3{16718.,1251.,202.15},
            Vec3{17376.,1632.,202.15}};
        return follow(trail,points,"Wandering Lands Dark Crystal trail",4200);
    }
    // The Forest Path bakes do not cover the floor between the native gates.
    // Use the authored ground and wall lanes, checked against the live capsule.
    constexpr ActivationBox lower{{220.,-34090.,2.15},{3560.,4000.,125.},0};
    constexpr ActivationBox upper{{-1510.,41000.,102.15},{4610.,4100.,130.},0};
    constexpr std::wstring_view forest_gate=L"/Game/Map/Dng/09Pha/09Pha_42/GD/Dng_09Pha_42_01_GD."
        L"Dng_09Pha_42_01_GD:PersistentLevel.";
    if(name.starts_with(forest_gate)) {
        const auto actor=std::wstring_view(name).substr(forest_gate.size());
        if(id==0&&actor==L"BP_MapTransitionTrigger_C_0") {
            constexpr std::array points{
            Vec3{-2897.75,-37529.09,2.15},Vec3{-2553.24,-36917.79,2.15},Vec3{-2450.17,-36751.71,2.15},
            Vec3{-2418.11,-36720.88,2.15},Vec3{-2261.14,-36631.83,2.15},Vec3{-2225.84,-36620.57,2.15},
            Vec3{-2028.19,-36600.46,2.15},Vec3{2633.35,-36330.00,2.15},Vec3{2644.25,-36329.34,2.15},
            Vec3{2827.02,-36307.04,2.15},Vec3{2859.53,-36296.55,2.15},Vec3{2998.55,-36220.01,2.15},
            Vec3{3035.73,-36181.26,2.15},Vec3{3109.78,-36032.41,2.15},Vec3{3118.19,-36005.78,2.15},
            Vec3{3142.41,-35845.64,2.15},Vec3{3143.42,-35832.08,2.15},Vec3{3238.,-35775.,2.15},Vec3{3243.,-35220.,2.15},
            Vec3{3243.43,-30309.61,2.15}};
            return follow(lower,points,"Forest Path lower trail",3500);
        }
        if(id==2&&actor==L"BP_MapTransitionTrigger_C_2") {
            constexpr std::array points{
            Vec3{-1215.17,40040.00,102.15},Vec3{-780.55,39903.39,102.15},Vec3{-601.15,39862.27,102.15},
            Vec3{-581.04,39860.00,102.15},Vec3{2214.70,39679.55,102.15}};
            return follow(upper,points,"Forest Path upper exit",2000);
        }
    }
    if(id==6&&name==L"/Game/Map/Dng/09Pha/09Pha_42/EV/Dng_09Pha_42_01_EV.Dng_09Pha_42_01_EV:"
        L"PersistentLevel.BP_MapGimmick_EventTrigger_C_0") {
        constexpr std::array points{
            Vec3{-5596.85,43164.43,102.15},Vec3{-5500.00,44011.74,102.15},Vec3{-5495.66,44039.33,102.15},
            Vec3{-5431.71,44237.85,102.15},Vec3{-5421.15,44259.84,102.15},Vec3{-5316.79,44417.92,102.15},
            Vec3{-5292.84,44442.38,102.15},Vec3{-5168.26,44528.43,102.15},Vec3{-5141.09,44541.13,102.15},
            Vec3{-4976.11,44586.75,102.15},Vec3{-4952.13,44590.00,102.15},Vec3{-2054.19,44590.00,102.15},
            Vec3{-2023.93,44584.76,102.15},Vec3{-1827.73,44514.74,102.15},Vec3{-1808.86,44505.38,102.15},
            Vec3{-1660.35,44408.65,102.15},Vec3{-1636.47,44385.88,102.15},Vec3{-1537.89,44249.19,102.15},
            Vec3{-1524.96,44223.31,102.15},Vec3{-1474.07,44059.91,102.15},Vec3{-1470.00,44033.15,102.15},
            Vec3{-1215.17,40121.69,102.15}};
        return follow(upper,points,"Forest Path Story trail",3500);
    }
    return {};
}

constexpr ActivationBox factory_upper_walkway{{2840,13700,2190},{1740,1300,190},0};
constexpr std::array factory_maze_turns{
    Vec3{2099,12898,2102.15},Vec3{2092,13804,2102.15},Vec3{2755,13808,2094.3},
    Vec3{2755,14707,2095.3},Vec3{3548,14707,2095.3},Vec3{3548,13808,2094.3},
    Vec3{4340,13808,2102.15}
};
std::optional<NativeRoute> factory_walk_on_live_floor(UObject* pawn,UObject* library,UObject* mesh,
        std::vector<Vec3> points,WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id,double arrival) {
    PathSnapshot route{std::move(points),axes.right,axes.up,scale,true,false};
    if(!describe_route(route))return {};
    std::optional<GroundCoverage> ground;
    std::string reason;
    std::size_t rejoin{};
    if(ground_check) {
        if(const auto policy=capture_ground_policy(pawn,scale))
            ground=ordinary_ground_path(pawn,library,mesh,route,path_id,policy->budget,reason,rejoin,GroundProof::CapsuleSweep);
        else reason="The character's walking and braking state could not be read.";
    }
    return NativeRoute{.snapshot=std::move(route),.ground=ground,.ground_reason=std::move(reason),
        .activation_arrival_metres=arrival,.swept_mesh=ground&&ground->swept_ground?path(mesh):std::wstring{},
        .bypass_rejoin=rejoin};
}
std::vector<Vec3> factory_walkway_suffix(Vec3 start,std::span<const Vec3> walkway,double scale,
        double start_height_tolerance_metres=2.25) {
    Vec3 join{};
    double distance=scale*2.7;
    std::size_t next{};
    for(std::size_t i=1;i<walkway.size();++i) {
        const auto a=walkway[i-1],b=walkway[i];
        const double x=b.x-a.x,y=b.y-a.y;
        const auto length_squared=x*x+y*y;
        if(length_squared<=0.)continue;
        const auto t=std::clamp(((start.x-a.x)*x+(start.y-a.y)*y)/length_squared,0.,1.);
        const Vec3 point{a.x+x*t,a.y+y*t,a.z+(b.z-a.z)*t};
        const auto gap=std::hypot(start.x-point.x,start.y-point.y);
        if(gap<=distance&&std::abs(start.z-point.z)<scale*start_height_tolerance_metres) {
            join=point;distance=gap;next=i;
        }
    }
    if(!next)return {};
    std::vector<Vec3> points{start};
    if(distance>scale*.07)points.push_back(join);
    for(;next<walkway.size();++next) {
        const auto point=walkway[next];
        if(std::hypot(points.back().x-point.x,points.back().y-point.y)>scale*.025)
            points.push_back(point);
    }
    return points;
}
std::optional<NativeRoute> factory_west_walk(UObject* pawn,UObject* target,UObject* library,UObject* mesh,
        Vec3 start,Vec3 goal,WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    const bool ladder=zoldaad_factory_west_ladder(target,goal);
    const bool pedestal=zoldaad_factory_west_pedestal(target,52);
    constexpr ActivationBox catwalk{{2550,11475,700},{930,790,125},0};
    constexpr ActivationBox ramp{{1800,11430,360},{1690,2430,400},0};
    const auto& bounds=pedestal?catwalk:ramp;
    if(!inside_box(bounds,start))return {};
    std::vector<Vec3> walkway;
    if(pedestal) {
        walkway={{3268,10850,702.15},{3268,12002,699.58},{2578,12002,694.18},
            {1853,12002,702.15},{1754,12133,702.15}};
    } else {
        walkway={{313,12650,12.49},{998,11110,2.15},{998,10458,2.15},{1164,10431,2.15},
            {2554,10430,202.15},{2786,10431,202.15},{2786,11164,502.01},
            {1529,11164,702.15},{1529,9703,702.15}};
        if(ladder) {
            if(start.x<1680.&&start.y<9703.)
                walkway={{1529,9356,702.15},{1529,9703,702.15}};
            walkway.insert(walkway.end(),{{2379,9703,691.33},{3239,9703,702.15},{3240,9150,702.15},{3170,9150,702.15}});
        } else walkway.push_back({1529,9356,702.15});
    }
    double arrival=.09;
    if(!ladder) {
        const auto box=capture_interaction_box(target,pawn);
        const auto sweep=box?capture_capsule_sweep(pawn):std::nullopt;
        if(!sweep)return {};
        const auto approach=activation_approach(*box,{sweep->radius,sweep->half_height},walkway.back(),scale);
        if(!approach)return {};
        arrival=approach->arrival_metres;
    }
    if(std::hypot(start.x-walkway.back().x,start.y-walkway.back().y)<=scale*arrival&&
       std::abs(start.z-walkway.back().z)<scale*.15)return NativeRoute{.activation_reached=true};
    auto route=factory_walk_on_live_floor(pawn,library,mesh,factory_walkway_suffix(start,walkway,scale),
        axes,scale,ground_check,path_id,arrival);
    if(route)route->capsule_room=bounds;
    return route;
}
std::optional<NativeRoute> factory_upper_walk(UObject* pawn,UObject* target,UObject* library,UObject* mesh,
        Vec3 start,WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id,bool returning) {
    if(!inside_box(factory_upper_walkway,start))return {};
    // The middle and right intersections connect in this orientation.
    // Check the live floor and rails along the next part of the walkway.
    constexpr Vec3 prompt{4488,13679,2102.15},corner{4340,13679,2102.15};
    double arrival=.08;
    std::vector<Vec3> walkway;
    if(returning) {
        walkway={prompt,corner};
        walkway.insert(walkway.end(),factory_maze_turns.rbegin(),factory_maze_turns.rend());
        walkway.insert(walkway.end(),{{1323,12898,2102.15},{1323,12650,2102.15},{1242,12650,2102.15}});
    } else {
        const auto box=capture_interaction_box(target,pawn);
        const auto sweep=box?capture_capsule_sweep(pawn):std::nullopt;
        if(!sweep)return {};
        const auto approach=activation_approach(*box,{sweep->radius,sweep->half_height},prompt,scale);
        if(!approach)return {};
        arrival=approach->arrival_metres;
        walkway={{1388,14350,2302.15},{1388,12898,2102.15}};
        walkway.insert(walkway.end(),factory_maze_turns.begin(),factory_maze_turns.end());
        walkway.insert(walkway.end(),{corner,prompt});
    }
    if(std::hypot(start.x-walkway.back().x,start.y-walkway.back().y)<=scale*arrival&&
       std::abs(start.z-walkway.back().z)<scale*.15)return NativeRoute{.activation_reached=true};
    auto route=factory_walk_on_live_floor(pawn,library,mesh,factory_walkway_suffix(start,walkway,scale),
        axes,scale,ground_check,path_id,arrival);
    if(route)route->capsule_room=factory_upper_walkway;
    return route;
}
std::optional<NativeRoute> factory_conveyor_walk(UObject* pawn,UObject* library,UObject* mesh,
        const ActivationBox& box,WalkingCapsule capsule,Vec3 start,WalkingAxes axes,double scale,
        bool ground_check,std::uint64_t path_id) {
    constexpr ActivationBox platform{{2750,17630,500},{310,1340,330},0};
    if(!inside_box(platform,start))return {};
    // The arch is straight in XY. Follow its live grade between the turns so
    // height samples do not become steering corners on the ramp seams.
    // Turn on the level strip above the lip, then descend into the overlap.
    constexpr std::array walkway{Vec3{2656,18850,302.15},Vec3{2656,16530,339.0},
        Vec3{2830,16530,339.0},Vec3{2830,16412,302.15}};
    const auto approach=activation_approach(box,capsule,walkway.back(),scale);
    if(!approach)return {};
    return factory_walk_on_live_floor(pawn,library,mesh,factory_walkway_suffix(start,walkway,scale,4.2),
        axes,scale,ground_check,path_id,approach->arrival_metres);
}
std::optional<NativeRoute> factory_bridge_walk(UObject* pawn,UObject* target,UObject* library,UObject* mesh,
        Vec3 start,WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id,bool returning) {
    const ActivationBox bounds{{4200,returning?13700.:13070.,25},{4100,returning?470.:1400.,170},0};
    if(!inside_box(bounds,start))return {};
    // Each deck stays in place after its pedestal disables the attached blocker.
    constexpr std::wstring_view prefix=L"/Game/Map/Dng/06Zol/06Zol_41/GD/Dng_06Zol_41_01_GD.Dng_06Zol_41_01_GD:PersistentLevel.";
    constexpr std::array decks{2048.,3743.,5438.};
    constexpr std::array blockers{5,6,8};
    for(std::size_t i=0;i<decks.size();++i) {
        const auto named=[&](const wchar_t* name,int suffix) {
            const auto path=std::wstring(prefix)+name+std::to_wstring(suffix);
            return UObjectGlobals::StaticFindObject(path.c_str());
        };
        auto* floor=named(L"BP_MapGimmick_Floor_MoveBySwitch_bridge002_C_",static_cast<int>(i)+3);
        auto* blocker=named(L"BP_MapGimmick_BlockingBox_Switch_C_",blockers[i]);
        auto* root=object(blocker,L"m_pRootBoxComponent");
        const auto position=getter<Vec3>(floor,L"K2_GetActorLocation");
        if(!zoldaad_factory_gimmick(floor,static_cast<int>(i)+58,L"/Script/mucha.CPP_MapGimmick_Floor")||
           !zoldaad_factory_gimmick(blocker,static_cast<int>(i)+21,L"/Script/mucha.CPP_MapGimmick_BlockingBox")||
           !position||!finite(*position)||std::hypot(position->x-decks[i],position->y-13700.,position->z)>10.||
           !is(root,L"/Script/Engine.BoxComponent")||get_object(root,L"GetOwner")!=blocker||
           getter<unsigned char>(root,L"GetCollisionEnabled")!=0)return {};
    }
    const auto box=capture_activation_box(target);
    const auto sweep=box?capture_capsule_sweep(pawn):std::nullopt;
    if(!sweep)return {};
    const WalkingCapsule capsule{sweep->radius,sweep->half_height};
    const auto at=activation_clearance(*box,capsule,start);
    if(at&&*at>0)return NativeRoute{.activation_reached=true};
    // The return door overlaps the hall before its blocking frame.
    const Vec3 end{returning?326.:6425.,13706.,2.15};
    const auto approach=activation_approach(*box,capsule,end,scale);
    if(!approach)return {};
    std::vector<Vec3> points{start};
    if(std::abs(start.y-end.y)>scale*.20)
        points.push_back({!returning&&start.y<13300.?772.:start.x,end.y,start.z});
    points.push_back(end);
    return factory_walk_on_live_floor(pawn,library,mesh,std::move(points),axes,scale,ground_check,path_id,approach->arrival_metres);
}
std::optional<NativeRoute> olderion_arsha_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    constexpr ActivationBox plaza{{10500,6400,1090},{1090,680,160},0};
    constexpr std::array turns{Vec3{9847,6650,1042.15},Vec3{10600,6800,1042.15},Vec3{11242,6800,1142.15}};
    return verified_trigger_turns(pawn,target,start,plaza,turns,axes,scale,ground_check,path_id);
}
std::optional<NativeRoute> wardens_cape_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    constexpr ActivationBox hillside{{4900,-1380,160},{710,480,210},0};
    constexpr std::array turns{
        Vec3{4435,-1075,179.},Vec3{4425,-1192,167.63},Vec3{4688,-1192,87.21},
        Vec3{4688,-975,80.04},Vec3{5482,-1668,84.1}
    };
    return verified_trigger_turns(pawn,target,start,hillside,turns,axes,scale,ground_check,path_id);
}
std::optional<NativeRoute> shrine_bridge_approach(UObject* pawn,UObject* target,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    constexpr ActivationBox bridge{{5980,-1727,86},{495,70,55},0};
    constexpr std::array turns{
        Vec3{5790,-1728,87.},Vec3{5850,-1728,85.26},Vec3{6240,-1728,85.26},Vec3{6350,-1728,85.26}
    };
    return verified_trigger_turns(pawn,target,start,bridge,turns,axes,scale,ground_check,path_id);
}
constexpr ActivationBox shrine_basin_east{{950,14830,306},{1330,1040,270},0};
constexpr std::array shrine_basin_stairs{
    Vec3{400,14700,102.15},Vec3{400,14900,102.15},Vec3{1700,14900,102.15},
    Vec3{1700,15620,302.15},Vec3{2050,15620,302.15},Vec3{2050,14000,502.15}
};
constexpr ActivationBox shrine_basin_west{{-1620,11315,304},{870,325,280},0};
constexpr Vec3 basin_dock{ -2360,11325,102.15 },basin_upper{ -1050,11325,502.15 };
std::optional<NativeRoute> shrine_dry_passage_approach(UObject* pawn,UObject* target,int passage,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    if(passage==4) {
        std::array<Vec3,shrine_basin_stairs.size()> turns;
        std::copy(shrine_basin_stairs.rbegin()+1,shrine_basin_stairs.rend(),turns.begin());
        turns.back()={-250,14700,102.15};
        return verified_trigger_turns(pawn,target,start,shrine_basin_east,turns,axes,scale,ground_check,path_id);
    }
    if(passage==10) {
        // The return dock and hall passage are on opposite sides of a wall.
        constexpr ActivationBox bank{{375,43370,102},{530,690,110},0};
        constexpr std::array turns{Vec3{750,42850,102.15},Vec3{0,42850,102.15},Vec3{0,43893.8,102.15}};
        return verified_trigger_turns(pawn,target,start,bank,turns,axes,scale,ground_check,path_id);
    }
    if(passage==11) {
        // Stay centered on the ramp before turning through the lower hall.
        constexpr ActivationBox hall{{-1430,33050,-1410},{920,1150,275},0};
        constexpr std::array turns{
            Vec3{-2060,33500,-1197.85},Vec3{-2060,32150,-1597.85},
            Vec3{-700,32150,-1597.85},Vec3{-700,33910,-1597.85}
        };
        return verified_trigger_turns(pawn,target,start,hall,turns,axes,scale,ground_check,path_id);
    }
    if(passage==16) {
        constexpr ActivationBox hall{{210,32900,-1730},{1100,1050,275},0};
        constexpr std::array turns{Vec3{-700,32150,-1597.85},Vec3{1000,32150,-1897.85}};
        return verified_trigger_turns(pawn,target,start,hall,turns,axes,scale,ground_check,path_id);
    }
    if(passage==17) {
        // The lift keeps the pawn's offset on its platform, outside the baked mesh.
        constexpr ActivationBox hall{{150,32120,-6498},{1000,350,100},0};
        constexpr std::array turns{Vec3{-760,32150,-6497.85},Vec3{1015,32150,-6497.85}};
        return verified_trigger_turns(pawn,target,start,hall,turns,axes,scale,ground_check,path_id);
    }
    // Follow the dry sunken bridge down and back up before turning for the passage.
    constexpr ActivationBox hall{{-1550,31300,74},{970,1030,140},0};
    constexpr std::array turns{
        Vec3{-1800,32150,102.15},Vec3{-1650,32150,27.15},Vec3{-1400,32150,27.15},
        Vec3{-1250,32150,102.15},Vec3{-700,32150,102.15},Vec3{-700,30400,102.15}
    };
    return verified_trigger_turns(pawn,target,start,hall,turns,axes,scale,ground_check,path_id);
}
constexpr ActivationBox shrine_crate_room{{535,60100,-95},{830,930,270},0};
std::optional<NativeRoute> shrine_crate_walk(UObject* pawn,UObject* target,Vec3 start,Vec3 goal,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id,bool push_side) {
    if(!inside_box(shrine_crate_room,start)||(!push_side&&start.z>=-200.))return {};
    const auto sweep=capture_capsule_sweep(pawn);
    if(!sweep)return {};
    constexpr Vec3 upper{1070,60075,102.15},lower{20,60075,-289.85};
    if(push_side) {
        const std::array turns{upper,lower,Vec3{20,60435,-289.85},Vec3{goal.x,60435,-289.85},goal};
        return verified_walking_turns(*sweep,shrine_crate_room,start,turns,axes,scale,ground_check,path_id,.22);
    }
    constexpr Vec3 prompt{1069,59522.49,102.15};
    const auto box=capture_interaction_box(target,pawn);
    const auto approach=box?activation_approach(*box,{sweep->radius,sweep->half_height},prompt,scale):std::nullopt;
    if(!approach)return {};
    constexpr std::array turns{Vec3{20,60650,-289.85},lower,upper,prompt};
    return verified_walking_turns(*sweep,shrine_crate_room,start,turns,axes,scale,ground_check,path_id,
        approach->arrival_metres);
}
std::optional<NativeRoute> waterfall_walking_route(UObject* pawn,UObject* target,Vec3 start,Vec3 goal,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id) {
    if(!waterfall_world(pawn)||actor_world(target)!=actor_world(pawn))return {};
    constexpr std::array first_ladder{
        Vec3{-830,11010,2.15},Vec3{-230,11010,182.15},Vec3{-230,12100,502.15},
        Vec3{-215,12505,502.15},Vec3{-750,12505,402.15},Vec3{-750,13295,402.15},
        Vec3{-380,13295,502.15},Vec3{-380,13525,502.15},Vec3{-615,13525,502.15},Vec3{-615,13720,502.15}
    };
    constexpr std::array second_ladder{
        Vec3{-700,-3600,2.15},Vec3{-700,-5700,2.15},Vec3{-1050,-5700,2.15},
        Vec3{-1050,-6300,2.15},Vec3{-700,-6300,2.15},Vec3{-700,-7100,2.15},
        Vec3{-1050,-7100,2.15},Vec3{-1050,-7600,2.15},Vec3{-700,-7600,2.15},
        Vec3{-700,-8400,2.15},Vec3{-1130,-8400,2.15},Vec3{-1130,-10300,302.15},
        Vec3{0,-10300,502.15},Vec3{0,-9000,502.15},Vec3{75,-9000,502.15}
    };
    constexpr std::array after_first{Vec3{0,13720,1802.15},Vec3{0,10100,1802.15}};
    constexpr std::array lower_gallery{Vec3{2895,6900,832.15},Vec3{2895,3680,1102.15}};
    constexpr std::array after_second{
        Vec3{340,-9000,1217.15},Vec3{340,-6900,1217.15},Vec3{400,-6900,1217.15},
        Vec3{400,-5000,1217.15},Vec3{1325,-5000,1217.15},Vec3{1325,-4390,1217.15}
    };
    constexpr std::array altar{Vec3{2895,7320,2802.15}};
    constexpr std::array north{Vec3{-400,18400,2.15},Vec3{0,18400,2.15},Vec3{0,19965,2.15}};
    constexpr std::array north_from_altar{Vec3{0,18985,2.15},Vec3{0,19965,2.15}};
    constexpr std::array altar_return{Vec3{-1155,52650,2.15}};
    constexpr std::array south_from_save{Vec3{670,5277,2.15},Vec3{-255,5277,2.15}};
    ActivationBox walkway;
    std::span<const Vec3> turns;
    std::optional<ActivationBox> activation;
    if(const auto ladder=waterfall_ladder_id(target)) {
        if(*ladder==0) {walkway={{-630,12060,270},{840,2200,390},0};turns=first_ladder;}
        else if(*ladder==2) {walkway={{-550,-6900,280},{900,3900,410},0};turns=second_ladder;}
        else return {};
        auto* bottom=object(target,L"m_pBottomBoxComponent");
        const auto bottom_position=getter<Vec3>(bottom,L"K2_GetComponentLocation");
        const auto collision=getter<unsigned char>(bottom,L"GetCollisionEnabled");
        if(!is(bottom,L"/Script/Engine.BoxComponent")||get_object(bottom,L"GetOwner")!=target||
           !bottom_position||!collision||(*collision!=1&&*collision!=3&&*collision!=5)||
           std::hypot(goal.x-bottom_position->x,goal.y-bottom_position->y,goal.z-bottom_position->z)>scale*.05)return {};
    } else if(is(target,L"/Script/mucha.CPP_MapTransitionTrigger")) {
        const auto id=read<int>(target,L"m_UniqueId");
        if(!id)return {};
        switch(*id) {
        case 1: walkway={{-180,11920,1802},{680,2030,220},0};turns=after_first;break;
        case 15: walkway={{2895,5430,955},{165,1980,300},0};turns=lower_gallery;break;
        case 2: walkway={{765,-6810,1217},{660,2450,250},0};turns=after_second;break;
        case 7: walkway={{2895,5550,2490},{165,1950,470},0};turns=altar;break;
        case 11: walkway={{320,5210,2},{790,440,125},0};turns=south_from_save;break;
        case 4:
            walkway={{330,19000,2},{1070,1260,160},0};
            if(start.y>=18700.)turns=north_from_altar;
            else turns=north;
            break;
        case 18: walkway={{2500,52650,-460},{4030,240,630},0};turns=altar_return;break;
        default:return {};
        }
        activation=capture_activation_box(target);
        if(!activation)return {};
    } else return {};
    if(!inside_box(walkway,start))return {};
    const auto sweep=capture_capsule_sweep(pawn);
    if(!sweep)return {};
    const WalkingCapsule capsule{sweep->radius,sweep->half_height};
    double arrival=.05;
    if(activation) {
        const auto at=activation_clearance(*activation,capsule,start);
        if(at&&*at>0)return NativeRoute{.activation_reached=true};
        const auto approach=activation_approach(*activation,capsule,turns.back(),scale);
        if(!approach)return {};
        arrival=approach->arrival_metres;
    }
    return verified_walking_turns(*sweep,walkway,start,turns,axes,scale,ground_check,path_id,arrival);
}
bool sanctissimus_pot_ladder(UObject* ladder,Vec3 destination) {
    return is(ladder,L"/Script/mucha.CPP_MapGimmickLadder")&&destination.z<=1150.&&
        read<int>(ladder,L"m_UniqueId")==79&&
        path(ladder)==L"/Game/Map/Dng/08Gro/08Gro_43/GD/Dng_08Gro_43_01_GD.Dng_08Gro_43_01_GD:"
            L"PersistentLevel.BP_MapGimmick_Object_Ladderwood_C_1";
}

std::optional<NativeRoute> sanctissimus_pot_ladder_route(UObject* pawn,UObject* ladder,Vec3 start,
        WalkingAxes axes,double scale,bool ground_check,std::uint64_t path_id,bool log_query) {
    constexpr ActivationBox gallery{{960.,58260.,525.},{795.,885.,590.},0};
    if(!inside_box(gallery,start))return {};
    const auto bottom=capture_ladder_bottom_box(ladder);
    const auto sweep=bottom?capture_capsule_sweep(pawn):std::nullopt;
    if(!sweep)return {};
    const WalkingCapsule capsule{sweep->radius,sweep->half_height};
    const auto at=activation_clearance(*bottom,capsule,start);
    if(at&&*at>0)return NativeRoute{.activation_reached=true};
    // The two ramps overlap in plan. Follow each landing at its own height
    // and keep the capsule away from the inner rail at the switchbacks.
    constexpr std::array turns{Vec3{1446.,58980.,2.15},Vec3{1446.,57600.,502.15},
        Vec3{1062.,57600.,502.15},Vec3{1062.,58980.,502.15},Vec3{1446.,58980.,502.15},
        Vec3{1446.,57600.,1002.15},Vec3{1062.,57600.,1002.15},Vec3{1062.,58980.,1002.15},
        Vec3{800.,59050.,1002.15}};
    const auto approach=activation_approach(*bottom,capsule,turns.back(),scale);
    if(!approach)return {};
    return verified_walking_turns(*sweep,gallery,start,turns,axes,scale,ground_check,path_id,
        approach->arrival_metres,2400,log_query?"Sanctissimus Magic Pot ladder":"",true);
}

std::optional<NativeRoute> route_on_mesh(UObject* p,UObject* target,Vec3 destination,bool ground_check,
        std::string* reason,std::uint64_t path_id,UObject* alternate=nullptr,bool log_query=true) {
    if(reason)reason->clear();
    const auto fail=[&](const char* message)->std::optional<NativeRoute>{if(reason)*reason=message;return {};};
    const auto method=read<unsigned char>(p,L"m_MapMovementMethod");
    if(!method||*method!=1)return fail("Only ordinary walking is supported in this scene.");
    const auto start=controlled_agent_position(p);
    std::string basis_reason;
    const auto axes=walking_input_axes(p,&basis_reason);
    if(!start||!finite(*start)||!finite(destination))return fail("The player position or destination could not be read.");
    if(!axes){if(reason)*reason=basis_reason;return {};}
    auto* library=nav_library(p);
    auto* mesh=alternate?alternate:library?walking_mesh(p,library,*start):nullptr;
    const bool has_mesh=library&&current_walking_mesh(p,library,mesh);
    auto* world=actor_world(p);if(!world)return fail("The current walking area is unavailable.");
    const bool transition=is(target,L"/Script/mucha.CPP_MapTransitionTrigger");
    const bool person=is(target,L"/Script/mucha.CPP_MapUnit_NPC");
    const bool save_point=is(target,L"/Script/mucha.CPP_MapGimmick_SavePoint");
    const bool checked_prop=requires_checked_interaction_box(target);
    const bool local_interaction=person||save_point||checked_prop;
    const bool surveyed_waterfall=waterfall_world(p)&&(transition||waterfall_ladder_id(target));
    const bool shrine_bridge=is_story_shrine_bridge_goal(target,destination);
    const bool tunnel_exit=zoldaad_tunnel_exit(target);
    const bool castle_stairs=zoldaad_throne_approach(target)||zoldaad_roof_exit(target);
    auto room=inn_floor(p);
    if(!room)room=wind_boarding_floor(p,target,*start);
    if(!room)room=story_walkway(target,*start);
    if(!room)if(auto floor=mobliz_escape_floor(p,target,*start)) {
        const auto surface=has_mesh?project_navigation(p,library,mesh,*start):std::nullopt;
        if(!surface||!surface->on_mesh)room=std::move(floor);
    }
    const bool swept_default=alternate&&swept_default_mesh(world,path(mesh));
    std::optional<float> scale;
    for(auto* setting:find(L"WorldSettings")) {
        if(actor_world(setting)==world) {
            const auto value=read<float>(setting,L"WorldToMeters");
            if(!value||!std::isfinite(*value)||*value<=0||(scale&&std::abs(*scale-*value)>.001f))return fail("The area's distance scale could not be verified.");
            scale=value;
        }
    }
    if(!scale)return fail("The area's distance scale is unavailable.");
    if(earth_shrine_entrance(target)&&actor_world(target)==world)
        if(auto route=earth_shrine_door_approach(p,target,*start,*axes,*scale,ground_check,path_id))return route;
    if(passage_map(p)==PassageMap::Mitra&&world==actor_world(target)) {
        // The mesh route crosses an uncovered edge beside this stair.
        // Check the walked centerline and the native doorstep interaction.
        if(inside_box(mitra_weapon_stair,*start)) {
            if(read<int>(target,L"m_UniqueId")==32&&
               path(target)==L"/Game/Map/Town/01Gra/01Gra_20/GD/Town_01Gra_20_GD.Town_01Gra_20_GD:"
                   L"PersistentLevel.BP_MapGimmick_Object_Door_Single_C_3") {
                constexpr std::array landing{Vec3{1426,4135,295.15},Vec3{2244,4163,496.15}};
                if(auto route=verified_interaction_turns(p,target,*start,mitra_weapon_stair,landing,*axes,*scale,ground_check,path_id))
                    return route;
            }
            if(transition&&read<int>(target,L"m_UniqueId")==7&&
               path(target)==L"/Game/Map/Town/01Gra/01Gra_20/GD/Town_01Gra_20_GD.Town_01Gra_20_GD:"
                   L"PersistentLevel.BP_MapTransitionTrigger_C_2") {
                constexpr std::array entrance{Vec3{2244,4163,496.15},Vec3{2441,4144,496.15}};
                if(auto route=verified_trigger_turns(p,target,*start,mitra_weapon_stair,entrance,*axes,*scale,ground_check,path_id))
                    return route;
            }
        }
    }
    if(surveyed_waterfall)
        if(auto route=waterfall_walking_route(p,target,*start,destination,*axes,*scale,ground_check,path_id))return route;
    if(olderion_tree_local(target))
        if(auto route=olderion_tree_approach(p,target,*start,*axes,*scale,ground_check,path_id))return route;
    if(olderion_bridge_soldier(target)) {
        // Pass the outside of the plaza wall and approach the soldier's prompt from the open side.
        constexpr ActivationBox street{{2450,-1200,290},{2800,1450,260},0};
        constexpr std::array turns{Vec3{120,-700,147.15},Vec3{198,-1056,122.15},
            Vec3{310,-1696,122.15},Vec3{390,-1910,122.15},Vec3{580,-2010,122.15},
            Vec3{774,-2107,122.15},Vec3{1010,-2220,122.15},Vec3{1395,-2385,122.24},
            Vec3{1869,-1911,457.15},Vec3{2498,-1282,457.15},Vec3{2990,-790,122.15},
            Vec3{3900,-1030,122.15},Vec3{4420,-1030,122.15},Vec3{4640,-725,122.15},
            Vec3{4770,-430,122.15},Vec3{4900,0,122.15},Vec3{4998,-165,122.15}};
        if(auto route=verified_interaction_turns(p,target,*start,street,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(olderion_arsha_trigger(target))
        if(auto route=olderion_arsha_approach(p,target,*start,*axes,*scale,ground_check,path_id))return route;
    if(olderion_harbor_check(target)) {
        // The prompt is on the near edge of the jetty, past the road-to-deck seam.
        constexpr ActivationBox dock{{-1800,4700,-58},{340,360,90},0};
        constexpr std::array turns{Vec3{-1746.75,4659.30,-50.03},Vec3{-1984.62,4960.98,-68.85}};
        if(auto route=verified_interaction_turns(p,target,*start,dock,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(olderion_south_exit(target)&&read<int>(target,L"m_UniqueId")==9)
        if(auto route=olderion_south_approach(p,target,*start,*axes,*scale,ground_check,path_id))return route;
    if(olderion_inn_door_open(target)==false) {
        constexpr ActivationBox street{{-390,-1950,155},{1070,2250,155},0};
        constexpr std::array turns{Vec3{-260,-700,147.15},Vec3{0,-3672,122.15},
            Vec3{260,-3910,122.15},Vec3{535,-3905,191.29}};
        if(auto route=verified_interaction_turns(p,target,*start,street,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(path(target)==L"/Game/Map/Town/05Old/05Old_01/GD/Town_05Old_01_01_GD.Town_05Old_01_01_GD:"
        L"PersistentLevel.BP_MapTransitionTrigger_C_14"&&read<int>(target,L"m_UniqueId")==13) {
        constexpr ActivationBox threshold{{580,-3905,200},{145,105,80},0};
        constexpr std::array turns{Vec3{535,-3905,191.29},Vec3{620,-3905,202.15}};
        if(auto route=verified_trigger_turns(p,target,*start,threshold,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(cave_world_south_entrance(target)) {
        constexpr ActivationBox approach{{1640,4200,207},{165,160,70},0};
        constexpr std::array turns{Vec3{1638,4163,206.5},Vec3{1701,4207,207.24},Vec3{1701,4231,207.24}};
        if(auto route=verified_trigger_turns(p,target,*start,approach,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(cave_world_north_entrance(target)) {
        constexpr ActivationBox approach{{3330,5220,372},{270,135,65},0};
        constexpr std::array turns{Vec3{3234,5221,370.83},Vec3{3234,5153,372.17}};
        if(auto route=verified_trigger_turns(p,target,*start,approach,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==35&&
       path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_42") {
        // The baked route cuts the final rock corner. Follow the open line
        // where the native Gaberada gate accepted the interaction.
        constexpr ActivationBox road{{19170,-14603,535},{480,140,145},0};
        constexpr std::array turns{Vec3{19159,-14603.066,537.189566},Vec3{19420.893,-14603.066,525.0267}};
        if(auto route=verified_trigger_turns(p,target,*start,road,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==0&&
       path(target)==L"/Game/Map/Town/08Gro/08Gro_01/GD/Town_08Gro_01_01_GD.Town_08Gro_01_01_GD:"
           L"PersistentLevel.BP_MapTransitionTrigger_C_1") {
        constexpr ActivationBox doorstep{{5490.,-3780.,490.},{220.,145.,110.},0.};
        constexpr std::array crossing{Vec3{5418.731,-3769.290,482.150},Vec3{5580.,-3776.,482.150}};
        if(auto route=verified_trigger_turns(p,target,*start,doorstep,crossing,*axes,*scale,ground_check,path_id))return route;
    }
    if(read<int>(target,L"m_UniqueId")==41&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_40/GD/Dng_08Gro_40_01_GD.Dng_08Gro_40_01_GD:"
           L"PersistentLevel.BP_MapGimmick_Object_Door_Slid_C_4") {
        // The native prompt is reached from the front of the door, beyond
        // the inset edge of the baked mesh. Sweep the walked approach.
        constexpr ActivationBox hall{{1120,-845,35},{640,565,95},0};
        constexpr Vec3 prompt{1482.274,-1069.559,4.900};
        constexpr std::array turns{Vec3{993.829,-628.809,4.900},Vec3{1482.274,-1030.495,10.000},prompt};
        if(auto route=verified_interaction_turns(p,target,*start,hall,turns,*axes,*scale,ground_check,path_id))return route;
        constexpr std::array baked_turns{Vec3{1254.,-852.,10.},Vec3{1386.,-930.,20.},
            Vec3{1440.,-1025.,10.},prompt};
        if(auto route=verified_interaction_turns(p,target,*start,hall,baked_turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==1&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_40/GD/Dng_08Gro_40_01_GD.Dng_08Gro_40_01_GD:"
           L"PersistentLevel.BP_MapTransitionTrigger_C_1") {
        // The open leaf connects two separately baked surfaces. Use the
        // swept doorway only while the current collision gives clearance.
        constexpr ActivationBox corridor{{1510.,-1250.,30.},{180.,350.,95.},0};
        constexpr std::array crossing{Vec3{1478.,-1085.,4.9},Vec3{1478.,-1258.,2.4}};
        if(auto route=verified_trigger_turns(p,target,*start,corridor,crossing,*axes,*scale,ground_check,path_id))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==9&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_40/GD/Dng_08Gro_40_01_GD.Dng_08Gro_40_01_GD:"
           L"PersistentLevel.BP_MapTransitionTrigger_C_4") {
        // The inner wall clips a diagonal turn at the baked corner. Cross
        // the full-width aisle before following it to the passage.
        constexpr std::array crossing{Vec3{600.,-6598.,2.400},Vec3{605.,-6715.,2.150},Vec3{850.,-6715.,2.150}};
        if(auto route=verified_trigger_turns(p,target,*start,laboratory_west_passage,crossing,*axes,*scale,ground_check,path_id,1600,
            log_query?"laboratory west passage":""))return route;
    }
    if(transition&&start->x>650.&&read<int>(target,L"m_UniqueId")==8&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_40/GD/Dng_08Gro_40_01_GD.Dng_08Gro_40_01_GD:"
           L"PersistentLevel.BP_MapTransitionTrigger_C_3") {
        // After the card-room door opens, the baked path still starts on
        // the wrong side of the threshold. Prove the walked doorway and turn.
        constexpr ActivationBox corridor{{260.,-4730.,20.},{640.,390.,90.},0};
        constexpr std::array crossing{Vec3{685.77,-5030.07,2.15},Vec3{-209.4,-4826.,2.15},Vec3{-209.9,-4410.,2.15}};
        if(auto route=verified_trigger_turns(p,target,*start,corridor,crossing,*axes,*scale,ground_check,path_id,1600,
            log_query?"laboratory card room return":""))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==3&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_42/GD/Dng_08Gro_42_01_GD.Dng_08Gro_42_01_GD:"
           L"PersistentLevel.BP_MapTransitionTrigger_C_4") {
        // Recast cuts the cap of the stair rail. Follow the supported
        // outer lane around the cap and back down toward the trigger.
        constexpr ActivationBox landing{{2100.,12200.,-150.},{600.,325.,215.},0};
        constexpr std::array crossing{Vec3{2231.,12393.,-93.},Vec3{2494.,12294.,-82.},
            Vec3{2494.,12006.,-97.85},Vec3{1700.,12012.,-276.8}};
        if(auto route=verified_trigger_turns(p,target,*start,landing,crossing,*axes,*scale,ground_check,path_id,1600,
            log_query?"Gronoa entrance stairs":""))return route;
    }
    if(start->x<-3552.&&read<int>(target,L"m_UniqueId")==25&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_42/EV/Dng_08Gro_42_01_EV.Dng_08Gro_42_01_EV:"
           L"PersistentLevel.CPP_MapGimmick_EventTrigger_1") {
        // At the lower berth the platform stands outside the baked mesh.
        // Check the walk off the stopped lift and into the live trigger.
        constexpr ActivationBox landing{{-3490.,350.,-8090.},{735.,250.,95.},0};
        constexpr std::array crossing{Vec3{-3638.,383.45,-8081.47},Vec3{-3380.,380.,-8097.85},
            Vec3{-2920.,350.,-8097.85}};
        if(auto route=verified_trigger_turns(p,target,*start,landing,crossing,*axes,*scale,ground_check,path_id,1600,
            log_query?"Gronoa elevator landing":""))return route;
    }
    if(read<int>(target,L"m_UniqueId")==9&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_40/GD/Dng_08Gro_40_01_GD.Dng_08Gro_40_01_GD:"
           L"PersistentLevel.BP_MapGimmick_Warp_Child04_C_0") {
        // The funnel corners touch the inside walls on both ramps. Check
        // the centre of the ramps and the live portal interaction.
        constexpr ActivationBox pocket{{130.,9120.,-140.},{520.,365.,90.},0};
        constexpr std::array turns{Vec3{-90.,9373.,-98.91},Vec3{106.,9373.,-181.61},
            Vec3{323.68,9208.57,-197.85},Vec3{417.,9208.57,-197.85},
            Vec3{417.,9083.,-190.},Vec3{417.,8809.,-98.91}};
        if(auto route=verified_interaction_turns(p,target,*start,pocket,turns,*axes,*scale,ground_check,path_id,
            log_query?"East lab central portal":""))return route;
    }
    if(path(world)==L"/Game/Map/Dng/08Gro/08Gro_43/Dng_08Gro_43_PL.Dng_08Gro_43_PL"&&
       is(target,L"/Script/mucha.CPP_MapGimmick_Elevator_Interact")) {
        struct Dock {int next_lift;double departure_y;double height;double aisle_x=45.;};
        constexpr std::array docks{Dock{117,102750.,650.},Dock{119,100850.,2650.},
            Dock{118,102590.,1650.},Dock{96,101350.,1150.},Dock{120,103650.,2150.,80.}};
        const auto id=read<int>(target,L"m_UniqueId");
        const auto dock=std::find_if(docks.begin(),docks.end(),[&](const Dock& entry){return id==entry.next_lift;});
        if(dock!=docks.end()&&std::abs(destination.z-dock->height)<=80.) {
            // The railing openings are in line with the lift centers.
            // Step off through the opening before following the landing.
            const auto z=dock->height;
            const auto y=dock->departure_y;
            const ActivationBox landing{{-190.,(y+destination.y)*.5,z},
                {420.,std::abs(y-destination.y)*.5+230.,95.},0};
            const std::array turns{Vec3{start->x,y,z+7.25},Vec3{dock->aisle_x,y,z+2.15},
                Vec3{dock->aisle_x,destination.y,z+2.15},Vec3{destination.x,destination.y,z+7.25}};
            if(auto route=verified_interaction_turns(p,target,*start,landing,turns,*axes,*scale,ground_check,path_id,
                log_query?"Sanctissimus lift landing":""))return route;
        }
    }
    if(read<int>(target,L"m_UniqueId")==90&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_43/GD/Dng_08Gro_43_01_GD.Dng_08Gro_43_01_GD:"
           L"PersistentLevel.BP_MapGimmick_Object_Door001_C_1") {
        // Leave the top lift on the same floor before approaching the door.
        constexpr ActivationBox landing{{0.,101850.,3155.},{490.,205.,85.},0};
        constexpr std::array turns{Vec3{-195.,101895.,3157.25},Vec3{-8.,101895.,3152.15},
            Vec3{313.,101890.,3152.15}};
        if(auto route=verified_interaction_turns(p,target,*start,landing,turns,*axes,*scale,ground_check,path_id,
            log_query?"Sanctissimus upper landing":""))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==8&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_43/GD/Dng_08Gro_43_01_GD.Dng_08Gro_43_01_GD:"
           L"PersistentLevel.BP_MapTransitionTrigger_C_17") {
        // The native courtyard transition starts the duel when approached
        // through the center aisle. Sweep that walk across the baked seam.
        constexpr ActivationBox courtyard{{-2990.,0.,45.},{1580.,300.,115.},0};
        constexpr std::array crossing{Vec3{-3975.,0.,2.15},Vec3{-3310.,0.,2.15},
            Vec3{-2630.,0.,2.15},Vec3{-1960.,0.,2.15},Vec3{-1585.,0.,2.15}};
        if(auto route=verified_trigger_turns(p,target,*start,courtyard,crossing,*axes,*scale,ground_check,path_id,1600,
            log_query?"Sanctissimus courtyard":""))return route;
    }
    if(read<int>(target,L"m_UniqueId")==0&&
       path(target)==L"/Game/Map/Dng/08Gro/08Gro_44/EV/Dng_08Gro_44_EV.Dng_08Gro_44_EV:"
           L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1") {
        // The tablet's approach turns around the west edge of the pedestal.
        constexpr ActivationBox alcove{{6150.,-920.,30.},{530.,350.,90.},0};
        constexpr std::array turns{Vec3{6210.,-859.,2.15},Vec3{6210.,-1064.,2.15},
            Vec3{6397.,-1064.,2.15}};
        if(auto route=verified_interaction_turns(p,target,*start,alcove,turns,*axes,*scale,ground_check,path_id,
            log_query?"Bladerain tablet":""))return route;
    }
    if(path(target)==L"/Game/Map/Wld/08Gro/EV/Wld_08Gro_01_EV.Wld_08Gro_01_EV:"
           L"PersistentLevel.BP_MapGimmick_EventTrigger_C_2"&&read<int>(target,L"m_UniqueId")==173) {
        // The Holy scene's thin box spans the trail. Meet it from the
        // walkable side of the miasma bank.
        constexpr ActivationBox bank{{18920.,-14955.,655.},{295.,445.,125.},0};
        constexpr std::array turns{Vec3{18890.78,-14987.16,627.69},Vec3{18845.36,-15061.63,639.68},
            Vec3{18823.,-15112.,648.24},Vec3{18839.16,-15220.94,676.48},
            Vec3{18844.80,-15246.67,688.07}};
        if(auto route=verified_trigger_turns(p,target,*start,bank,turns,*axes,*scale,ground_check,path_id,1600,
            log_query?"Gaberada miasma approach":"",true))return route;
    }
    if(path(target)==L"/Game/Map/Wld/08Gro/EV/Wld_08Gro_01_EV.Wld_08Gro_01_EV:"
           L"PersistentLevel.BP_MapGimmick_EventTrigger_C_3"&&read<int>(target,L"m_UniqueId")==72) {
        // Stay on the trail across the far side of the mountain pass.
        constexpr ActivationBox trail{{16485.,-18640.,1530.},{540.,390.,110.},0};
        constexpr std::array turns{Vec3{16410.94,-18627.89,1516.56},Vec3{16355.75,-18652.57,1518.71},
            Vec3{16271.76,-18704.94,1520.63},Vec3{16170.88,-18767.84,1517.24},
            Vec3{16091.51,-18817.33,1517.24}};
        if(auto route=verified_trigger_turns(p,target,*start,trail,turns,*axes,*scale,ground_check,path_id,1600,
            log_query?"Mountain weather approach":"",true))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==36&&
       path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_43") {
        // The corner of the exit volume sits behind the roadside rock.
        // The flat lane through the middle reaches the live volume.
        constexpr ActivationBox lane{{18330.,-17474.,789.},{220.,295.,65.},0};
        constexpr std::array turns{Vec3{18306.,-17460.,788.83},Vec3{18306.,-17669.,789.03}};
        if(auto route=verified_trigger_turns(p,target,*start,lane,turns,*axes,*scale,ground_check,path_id,1200,
            log_query?"Rugged Mountain Path entrance":""))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==39&&
       path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_47") {
        constexpr ActivationBox road{{14570.,-19200.,1450.},{2750.,1550.,245.},0};
        // The baked road clips the rocks by town and ends below the bridge.
        // Recheck the controller-walked road and the supported bridge crossing.
        constexpr std::array turns{Vec3{15492.,-19045.,1347.15},Vec3{15492.,-19137.,1359.48},
            Vec3{15512.,-19214.,1369.99},Vec3{15569.83,-19328.80,1392.12},
            Vec3{16152.14,-19328.80,1516.05},Vec3{16152.14,-19221.61,1515.21},
            Vec3{16113.22,-19221.61,1514.80},Vec3{16113.22,-19114.28,1511.30},
            Vec3{16037.12,-18982.35,1514.01},Vec3{15720.44,-18607.62,1531.77},
            Vec3{15659.71,-18544.18,1535.75},Vec3{15583.63,-18493.45,1541.70},
            Vec3{15531.99,-18466.37,1548.60},Vec3{15205.59,-18283.13,1537.33},
            Vec3{15117.02,-18283.13,1535.99},Vec3{15035.83,-18204.03,1513.90},
            Vec3{14862.22,-18285.03,1430.19},Vec3{14832.73,-18311.31,1414.89},
            Vec3{14522.32,-18726.80,1384.05},Vec3{14454.51,-18810.43,1384.17},
            Vec3{14391.22,-18863.29,1384.18},Vec3{14303.48,-18904.71,1384.19},
            Vec3{14217.63,-18930.32,1384.19},Vec3{14183.90,-18938.20,1384.19},
            Vec3{14094.69,-18948.55,1384.19},Vec3{13967.24,-18930.76,1384.19},
            Vec3{13668.04,-18908.49,1384.84},Vec3{13336.78,-18908.49,1389.67},
            Vec3{13254.79,-18969.11,1399.17},Vec3{13192.33,-19030.05,1421.40},
            Vec3{13107.68,-19088.46,1454.61},Vec3{13057.21,-19112.09,1476.80},
            Vec3{12754.23,-19462.18,1565.35},Vec3{12192.14,-20118.98,1625.42},
            Vec3{12165.09,-20195.91,1615.11},Vec3{12094.47,-20216.27,1621.32}};
        if(auto route=verified_trigger_turns(p,target,*start,road,turns,*axes,*scale,ground_check,path_id,5000,
            log_query?"Duggle to Gronoa Shrine":"",true))return route;
    }
    if(transition&&read<int>(target,L"m_UniqueId")==40&&
       path(target)==L"/Game/Map/Wld/GD/Wld_GD.Wld_GD:PersistentLevel.BP_MapTransitionTrigger_C_48") {
        // The road crosses two gaps in the baked surface. The west face of
        // the entrance has the clear overlap. Sweep the controller-walked route.
        constexpr ActivationBox road{{12665.,-18550.,1490.},{1510.,1800.,235.},0};
        constexpr std::array crossing{
            Vec3{12181.26,-20184.64,1625.32},Vec3{12181.26,-20132.52,1625.32},
            Vec3{12744.62,-19473.83,1567.39},Vec3{13051.42,-19115.11,1479.49},
            Vec3{13104.,-19091.,1453.64},Vec3{13177.,-19045.,1425.66},
            Vec3{13236.61,-18983.,1403.52},Vec3{13317.14,-18907.30,1388.59},
            Vec3{13495.35,-18767.,1384.02},Vec3{13554.,-18762.,1384.02},
            Vec3{13607.,-18774.,1384.19},Vec3{13674.,-18755.,1384.19},
            Vec3{13736.,-18704.,1384.19},Vec3{13810.88,-18648.,1401.83},
            Vec3{13873.23,-18593.91,1433.08},Vec3{13904.,-18579.,1445.16},
            Vec3{13910.,-18533.,1463.06},Vec3{13932.,-18481.,1482.81},
            Vec3{13932.,-18429.,1507.22},Vec3{13917.57,-18345.16,1535.55},
            Vec3{13534.89,-18345.16,1538.15},Vec3{13497.,-18374.,1543.47},
            Vec3{13488.,-18423.,1542.47},Vec3{13448.79,-18412.,1542.47},
            Vec3{13161.41,-18403.04,1537.13},Vec3{13104.,-18412.,1545.},
            Vec3{13055.,-18392.,1543.47},Vec3{12830.,-18259.,1539.15},
            Vec3{12727.,-18223.,1539.04},Vec3{12649.,-18202.,1538.89},
            Vec3{12602.,-18185.,1538.89},Vec3{12564.,-18162.,1543.47},
            Vec3{12427.,-18080.,1530.04},Vec3{12378.,-18037.,1527.95},
            Vec3{12304.,-17962.,1535.95},Vec3{12229.,-17887.,1543.47},
            Vec3{12120.,-17884.,1537.},Vec3{12051.,-17882.,1539.57},
            Vec3{11922.,-17837.,1539.57},Vec3{11845.,-17815.73,1539.57},
            Vec3{11519.,-17815.73,1539.57},Vec3{11519.,-17703.,1539.57},
            Vec3{11494.,-17651.,1539.29},Vec3{11488.,-17479.,1525.64},
            Vec3{11405.47,-17352.29,1477.66},Vec3{11385.,-17342.,1472.},
            Vec3{11371.,-17309.,1466.},Vec3{11361.,-17280.,1459.},
            Vec3{11351.,-17234.15,1448.75},
            Vec3{11338.12,-17141.83,1423.67},Vec3{11338.05,-17054.25,1400.52},
            Vec3{11497.,-17054.25,1352.28},Vec3{11497.,-16911.78,1352.28},
            Vec3{11574.47,-16899.63,1352.28}};
        if(auto route=verified_trigger_turns(p,target,*start,road,crossing,*axes,*scale,ground_check,path_id,3200,
            log_query?"Gronoa to Sanctissimus":"",true))return route;
    }
    if(auto route=sian_ayaka_approach(p,target,*start,*axes,*scale,ground_check,path_id,log_query))return route;
    if(auto route=chapter_seven_walking_route(p,target,*start,*axes,*scale,ground_check,path_id,log_query))return route;
    if(auto route=mozul_station_approach(p,target,*start,*axes,*scale,ground_check,path_id))return route;
    if(has_mesh&&zoldaad_factory_maze_pedestal(target)) {
        if(auto route=factory_upper_walk(p,target,library,mesh,*start,*axes,*scale,ground_check,path_id,false))return route;
        if(start->x<3920.&&inside_box(factory_upper_walkway,*start))
            return fail("No clear route across the rotating walkways. Try the rotation pedestals.");
    }
    if(has_mesh&&zoldaad_factory_upper_ladder(target,destination))
        if(auto route=factory_upper_walk(p,target,library,mesh,*start,*axes,*scale,ground_check,path_id,true))return route;
    if(has_mesh&&(zoldaad_factory_west_ladder(target,destination)||zoldaad_factory_west_pedestal(target,38)||
       zoldaad_factory_west_pedestal(target,52)))
        if(auto route=factory_west_walk(p,target,library,mesh,*start,destination,*axes,*scale,ground_check,path_id))return route;
    if(has_mesh&&(zoldaad_factory_generator(target)||zoldaad_factory_bridge_return(target)))
        if(auto route=factory_bridge_walk(p,target,library,mesh,*start,*axes,*scale,ground_check,path_id,
            zoldaad_factory_bridge_return(target)))return route;
    if(zoldaad_dungeon_castle_exit(target)) {
        if(has_mesh)if(auto route=dungeon_gate_crossing(p,target,library,mesh,*start,*axes,*scale,ground_check,path_id))
            return route;
        // The baked surfaces stay disconnected when the key opens the gate.
        // Check the corridor through the open gate and the start of the stairs.
        constexpr ActivationBox corridor{{2470,-47220,48},{1430,380,150},0};
        constexpr std::array turns{Vec3{1251,-47055,2.15},Vec3{1843,-47055,2.15},
            Vec3{3653,-47273,2.15},Vec3{3710,-47316,2.15},Vec3{3718,-47477,55.2}};
        if(auto route=verified_trigger_turns(p,target,*start,corridor,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(zoldaad_captain_exit(target)) {
        constexpr ActivationBox corridor{{30350,5600,1182},{1100,820,130},0};
        constexpr std::array exit{Vec3{30270,5600,1182.15},Vec3{29650,5600,1182.15},Vec3{29320,5600,1182.15}};
        if(const auto sweep=capture_capsule_sweep(p)) {
            if(auto route=verified_walking_turns(*sweep,corridor,*start,exit,*axes,*scale,ground_check,path_id,.08))return route;
            const std::span staging{exit.begin(),exit.size()-1};
            if(auto route=verified_walking_turns(*sweep,corridor,*start,staging,*axes,*scale,ground_check,path_id,.08))return route;
        }
    }
    if(path(target)==L"/Game/Map/Dng/06Zol/06Zol_40/EV/Dng_06Zol_40_01_EV.Dng_06Zol_40_01_EV:"
        L"PersistentLevel.BP_MapUnit_NPC_C_1"&&read<int>(target,L"m_UniqueId")==2) {
        // Use the aisle across the front of the car. Cutting the seat corner
        // shown on the walking mesh does not leave enough capsule clearance.
        constexpr ActivationBox aisle{{300,25,5.65},{390,230,90},0};
        constexpr std::array turns{Vec3{2,-139,5.65},Vec3{9,28,5.65},Vec3{522.6,70.1,5.65}};
        if(auto route=verified_interaction_turns(p,target,*start,aisle,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(waterfall_south_entrance(target)) {
        // The gate sits above the road. Follow the open side of the final
        // bend and overlap the trigger from the walkable edge.
        constexpr ActivationBox approach{{1850,-424,-24},{310,115,70},0};
        constexpr std::array turns{Vec3{1717,-418.15,-23.30},Vec3{1839,-418.15,-22.40},waterfall_south_road};
        if(auto route=verified_trigger_turns(p,target,*start,approach,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(const auto event=old_town_story_trigger(target))
        if(auto route=old_town_story_route(p,target,*event,*start,*axes,*scale,ground_check,path_id))return route;
    if(wardens_cape_trigger(target))
        if(auto route=wardens_cape_approach(p,target,*start,*axes,*scale,ground_check,path_id))return route;
    if(water_shrine_bridge_exit(target))
        if(auto route=shrine_bridge_approach(p,target,*start,*axes,*scale,ground_check,path_id))return route;
    if(amore_north_entrance(target)) {
        constexpr ActivationBox bank{{4090,-635,320},{150,215,105},0};
        constexpr std::array turns{Vec3{4043.34,-541.25,331.34}};
        if(auto route=verified_trigger_turns(p,target,*start,bank,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(amore_south_exit(target)) {
        // The southbound funnel clips the inner street corner on the return from the north bridge.
        constexpr ActivationBox street{{2630,1190,105},{1600,1100,115},0};
        constexpr std::array turns{Vec3{3540,1770,102.15},Vec3{3235,1770,102.15},
            Vec3{3235,1346,110.60},Vec3{2960,784,110.07},Vec3{2895,691,104.90},
            Vec3{2632,424,102.15},Vec3{2522,344,109.07},Vec3{1260,210,110.60}};
        if(auto route=verified_trigger_turns(p,target,*start,street,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(sanctissimus_pot_ladder(target,destination))
        if(auto route=sanctissimus_pot_ladder_route(p,target,*start,*axes,*scale,ground_check,path_id,log_query))return route;
    if(transition||is(target,L"/Script/mucha.CPP_MapGimmickLadder"))
        if(auto route=cave_lake_walking_route(p,target,*start,destination,*axes,*scale,ground_check,path_id))return route;
    if(const auto passage=water_shrine_dry_passage(target))
        if(auto route=shrine_dry_passage_approach(p,target,*passage,*start,*axes,*scale,ground_check,path_id))return route;
    if(shrine_crate_push_goal(target,destination)) {
        if(auto route=shrine_crate_walk(p,target,*start,destination,*axes,*scale,ground_check,path_id,true))return route;
        return fail("A clear walking route to the side of the crate could not be verified.");
    }
    if(shrine_water_switch(target,54))
        if(auto route=shrine_crate_walk(p,target,*start,destination,*axes,*scale,ground_check,path_id,false))return route;
    if(shrine_water_switch(target,53))
        if(auto route=verified_interaction_turns(p,target,*start,shrine_basin_east,shrine_basin_stairs,
            *axes,*scale,ground_check,path_id))return route;
    if(shrine_water_switch(target,52)) {
        constexpr std::array turns{basin_dock,basin_upper,Vec3{-1050,11245,502.15}};
        if(auto route=verified_interaction_turns(p,target,*start,shrine_basin_west,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(shrine_basin_boat(target)) {
        constexpr std::array turns{basin_upper,basin_dock,Vec3{-2360,11470,102.15}};
        if(auto route=verified_interaction_turns(p,target,*start,shrine_basin_west,turns,*axes,*scale,ground_check,path_id))return route;
    }
    if(shrine_tower_ladder_base(target,destination)) {
        constexpr ActivationBox landing{{-800,12200,103},{190,250,115},0};
        constexpr std::array turns{Vec3{-775,12090,102.15},Vec3{-775,12260,102.15}};
        const auto sweep=capture_capsule_sweep(p);
        if(sweep)
            if(auto route=verified_walking_turns(*sweep,landing,*start,turns,*axes,*scale,ground_check,path_id,.07))return route;
    }
    if(shrine_lower_ladder_top(target,destination)) {
        constexpr ActivationBox stairs{{-640,31600,-2100},{1600,1030,640},0};
        constexpr std::array turns{
            Vec3{-2100,32150,-1597.85},Vec3{-2100,30780,-2097.85},
            Vec3{-485,30780,-2597.85},Vec3{-485,31050,-2597.85}
        };
        const auto sweep=capture_capsule_sweep(p);
        if(sweep)
            if(auto route=verified_walking_turns(*sweep,stairs,*start,turns,*axes,*scale,ground_check,path_id,.09))return route;
    }
    if(shrine_bridge) {
        const auto bridge=capture_story_shrine_bridge(p,target,destination);
        const auto sweep=bridge?capture_capsule_sweep(p):std::nullopt;
        if(sweep)
            if(auto route=verified_walking_turns(*sweep,bridge->bounds,*start,bridge->turns,*axes,*scale,
                ground_check,path_id,.18))return route;
        return fail("The floating crates do not give a verified route to the far landing.");
    }
    if(!has_mesh&&!room&&(!(transition||local_interaction)||world_map(world))) {
        return fail(library?"A compatible registered walking mesh could not be verified for this character.":
            "The game's navigation system is unavailable in this area.");
    }
    const bool trigger_door=is(target,L"/Script/mucha.CPP_MapGimmick_Trigger_DoorBase");
    const auto castle_leg=trigger_door?capture_castle_leg(p):std::nullopt;
    const bool room_door=castle_leg&&castle_leg->actor==target;
    const auto room_door_id=room_door?utf8(path(target)):std::string{};
    const auto conveyor=capture_factory_conveyor(p,target);
    const auto lab_lift=capture_research_lab_lift_entry(p,target);
    const bool activation_target=trigger_door||transition||conveyor||lab_lift||
        is(target,L"/Script/mucha.CPP_MapGimmick_EventTrigger");
    std::optional<ActivationBox> box;
    WalkingCapsule capsule;
    const auto local_bounds=[&](const ActivationBox& target_area,bool use_section)->std::optional<ActivationBox> {
        if(alternate||world_map(world))return {};
        if(use_section)if(const auto section=capture_walking_section(p,false,*start))return section->bounds;
        const auto edge=closest_activation_point(target_area,*start);
        if(!edge||std::hypot(edge->x-start->x,edge->y-start->y,edge->z-start->z)>*scale*3.)return {};
        return ActivationBox{*start,{*scale*3.,*scale*3.,*scale},0.};
    };
    const auto direct_transition=[&](bool use_section)->std::optional<NativeRoute> {
        if(!transition||!box)return {};
        const auto approach=closest_activation_point(*box,*start);
        const auto bounds=local_bounds(*box,use_section);
        if(!approach||!bounds)return {};
        auto route=capsule_route(p,target,library,mesh,*start,*approach,*axes,*scale,box,
            ground_check,path_id,bounds,true);
        if(route&&log_query)log("NAV collision-proven exit approach target="+utf8(path(target))+
            " registered_mesh="+std::to_string(has_mesh));
        return route;
    };
    const auto direct_interaction=[&](const ActivationBox& interaction)->std::optional<NativeRoute> {
        const auto bounds=local_bounds(interaction,true);
        if(!bounds)return {};
        auto route=capsule_route(p,target,library,mesh,*start,destination,*axes,*scale,std::nullopt,
            ground_check,path_id,bounds,true);
        if(route&&log_query)log("NAV collision-proven interaction approach target="+utf8(path(target)));
        return route;
    };
    std::string projection_reason;
    const auto approach_from=[&](ActivationAnchor anchor) {
        std::optional<Vec3> requested,projected;
        const auto approach=projected_activation_approach(*box,capsule,*start,*scale,[&](Vec3 point)->std::optional<Vec3> {
            requested=point;
            // Zero QueryExtent selects this mesh's own default extent.
            const auto surface=project_navigation(p,library,mesh,point);
            if(surface&&surface->on_mesh)projected=surface->position;
            return projected;
        },anchor);
        if(log_query) {
            const bool centerline=anchor==ActivationAnchor::Centerline;
            static std::array<std::string,4> last_approaches;
            auto& last_approach=last_approaches[static_cast<std::size_t>(anchor)];
            const auto outcome="target="+utf8(path(target))+" centerline="+std::to_string(centerline)+
                " capsule_height="+std::to_string(anchor==ActivationAnchor::CapsuleHeight)+
                " projection="+std::to_string(projected.has_value())+
                " overlap="+std::to_string(approach.has_value());
            if(outcome!=last_approach) {
                last_approach=outcome;
                log("NAV approach "+outcome+" requested="+(requested?coordinates(*requested):"unavailable")+
                    " projected="+(projected?coordinates(*projected):"unavailable")+
                    " arrival_metres="+(approach?std::to_string(approach->arrival_metres):"unavailable"));
            }
        }
        if(!approach)projection_reason=projected?"The projected walking point does not give a verified approach to this activation area.":
            "The game's walking surface could not be verified at this approach.";
        return approach;
    };
    auto initial_anchor=ActivationAnchor::Nearest;
    bool has_activation_goal=false;
    if(activation_target) {
        box=conveyor?conveyor:lab_lift?lab_lift:capture_activation_box(target);
        if(!box||actor_world(target)!=world)return fail("The target's activation area could not be verified.");
        auto* component=object(p,L"CapsuleComponent");
        if(!component||component!=object(p,L"RootComponent")||
           !is(component,L"/Script/Engine.CapsuleComponent")||get_object(component,L"GetOwner")!=p)
            return fail("The player's walking shape could not be verified.");
        const auto rotation=getter<Vec3>(component,L"K2_GetComponentRotation");
        if(!rotation||!finite(*rotation)||std::abs(std::remainder(rotation->x,360.))>.001||
           std::abs(std::remainder(rotation->z,360.))>.001)
            return fail("The player's walking shape is not upright.");
        const auto radius=getter<float>(component,L"GetScaledCapsuleRadius");
        const auto height=getter<float>(component,L"GetScaledCapsuleHalfHeight");
        if(!radius||!height)return fail("The player's walking shape could not be verified.");
        capsule={*radius,*height};
        const auto clearance=activation_clearance(*box,capsule,*start);
        if(!clearance)return fail("The player's walking shape could not be verified.");
        if(*clearance>0)return NativeRoute{.activation_reached=true,.room_door=room_door_id};
        if(has_mesh&&conveyor&&zoldaad_factory_pedestal_conveyor(target))
            if(auto route=factory_conveyor_walk(p,library,mesh,*box,capsule,*start,*axes,*scale,ground_check,path_id))return route;
        if(path(target)==L"/Game/Map/Dng/05Old/05Old_40/GD/Dng_05Old_40_01_GD.Dng_05Old_40_01_GD:"
            L"PersistentLevel.BP_MapTransitionTrigger_C_14"&&read<int>(target,L"m_UniqueId")==5) {
            // The native disembark point is outside the first polygon on the
            // bank. Check the walking capsule from the actual landing.
            constexpr ActivationBox bank{{2470,11580,15},{250,290,105},0};
            constexpr std::array turns{Vec3{2620,11501.4,2.15}};
            if(inside_box(bank,*start)) {
                const auto sweep=capture_capsule_sweep(p);
                const auto arrival=activation_approach(*box,capsule,turns.back(),*scale);
                if(sweep&&arrival)
                    if(auto route=verified_walking_turns(*sweep,bank,*start,turns,*axes,*scale,ground_check,
                        path_id,arrival->arrival_metres))return route;
            }
        }
        const auto sliding_door=has_mesh?sliding_door_approach(target):std::nullopt;
        if(sliding_door) {
            // The sliding door opens when the pawn enters its own trigger.
            // Approach it on the mesh, then check the road through the open door.
            const auto& [join,gate,road,failure]=*sliding_door;
            const auto sweep=capture_capsule_sweep(p);
            const auto local=inside_box(road,*start);
            const auto end=local&&sweep?room_lane(*sweep,road,*start,gate,*scale):std::nullopt;
            const auto entry=end?activation_approach(*box,capsule,*end,*scale):std::nullopt;
            constexpr double staging_arrival=.10;
            if(local&&sweep) {
                const auto floor=entry?std::optional<Vec3>{entry->point}:room_lane(*sweep,road,*start,join,*scale);
                if(floor) {
                    const auto arrival=entry?entry->arrival_metres:staging_arrival;
                    if(std::hypot(floor->x-start->x,floor->y-start->y)<=*scale*arrival)
                        return NativeRoute{.activation_reached=true};
                    PathSnapshot path{{*start,*floor},axes->right,axes->up,*scale,true,false};
                    std::optional<GroundCoverage> proof;
                    if(ground_check) {
                        const auto metres=route_arc_metres(path,{1,1});
                        if(!metres)return fail(failure);
                        proof=GroundCoverage{.path_id=path_id,.end={1,1},.end_point=*floor,.metres=*metres,
                            .reaches_end=true,.follow_surface=true};
                    }
                    return NativeRoute{.snapshot=std::move(path),.ground=proof,
                        .activation_arrival_metres=arrival,.capsule_room=road};
                }
            } else if(!local) {
                auto route=direct_nav::find_path(direct_navigation_api(),p,library,mesh,*start,join);
                if(route.valid&&!route.points.empty()&&std::hypot(route.points.back().x-join.x,
                    route.points.back().y-join.y)<=*scale*.05) {
                    PathSnapshot path{std::move(route.points),axes->right,axes->up,*scale,true,false};
                    std::optional<GroundCoverage> proof;
                    std::string issue;
                    std::size_t rejoin{};
                    if(ground_check)if(const auto policy=capture_ground_policy(p,*scale))
                        proof=ordinary_ground_path(p,library,mesh,path,path_id,policy->budget,issue,rejoin);
                    return NativeRoute{.snapshot=std::move(path),.ground=proof,.ground_reason=std::move(issue),
                        .activation_arrival_metres=staging_arrival,.bypass_rejoin=rejoin};
                }
            }
        }
        if(olderion_south_exit(target)) {
            // The gate opens from a prompt on the walkway. The transition's
            // box extends past the end of the standing surface.
            const ActivationBox entrance_walkway{{-1025,0,150},{1250,330,250},0};
            const Vec3 prompt{-1660,0,52.15};
            if(inside_box(entrance_walkway,*start)&&read<unsigned char>(target,L"m_IsAutoTransition")==0) {
                constexpr double arrival=.04;
                if(std::hypot(start->x-prompt.x,start->y-prompt.y)<=*scale*arrival&&
                    std::abs(start->z-prompt.z)<10)
                    return NativeRoute{.activation_reached=true};
                const auto mover=capture_capsule_sweep(p);
                const auto end=mover?room_lane(*mover,entrance_walkway,*start,prompt,*scale):std::nullopt;
                if(end&&std::abs(end->z-prompt.z)<10) {
                    PathSnapshot snapshot{{*start,*end},axes->right,axes->up,*scale,true,false};
                    std::optional<GroundCoverage> proof;
                    if(ground_check) {
                        const auto distance=route_arc_metres(snapshot,{1,1});
                        if(!distance)return fail("The approach to the south gate could not be measured.");
                        proof=GroundCoverage{.path_id=path_id,.end={1,1},.end_point=*end,.metres=*distance,
                            .reaches_end=true,.follow_surface=true};
                    }
                    return NativeRoute{.snapshot=std::move(snapshot),.ground=proof,.activation_arrival_metres=arrival,
                        .capsule_room=entrance_walkway};
                }
            }
        }
        if(room) {
            const auto approach=closest_activation_point(*box,*start);
            if(!approach)return fail("The doorway's walking approach could not be read.");
            auto route=capsule_route(p,target,library,mesh,*start,*approach,*axes,*scale,box,ground_check,path_id,room);
            if(!route)return fail("A clear walking approach for this doorway could not be verified.");
            return route;
        }
        if(transition&&(!has_mesh||(!alternate&&passage_map(p)!=PassageMap::Other&&
           boolean(target,L"m_IsSameMapTrasition")))) {
            if(auto route=direct_transition(!has_mesh))return route;
            if(!has_mesh)return fail("A clear walking approach to this exit could not be verified.");
        }
        std::optional<ActivationApproach> approach;
        std::optional<Vec3> road;
        if(amore_south_exit(target))road=Vec3{1260,210,110};
        else if(olderion_world_entrance(target))road=Vec3{48,2394,-40};
        else if(waterfall_south_entrance(target))road=waterfall_south_road;
        else if(cave_world_south_entrance(target))road=Vec3{1701,4231,210};
        else if(cave_world_north_entrance(target))road=Vec3{3234,5153,372};
        else if(const auto zoldaad=zoldaad_world_approach(target))road=*zoldaad;
        else if(tunnel_exit)road=Vec3{-10644,14960,-1087};
        else if(zoldaad_dungeon_castle_exit(target))road=Vec3{4000,-47520,74.04};
        else if(path(target)==L"/Game/Map/Town/06Zol/06Zol_21/GD/Town_06Zol_21_01_GD.Town_06Zol_21_01_GD:"
            L"PersistentLevel.BP_MapTransitionTrigger_C_5"&&read<int>(target,L"m_UniqueId")==5)
            road=Vec3{2810,1970,317.35};
        else if(transition&&path(target)==L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
            L"PersistentLevel.BP_MapTransitionTrigger_C_3"&&read<int>(target,L"m_UniqueId")==1)
            road=Vec3{11880.,-49800.,1691.1};
        else if(transition&&path(target)==L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
            L"PersistentLevel.BP_MapTransitionTrigger_C_17"&&read<int>(target,L"m_UniqueId")==12)
            road=Vec3{3490.,-302850.,3290.};
        else if(tower_spiral_exit(target)==TowerSpiralExit::RestStop)
            road=chapter_eight::rest_stop_approach;
        else if(olderion_south_exit(target)&&path(mesh).ends_with(L".RecastNavMesh-Default"))
            road=Vec3{-1692,0,50};
        else if(wandering_crystal_gate(target)&&path(mesh).ends_with(L".RecastNavMesh-Default"))
            road=Vec3{17376,1632,210};
        else if(path(target)==L"/Game/Map/Dng/05Old/05Old_40/GD/Dng_05Old_40_01_GD.Dng_05Old_40_01_GD:"
            L"PersistentLevel.BP_MapTransitionTrigger_C_11"&&read<int>(target,L"m_UniqueId")==2)
            road=Vec3{2958,-9200,1810};
        if(has_mesh&&road) {
            // Use the connected approach when projecting the box's center
            // would land on a wall, disconnected polygon, or above the road.
            const Vec3 extent{*scale*.04,*scale*.04,*scale*.22};
            const auto floor=project_navigation(p,library,mesh,*road,extent);
            if(floor&&floor->on_mesh)approach=activation_approach(*box,capsule,floor->position,*scale);
        }
        for(const auto anchor:{ActivationAnchor::Nearest,ActivationAnchor::CapsuleHeight,ActivationAnchor::Centerline,
                ActivationAnchor::BoxCenter}) {
            if(approach)break;
            approach=approach_from(anchor);
            if(approach) {initial_anchor=anchor;break;}
        }
        if(!approach&&!alternate) {
            // A registered bake can stop short of a live transition volume.
            // Verify continuous floor, capsule clearance and actual overlap.
            if(auto route=direct_transition(true))return route;
            return fail(projection_reason.c_str());
        }
        has_activation_goal=approach.has_value();
        if(approach)destination=approach->point;
        else destination={box->center.x,box->center.y,start->z};
    }
    if(!has_mesh&&!activation_target&&!room) {
        if(actor_world(target)==world)if(const auto interaction=capture_interaction_box(target,p))
            if(auto route=direct_interaction(*interaction))return route;
        return fail("A clear walking approach to this interaction could not be verified.");
    }
    if(room) {
        auto route=capsule_route(p,target,library,mesh,*start,destination,*axes,*scale,box,ground_check,path_id,room);
        if(!route)return fail("A clear walking approach on this floor could not be verified.");
        return route;
    }
    if(alternate&&!swept_default&&!olderion_south_exit(target)&&!wandering_crystal_gate(target)) {
        auto route=capsule_route(p,target,library,mesh,*start,destination,*axes,*scale,box,ground_check,path_id);
        if(!route)return fail("A clear walking approach for the player could not be verified.");
        route->room_door=room_door_id;
        return route;
    }
    const auto offset_radius=swept_default&&ground_check?
        getter<float>(object(p,L"CapsuleComponent"),L"GetScaledCapsuleRadius"):std::nullopt;
    const direct_nav::CornerOffset corner_offset{offset_radius.value_or(0.f),*scale};
    const auto query=[&](Vec3 goal,ActivationAnchor anchor,bool face_center=false) {
        if(!current_walking_mesh(p,library,mesh))return direct_nav::Path{.reason="The current walking mesh changed before pathfinding."};
        stage="direct native walking path";
        auto result=direct_nav::find_path(direct_navigation_api(),p,library,mesh,*start,goal,corner_offset);
        if(log_query) {
            const bool centerline=anchor==ActivationAnchor::Centerline;
            static std::array<std::string,5> last_queries;
            auto& last_query=last_queries[face_center?4:static_cast<std::size_t>(anchor)];
            const auto diagnostic="target="+utf8(path(target))+" activation="+std::to_string(activation_target)+
                " centerline="+std::to_string(centerline)+" capsule_height="+std::to_string(anchor==ActivationAnchor::CapsuleHeight)+
                (swept_default?" mesh=Default face_center="+std::to_string(face_center):std::string{})+
                " status="+std::to_string(result.status)+
                " flags="+(result.native_flags?std::to_string(*result.native_flags):"unavailable")+
                " points="+std::to_string(result.points.size())+
                (swept_default&&ground_check?" offset_points="+std::to_string(result.offset_points.size()):std::string{})+
                " valid="+std::to_string(result.valid)+
                " partial="+std::to_string(result.partial)+" reason="+result.reason;
            if(diagnostic!=last_query) {
                last_query=diagnostic;
                log("NAV direct "+diagnostic+" start="+coordinates(*start)+" goal="+coordinates(goal));
            }
        }
        if(!current_walking_mesh(p,library,mesh))return direct_nav::Path{.reason="The current walking mesh changed during pathfinding."};
        return result;
    };
    std::optional<double> arrival_metres;
    const bool chest=is(target,L"/Script/mucha.CPP_MapGimmick_TreasureBox");
    const bool mailbox=is(target,L"/Script/mucha.CPP_MapGimmick_Object_DLC");
    const bool interact_door=mitra_passage_door(target)!=nullptr;
    const auto interaction=!activation_target?capture_interaction_box(target,p):std::nullopt;
    const auto sweep=interaction?capture_capsule_sweep(p):std::nullopt;
    const auto boarding=interaction?capture_raft_boarding_point(target,p):std::nullopt;
    if(person&&(!interaction||!sweep||actor_world(target)!=world))
        return fail("This person's talk area could not be verified.");
    auto preferred_approach=olderion_harbor_check(target)?std::optional{Vec3{-1984.62,4960.98,-65.}}:std::nullopt;
    const bool lab_portal=is(target,L"/Script/mucha.CPP_MapGimmick_Warp")&&
        path(target).starts_with(L"/Game/Map/Dng/08Gro/08Gro_40/GD/");
    if(lab_portal&&read<int>(target,L"m_UniqueId")==18)preferred_approach=Vec3{-31.475,-9852.154,103.215};
    const bool elder_door=gaberada_elder_door_open(target)==false;
    constexpr Vec3 elder_prompt{5418.731,-3769.290,482.150};
    auto approach=!boarding&&interaction&&sweep&&actor_world(target)==world?
        interaction_route_on_mesh(*sweep,library,mesh,*start,*interaction,destination.z,*scale,
            chest||mailbox||interact_door||checked_prop,elder_door?std::optional{elder_prompt}:preferred_approach,
            lab_portal):std::nullopt;
    if(!approach&&elder_door&&interaction&&sweep&&has_mesh) {
        // The door's standing prompt is ahead of the authored outside spawn.
        // Join its native route to a swept approach across the stone landing.
        constexpr Vec3 landing{5300.,-3780.,490.};
        constexpr ActivationBox doorstep{{5350.,-3776.,490.},{265.,200.,130.},0.};
        auto prefix=query(landing,ActivationAnchor::Nearest);
        if(prefix.valid&&!prefix.points.empty()) {
            const auto join=prefix.points.back();
            const auto floor=sweep->ground_probe().project(join);
            if(floor&&std::hypot(floor->x-landing.x,floor->y-landing.y)<=*scale*.05) {
                const auto end=room_lane(*sweep,doorstep,*floor,elder_prompt,*scale);
                const auto overlap=end?activation_approach(*interaction,{sweep->radius,sweep->half_height},
                    *end,*scale):std::nullopt;
                if(overlap) {
                    prefix.points.back()=*floor;
                    prefix.points.push_back(overlap->point);
                    approach=InteractionRoute{std::move(prefix),overlap->arrival_metres};
                }
            }
        }
    }
    if(approach&&elder_door&&sweep&&start->x<2320.) {
        // The baked line grazes the outside of the lower ramp. Check the
        // inside line before rejoining the complete route on the landing.
        constexpr std::array turns{Vec3{1120.,-713.,15.},Vec3{1380.,-821.,100.},Vec3{1727.022,-1104.918,156.301},
            Vec3{2307.607,-1512.492,156.301},Vec3{2324.210,-1534.638,156.301}};
        auto entry=verified_walking_turns(*sweep,gaberada_elder_ramp,*start,turns,*axes,*scale,false,path_id,.12,850,
            log_query?"Gaberada ramp":"");
        if(!entry&&start->x<2240.)
            entry=verified_walking_turns(*sweep,gaberada_elder_ramp,*start,std::span{turns}.first<4>(),*axes,*scale,false,path_id,.12,850);
        if(entry&&entry->snapshot.valid) {
            auto points=entry->snapshot.points;
            auto onward=direct_nav::find_path(direct_navigation_api(),p,library,mesh,points.back(),approach->path.points.back());
            if(onward.valid&&std::hypot(onward.points.front().x-points.back().x,onward.points.front().y-points.back().y)<*scale*.05) {
                onward.points.back()=approach->path.points.back();
                points.insert(points.end(),onward.points.begin()+1,onward.points.end());
                approach->path.points=std::move(points);
            }
        }
    }
    if(!boarding&&!approach&&checked_prop&&interaction&&sweep&&actor_world(target)==world) {
        approach=wind_lift_landing_route(*sweep,library,mesh,*start,*interaction,destination.z,*scale,log_query);
        if(approach&&log_query)log("NAV collision-proven lift landing and onward route target="+utf8(path(target)));
    }
    if(!approach&&checked_prop&&interaction&&sweep&&has_mesh&&actor_world(target)==world&&
       read<int>(target,L"m_UniqueId")==133&&
       path(target)==L"/Game/Map/Dng/10Cry/10Cry_40/GD/Dng_10Cry_40_01_GD.Dng_10Cry_40_01_GD:"
           L"PersistentLevel.BP_MapGimmick_InteractEventTrigger_C_1") {
        // From Upper Reach the native path turns onto the approach to the
        // white portal. Join at the measured slope before checking the prompt.
        constexpr Vec3 landing{13182.94,-55294.47,2019.06};
        constexpr Vec3 entry{15678.,-55300.,2482.15},prompt{15778.,-55300.,2482.15};
        constexpr ActivationBox platform{{15730.,-55300.,2482.15},{150.,140.,80.},0.};
        auto prefix=query(landing,ActivationAnchor::Nearest);
        if(prefix.valid&&!prefix.points.empty()) {
            const auto join=prefix.points.back();
            const auto floor=sweep->ground_probe().project(entry);
            const auto end=floor?room_lane(*sweep,platform,*floor,prompt,*scale):std::nullopt;
            const auto overlap=end?activation_approach(*interaction,{sweep->radius,sweep->half_height},*end,*scale):std::nullopt;
            if(floor&&overlap&&std::hypot(join.x-landing.x,join.y-landing.y)<=*scale*.05&&
               std::abs(join.z-landing.z)<=sweep->step_height) {
                auto onward=join_verified_walking_prefix(p,library,mesh,std::move(prefix.points),*floor,*scale);
                if(onward&&std::hypot(onward->points.back().x-floor->x,onward->points.back().y-floor->y)<=*scale*.05&&
                   std::abs(onward->points.back().z-floor->z)<=sweep->step_height) {
                    onward->points.back()=*floor;
                    onward->points.push_back(overlap->point);
                    approach=InteractionRoute{std::move(*onward),overlap->arrival_metres};
                }
            }
        }
    }
    if(!boarding&&!approach&&local_interaction&&interaction&&sweep&&actor_world(target)==world)
        if(auto route=direct_interaction(*interaction))return route;
    if(!approach&&person&&interaction&&sweep&&
       path(target)==L"/Game/Map/Town/05Old/05Old_01/EV/Town_05Old_01_01_EV.Town_05Old_01_01_EV:"
           L"PersistentLevel.BP_MapUnit_NPC_C_4") {
        // The soldier's prompt is reachable from the flat walkway. Join a
        // complete native route to that walkway to the short swept approach.
        constexpr Vec3 walkway{4900.,-260.,122.15};
        constexpr Vec3 prompt{4983.,-260.,122.15};
        const ActivationBox bounds{walkway,{300.,250.,120.},0.};
        auto prefix=query(walkway,ActivationAnchor::Nearest);
        if(prefix.valid&&!prefix.points.empty()) {
            const auto join=prefix.points.back();
            const auto floor=sweep->ground_probe().project(join);
            if(floor&&std::hypot(floor->x-walkway.x,floor->y-walkway.y)<=*scale*.05&&
               std::abs(floor->z-walkway.z)<=sweep->step_height) {
                const auto end=room_lane(*sweep,bounds,*floor,prompt,*scale);
                const auto overlap=end?activation_approach(*interaction,{sweep->radius,sweep->half_height},
                    *end,*scale):std::nullopt;
                if(overlap) {
                    prefix.points.back()=*floor;
                    prefix.points.push_back(overlap->point);
                    approach=InteractionRoute{std::move(prefix),overlap->arrival_metres};
                }
            }
        }
    }
    direct_nav::Path native;
    std::size_t surveyed_turn_prefix{};
    if(boarding) {
        if(!boarding->level_with_bank)return fail("Wait for the water to rise to the bank before boarding.");
        const auto& landing=boarding->position;
        native=query(landing,ActivationAnchor::Nearest);
        if(native.valid&&(native.points.empty()||
            std::hypot(native.points.back().x-landing.x,native.points.back().y-landing.y)>*scale*.10))
            return fail("The walking path does not reach the boat's boarding point.");
        arrival_metres=.12;
    } else if(approach) {
        arrival_metres=approach->arrival_metres;
        native=std::move(approach->path);
        if(!native.points.empty()) {
            if(auto turn=surveyed_walking_turn(p,target,library,mesh,*start,native.points.back(),*scale,surveyed_turn_prefix,&arrival_metres))
                native=std::move(*turn);
        }
    } else if(interact_door) {
        const auto edge=interaction?closest_activation_point(*interaction,*start):std::nullopt;
        if(edge&&std::hypot(edge->x-start->x,edge->y-start->y,edge->z-start->z)<=*scale*3.) {
            const ActivationBox threshold{*start,{*scale*3.,*scale*3.,*scale},0.};
            auto route=capsule_route(p,target,library,mesh,*start,destination,*axes,*scale,box,
                ground_check,path_id,threshold,true);
            if(route)return route;
        }
        return fail("A clear approach to the door's interaction could not be verified.");
    } else if(mailbox) {
        return fail("A clear approach to the mailbox could not be verified.");
    } else if(checked_prop) {
        return fail("A clear approach to the interaction point could not be verified.");
    } else if(person) {
        const auto leg=capture_castle_leg(p);
        if(leg&&leg->hall_side&&leg->actor!=target&&current_walking_mesh(p,library,mesh)&&
           interaction_route_on_mesh(*sweep,library,mesh,*leg->hall_side,*interaction,destination.z,*scale))
            return route_on_mesh(p,leg->actor,leg->position,ground_check,reason,path_id,nullptr,log_query);
        return fail("A clear walking approach to this person's talk area could not be verified.");
    } else if(!box||has_activation_goal) {
        auto turn=swept_default?cave_world_switchback(p,target,library,mesh,*start,destination,*scale):std::nullopt;
        if(swept_default&&!turn)turn=factory_desert_bend(p,target,library,mesh,*start,destination,*scale);
        if(!turn)turn=surveyed_walking_turn(p,target,library,mesh,*start,destination,*scale,surveyed_turn_prefix);
        native=turn?std::move(*turn):query(destination,initial_anchor);
    }
    else native.reason=projection_reason;
    if(box&&has_activation_goal&&!native.valid) {
        // A trigger can sit on a lower floor, and a box can overlap disconnected polygons.
        // Try both heights and keep only a complete native path into the volume.
        for(const auto anchor:{ActivationAnchor::Centerline,ActivationAnchor::BoxCenter}) {
            if(anchor==initial_anchor)continue;
            const auto alternative=approach_from(anchor);
            if(!alternative||std::hypot(alternative->point.x-destination.x,alternative->point.y-destination.y,
                                        alternative->point.z-destination.z)<=.001)continue;
            native=query(alternative->point,anchor);
            if(native.valid)break;
        }
    }
    const auto world_sweep=swept_default&&box?capture_capsule_sweep(p):std::nullopt;
    if(box&&swept_default&&!world_sweep)return fail("The player's walking collision could not be verified for this approach.");
    const auto box_endpoint=[&](const direct_nav::Path& candidate)->std::optional<ActivationApproach> {
        if(!candidate.valid||candidate.points.empty()||!box)return {};
        const auto end=candidate.points.back();
        if(!swept_default)return activation_approach(*box,capsule,end,*scale);
        if(!world_sweep||!activation_approach(*box,capsule,end,*scale))return {};
        std::optional<CapsuleSweep::Contact> contact;
        const auto floor=world_sweep->ground_probe(&contact).project(end);
        if(!floor||std::abs(floor->z-end.z)>world_sweep->step_height||
           std::hypot(floor->x-end.x,floor->y-end.y)>*scale*.01)return {};
        return activation_approach(*box,capsule,*floor,*scale);
    };
    auto endpoint=box_endpoint(native);
    if(box&&!endpoint)if(auto route=direct_transition(true))return route;
    if(box&&(swept_default||transition)&&!endpoint) {
        // The nearest box corner can project beyond capsule overlap even
        // when another face sits on the connected walking surface.
        for(const auto sample:box_face_approaches(*box,start->z)) {
            const auto surface=project_navigation(p,library,mesh,sample);
            if(!surface||!surface->on_mesh||!activation_approach(*box,capsule,surface->position,*scale))continue;
            auto trial=query(surface->position,ActivationAnchor::Nearest,true);
            if(auto overlap=box_endpoint(trial)) {
                native=std::move(trial);endpoint=overlap;
                break;
            }
            if(!native.valid)native.reason=trial.valid?"No supported capsule overlap at this exit could be verified.":std::move(trial.reason);
        }
    }
    if(!activation_target&&native.partial) {
        // Rain's room and the hall have separate walking surfaces. Verify the
        // requested target is reachable from the hall side before offering the
        // door as a separate first leg. Only the player's side is walked.
        const auto leg=capture_castle_leg(p);
        if(leg&&leg->hall_side&&leg->actor!=target&&current_walking_mesh(p,library,mesh)) {
            const auto onward=direct_nav::find_path(direct_navigation_api(),p,library,mesh,*leg->hall_side,destination);
            if(onward.valid&&current_walking_mesh(p,library,mesh))
                return route_on_mesh(p,leg->actor,leg->position,ground_check,reason,path_id,alternate,log_query);
        }
    }
    if(!native.valid){if(reason)*reason=native.reason;return {};}
    if(box) {
        if(!endpoint)return fail("The native route does not reach the target's activation area.");
        arrival_metres=endpoint->arrival_metres;
    }
    bool centered_spiral=false;
    const auto spiral_exit=box?tower_spiral_exit(target):TowerSpiralExit::None;
    if(spiral_exit!=TowerSpiralExit::None) {
        auto centerline=chapter_eight::spiral_route(*start,spiral_exit==TowerSpiralExit::RestStop);
        const auto overlap=centerline?activation_approach(*box,capsule,centerline->back(),*scale):std::nullopt;
        if(overlap) {
            native.points=std::move(*centerline);
            native.offset_points.clear();
            arrival_metres=overlap->arrival_metres;
            centered_spiral=true;
        }
    }
    std::size_t bypass_rejoin{};
    if(swept_default&&earth_shrine_exterior(world)) {
        if(auto departure=route_around_start_contact(p,library,mesh,*start,native,*scale,bypass_rejoin)) {
            native=std::move(*departure);
            if(log_query)log("NAV collision-proven departure from live feet target="+utf8(path(target))+
                " from="+coordinates(*start)+" rejoin="+coordinates(native.points[bypass_rejoin]));
        }
    }
    PathSnapshot snapshot{std::move(native.points),axes->right,axes->up,*scale,true,false};
    if(!describe_route(snapshot))return fail("The native path points could not be converted to walking instructions.");
    std::optional<GroundCoverage> ground;std::string ground_reason;
    if(ground_check) {
        const auto policy=capture_ground_policy(p,*scale);
        if(policy) {
            const auto swept_prefix=surveyed_turn_prefix?surveyed_turn_prefix:(approach?approach->lift_prefix:0);
            const auto proof=elder_door?GroundProof::CapsuleSweep:swept_prefix?GroundProof::CapsulePrefix:
                (swept_default||centered_spiral||tunnel_exit||castle_stairs)?GroundProof::CapsuleSweep:GroundProof::MeshFirst;
            ground=ordinary_ground_path(p,library,mesh,snapshot,path_id,policy->budget,ground_reason,bypass_rejoin,
                proof,swept_prefix);
            const bool try_offset=swept_default&&!native.offset_points.empty()&&
                (!ground||ground->limit==GroundLimit::Obstruction||ground->limit==GroundLimit::Projection||
                 ground->limit==GroundLimit::Surface);
            if(try_offset) {
                PathSnapshot adjusted{std::move(native.offset_points),axes->right,axes->up,*scale,true,false};
                std::size_t adjusted_rejoin{};
                std::string adjusted_reason;
                const auto alternative=describe_route(adjusted)?
                    ordinary_ground_path(p,library,mesh,adjusted,path_id,policy->budget,adjusted_reason,
                        adjusted_rejoin,GroundProof::CapsuleSweep):std::nullopt;
                const auto original_clear=ground?horizontal_arc_metres(snapshot,ground->end).value_or(0.):0.;
                const auto adjusted_clear=alternative?horizontal_arc_metres(adjusted,alternative->end).value_or(0.):0.;
                if(alternative&&(!ground||alternative->reaches_end||adjusted_clear>original_clear+.05)) {
                    log("NAV world native corner offset verified_metres="+std::to_string(adjusted_clear)+
                        " previous_metres="+std::to_string(original_clear));
                    snapshot=std::move(adjusted);ground=alternative;
                    ground_reason=std::move(adjusted_reason);bypass_rejoin=adjusted_rejoin;
                }
            }
        }
        else ground_reason="The character's walking and braking state could not be read.";
        if(swept_default&&!ground&&ground_reason.empty())ground_reason="The walking capsule could not verify clear ground ahead.";
    }
    return NativeRoute{std::move(snapshot),ground,std::move(ground_reason),arrival_metres,false,room_door_id,
        {},ground&&ground->swept_ground?path(mesh):std::wstring{},bypass_rejoin};
}
std::optional<NativeRoute> route_snapshot(UObject* pawn,UObject* target,Vec3 destination,bool ground_check,
        std::string* reason=nullptr,std::uint64_t path_id=0,bool log_query=true) {
    const auto world=actor_world(pawn);
    if(actor_world(target)==world&&tower_spiral_exit(target)!=TowerSpiralExit::None) {
        // LocalMap can return a complete path while the party stands in a
        // seam it cannot project onto. Keep the same continuous bake for the
        // measured spiral and sweep the actual capsule along the route.
        const auto start=controlled_agent_position(pawn);
        auto* system=nav_library(pawn);
        auto* mesh=start&&system?registered_default_surface(pawn,system):nullptr;
        const auto surface=mesh?project_navigation(pawn,system,mesh,*start):std::nullopt;
        if(surface&&surface->on_mesh&&std::hypot(surface->position.x-start->x,surface->position.y-start->y)<25.)
            if(auto route=route_on_mesh(pawn,target,destination,ground_check,reason,path_id,mesh,log_query))
                return route;
    }
    if(world_map(world)&&read<unsigned char>(pawn,L"m_MapMovementMethod")==1) {
        const auto start=controlled_agent_position(pawn);
        auto* system=nav_library(pawn);
        auto* preferred=start&&system?walking_mesh(pawn,system,*start):nullptr;
        if(preferred) {
            // World MakeWall can register without supplying a floor at the
            // pawn. Select the proven Default before projecting the target.
            auto* alternate=default_walking_surface(pawn,system,preferred,*start);
            if(alternate)return route_on_mesh(pawn,target,destination,ground_check,reason,path_id,alternate,log_query);
            auto native=route_on_mesh(pawn,target,destination,ground_check,reason,path_id,nullptr,log_query);
            if(native)return native;
            // MakeWall can cover the player's feet but end before the city
            // entrance. Retry on Default only where it also covers this start.
            auto* candidate=registered_default_surface(pawn,system);
            const auto projected=candidate&&candidate!=preferred?
                project_navigation(pawn,system,candidate,*start):std::nullopt;
            if(projected&&projected->on_mesh&&std::hypot(projected->position.x-start->x,projected->position.y-start->y)<25.)
                return route_on_mesh(pawn,target,destination,ground_check,reason,path_id,candidate,log_query);
            return {};
        }
    }
    auto native=route_on_mesh(pawn,target,destination,ground_check,reason,path_id,nullptr,log_query);
    if(native)return native;
    const auto start=controlled_agent_position(pawn);
    auto* system=nav_library(pawn);
    if(!start||!system)return {};
    auto* preferred=walking_mesh(pawn,system,*start);
    if(!preferred)return {};
    // Some levels stream only the Default mesh. Use it only when the player
    // has no surface on the preferred mesh and their real capsule fits the route.
    auto* alternate=default_walking_surface(pawn,system,preferred,*start);
    if(!alternate&&(olderion_south_exit(target)||wandering_crystal_gate(target)||
       crystal_tower_spiral(actor_world(pawn)))) {
        // The local bake can cover the party while ending before a gate or spiral seam.
        // Confirm that the alternate bake is connected at the party's feet.
        auto* candidate=registered_default_surface(pawn,system);
        const auto projected=candidate&&candidate!=preferred?
            project_navigation(pawn,system,candidate,*start):std::nullopt;
        if(projected&&projected->on_mesh&&std::hypot(projected->position.x-start->x,projected->position.y-start->y)<25.)
            alternate=candidate;
    }
    if(!alternate)return {};
    return route_on_mesh(pawn,target,destination,ground_check,reason,path_id,alternate,log_query);
}
bool area_target_reachable(UObject* pawn,const AreaTarget& target) {
    if(target.snapshot.interactable)return true;
    const auto map=passage_map(pawn);
    const auto section=map!=PassageMap::Other?capture_walking_section(pawn,false):std::nullopt;
    const auto location=section?capture_walking_section(target.actor,false,target.position):std::nullopt;
    if(location&&separate_walking_room(map,section->id,location->id))return false;
    const auto route=route_snapshot(pawn,target.actor,target.position,false,nullptr,0,false);
    return route&&(route->snapshot.valid||route->activation_reached)&&
        (route->room_door.empty()||route->room_door==utf8(path(target.actor)));
}
bool same_area_position(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}
bool nearby_area_origin(Vec3 a,Vec3 b,double capsule_radius) {
    // Keep a positive scanner proof during footing jitter and small steps
    // in the same field section. Starting a route always checks live feet.
    return std::isfinite(capsule_radius)&&std::hypot(a.x-b.x,a.y-b.y)<=.5*capsule_radius&&
        std::abs(a.z-b.z)<=.25*capsule_radius;
}
constexpr std::uint64_t area_route_proof_ms=8000;
struct DestinationOptions {
    std::vector<LiveTravelPassage> first;
    std::vector<DestinationRoute> places;
};
std::optional<TravelLocation> loaded_travel_location(UObject* pawn) {
    const auto location=capture_travel_location(pawn);
    return location&&travel_catalog::matches_world(location->point.map,path(actor_world(pawn)))?location:std::nullopt;
}
DestinationOptions destination_options(UObject* pawn,const TravelLocation& location,std::span<const TravelPassage> completed) {
    stage="planning field destinations";
    DestinationOptions options;
    const auto map=passage_map(pawn);
    const auto section=map!=PassageMap::Other?capture_walking_section(pawn,false):std::nullopt;
    for(auto& leg:capture_travel_passages(pawn,location)) {
        const auto& target=leg.target;
        if(area_input_detail::map_title(leg.passage.arrival.map).empty()||
           std::find(completed.begin(),completed.end(),leg.passage)!=completed.end())continue;
        const auto target_section=section?capture_walking_section(target.actor,false,target.position):std::nullopt;
        if(target_section&&separate_walking_room(map,section->id,target_section->id))continue;
        const auto route=route_snapshot(pawn,target.actor,target.position,false,nullptr,0,false);
        if(route&&(route->snapshot.valid||route->activation_reached)&&route->room_door.empty())
            options.first.push_back(std::move(leg));
    }
    if(options.first.empty())return options;
    std::vector<TravelPassage> first;
    for(const auto& leg:options.first)first.push_back(leg.passage);
    const auto links=travel_catalog::available_links(evaluate_travel_condition);
    const auto current=area_input_detail::map_title(location.point.map);
    std::map<std::string_view,DestinationRoute> by_name;
    for(const auto& place:plan_destinations(first,links,completed)) {
        const auto name=area_input_detail::map_title(place.map);
        if(!name.empty()&&name!=current)by_name.try_emplace(name,place);
    }
    for(const auto& [name,place]:by_name)options.places.push_back(place);
    return options;
}
const DestinationRoute* destination_choice(const DestinationOptions& options,int map) {
    const auto name=area_input_detail::map_title(map);
    const auto i=std::find_if(options.places.begin(),options.places.end(),[&](const auto& place) {
        return area_input_detail::map_title(place.map)==name;
    });
    return i!=options.places.end()?std::addressof(*i):nullptr;
}
std::string field_selection_context(UObject* pawn) {
    auto context=utf8(path(actor_world(pawn)))+"|"+utf8(path(pawn));
    if(const auto location=capture_field_location(pawn))
        context+="|"+std::to_string(location->map)+":"+std::to_string(location->area);
    return context;
}

enum NavigationKey : std::size_t { CategoryBack,CategoryForward,TargetBack,TargetForward,GuidanceKey,AutomaticKey };
struct NavigationGuard {
    UObject* pawn{};
    bool eligible{},foreground{},physical_input{},actuator{},field_surface{},controller_allowed{},rowing{},flying{};
    bool walking{};
    std::array<bool,6> allowed{};
    std::array<std::string,6> conflicts;
    std::string reason="Native navigation input state is unavailable.";
    std::string actuator_reason;
};
bool ordinary_walking(UObject* pawn) {
    const auto travel=read<unsigned char>(pawn,L"m_MapMovementMethod");
    const auto movement=read<unsigned char>(object(pawn,L"CharacterMovement"),L"MovementMode",L"ByteProperty");
    // Both exact enums use 1 for ordinary walking. Nav-walking, falling,
    // swimming, flying and game-specific travel require separate adapters.
    return travel&&*travel==1&&movement&&*movement==1;
}
FieldWallCues capture_field_walls(UObject* pawn,const StorySpatialContext& spatial,std::span<const ActivationBox> passages) {
    FieldWallCues cues{};
    if(!ordinary_walking(pawn))return cues;
    auto sweep=capture_capsule_sweep(pawn);
    if(!sweep)return cues;
    auto* function=sweep->library->GetFunctionByNameInChain(L"CapsuleTraceSingleByProfile");
    auto* field=function?function->FindProperty(FName(L"OutHit",FNAME_Find)):nullptr;
    if(!field||field->GetOffset_Internal()!=0x68||field->GetSize()!=256||
       !field->field_class()||field->field_class()->GetName()!=L"StructProperty")return cues;
    auto& handle=static_cast<FStructProperty*>(field)->GetStruct();
    UScriptStruct* hit_type{};
    std::memcpy(&hit_type,std::addressof(handle),sizeof hit_type);
    if(!native_metadata(hit_type,NativeMetadataKind::ScriptStruct)||path(hit_type)!=L"/Script/Engine.HitResult"||
       hit_type->GetPropertiesSize()!=256||
       !native_struct_shape(hit_type,{{L"Time",4,4},{L"ImpactPoint",40,24},{L"ImpactNormal",88,24},{L"Component",216,8}}))
        return cues;

    const double full_height=sweep->half_height;
    const double reach=field_wall_radius_metres*spatial.world_to_metres;
    // Start the sweep above ordinary steps. Keep its top within the live
    // walking capsule so a doorway's lintel does not close a usable passage.
    sweep->half_height=std::max(sweep->radius,sweep->half_height*.64f);
    const double lift=std::min<double>(sweep->step_height+2.15,2.*(full_height-sweep->half_height));
    const Vec3 feet{spatial.origin.x,spatial.origin.y,spatial.origin.z-full_height};
    const Vec3 from{feet.x,feet.y,feet.z+lift};
    const WalkingCapsule walking{sweep->radius,full_height};
    const std::array<Vec3,4> axes{spatial.up,spatial.right,{-spatial.up.x,-spatial.up.y,0},
        {-spatial.right.x,-spatial.right.y,0}};
    constexpr std::array<std::string_view,4> directions{"up","right","down","left"};
    auto* world=actor_world(pawn);
    // A wall-like blocking hit on level geometry owned by this world, never a pawn or map unit.
    const auto wall_hit=[&](const std::optional<CapsuleSweep::Hit>& hit,const CapsuleSweep::Obstacle& obstacle) {
        if(!hit||!hit->blocking||!is(obstacle.component,L"/Script/Engine.PrimitiveComponent"))return false;
        auto* owner=get_object(obstacle.component,L"GetOwner");
        return actor_world(owner)==world&&!is(owner,L"/Script/Engine.Pawn")&&!is(owner,L"/Script/mucha.CPP_MapUnit_Object");
    };
    std::string apertures,aperture_sides;
    for(std::size_t i=0;i<axes.size();++i) {
        const double length=std::hypot(axes[i].x,axes[i].y);
        if(!std::isfinite(length)||length<.01)continue;
        const Vec3 direction{axes[i].x/length,axes[i].y/length,0};
        const Vec3 side{direction.y,-direction.x,0};
        CapsuleSweep::Obstacle obstacle{};
        const auto hit=sweep->trace(from,{from.x+reach*direction.x,from.y+reach*direction.y,from.z},&obstacle);
        if(!wall_hit(hit,obstacle))continue;
        auto cue=field_wall_cue(obstacle.point,obstacle.normal,direction,
            obstacle.fraction*field_wall_radius_metres,sweep->floor_z,spatial,directions[i]);
        if(!cue)continue;
        Vec3 stopped{hit->location.x,hit->location.y,feet.z};
        bool penetrating=hit->penetrating;
        // The probe is as wide as the player. Beside an opening it clips the jamb at
        // the edge of its width; the hit then sits off the axis. A side-step of one
        // radius that still travels a full capsule farther proves a usable aperture:
        // no tone for that direction, or the farther wall seen through it.
        const double hit_depth=obstacle.fraction*reach;
        for(const double shift:wall_aperture_shifts(wall_probe_lateral(from,obstacle.point,direction),sweep->radius)) {
            const Vec3 shifted{from.x+side.x*shift,from.y+side.y*shift,from.z};
            const auto step=sweep->trace(from,shifted);
            if(!step||step->blocking)continue; // That side-step is itself obstructed or unverifiable.
            CapsuleSweep::Obstacle ahead{};
            const auto through=sweep->trace(shifted,{shifted.x+reach*direction.x,shifted.y+reach*direction.y,shifted.z},&ahead);
            if(!through)continue;
            const std::optional<double> depth=through->blocking?std::optional<double>(ahead.fraction*reach):std::nullopt;
            if(!wall_aperture_open(hit_depth,depth,sweep->radius))continue;
            std::optional<FieldProximityCue> beyond;
            if(through->blocking&&wall_hit(through,ahead))
                beyond=field_wall_cue(ahead.point,ahead.normal,direction,ahead.fraction*field_wall_radius_metres,
                    sweep->floor_z,spatial,directions[i]);
            apertures+=" "+std::string(directions[i])+":hit_m="+std::to_string(hit_depth/spatial.world_to_metres)+
                ",shift_cm="+std::to_string(shift)+",beyond_m="+(depth?std::to_string(*depth/spatial.world_to_metres):"clear");
            aperture_sides+=" "+std::string(directions[i]);
            cue=std::move(beyond);
            stopped={through->location.x,through->location.y,feet.z};
            penetrating=through->penetrating;
            break;
        }
        if(cue&&!penetrating&&std::any_of(passages.begin(),passages.end(),[&](const auto& passage) {
            return reaches_activation_before_wall(passage,walking,feet,stopped,spatial.world_to_metres);
        }))cue.reset();
        cues[i]=std::move(cue);
    }
    // One line when the set of excused directions changes, with that moment's geometry.
    static std::string previous;
    if(aperture_sides!=previous) {
        previous=aperture_sides;
        log(apertures.empty()?std::string("WALL aperture none"):"WALL aperture"+apertures);
    }
    return cues;
}
bool native_rowing(UObject* pawn) {
    if(!is(pawn,L"/Script/mucha.CPP_MapUnit_Ship")||read<unsigned char>(pawn,L"m_MapMovementMethod")!=4)
        return false;
    const auto world=path(actor_world(pawn));
    return world==L"/Game/Map/Dng/05Old/05Old_40/Dng_05Old_40_PL.Dng_05Old_40_PL"||
           world==L"/Game/Map/Dng/05Old/05Old_44/Dng_05Old_44_PL.Dng_05Old_44_PL";
}
bool native_airship(UObject* pawn) {
    return is(pawn,L"/Script/mucha.CPP_MapUnit_AirShip")&&read<unsigned char>(pawn,L"m_MapMovementMethod")==2&&
        path(actor_world(pawn))==L"/Game/Map/Wld/Wld_PL.Wld_PL";
}
bool navigation_field_surface(UObject* top) {
    if(is(top,L"/Game/UI/Field/Widget/WBP_FieldLayout.WBP_FieldLayout_C")||
       is(top,L"/Game/UI/WBP_OverAllLayout.WBP_OverAllLayout_C"))return true;
    // The shipped sequencer layout has bCanReceiveInput=false and can remain
    // on the layer after battle. It is not itself an open menu. Require the
    // live reflected policy too; unknown or changed policy remains blocking.
    return is(top,L"/Game/UI/Battle/Widget/WBP_BattleSequencerLay.WBP_BattleSequencerLay_C")&&
        is(top,L"/Script/mucha.CPP_WBP_BattleSequencer")&&!boolean(top,L"bCanReceiveInput",true);
}
UObject* navigation_blocking_widget() {
    auto* top=top_widget();if(!top)return nullptr;
    if(!navigation_field_surface(top))return top;
    bool blocked=false;
    walk(top,[&](auto* widget) {
        if(blocked)return;
        if(is(widget,L"/Script/mucha.CPP_WBP_Talk")||is(widget,L"/Script/mucha.CPP_WBP_BattleSequencer")) {
            auto* window=object(object(widget,L"WindowManager"),L"m_pWindow");
            if(visible(window))blocked=true;
        }
        if(is(widget,L"/Script/EnhancedUI.CPP_WBP_MenuBase")&&selection(widget).entry)blocked=true;
    });
    return blocked?top:nullptr;
}
NavigationGuard capture_navigation_guard(bool controller_gate_only=false) {
    const auto capture_started=std::chrono::steady_clock::now();
    NavigationGuard result;
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    result.foreground=foreground==GetCurrentProcessId();
    result.pawn=player();
    auto* blocking=navigation_blocking_widget();
    const auto input=capture_navigation_input(result.pawn,blocking,controller_gate_only);
    auto* top=top_widget();
    const bool field_layout=navigation_field_surface(top);
    result.field_surface=result.pawn&&!blocking&&(field_layout||(!top&&input.unit_input_enabled==InputFact::Yes))&&
        input.selecting_key!=InputFact::Yes&&input.text_entry!=InputFact::Yes&&
        input.talk_event!=InputFact::Yes&&input.paused!=InputFact::Yes&&input.blocking_context!=InputFact::Yes;
    const auto method=read<unsigned char>(result.pawn,L"m_MapMovementMethod");
    result.walking=ordinary_walking(result.pawn);
    result.rowing=native_rowing(result.pawn);
    result.flying=native_airship(result.pawn);
    const bool supported_movement=result.walking||result.rowing||result.flying;
    result.eligible=result.foreground&&input.known&&input.field_control&&supported_movement&&actor_world(result.pawn);
    if(!result.pawn)result.reason="A field character is unavailable.";
    else if(input.selecting_key==InputFact::Yes)result.reason="Key assignment is active.";
    else if(input.text_entry==InputFact::Yes)result.reason="Text entry is active.";
    else if(blocking||input.blocking_context==InputFact::Yes)result.reason="A menu or scene is active.";
    else if(input.talk_event==InputFact::Yes)result.reason="A scripted scene is active.";
    else if(input.paused==InputFact::Yes)result.reason="The game is paused.";
    else if(!method||!supported_movement)result.reason="Navigation is unavailable in this travel mode.";
    else if(!actor_world(result.pawn))result.reason="The current field location could not be read.";
    else if(!input.known)result.reason="Field controls could not be read.";
    else result.reason=result.eligible?"":"Field controls are unavailable.";
    auto key=[&](const char* name)->const KeyBindingFact* {
        for(const auto& fact:input.keys)if(fact.key==name)return &fact;
        return nullptr;
    };
    const auto modifier=controller_modifier();
    const auto* button=key(modifier==ControllerModifier::RightShoulder?"Gamepad_RightShoulder":
        modifier==ControllerModifier::RightTrigger?"Gamepad_RightTrigger":"Gamepad_LeftTrigger");
    const auto* axis=key(modifier==ControllerModifier::RightTrigger?"Gamepad_RightTriggerAxis":"Gamepad_LeftTriggerAxis");
    result.controller_allowed=controller_bindings_supported(modifier,button&&button->conflict!=InputFact::Unknown,
        button?std::span<const std::string>(button->mappings):std::span<const std::string>{},
        axis&&axis->conflict!=InputFact::Unknown,
        axis?std::span<const std::string>(axis->mappings):std::span<const std::string>{});
    if(controller_gate_only)return result;
    const std::array<const char*,6> names={"Home","End","PageUp","PageDown","Delete","Delete"};
    const std::array<const char*,6> spoken={"Home","End","Page Up","Page Down","Delete","Shift Delete"};
    for(std::size_t i=0;i<names.size();++i) {
        const auto* fact=key(names[i]);
        result.allowed[i]=fact&&fact->conflict==InputFact::No;
        if(!result.allowed[i])result.conflicts[i]=trf(fact&&fact->conflict==InputFact::Yes?
            "{0} navigation is unavailable because it is assigned to a game control.":
            "{0} navigation is unavailable because its current game binding could not be read.",{tr(spoken[i])});
    }
    // Shift's existing dash action remains native. Reject a reassignment of the
    // modifier to another action rather than firing both unrelated commands.
    for(const auto& [vk,name]:std::array<std::pair<int,const char*>,2>{{{VK_LSHIFT,"LeftShift"},{VK_RSHIFT,"RightShift"}}})if(GetAsyncKeyState(vk)&0x8000) {
        const auto* fact=key(name);
        const bool known_dash=fact&&fact->conflict!=InputFact::Unknown&&std::all_of(fact->mappings.begin(),fact->mappings.end(),[](const auto& mapping) {
            return mapping=="KBM_Dash"||mapping=="MapUnit_Dash"||mapping=="Dash"||mapping=="MapUnit_LeftAnalogStickPush";
        });
        if(!known_dash){result.allowed[AutomaticKey]=false;result.conflicts[AutomaticKey]="Automatic walking is unavailable because Shift is assigned to another game control or could not be read.";}
    }
    // Only the game's movement action or its current held movement keys
    // take over from the separate Pawn input supplied by the mod.
    result.physical_input=input.native_move_input==InputFact::Yes;
    const bool physical_known=input.native_move_input!=InputFact::Unknown;
    const auto retained_speed=read<float>(object(result.pawn,L"CharacterMovement"),L"MaxWalkSpeed",L"FloatProperty");
    const bool speed_known=retained_speed&&std::isfinite(*retained_speed)&&*retained_speed>0;
    result.actuator=physical_known&&speed_known&&native_walking_ready();
    if(!physical_known)result.actuator_reason="Physical controls could not be read.";
    else if(!speed_known)result.actuator_reason="The current walking speed could not be read.";
    else if(!native_walking_ready())result.actuator_reason="Native walking input is unavailable.";
    static std::string last;
    auto diagnostic="field="+std::to_string(result.eligible)+" known="+std::to_string(input.known)+" physical_known="+std::to_string(physical_known)+
        " local="+std::to_string(static_cast<int>(input.local_player_pawn))+" unit="+std::to_string(static_cast<int>(input.unit_input_enabled))+
        " paused="+std::to_string(static_cast<int>(input.paused))+" ignored="+std::to_string(static_cast<int>(input.move_input_ignored))+
        " UIblocked="+std::to_string(static_cast<int>(input.ui_blocked))+" event="+std::to_string(static_cast<int>(input.talk_event))+
        " selector="+std::to_string(static_cast<int>(input.selecting_key))+" text="+std::to_string(static_cast<int>(input.text_entry))+
        " speed_known="+std::to_string(speed_known)+" dash="+std::to_string(boolean(result.pawn,L"m_IsDash"))+
        " retained_speed="+(retained_speed?std::to_string(*retained_speed):"unknown")+" contexts=";
    for(std::size_t i=0;i<input.contexts.size()&&i<16;++i) {
        const auto& context=input.contexts[i];diagnostic+=std::to_string(context.parent)+"[";
        for(std::size_t n=0;n<context.children.size()&&n<16;++n)diagnostic+=std::to_string(context.children[n])+",";
        diagnostic+="]";
    }
    diagnostic+=" keys=";for(bool allowed:result.allowed)diagnostic+=allowed?'1':'0';
    diagnostic+=" surface="+utf8(path(top))+" field_surface="+std::to_string(field_layout)+
        " widget_blocking="+std::to_string(blocking!=nullptr)+" reason="+input.reason;
    if(diagnostic!=last){last=diagnostic;log("NAV input "+diagnostic);}
    const auto microseconds=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-capture_started).count();
    static double sum_us=0,max_us=0;static unsigned samples=0;static unsigned long long measured_at=0;
    sum_us+=microseconds;max_us=std::max(max_us,microseconds);++samples;
    const auto measured_now=GetTickCount64();
    if(measured_now-measured_at>=5000) {
        log("NAV guard cost samples="+std::to_string(samples)+" mean_us="+std::to_string(sum_us/samples)+" max_us="+std::to_string(max_us));
        measured_at=measured_now;sum_us=0;max_us=0;samples=0;
    }
    return result;
}
std::vector<NarrationClock> capture_narration_clocks(bool foreground) {
    std::vector<NarrationClock> result;
    const auto sampled=static_cast<double>(GetTickCount64())/1000.;
    // Only a manager's current scene/player is authoritative. Loaded sequence
    // assets, templates, preloaded future scenes and unrelated players are ignored.
    for(auto* manager:find(L"CPP_TalkEventManager")) {
        auto* scene=object(manager,L"m_pScene");
        if(!scene||object(scene,L"m_pEventManager")!=manager) continue;
        auto* event=object(scene,L"m_pEventPlayer");
        if(!is(event,L"/Script/mucha.CPP_TalkEventSequenceBase")||object(event,L"m_pEventManager")!=manager) continue;
        auto* sequence_player=object(event,L"m_pSequencePlayer");
        auto* asset=get_object(sequence_player,L"GetSequence");
        if(!asset) continue;
        // Exact reflected QualifiedFrameTime: FrameNumber.Value int32,
        // SubFrame float32, FrameRate numerator/denominator int32.
        struct QualifiedTime { int frame; float fraction; int numerator,denominator; };
        const auto current=getter<QualifiedTime>(sequence_player,L"GetCurrentTime");
        const auto rate=getter<float>(sequence_player,L"GetPlayRate");
        if(!current||!rate||current->numerator<=0||current->denominator<=0||
           !std::isfinite(current->fraction)||current->fraction<0||current->fraction>=1) continue;
        const auto seconds=(static_cast<double>(current->frame)+current->fraction)*current->denominator/current->numerator;
        result.push_back({utf8(path(scene))+"|"+utf8(path(sequence_player)),"sequence:"+utf8(path(asset)),
            seconds,sampled,*rate,true,get_bool(sequence_player,L"IsPlaying"),get_bool(sequence_player,L"IsPaused"),foreground,false});
    }
    // Movie screens can also be played outside a TalkEvent root. Require the
    // actual live viewport projector rather than enumerating all Mana assets.
    std::set<UObject*> seen;
    for(const auto* type:{L"CPP_WBP_MovieScreen",L"CPP_WBP_Movie"}) for(auto* screen:find(type)) {
        if(!visible(screen)) continue;
        auto* projector=object(screen,L"Projector");
        if(!get_bool(screen,L"IsInViewport")&&!get_bool(projector,L"IsInViewport")) continue;
        auto* movie_player=object(screen,L"m_pManaPlayer");
        if(!movie_player||!seen.insert(movie_player).second) continue;
        auto* source=get_object(movie_player,L"GetMovieSource");
        if(!source) source=object(movie_player,L"ManaMovie");
        const auto ticks=getter<long long>(movie_player,L"GetMovieTime");
        const auto rate=getter<float>(movie_player,L"GetRate");
        if(!source||!ticks||!rate) continue;
        result.push_back({utf8(path(movie_player)),"movie:"+utf8(path(source)),static_cast<double>(*ticks)/10000000.,
            sampled,*rate,true,get_bool(movie_player,L"IsPlaying"),get_bool(movie_player,L"IsPaused"),foreground,get_bool(movie_player,L"IsSeeking")});
    }
    return result;
}
}
void UnrealReader::navigation_output(const NavigationUpdate& update,bool passive) {
    if(description_busy_||controller_start_pending_)
        beacon_.publish(NavigationMode::Off,{});
    else
        beacon_.publish(update.mode,update.beacon);
    if(update.mode!=NavigationMode::Automatic) {
        navigation_input_path_id_=0;
        navigation_start_wait_until_=0;
        navigation_motion_origin_.reset();
    }
    if(update.mode==NavigationMode::Off) {
        navigation_field_exit_.reset();
        navigation_path_={};navigation_path_sample_=0;navigation_ground_.reset();
        navigation_crossing_.reset();
        navigation_capsule_mesh_.clear();
        navigation_capsule_room_.reset();
        navigation_swept_mesh_.clear();navigation_bypass_rejoin_=0;
        controller_start_pending_=false;
        clear_controller_starter_grace();
    }
    const auto events=navigation_speech_for_output(update,passive);
    if(events.empty())return;
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool focused=foreground==GetCurrentProcessId();
    const bool defer_stop=update.mode==NavigationMode::Off&&
        (!focused||!speech_.ready()||(passive&&description_busy_));
    for(const auto& event:events) {
        if(defer_stop)navigation_speech_stop_pending_=event.text;
        else if(focused)speech_.say(event.text,event.interrupt);
    }
}
void UnrealReader::stop_navigation(std::string reason,bool passive) {
    auto update=navigation_.stop(tr(reason));
    navigation_path_={};navigation_path_sample_=0;navigation_path_reason_.clear();navigation_ground_.reset();navigation_ground_reason_.clear();navigation_controller_=false;
    navigation_activation_arrival_metres_.reset();
    navigation_room_door_.clear();
    navigation_input_path_id_=0;
    navigation_output(update,passive);
}
void UnrealReader::apply_story_update(const StoryUpdate& update) {
    if(navigation_.mode()==NavigationMode::Manual&&navigation_crossing_)return;
    if(!update.navigation_invalidated||!story_target(target_id_))return;
    const auto& snapshot=story_.snapshot();
    const auto destinations=story_destinations(snapshot);
    const auto selected=std::find_if(destinations.begin(),destinations.end(),[&](const auto& d){return story_target_id(snapshot,d)==target_id_;});
    const bool still_listed=selected!=destinations.end();
    // A read failure on another candidate cannot stop this still-authorized
    // destination. Actual objective/context/filtered-destination changes still do.
    if(!update.context_changed&&!update.objective_changed&&!update.destination_changed&&still_listed&&story_route_available(snapshot,*selected))return;
    auto stopped=story_walk_&&suspend_auto_walk_trip()?std::nullopt:
        stop_changed_story_navigation(update,true,navigation_);
    if(stopped) {
        if(still_listed&&!update.objective_changed&&!update.context_changed&&!update.destination_changed&&!selected->unavailable_reason.empty())
            for(auto& event:stopped->speech)event.text=trf("Story navigation stopped. {0}",{tr(selected->unavailable_reason)});
        else if(!update.context_changed&&!update.objective_changed&&(!snapshot.ready||snapshot.map_id<0||snapshot.destination_epoch.empty()))
            for(auto& event:stopped->speech)event.text=trf("Story navigation stopped. {0}",
                {tr(snapshot.unavailable_reason.empty()?"The current destination data is unavailable.":snapshot.unavailable_reason)});
        navigation_output(*stopped,true);
    }
    active_target_.reset();
    // Unknown native reads revoke coordinates and movement, while retaining only
    // the opaque deliberate selection until the source can authorize it again.
    if(!update.context_changed&&!update.objective_changed&&(!snapshot.ready||snapshot.map_id<0||snapshot.destination_epoch.empty()))return;
    if(!update.context_changed&&!update.objective_changed&&!update.destination_changed&&still_listed)return;
    target_id_.clear();target_type_.clear();
    for(auto& [key,selection]:navigation_selections_)selection.targets[story_category].clear();
}
resonance::unreal::UObject* UnrealReader::refresh_navigation_target(resonance::unreal::UObject* pawn) {
    active_target_.reset();
    navigation_target_reason_.clear();
    if(!valid(pawn)||target_id_.empty())return nullptr;
    const auto world=utf8(path(actor_world(pawn))),owner=utf8(path(pawn));
    if(story_target(target_id_)) {
        const auto& snapshot=story_.snapshot();
        if(!snapshot.ready||snapshot.map_id<0||snapshot.destination_epoch.empty()) {
            navigation_target_reason_=snapshot.unavailable_reason.empty()?"The current story destination could not be read.":snapshot.unavailable_reason;
            return nullptr;
        }
        if(snapshot.world!=world||snapshot.player!=owner)return nullptr;
        for(const auto& destination:story_destinations(snapshot))if(story_target_id(snapshot,destination)==target_id_) {
            if(!story_route_available(snapshot,destination)||!destination.actor_available||
               destination.actor_id.empty()||destination.actor_type.empty()) {
                navigation_target_reason_=destination.unavailable_reason;return nullptr;
            }
            for(auto* actor:find(destination.actor_type.c_str()))if(utf8(path(actor))==destination.actor_id) {
                if(actor_world(actor)!=actor_world(pawn))return nullptr;
                const bool opening=destination.id.find(":opening:")!=std::string::npos;
                std::optional<Vec3> leg_position;
                if(!destination.exit_source.empty()) {
                    auto* source=UObjectGlobals::StaticFindObject(destination.exit_source.c_str());
                    const auto route=capture_story_exit_route(pawn,source);
                    if(!route||route->actor!=actor||route->route_key!=destination.exit_route_key) {
                        navigation_target_reason_="The selected Story exit has changed. Choose the current destination.";
                        return nullptr;
                    }
                    leg_position=route->position;
                } else if(destination.castle_leg) {
                    const auto leg=capture_castle_leg(pawn);
                    if(!leg||leg->actor!=actor) {
                        navigation_target_reason_="The current castle doorway or passage has changed.";
                        return nullptr;
                    }
                    leg_position=leg->position;
                } else if(destination.mitra_leg) {
                    const auto leg=capture_mitra_leg(pawn);
                    if(!leg||leg->actor!=actor) {
                        navigation_target_reason_="The current Mitra passage has changed.";
                        return nullptr;
                    }
                    leg_position=leg->position;
                } else if(destination.transition_map) {
                    const auto section=capture_walking_section(pawn);
                    if(!section||section->id!=0) {
                        navigation_target_reason_="The current Battlefield walking section could not be verified.";
                        return nullptr;
                    }
                    if(active_section_exit(pawn,*section,*destination.transition_map,navigation_target_reason_)!=actor) {
                        if(navigation_target_reason_.empty())
                            navigation_target_reason_="The selected Battlefield exit has changed.";
                        return nullptr;
                    }
                } else if(opening||destination.event_trigger) {
                    const auto state=getter<unsigned char>(actor,L"GetGimmickCondition");
                    const auto collision=getter<bool>(actor,L"GetActorEnableCollision");
                    if(!state||!collision){navigation_target_reason_="The story trigger's current state could not be read.";return nullptr;}
                    if(*state!=0||!*collision)return nullptr;
                } else if(!story_actor_available(pawn,actor,destination.exit)) {
                    navigation_target_reason_=area_actor_unavailable_reason();return nullptr;
                }
                const bool story_step=destination.id.find("|step:")!=std::string::npos;
                const auto point=leg_position?leg_position:story_step?destination.position:tracked_actor_position(actor);
                if(!point||!finite(*point)){navigation_target_reason_="The story target's position could not be read.";return nullptr;}
                active_target_=KnownTarget{target_id_,world,owner,destination.label,destination.actor_type,*point};
                return actor;
            }
            return nullptr;
        }
        return nullptr;
    }
    stage="refresh area target";
    const auto current=capture_area_target(pawn,target_id_);
    const auto summary=last_area_capture_summary();
    if(summary!=area_refresh_status_){area_refresh_status_=summary;log("NAV area refresh "+summary);}
    const auto report=area_capture_report();
    navigation_target_reason_=!report.unavailable_reason.empty()?report.unavailable_reason:
        report.unreadable[0]?report.first_issue[0]:std::string{};
    if(!current)return nullptr;
    target_type_=current->type;
    active_target_=KnownTarget{current->snapshot.id,world,owner,current->snapshot.label,current->type,current->position};
    return current->actor;
}
bool nearby_navigation_target(UObject* pawn,UObject* target) {
    if(!valid(pawn)||!valid(target))return false;
    const auto units=array<UObject*>(pawn,L"m_InteractMapUnitList");
    const auto objects=array<UObject*>(pawn,L"m_pInteractMapGimmickList");
    return std::find(units.begin(),units.end(),target)!=units.end()||
        std::find(objects.begin(),objects.end(),target)!=objects.end();
}
namespace {
struct RowingTarget {
    ActivationBox box;
    bool can_disembark{};
};
std::optional<RowingTarget> rowing_target(UObject* pawn,const chapter_three::Crossing& crossing) {
    auto* world=actor_world(pawn);
    UObject* actor=nullptr;
    for(auto* item:find(crossing.landing?L"CPP_MapGimmick_Raft_GetOff":L"CPP_MapTransitionTrigger")) {
        if(actor_world(item)!=world||read<int>(item,L"m_UniqueId")!=crossing.actor)continue;
        if(actor)return {};
        actor=item;
    }
    if(!actor)return {};
    if(!crossing.landing) {
        const auto exit=capture_story_exit_route(pawn,actor);
        const auto box=capture_activation_box(actor);
        if(!exit||!box||exit->actor!=actor||exit->event_only||exit->destination_map!=crossing.map||
            exit->destination_point!=crossing.entry||read<unsigned char>(actor,L"m_IsAutoTransition")!=1)return {};
        return RowingTarget{*box};
    }
    UObject* component=nullptr;
    const auto name=L"."+std::wstring(crossing.landing);
    auto components=array<UObject*>(actor,L"m_GetOffBoxComponentList");
    components.push_back(object(actor,L"m_GetOffAreaBoxComponent"));
    components.push_back(object(actor,L"m_GetOffAreaBoxComponent2"));
    for(auto* item:components) {
        if(!valid(item)||!is(item,L"/Script/mucha.CPP_MapGimmick_Raft_GetOff_BoxComponent")||
            !path(item).ends_with(name)||get_object(item,L"GetOwner")!=actor)continue;
        if(component&&component!=item)return {};
        component=item;
    }
    if(!component)return {};
    const auto collision=getter<unsigned char>(component,L"GetCollisionEnabled");
    const auto center=getter<Vec3>(component,L"K2_GetComponentLocation");
    const auto extent=getter<Vec3>(component,L"GetScaledBoxExtent");
    const auto rotation=getter<Vec3>(component,L"K2_GetComponentRotation");
    if(!collision||(*collision!=1&&*collision!=3&&*collision!=5)||!center||!extent||!rotation||
        !finite(*rotation)||std::abs(rotation->x)>.05||std::abs(rotation->z)>.05||
        std::hypot(center->x-crossing.goal.x,center->y-crossing.goal.y)>125.)return {};
    const ActivationBox box{*center,*extent,rotation->y};
    if(!closest_activation_point(box,*center))return {};
    bool overlapping=false;
    if(auto* function=component->GetFunctionByNameInChain(L"IsOverlappingActor");
       path(function)==L"/Script/Engine.PrimitiveComponent:IsOverlappingActor"&&
       native_struct_shape(function,{{L"Other",0,8},{L"ReturnValue",8,1}})) {
        Call overlap(component,L"IsOverlappingActor",16,8);
        if(overlap) {
            overlap.put(0,pawn);
            overlap.invoke(component);
            overlapping=overlap.get<unsigned char>(8)==1;
        }
    }
    return RowingTarget{box,overlapping&&nearby_navigation_target(pawn,actor)};
}
bool rowing_lane(const CapsuleSweep& sweep,Vec3 from,Vec3 to) {
    const auto hit=sweep.trace(from,to);
    if(!hit||hit->blocking)return false;
    // Match the live raft's support plane. This excludes an open sightline
    // over an unfilled pool, a dry terrace, or another water level.
    const auto length=std::hypot(to.x-from.x,to.y-from.y);
    const auto steps=std::max(1,static_cast<int>(std::ceil(length/85.)));
    for(int i=1;i<=steps;++i) {
        const auto fraction=static_cast<double>(i)/steps;
        const auto x=from.x+(to.x-from.x)*fraction,y=from.y+(to.y-from.y)*fraction;
        const auto floor=sweep.trace({x,y,from.z+8.},{x,y,from.z-12.});
        if(!floor||!floor->blocking||floor->penetrating||floor->normal.z<.8||
            std::abs(floor->location.z-sweep.half_height+2.15-from.z)>10.)return false;
    }
    return true;
}
}
NavigationFrame UnrealReader::rowing_frame(UObject* pawn,NavigationFrame frame,bool refresh) {
    frame.field_eligible=!frame.world.empty();
    frame.target_available=false;
    const auto& story=story_.snapshot();
    if(!story.ready||story.world!=frame.world||story.player!=frame.player||!story.crossing) {
        frame.target_unavailable_reason="The current Story crossing is unavailable.";
        return frame;
    }
    const auto crossing=chapter_three::crossing(*story.crossing);
    if(!crossing||story.map_id!=crossing->map)return frame;
    frame.target="rowing:"+std::to_string(static_cast<unsigned>(*story.crossing));
    frame.target_label=tr(crossing->label);
    frame.arrival_message=trf(crossing->landing?"Near {0}. Row toward the landing until the interaction appears.":
        "At {0}. Row through it to continue.",{frame.target_label});
    const auto hull=getter<Vec3>(pawn,L"K2_GetActorLocation");
    const auto half=getter<float>(object(pawn,L"CapsuleComponent"),L"GetScaledCapsuleHalfHeight");
    if(!hull||!finite(*hull)||!half||!std::isfinite(*half)||*half<=0) {
        frame.target_unavailable_reason="The raft's current position could not be read.";
        return frame;
    }
    frame.position={hull->x,hull->y,hull->z-*half};
    if(!crossing->water.contains(frame.position)) {
        frame.target_unavailable_reason="The raft is outside the current Story crossing.";
        return frame;
    }
    frame.target_available=true;
    const bool stale=frame.sample_seconds-navigation_path_sample_>=.25;
    const bool refresh_path=refresh||stale||navigation_crossing_!=story.crossing;
    const bool near_landing=crossing->landing&&
        std::hypot(frame.position.x-crossing->goal.x,frame.position.y-crossing->goal.y)<100.;
    const auto target=refresh_path||near_landing?rowing_target(pawn,*crossing):std::nullopt;
    if(target&&target->can_disembark) {
        frame.activation_reached=true;
        frame.arrival_message=trf("At {0}. Use the game's interaction control to disembark.",{frame.target_label});
        return frame;
    }
    if(refresh_path) {
        const auto sweep=capture_capsule_sweep(pawn,true);
        const auto spatial=story_spatial_context(pawn);
        if(!target||!sweep||!spatial) {
            frame.route_unavailable_reason=!target?"The native landing or passage is unavailable.":
                !sweep?"The raft's collision could not be verified.":"The raft's steering directions could not be read.";
            navigation_path_={};navigation_path_sample_=frame.sample_seconds;
            navigation_path_reason_=frame.route_unavailable_reason;
            log("NAV rowing "+frame.target+" "+frame.route_unavailable_reason);
            return frame;
        }
        const auto clear=[&](Vec3 from,Vec3 to) {return rowing_lane(*sweep,from,to);};
        const Vec3 goal{crossing->goal.x,crossing->goal.y,frame.position.z};
        const auto approach=activation_approach(target->box,{sweep->radius,sweep->half_height},goal,spatial->world_to_metres);
        const auto leg=navigation_.leg();
        bool reuse=false;
        if(approach&&!refresh&&navigation_crossing_==story.crossing&&navigation_path_.valid&&leg) {
            auto end=leg->second;end.z=frame.position.z;
            const auto length=std::hypot(end.x-frame.position.x,end.y-frame.position.y);
            if(length>0) {
                const auto fraction=std::min(1.,135./length);
                end.x=frame.position.x+(end.x-frame.position.x)*fraction;
                end.y=frame.position.y+(end.y-frame.position.y)*fraction;
                reuse=crossing->water.contains(end)&&clear(frame.position,end);
            }
        }
        if(!reuse) {
            auto points=approach?chapter_three::rowing_path(frame.position,goal,*crossing,clear):std::vector<Vec3>{};
            navigation_path_={std::move(points),spatial->right,spatial->up,spatial->world_to_metres,true,false};
            navigation_path_.valid=navigation_path_.points.size()>=2;
            navigation_crossing_=story.crossing;
            if(++navigation_path_id_==0)++navigation_path_id_;
            navigation_path_reason_=navigation_path_.valid?std::string{}:"A clear water route to this Story destination could not be verified.";
            log("NAV rowing "+frame.target+" points="+std::to_string(navigation_path_.points.size())+
                " from="+coordinates(frame.position)+" goal="+coordinates(goal)+" reason="+navigation_path_reason_);
            if(refresh&&navigation_path_.valid) {
                std::string turns;
                for(const auto& point:navigation_path_.points)turns+=" "+coordinates(point);
                log("NAV rowing waypoints"+turns);
            }
        } else {
            navigation_path_.right=spatial->right;navigation_path_.up=spatial->up;
        }
        if(approach)navigation_activation_arrival_metres_=crossing->landing?
            std::min(.08,approach->arrival_metres):approach->arrival_metres;
        navigation_path_sample_=frame.sample_seconds=static_cast<double>(GetTickCount64())/1000.;
    }
    frame.path=navigation_path_;frame.path_id=navigation_path_id_;
    frame.path_sample_seconds=navigation_path_sample_;
    frame.route_unavailable_reason=navigation_path_reason_;
    frame.arrival_metres=navigation_activation_arrival_metres_.value_or(.15);
    return frame;
}
NavigationFrame UnrealReader::flight_frame(UObject* pawn,NavigationFrame frame,bool refresh) {
    const auto& story=story_.snapshot();
    frame.field_eligible=!frame.world.empty();

    frame.target_available=story.ready&&story.field_control&&story.flight_destination&&
        story.world==frame.world&&story.player==frame.player;
    if(!frame.target_available) {
        frame.target_unavailable_reason="The current Story flight destination could not be verified.";
        return frame;
    }
    const auto& destination=*story.flight_destination;
    frame.target=destination.id;
    frame.target_label=destination.label;
    const auto spatial=story_spatial_context(pawn);
    if(!spatial) {
        frame.route_unavailable_reason="The airship's steering directions could not be read.";
        return frame;
    }
    frame.position=spatial->origin;
    const Vec3 goal{destination.position.x,destination.position.y,frame.position.z};
    frame.path={{frame.position,goal},spatial->right,spatial->up,spatial->world_to_metres,true,false};
    frame.path_id=1;
    frame.path_sample_seconds=frame.sample_seconds;
    frame.arrival_message=destination.arrival_message;
    if(refresh)log("NAV flight "+frame.target+" from="+coordinates(frame.position)+" goal="+coordinates(goal));
    return frame;
}

NavigationFrame UnrealReader::navigation_frame(resonance::unreal::UObject* pawn,bool refresh,bool eligible,bool foreground,bool physical_input,bool automatic) {
    NavigationFrame frame;
    frame.use_beacon=beacon_.ready()&&audio_settings_.beacon_volume>0;
    frame.sample_seconds=static_cast<double>(GetTickCount64())/1000.;
    frame.foreground=foreground;frame.manual_input=physical_input;
    if(!eligible||!valid(pawn))return frame;
    if(!get_bool(pawn,L"IsPlayerControlled")||!get_bool(pawn,L"IsLocallyControlled"))return frame;
    frame.world=utf8(path(actor_world(pawn)));frame.player=utf8(path(pawn));
    if(!automatic&&native_rowing(pawn))return rowing_frame(pawn,std::move(frame),refresh);
    if(!automatic&&native_airship(pawn))return flight_frame(pawn,std::move(frame),refresh);
    if(!ordinary_walking(pawn))return frame;
    frame.field_eligible=!frame.world.empty();
    frame.target=target_id_;frame.target_available=false;
    const auto position=controlled_agent_position(pawn);
    if(!position||!finite(*position)){frame.target_unavailable_reason="The player position could not be read.";return frame;}
    frame.position=*position;
    auto* live_target=refresh_navigation_target(pawn);
    frame.target_unavailable_reason=navigation_target_reason_;
    if(!active_target_||active_target_->id!=target_id_||active_target_->world!=frame.world||
       active_target_->player!=frame.player)return frame;
    frame.target_label=active_target_->label;frame.target_available=live_target!=nullptr;
    if(!frame.target_available)return frame;
    if(!refresh&&!navigation_room_door_.empty()&&
       (!story_target(target_id_)||utf8(path(live_target))!=navigation_room_door_)) {
        const auto leg=capture_castle_leg(pawn);
        if(!leg||utf8(path(leg->actor))!=navigation_room_door_) {
            if(story_target(target_id_)) {
                frame.target_available=false;
                frame.target_unavailable_reason="The current castle doorway has changed. Request the route again.";
                return frame;
            }
            refresh=true;
        }
    }
    const bool shrine_bridge=is_story_shrine_bridge_goal(live_target,active_target_->position);
    const bool shrine_crate=shrine_crate_push_goal(live_target,active_target_->position);
    frame.interaction_available=!shrine_bridge&&nearby_navigation_target(pawn,live_target)&&navigation_room_door_.empty();
    if(frame.interaction_available)return frame;
    bool aligned_with_activation=false;
    if(navigation_activation_arrival_metres_&&!navigation_path_.points.empty()) {
        const auto& end=navigation_path_.points.back();
        const auto scale=navigation_path_.world_to_metres;
        aligned_with_activation=std::hypot(end.x-position->x,end.y-position->y)<1e-8*scale&&
            std::abs(end.z-position->z)<=.5*scale;
    }
    const double refresh_interval=navigation_path_.valid?.25:.5;
    const bool stale=frame.sample_seconds-navigation_path_sample_>=refresh_interval;
    bool renewed=false;
    if(stale&&!refresh&&!aligned_with_activation&&frame.target_available&&
       (!navigation_capsule_mesh_.empty()||navigation_capsule_room_)&&
       navigation_path_.valid&&(!automatic||navigation_ground_)) {
        const auto scale=navigation_path_.world_to_metres;
        const auto goal=active_target_->position;
        const auto leg=navigation_.leg();
        if(leg&&std::hypot(goal.x-navigation_route_goal_.x,goal.y-navigation_route_goal_.y,
                           goal.z-navigation_route_goal_.z)<=scale*.05) {
            bool endpoint_available=true;
            if(shrine_bridge)endpoint_available=capture_story_shrine_bridge(pawn,live_target,goal).has_value();
            const bool exit=is(live_target,L"/Script/mucha.CPP_MapTransitionTrigger");
            const bool event_trigger=olderion_arsha_trigger(live_target)||wardens_cape_trigger(live_target);
            if(navigation_capsule_room_&&(exit||event_trigger||is(live_target,L"/Script/mucha.CPP_MapUnit_NPC")||
               is(live_target,L"/Script/mucha.CPP_MapGimmick_SavePoint")||requires_checked_interaction_box(live_target))) {
                const auto box=exit||event_trigger?capture_activation_box(live_target):capture_interaction_box(live_target,pawn);
                auto* component=object(pawn,L"CapsuleComponent");
                const auto radius=getter<float>(component,L"GetScaledCapsuleRadius");
                const auto height=getter<float>(component,L"GetScaledCapsuleHalfHeight");
                const auto approach=box&&radius&&height?
                    activation_approach(*box,{*radius,*height},navigation_path_.points.back(),scale):std::nullopt;
                endpoint_available=approach.has_value();
                if(approach)navigation_activation_arrival_metres_=approach->arrival_metres;
            }
            const auto axes=endpoint_available?walking_input_axes(pawn):std::nullopt;
            if(axes&&renew_capsule_lane(pawn,navigation_capsule_mesh_,*position,*leg,scale,navigation_capsule_room_)) {
                renewed=true;
                navigation_path_.right=axes->right;navigation_path_.up=axes->up;
                frame.sample_seconds=static_cast<double>(GetTickCount64())/1000.;
                navigation_path_sample_=frame.sample_seconds;
            }
        }
    }
    if(refresh||aligned_with_activation||(stale&&!renewed)) {
        // Follow the active actor's current position across the area. Automatic
        // walking additionally needs conservative ordinary-ground proof.
        const auto previous_path_id=navigation_path_id_++;
        std::optional<NativeRoute> current;
        const auto goal=active_target_->position;
        const auto scale=navigation_path_.world_to_metres;
        const bool retain_bypass=!refresh&&!aligned_with_activation&&automatic&&navigation_bypass_rejoin_&&
            !navigation_swept_mesh_.empty()&&std::hypot(goal.x-navigation_route_goal_.x,
                goal.y-navigation_route_goal_.y,goal.z-navigation_route_goal_.z)<=scale*.05;
        if(retain_bypass&&mozul_world_entrance(live_target)) {
            // The road detour still leads to the baked point behind the frame.
            // Replace it once the approach from the current feet is clear.
            if(const auto axes=walking_input_axes(pawn))
                current=mozul_station_approach(pawn,live_target,*position,*axes,scale,true,navigation_path_id_);
        }
        if(retain_bypass&&earth_shrine_entrance(live_target))
            if(const auto axes=walking_input_axes(pawn))
                current=earth_shrine_door_approach(pawn,live_target,*position,*axes,scale,true,navigation_path_id_);
        if(retain_bypass&&!current) {
            // The native solver can still cut the same corner after a detour
            // was found. Recheck that detour from the live feet until rejoin.
            const auto* previous_ground=navigation_ground_?&*navigation_ground_:nullptr;
            auto remaining=navigation_.remaining_bypass(navigation_path_,previous_path_id,*position,navigation_bypass_rejoin_,
                true,previous_ground);
            auto* library=remaining?nav_library(pawn):nullptr;
            auto* mesh=library?swept_walking_mesh(pawn,library,*position,navigation_swept_mesh_):nullptr;
            if(mesh) {
                const auto axes=walking_input_axes(pawn);
                const auto policy=capture_ground_policy(pawn,scale);
                if(axes&&policy) {
                    const auto check=[&](WalkingBypass bypass)->std::optional<NativeRoute> {
                        auto& snapshot=bypass.path;
                        snapshot.right=axes->right;snapshot.up=axes->up;
                        std::string reason;
                        auto* world=actor_world(pawn);
                        const auto proof=swept_default_mesh(world,navigation_swept_mesh_)||crystal_tower_spiral(world)||
                            zoldaad_swept_walkway(world)?
                            GroundProof::CapsuleSweep:GroundProof::MeshFirst;
                        const auto ground=ordinary_ground_path(pawn,library,mesh,snapshot,navigation_path_id_,
                            policy->budget,reason,bypass.rejoin,proof);
                        if(!ground)return {};
                        return NativeRoute{std::move(snapshot),ground,{},navigation_activation_arrival_metres_,
                            false,navigation_room_door_,{},navigation_swept_mesh_,bypass.rejoin,navigation_capsule_room_};
                    };
                    const auto trimmed_count=remaining->path.points.size();
                    current=check(std::move(*remaining));
                    if(!current) {
                        // The outgoing chord still clips the corner. Keep
                        // steering toward the pivot if its lane is clear.
                        auto approach=navigation_.remaining_bypass(navigation_path_,previous_path_id,*position,
                            navigation_bypass_rejoin_,false);
                        if(approach&&approach->path.points.size()>trimmed_count)current=check(std::move(*approach));
                    }
                }
            }
        }
        if(current)navigation_path_reason_.clear();
        else {
            current=route_snapshot(pawn,live_target,goal,automatic,&navigation_path_reason_,navigation_path_id_);
            if(!current&&navigation_path_reason_.empty())
                navigation_path_reason_="A complete native walking route is unavailable.";
            navigation_route_goal_=goal;
        }
        // The synchronous query holds the game thread, so stamp the completed
        // proof here. A slow check must not force another replan before a step.
        frame.sample_seconds=static_cast<double>(GetTickCount64())/1000.;
        navigation_activation_arrival_metres_=current?current->activation_arrival_metres:std::nullopt;
        navigation_room_door_=current?std::move(current->room_door):std::string{};
        navigation_capsule_mesh_=current?std::move(current->capsule_mesh):std::wstring{};
        navigation_capsule_room_=current?std::move(current->capsule_room):std::nullopt;
        navigation_swept_mesh_=current?std::move(current->swept_mesh):std::wstring{};
        navigation_bypass_rejoin_=current?current->bypass_rejoin:0;
        navigation_path_={};navigation_ground_.reset();navigation_ground_reason_.clear();navigation_path_sample_=frame.sample_seconds;
        frame.activation_reached=current&&current->activation_reached;
        if(current){navigation_path_=std::move(current->snapshot);navigation_ground_=current->ground;navigation_ground_reason_=std::move(current->ground_reason);}
        log("NAV replan points="+std::to_string(navigation_path_.points.size())+" ordinary_ground="+
            std::to_string(navigation_ground_.has_value())+" path_id="+std::to_string(navigation_path_id_)+
            " from="+coordinates(frame.position)+" target="+coordinates(active_target_->position)+
            " reason="+navigation_path_reason_+" ground_reason="+navigation_ground_reason_);
    }
    if(!navigation_room_door_.empty()) {
        frame.target_label=tr("Door to the hall");
        frame.arrival_message=tr(hall_door_arrival);
        frame.interaction_available=false;
    }
    if(navigation_room_door_.empty()&&(!navigation_capsule_mesh_.empty()||navigation_capsule_room_||
       is(live_target,L"/Script/mucha.CPP_MapUnit_NPC")))
        frame.arrival_message=trf("Reached the verified walking point near {0}.",{frame.target_label});
    if(navigation_activation_arrival_metres_&&is(live_target,L"/Script/mucha.CPP_MapGimmick_Object_Raft"))
        frame.arrival_message=trf("At {0}. Use the game's interaction control to board.",{frame.target_label});
    if(is(live_target,L"/Script/mucha.CPP_MapGimmickLadder")&&
       (story_target(target_id_)||sanctissimus_pot_ladder(live_target,active_target_->position))) {
        const auto axes=navigation_path_.valid?std::optional<WalkingAxes>{{navigation_path_.right,navigation_path_.up}}:
            walking_input_axes(pawn);
        auto instruction=live_ladder_instruction(live_target,active_target_->position,axes);
        if(sanctissimus_pot_ladder(live_target,active_target_->position))
            if(const auto box=capture_ladder_bottom_box(live_target))
                instruction=live_ladder_instruction(live_target,box->center,axes);
        if(const auto ladder=waterfall_ladder_id(live_target);ladder&&(*ladder==0||*ladder==2))
            instruction=ladder_instruction(false,{1,0,0},axes);
        if(instruction)frame.arrival_message=trf("At {0}. {1}",{frame.target_label,*instruction});
    }
    if(shrine_crate) {
        const auto direction=cardinal_motion({-1,0,0},walking_input_axes(pawn));
        frame.arrival_message=direction.empty()?tr("At the push side of the loose crate. Walk into it toward the row."):
            trf("At the push side of the loose crate. Walk {0} to push it toward the row.",{tr(direction)});
    }
    if(dilmagia_shipyard_gate(live_target))
        frame.arrival_message=tr("At the Shipyard gate. Continue up the road through the sliding door.");
    if(zoldaad_factory_hall_gate(live_target))
        frame.arrival_message=tr("At the factory's sliding door. Continue through it into the bridge hall.");
    if(const bool entering=zoldaad_captain_approach(live_target);entering||zoldaad_captain_exit(live_target)) {
        const auto direction=cardinal_motion({entering?1.:-1.,0,0},walking_input_axes(pawn));
        const auto instruction=direction.empty()?tr("Continue through the sliding door."):
            trf("Walk {0} through the sliding door.",{tr(direction)});
        frame.arrival_message=tr(entering?"At the captain's passage.":"At the sliding door.")+" "+instruction;
    }
    if((frame.activation_reached||((navigation_capsule_room_||olderion_world_entrance(live_target))&&
       navigation_activation_arrival_metres_))&&
       is(live_target,L"/Script/mucha.CPP_MapTransitionTrigger")&&
       read<unsigned char>(live_target,L"m_IsAutoTransition")==0)
        frame.arrival_message=trf("At {0}. Use the game's interaction control to continue.",{frame.target_label});
    if(frame.activation_reached)return frame;
    frame.path=navigation_path_;frame.path_sample_seconds=navigation_path_sample_;
    frame.arrival_metres=navigation_activation_arrival_metres_.value_or(.5);
    frame.path_id=navigation_path_id_;frame.ground=navigation_ground_;frame.walking_path=navigation_ground_.has_value();
    frame.bypass_rejoin=navigation_bypass_rejoin_;
    frame.check_route_turns=automatic&&((navigation_capsule_room_&&(world_map(actor_world(pawn))||
        sanctissimus_pot_ladder(live_target,active_target_->position)||mobliz_world(pawn)||waterfall_world(pawn)||
        shrine_bridge||shrine_crate||shrine_water_switch(live_target,54)||shrine_water_switch(live_target,53)||
        shrine_water_switch(live_target,52)||shrine_basin_boat(live_target)||
        shrine_tower_ladder_base(live_target,active_target_->position)||
        shrine_lower_ladder_top(live_target,active_target_->position)||
        olderion_tree_local(live_target)||olderion_bridge_soldier(live_target)||olderion_arsha_trigger(live_target)||
        olderion_harbor_check(live_target)||zoldaad_dungeon_castle_exit(live_target)||
        zoldaad_factory_maze_pedestal(live_target)||zoldaad_factory_upper_ladder(live_target,active_target_->position)||
        zoldaad_factory_west_ladder(live_target,active_target_->position)||zoldaad_factory_west_pedestal(live_target,38)||
        zoldaad_factory_west_pedestal(live_target,52)||
        amore_south_exit(live_target)||gaberada_elder_door_open(live_target)==false||
        path(live_target).starts_with(L"/Game/Map/Dng/08Gro/08Gro_40/GD/")||
        wardens_cape_trigger(live_target)||water_shrine_bridge_exit(live_target)||water_shrine_dry_passage(live_target)))||
        swept_default_mesh(actor_world(pawn),navigation_swept_mesh_)||
        ((crystal_tower_spiral(actor_world(pawn))||tower_chamber_return(live_target))&&
            !navigation_swept_mesh_.empty()));
    frame.ground_unavailable_reason=navigation_ground_reason_;
    if(automatic&&frame.path.valid) {
        const auto policy=capture_ground_policy(pawn,frame.path.world_to_metres);
        if(policy)frame.ground_stop_margin_metres=policy->stop_margin_metres;
        else {frame.walking_path=false;frame.ground_unavailable_reason="The character's walking and braking state could not be read.";}
    }
    frame.route_unavailable_reason=navigation_path_reason_;
    return frame;
}
bool UnrealReader::recheck_floor_turn(UObject* pawn,NavigationFrame& frame,std::size_t next) {
    const bool room=navigation_capsule_room_&&frame.check_route_turns;
    if(next<2||next>=frame.path.points.size()||(!room&&navigation_swept_mesh_.empty()))return false;
    const auto policy=capture_ground_policy(pawn,frame.path.world_to_metres);
    if(!policy)return false;
    auto turn=frame.path;
    turn.points.erase(turn.points.begin(),turn.points.begin()+(next-1));
    turn.points.front()=frame.position;
    const auto next_path_id=navigation_path_id_+1;
    auto bypass_rejoin=frame.bypass_rejoin>=next?frame.bypass_rejoin-next+1:0;
    std::optional<GroundCoverage> checked;
    if(room) {
        // Both original legs can be walkable while the shortcut between
        // them clips a cabinet. Keep the current corner until the real
        // pawn's outgoing chord has floor support and capsule clearance.
        const auto sweep=capture_capsule_sweep(pawn);
        const auto floor=sweep?room_lane(*sweep,*navigation_capsule_room_,frame.position,turn.points[1],
            turn.world_to_metres):std::nullopt;
        if(!floor||std::abs(floor->z-turn.points[1].z)>turn.world_to_metres*.12)return false;
        const RouteLocation end{turn.points.size()-1,1};
        if(const auto length=route_arc_metres(turn,end))
            checked=GroundCoverage{.path_id=next_path_id,.end=end,.end_point=turn.points.back(),
                .metres=*length,.reaches_end=true,.follow_surface=true};
    } else {
        auto* library=nav_library(pawn);
        auto* mesh=library?swept_walking_mesh(pawn,library,frame.position,navigation_swept_mesh_):nullptr;
        if(!mesh)return false;
        std::string reason;
        checked=ordinary_ground_path(pawn,library,mesh,turn,next_path_id,policy->budget,reason,bypass_rejoin,GroundProof::SweptTurn);
    }
    if(!checked)return false;
    const auto clearance=horizontal_arc_metres(turn,checked->end);
    if(!clearance||(!checked->reaches_end&&*clearance<=policy->stop_margin_metres))return false;
    frame.sample_seconds=frame.path_sample_seconds=navigation_path_sample_=static_cast<double>(GetTickCount64())/1000.;
    frame.path=std::move(turn);navigation_path_=frame.path;
    frame.path_id=navigation_path_id_=next_path_id;
    frame.ground=navigation_ground_=checked;
    frame.ground_stop_margin_metres=policy->stop_margin_metres;
    frame.bypass_rejoin=navigation_bypass_rejoin_=bypass_rejoin;
    log("NAV swept turn verified from="+coordinates(frame.position)+" waypoint="+coordinates(frame.path.points[1])+
        " path_id="+std::to_string(frame.path_id));
    return true;
}
unsigned UnrealReader::controller_commands() {
    game_thread_=GetCurrentThreadId();
    retry_controller_input();
    const auto status=controller_backend_status();
    if(!status.ready||status.connected!=1) {
        battle_pad_edges_.observe(0,false);
        controller_unavailable_reason_=!status.ready?"Controller input capture is unavailable.":
            status.connected==0?"No active controller was detected.":"More than one controller is active.";
        publish_controller_permission(false,controller_epoch_);controller_modifier_seen_=status.modifier_down;
        publish_battle_controller_permission(false,battle_review_epoch_);
        return 0;
    }
    DWORD foreground{};
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool focused=foreground==GetCurrentProcessId();
    const auto pressed=battle_pad_edges_.observe(status.buttons,focused);
    auto* top=focused&&battle_commands_available()&&speech_.ready()?top_widget():nullptr;
    if(top&&top==battle_results_layout_) {
        publish_controller_permission(false,controller_epoch_);
        publish_battle_controller_permission(false,battle_review_epoch_);
        controller_modifier_seen_=status.modifier_down;
        if(pressed&ControllerDpadUp)return ResultUp|ControllerOrigin;
        if(pressed&ControllerDpadDown)return ResultDown|ControllerOrigin;
        return 0;
    }
    if(top&&top==battle_control_layout_) {
        const bool review=battle_review_layout_==top;
        const auto now=GetTickCount64();
        if(review&&now>=battle_binding_checked_) {
            battle_binding_checked_=now+500;
            battle_controller_binding_=battle_review_binding_available(L"Gamepad_LeftThumbstick");
        }
        publish_controller_permission(false,controller_epoch_);
        controller_modifier_seen_=status.modifier_down;
        publish_battle_controller_permission(review&&battle_controller_binding_,battle_review_epoch_);
        unsigned request{};
        for(const auto action:take_battle_controller_actions(battle_review_epoch_)) {
            switch(action) {
            case BattleControllerAction::Opened: request=BattlePanelOpen;break;
            case BattleControllerAction::Closed: request=BattlePanelClose;break;
            case BattleControllerAction::Previous: request=PreviousCategory;break;
            case BattleControllerAction::Next: request=NextCategory;break;
            case BattleControllerAction::Vitals: request=BattleVitals;break;
            case BattleControllerAction::Effects: request=BattleEffects;break;
            case BattleControllerAction::Gauges: request=BattleGauges;break;
            case BattleControllerAction::None: break;
            }
        }
        if(pressed)request|=BattleInputKey;
        if(pressed&ControllerR3)request|=BattleInterrupt;
        return request?request|ControllerOrigin:0;
    }
    publish_battle_controller_permission(false,battle_review_epoch_);
    stage="controller native field permission";
    const auto guard=capture_navigation_guard(true);
    const auto context=guard.pawn?utf8(path(actor_world(guard.pawn)))+"|"+utf8(path(guard.pawn)):std::string{};
    if(context!=controller_context_) {controller_context_=context;++controller_epoch_;}
    const auto& story=story_.snapshot();
    bool crossing_ready=!guard.rowing;
    if(guard.rowing&&story.ready&&story.crossing&&
        story.world==utf8(path(actor_world(guard.pawn)))&&story.player==utf8(path(guard.pawn))) {
        const auto crossing=chapter_three::crossing(*story.crossing);
        const auto hull=getter<Vec3>(guard.pawn,L"K2_GetActorLocation");
        crossing_ready=crossing&&hull&&crossing->water.contains(*hull);
    }
    if(guard.flying)crossing_ready=story.ready&&story.field_control&&story.flight_destination&&
        controller_context_==story.world+"|"+story.player;
    const bool permitted=guard.eligible&&guard.controller_allowed&&speech_.ready()&&crossing_ready;
    controller_unavailable_reason_=!guard.eligible?guard.reason:!guard.controller_allowed?
        "The modifier has another game binding or its binding could not be read.":!crossing_ready?
        "No current Story crossing could be verified.":!speech_.ready()?"Speech output is unavailable.":"";
    const bool reserve_field_rb=guard.foreground&&guard.field_surface&&guard.controller_allowed&&speech_.ready()&&
        crossing_ready&&(guard.walking||guard.rowing||guard.flying);
    publish_controller_permission(permitted,controller_epoch_,reserve_field_rb);
    const auto name=controller_modifier()==ControllerModifier::RightShoulder?"RB":
        controller_modifier()==ControllerModifier::RightTrigger?"RT":"LT";
    const auto gate="field="+std::to_string(guard.eligible)+" modifier="+name+" available="+std::to_string(guard.controller_allowed)+" reason="+guard.reason;
    if(gate!=controller_gate_status_){controller_gate_status_=gate;log("CONTROLLER gate "+gate);}
    if(status.modifier_down&&!controller_modifier_seen_&&guard.eligible&&!guard.controller_allowed&&speech_.ready())
        speech_.say(trf("{0} navigation unavailable. Its game binding is assigned to another control or could not be read.",{name}));
    controller_modifier_seen_=status.modifier_down;
    const auto actions=take_controller_actions(controller_epoch_);
    if(!permitted)return 0;
    if(actions.size()>1&&std::find(actions.begin(),actions.end(),ControllerAction::Stop)==actions.end()) {
        speech_.say(tr("Navigation commands arrived together. Release the modifier, then press one command at a time."));return 0;
    }
    unsigned command=0;
    for(const auto action:actions) {
        switch(action) {
        case ControllerAction::Opened:command=ReviewNavigation;break;
        case ControllerAction::PreviousCategory:command=PreviousCategory;break;
        case ControllerAction::NextCategory:command=NextCategory;break;
        case ControllerAction::PreviousTarget:command=Previous;break;
        case ControllerAction::NextTarget:command=Next;break;
        case ControllerAction::Guidance:command=ToggleGuidance;break;
        case ControllerAction::Automatic:command=ToggleAutomatic;break;
        case ControllerAction::Stop:command=StopNavigation;break;
        case ControllerAction::Help:command=Help;break;
        case ControllerAction::None:continue;
        }
        if(command==StopNavigation)break;
    }
    return command?command|ControllerOrigin:0;
}
unsigned UnrealReader::area_browse_commands() {
    if(!area_browse_)return 0;
    auto& browse=*area_browse_;
    if(browse.next>=browse.checks.size()&&!browse.request)return 0;

    const auto guard=capture_navigation_guard(true);
    if(!guard.foreground||!speech_.ready()||navigation_category_!=browse.category) {
        area_browse_.reset();
        return 0;
    }
    if(!guard.eligible||!guard.walking) {
        if(browse.request)browse.area.clear();
        else area_browse_.reset();
        return 0;
    }
    const auto origin=controlled_agent_position(guard.pawn);
    if(!origin||!finite(*origin)) {
        if(browse.request)browse.area.clear();
        else area_browse_.reset();
        return 0;
    }
    if(browse.revision!=area_browse_revision_||field_selection_context(guard.pawn)!=browse.area) {
        const auto request=browse.request;
        area_browse_.reset();
        return request;
    }
    if(browse.next>=browse.checks.size())return std::exchange(browse.request,0u);

    // Only one native route check may run between engine ticks. Reacquire
    // the actor from the current registries before inspecting its route.
    const auto started=std::chrono::steady_clock::now();
    stage="verify one area route";
    const auto& id=browse.checks[browse.next++];
    const auto target=capture_area_target(guard.pawn,id);
    const bool reachable=target&&area_target_reachable(guard.pawn,*target);
    if(target)browse.routes.insert_or_assign(id,AreaRouteVerdict{*origin,target->position,GetTickCount64(),reachable});
    const auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-started).count());
    ++browse.probes;
    browse.total_us+=elapsed;
    browse.max_us=std::max(browse.max_us,elapsed);
    const bool finished=browse.next==browse.checks.size();
    if(finished) {
        log("NAV area checks category="+std::to_string(browse.category)+" probes="+std::to_string(browse.probes)+
            " total_us="+std::to_string(browse.total_us)+" max_frame_us="+std::to_string(browse.max_us));
        browse.probes=browse.total_us=browse.max_us=0;
    }
    return finished?std::exchange(browse.request,0u):0u;
}
void UnrealReader::movement_tick() {
    game_thread_=GetCurrentThreadId();
    clear_native_walking();
    if(navigation_.mode()==NavigationMode::Off) {
        if(!story_walk_||!story_walk_->recovering_route)return;
        const auto guard=capture_navigation_guard();
        if(!guard.foreground) {
            cancel_auto_walk_trip();
            navigation_speech_stop_pending_="Navigation stopped. Game focus lost.";
            log("NAV cancelled a pending ground retry because game focus was lost");
        } else if(guard.physical_input||controller_walking_state(false).physical_input) {
            cancel_auto_walk_trip();
            navigation_speech_stop_pending_="Automatic walking cancelled by movement.";
            log("NAV cancelled a pending ground retry because the player moved");
        } else if(!guard.eligible) {
            cancel_auto_walk_trip();
            navigation_speech_stop_pending_="Automatic walking stopped. Player control changed.";
            log("NAV cancelled a pending ground retry because field control changed");
        }
        return;
    }
    const auto stop=[this](std::string reason) {
        cancel_auto_walk_trip();
        stop_navigation(std::move(reason),true);
    };
    if(!speech_.ready()) {
        navigation_speech_stop_pending_="Navigation stopped while speech output was unavailable.";
        stop("");log("NAV stopped because speech output became unavailable");return;
    }
    stage="navigation movement guard";
    const auto guard=capture_navigation_guard();
    const bool automatic=navigation_.mode()==NavigationMode::Automatic;
    if(navigation_controller_&&!automatic) {
        const auto controller=controller_backend_status();
        if(!controller.ready||controller.connected!=1){stop_navigation("Navigation stopped. Controller unavailable.",true);return;}
    }
    if(!guard.foreground){
        navigation_speech_stop_pending_="Navigation stopped. Game focus lost.";
        stop("");log("NAV stopped because game focus was lost");return;
    }
    if(!guard.eligible) {
        if(suspend_auto_walk_trip()||pause_field_guidance())return;
        stop_navigation(trf("Navigation stopped. {0}",{tr(guard.reason)}),true);
        return;
    }
    if(navigation_field_exit_&&navigation_path_sample_==0) {
        const auto current=loaded_travel_location(guard.pawn);
        if(current&&current->area>=0&&travel_arrived(navigation_field_exit_->departure,
                navigation_field_exit_->passage,*current)) {
            log("NAV crossed selected exit map="+std::to_string(current->point.map)+
                " entry="+std::to_string(current->point.entry));
            stop_navigation("",true);
            return;
        }
    }
    if(!(navigation_controller_?guard.controller_allowed:guard.allowed[automatic?AutomaticKey:GuidanceKey])){stop("Navigation stopped. Its control binding is unavailable or assigned to another game control.");return;}
    if(automatic&&guard.physical_input) {
        log("NAV automatic cancelled by a native movement control");
        stop("Automatic walking cancelled by movement.");return;
    }
    if(automatic&&!guard.actuator){stop(trf("Automatic walking unavailable. {0}",{tr(guard.actuator_reason)}));return;}
    if(story_target(target_id_)&&story_dirty_.exchange(false)) {
        // Native progression callbacks revoke stale Story destinations before
        // this engine tick can submit another walking input. Speech stays pending
        // until the normal UI/description capture can establish its priority.
        stage="capture Story input for controller navigation";
        const auto update=story_.observe(capture_story_input(guard.pawn),false,speech_.epoch());
        apply_story_update(update);
        if(navigation_.mode()==NavigationMode::Off)return;
    }
    const auto check_controller=[&](bool refresh)->std::optional<ControllerWalkingState> {
        const auto state=controller_walking_state(refresh);
        if(state.read_failed)stop("Automatic walking stopped. Controller state could not be verified.");
        else if(navigation_controller_&&(!state.ready||state.connected!=1))
            stop("Navigation stopped. Controller unavailable.");
        else if(state.physical_input) {
            log("NAV automatic cancelled by left-stick movement");
            stop("Automatic walking cancelled by movement.");
        } else return state;
        return {};
    };
    bool released=false;
    if(automatic&&controller_start_pending_) {
        const auto state=check_controller(false);
        if(!state)return;
        // Keep the walk armed through the original chord. All context and
        // takeover checks above still run while the player releases it.
        if(state->starter_held)return;
        controller_start_pending_=false;
        clear_controller_starter_grace();
        released=true;
    }
    // Resuming through an exit can spend several frames rebuilding the route
    // before the first input can be queued. Give that startup a bounded wait,
    // then measure blockage using the ordinary native movement checks.
    const auto now=GetTickCount64();
    const bool waiting_for_input=automatic&&!navigation_input_path_id_;
    if(waiting_for_input&&!navigation_start_wait_until_)navigation_start_wait_until_=now+4000;
    const bool beginning_movement=released||(waiting_for_input&&now<navigation_start_wait_until_);
    auto frame=navigation_frame(guard.pawn,false,guard.eligible,guard.foreground,guard.physical_input,automatic);
    frame.beginning_movement=beginning_movement;
    if(!frame.field_eligible){stop("Navigation stopped. Player walking state or location is unavailable.");return;}
    if(frame.target.empty()){stop("Navigation stopped. The selected target has no known position in this area.");return;}
    if(automatic&&navigation_motion_origin_&&frame.path.valid) {
        const auto origin=*navigation_motion_origin_;
        const auto distance=std::hypot(frame.position.x-origin.x,frame.position.y-origin.y)/frame.path.world_to_metres;
        if(std::isfinite(distance)&&distance>=.1) {
            log("NAV automatic movement observed metres="+std::to_string(distance));
            navigation_motion_origin_.reset();
        }
    }
    const auto previous_leg=navigation_.leg();
    auto update=navigation_.observe(frame);
    const bool checking_floor_turn=update.floor_turn_waypoint!=0;
    if(checking_floor_turn) {
        frame.hold_floor_turn=!recheck_floor_turn(guard.pawn,frame,update.floor_turn_waypoint);
        update=navigation_.observe(frame);
    }
    if(update.replan!=NavigationReplan::None) {
        log(update.replan==NavigationReplan::OffRoute?"NAV refreshing route after deviation":
            "NAV refreshing route after blocked progress");
        frame=navigation_frame(guard.pawn,true,guard.eligible,guard.foreground,guard.physical_input,automatic);
        frame.beginning_movement=beginning_movement;
        update=navigation_.observe(frame);
    }
    if(update.stopped_auto&&previous_leg) {
        log("NAV stop sample from="+coordinates(frame.position)+" previous_waypoint="+coordinates(previous_leg->second)+
            " path_id="+std::to_string(frame.path_id)+" points="+std::to_string(frame.path.points.size()));
    }
    if(automatic&&update.mode==NavigationMode::Off) {
        const bool needs_ground_recheck=frame.path.valid&&(update.stopped_for_projection||!frame.ground||
            frame.ground->limit==GroundLimit::Obstruction||frame.ground->limit==GroundLimit::Surface||
            frame.ground->limit==GroundLimit::Projection);
        const auto& last_recovery=story_walk_?story_walk_->recovery_origin:std::nullopt;
        const bool new_recovery_location=!last_recovery||
            std::hypot(frame.position.x-last_recovery->x,frame.position.y-last_recovery->y)>=2.*frame.path.world_to_metres;
        if(update.reached_target) {
            if(story_walk_)story_walk_->at_target=true;
        } else if(update.stopped_auto&&needs_ground_recheck&&frame.foreground&&frame.field_eligible&&
                  !frame.manual_input&&story_walk_&&new_recovery_location) {
            // A prior projection can end at an inset mesh corner.
            // Release movement, let the pawn settle, and plan once from its fresh position.
            story_walk_->recovering_route=true;
            story_walk_->recovery_origin=frame.position;
            story_walk_->retry_at=GetTickCount64()+200;
            update.speech.clear();
            log("STORY WALK refreshing from the current position after lost ground support");
        } else cancel_auto_walk_trip();
    }
    navigation_output(update,true);
    if(!update.movement)return;
    const auto pivot=(frame.bypass_rejoin||frame.check_route_turns)&&previous_leg&&update.movement_waypoint&&!frame.path.points.empty()&&
        (update.movement_waypoint->x!=frame.path.points.back().x||update.movement_waypoint->y!=frame.path.points.back().y);
    std::wstring_view steering_mesh=navigation_capsule_mesh_;
    if(steering_mesh.empty()&&navigation_swept_mesh_.ends_with(L".RecastNavMesh-Default")&&world_map(actor_world(guard.pawn)))
        steering_mesh=navigation_swept_mesh_;
    const auto pulse=update.movement_waypoint?prepare_walking_pulse(guard.pawn,steering_mesh,
        frame.position,*update.movement,*update.movement_waypoint,frame.path.world_to_metres,update.movement_ground_metres,
        pivot?std::wstring_view(navigation_swept_mesh_):std::wstring_view{},navigation_capsule_room_,
        pivot&&navigation_capsule_room_&&frame.check_route_turns):std::nullopt;
    if(!pulse){stop("Automatic walking stopped. Clearance or braking room could not be verified.");return;}
    if(!pulse->pawn)return;
    const auto controller=check_controller(navigation_input_path_id_!=frame.path_id||checking_floor_turn);
    if(!controller||controller->starter_held)return;
    // Recheck control immediately before queuing a single native walking tick.
    DWORD final_foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&final_foreground);
    // Getters and the synchronous path query do not yield the game thread.
    // Recheck asynchronous focus and walking control. The fresh XInput poll
    // above covers controller changes during the path query.
    if(final_foreground!=GetCurrentProcessId()||player()!=guard.pawn||!ordinary_walking(guard.pawn)||!get_bool(guard.pawn,L"IsEnableUnitInput")||
       get_bool(guard.pawn,L"IsPaused",true)||get_bool(guard.pawn,L"IsMoveInputIgnored",true)) {
        if(final_foreground!=GetCurrentProcessId()) {
            navigation_speech_stop_pending_="Navigation stopped. Game focus lost.";
            stop("");log("NAV stopped because game focus was lost before movement");
        } else if(!suspend_auto_walk_trip()&&!pause_field_guidance())stop("Automatic walking stopped. Player control changed.");
        return;
    }
    if(!navigation_input_path_id_) {
        navigation_motion_origin_=frame.position;
        log("NAV automatic input queued for native walking");
    }
    queue_native_walking(*pulse);
    navigation_input_path_id_=frame.path_id;
    if(released)log("NAV controller start released");
}
void UnrealReader::on_event(resonance::unreal::UObject* object,resonance::unreal::UFunction* fn,void* params) {
    if(!game_thread_||GetCurrentThreadId()!=game_thread_||!object||!fn) return;
    const auto name=fn->GetNamePrivate().ToString();
    if(name==L"InputScroll"&&params&&is(object,journal_class)&&
       path(fn)==L"/Game/UI/MainMenu/Journal/Widget/WBP_JournalLayout.WBP_JournalLayout_C:InputScroll"&&fn->GetParmsSize()>=5) {
        auto* axis=fn->FindProperty(FName(L"InAxis",FNAME_Find));
        if(axis&&axis->GetOffset_Internal()==0&&axis->GetSize()==4&&axis->field_class()&&
           axis->field_class()->GetName()==L"FloatProperty") {
            float value{};
            std::memcpy(&value,params,sizeof value);
            if(std::isfinite(value)&&std::abs(value)>.01f) {
                auto* model=blueprint_object(object,journal_class,L"VM_Journal");
                if(const auto quest=read<int>(model,L"FocusQuestId",L"IntProperty");quest&&*quest>0) {
                    journal_scroll_={object,*quest,GetTickCount64()};
                    menu_focus_changed_=true;
                }
            }
        }
    }
    if((name==L"HandleInputDirection"&&fn->GetParmsSize()==32&&
        is(object,L"/Script/mucha.CPP_GameWidgetInputManager"))||
       ((name==L"BP_OnItemFocused"||name==L"BP_OnFocusedItemByIndex")&&
        is(object,L"/Script/EnhancedUI.CPP_WBP_MenuBase"))||
       (name==L"HandleEntriesGenerated"&&fn->GetParmsSize()==4&&
        is(object,L"/Script/mucha.CPP_WBP_ListViewMenuBase"))) menu_focus_changed_=true;
    if(name==L"HandleDisplayDamage")stage="capture battle result "+utf8(path(fn));
    battle_input_.on_event(object,fn,params);
    const bool scenario_update=(((name==L"HandleSaveDataUpdate"||name==L"HandleBeginMap")&&fn->GetParmsSize()==8)||
        (name==L"OnLoadAsset"&&fn->GetParmsSize()==0))&&is(object,L"/Script/mucha.CPP_ScenarioSubsystem");
    const bool quest_update=(name==L"HandleUpdateScenario"||name==L"HandleSaveDataUpdate"||name==L"HandleUpdateSaveData"||
        name==L"HandleUpdateRequest")&&is(object,L"/Script/mucha.CPP_QuestSubsystem");
    if(scenario_update||quest_update) {story_dirty_=true;++area_browse_revision_;}
    if((name==L"SetActorHiddenInGame"||name==L"SetActorEnableCollision")&&fn->GetParmsSize()==1&&
       (is(object,L"/Script/mucha.CPP_MapGimmick")||is(object,L"/Script/mucha.CPP_MapTransitionTrigger")))
        ++area_browse_revision_;
    if(name==L"BP_SetSpeakerName" && params && fn->GetParmsSize()==16 && is(object,L"/Script/mucha.CPP_WBP_TalkWindow")) {
        if(speakers_.size()>32) speakers_.clear();
        speakers_[utf8(path(object))]=clean(reinterpret_cast<FText*>(params)->ToString());
    }
    if(name==L"BP_BeginText" && fn->GetParmsSize()==0 && is(object,L"/Script/mucha.CPP_WBP_TextWindow")) {
        if(lines_.size()>32) lines_.clear();
        ++lines_[utf8(path(object))];
    }
}
bool UnrealReader::suspend_auto_walk_trip() {
    if(navigation_.mode()!=NavigationMode::Automatic)return false;
    if(story_walk_)story_walk_->continuation.interrupt();
    else if(destination_leg_&&destination_leg_->mode==NavigationMode::Automatic)destination_leg_->continuation.interrupt();
    else return false;
    stop_navigation("",true);
    log(story_walk_?"STORY WALK suspended":"TRAVEL automatic leg suspended");
    return true;
}

bool UnrealReader::pause_field_guidance() {
    if(navigation_.mode()==NavigationMode::Off||navigation_.target()!=target_id_||
       story_target(target_id_)||destination_leg_||navigation_crossing_)return false;
    if(navigation_path_sample_!=0)log("NAV field guidance paused target="+target_id_);
    navigation_path_={};navigation_path_sample_=0;navigation_ground_.reset();
    navigation_activation_arrival_metres_.reset();
    navigation_room_door_.clear();
    navigation_input_path_id_=0;navigation_start_wait_until_=0;navigation_motion_origin_.reset();
    navigation_.pause();
    beacon_.publish(NavigationMode::Off,{});
    return true;
}

void UnrealReader::cancel_auto_walk_trip() {
    story_walk_.reset();
    if(destination_leg_&&destination_leg_->mode==NavigationMode::Automatic) {
        destination_leg_.reset();
        destination_crossed_.clear();
    }
}

std::string automatic_resume_issue(const NavigationGuard& guard,bool controller) {
    if(controller&&!guard.controller_allowed)return "Controller navigation is unavailable.";
    if(!controller&&!guard.allowed[AutomaticKey])return guard.conflicts[AutomaticKey];
    if(!guard.actuator)return trf("Automatic walking unavailable. {0}",{tr(guard.actuator_reason)});
    const auto state=controller_walking_state(true);
    if(state.read_failed||(controller&&(!state.ready||state.connected!=1)))
        return "Start automatic walking when controller input is available.";
    if(guard.physical_input||state.physical_input)return "Automatic walking cancelled by movement.";
    return {};
}

void UnrealReader::follow_destination_leg(UObject* pawn,const TravelLocation& here,
        const TravelPassage& passage,const std::string& target,NavigationMode mode,bool controller,TravelHandoff handoff) {
    const auto guard=capture_navigation_guard();
    if(guard.pawn!=pawn||!guard.foreground||!guard.eligible||!ordinary_walking(pawn))return;
    const bool automatic=mode==NavigationMode::Automatic;
    const bool resuming=handoff!=TravelHandoff::Wait;
    if(controller&&!guard.controller_allowed) {
        speech_.say(tr("Controller navigation is unavailable. Start guidance when it is available."));return;
    }
    if(automatic&&!guard.actuator){
        speech_.say(trf("Automatic walking unavailable. {0}",{tr(guard.actuator_reason)}));return;
    }
    if(automatic&&resuming) {
        if(const auto issue=automatic_resume_issue(guard,controller);!issue.empty()) {
            speech_.say(tr(issue));return;
        }
    }
    target_id_=target;
    target_type_.clear();
    if(automatic&&controller&&!resuming)controller_automatic_starter_grace();
    const auto route=navigation_frame(pawn,true,guard.eligible,guard.foreground,guard.physical_input,automatic);
    auto update=navigation_.start(mode,route);
    navigation_controller_=update.mode!=NavigationMode::Off&&controller;
    controller_start_pending_=update.mode==NavigationMode::Automatic&&controller&&!resuming;
    const auto name=localized_map_title(destination_map_,current_language());
    if(!update.speech.empty())
        update.speech.front().text=trf("To {0}. {1}",{name,update.speech.front().text});
    navigation_output(update);
    if(update.mode==NavigationMode::Off&&!update.reached_target) {
        destination_leg_.reset();
        destination_crossed_.clear();
        return;
    }
    destination_leg_=DestinationLeg{here,passage,mode,controller,{}};
    log("TRAVEL next destination="+std::to_string(destination_map_)+" from="+std::to_string(here.point.map)+
        " area="+std::to_string(here.area)+" via="+std::to_string(passage.trigger)+" arrival="+
        std::to_string(passage.arrival.map)+":"+std::to_string(passage.arrival.entry)+
        (automatic?" mode=automatic":" mode=manual")+
        (handoff==TravelHandoff::BattleReturn?" handoff=battle":resuming?" handoff=exit":" handoff=start"));
}

void UnrealReader::destination_commands(UObject* pawn,unsigned commands,bool category_changed) {
    constexpr unsigned action=Route|ToggleGuidance|ToggleAutomatic;
    constexpr unsigned select=Previous|Next|PreviousCategory|NextCategory;
    const auto current_name=localized_map_title(destination_map_,current_language());
    if(commands==static_cast<unsigned>(ReviewNavigation|ControllerOrigin)) {
        speech_.say(current_name.empty()?tr("Destinations."):trf("Destinations. {0}.",{current_name}));
        return;
    }
    if((commands&(ToggleGuidance|ToggleAutomatic))&&destination_leg_) {
        const auto requested=commands&ToggleAutomatic?NavigationMode::Automatic:NavigationMode::Manual;
        if(navigation_.mode()==requested||(!(commands&ControllerOrigin)&&destination_leg_->mode==requested)) {
            if(commands&ControllerOrigin){
                speech_.say(tr(requested==NavigationMode::Manual?"Guidance is already on.":"Automatic walking is already on."));
                return;
            }
            destination_leg_.reset();
            destination_crossed_.clear();
            stop_navigation("");
            speech_.say(tr(requested==NavigationMode::Automatic?"Automatic walking off.":"Guidance off."));
            return;
        }
    }
    const auto here=loaded_travel_location(pawn);
    if(!here){speech_.say(tr("The current area is still loading."));return;}
    const bool browse=category_changed||(commands&(select|ReviewNavigation))!=0;
    if(!browse&&!current_name.empty()&&area_input_detail::map_title(here->point.map)==area_input_detail::map_title(destination_map_)) {
        speech_.say(trf("You are at {0}.",{current_name}));return;
    }
    const auto crossed=browse?std::span<const TravelPassage>{}:std::span<const TravelPassage>{destination_crossed_};
    const auto options=destination_options(pawn,*here,crossed);
    auto* picked=destination_choice(options,destination_map_);
    if(browse) {
        if(options.places.empty()){speech_.say(tr("Destinations. No walking destinations are available from here."));return;}
        auto index=picked?static_cast<std::size_t>(picked-options.places.data()):options.places.size();
        if(commands&Next)index=index==options.places.size()?0:(index+1)%options.places.size();
        if(commands&Previous)index=index==0||index==options.places.size()?options.places.size()-1:index-1;
        if(index==options.places.size())index=0;
        picked=&options.places[index];
        if(area_input_detail::map_title(destination_map_)!=area_input_detail::map_title(picked->map)) {
            destination_leg_.reset();
            destination_crossed_.clear();
            stop_navigation("");
        }
        destination_map_=picked->map;
        speech_.say((category_changed?tr("Destinations.")+" ":std::string{})+
            trf(picked->leg_count==1?"{0}. {1} passage. {2}":"{0}. {1} passages. {2}",
                {localized_map_title(picked->map,current_language()),std::to_string(picked->leg_count),
                 trf("{0} of {1}",{std::to_string(index+1),std::to_string(options.places.size())})}));
    }
    if(!(commands&action))return;
    if(!picked) {
        speech_.say(current_name.empty()?tr("Choose a destination first."):
            trf("A walking route to {0} is unavailable from here.",{current_name}));
        return;
    }
    const auto& leg=options.first[picked->first_leg];
    if(commands&Route) {
        const auto path=route_snapshot(pawn,leg.target.actor,leg.target.position,false);
        const auto directions=path?describe_route(path->snapshot):std::nullopt;
        speech_.say(trf("To {0}. Next, {1}. {2}",
            {localized_map_title(picked->map,current_language()),leg.target.snapshot.label,
             directions?*directions:tr("Use the game's interaction control when available.")}));
    }
    if(commands&(ToggleGuidance|ToggleAutomatic)) {
        const auto mode=(commands&ToggleAutomatic)?NavigationMode::Automatic:NavigationMode::Manual;
        follow_destination_leg(pawn,*here,leg.passage,leg.target.snapshot.id,mode,(commands&ControllerOrigin)!=0);
    }
}

void UnrealReader::observe_destination(UObject* pawn,bool battle) {
    if(!destination_leg_||navigation_category_!=destination_category)return;
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId()||!speech_.ready()) {
        destination_leg_.reset();
        destination_crossed_.clear();
        return;
    }
    auto& trip=*destination_leg_;
    if(battle)suspend_auto_walk_trip();
    auto current=!battle&&!description_busy_&&pawn?loaded_travel_location(pawn):std::nullopt;
    if(current&&(current->point!=trip.departure.point||trip.continuation.pending()||
        travel_arrived(trip.departure,trip.passage,*current))) {
        const auto control=capture_navigation_guard();
        if(control.pawn!=pawn||!control.eligible||!ordinary_walking(pawn))current.reset();
    }
    const auto handoff=trip.continuation.observe(trip.departure,trip.passage,current,
        battle&&trip.mode==NavigationMode::Automatic);
    if(handoff==TravelHandoff::Wait)return;
    const auto previous=std::exchange(destination_leg_,std::nullopt).value();
    stop_navigation("");
    if(handoff==TravelHandoff::Stopped||handoff==TravelHandoff::DifferentArrival) {
        destination_crossed_.clear();
        speech_.say(tr(handoff==TravelHandoff::Stopped?"Automatic walking stopped. Start it again to continue.":
            "Destination kept. Start guidance to find a route from here."));
        return;
    }
    const auto name=localized_map_title(destination_map_,current_language());
    if(handoff==TravelHandoff::Crossed) {
        destination_crossed_.push_back(previous.passage);
        log("TRAVEL arrived map="+std::to_string(current->point.map)+" entry="+std::to_string(current->point.entry));
        if(area_input_detail::map_title(current->point.map)==area_input_detail::map_title(destination_map_)) {
            speech_.say(trf("Arrived at {0}.",{name}));
            destination_crossed_.clear();
            return;
        }
    }
    if(previous.controller&&previous.mode==NavigationMode::Manual&&controller_backend_status().connected!=1) {
        speech_.say(tr("Destination kept. Start guidance when controller navigation is available."));
        return;
    }
    const auto options=destination_options(pawn,*current,destination_crossed_);
    const auto* chosen=destination_choice(options,destination_map_);
    if(!chosen){speech_.say(trf("A walking route to {0} is unavailable from here.",{name}));return;}
    const auto& leg=options.first[chosen->first_leg];
    follow_destination_leg(pawn,*current,leg.passage,leg.target.snapshot.id,previous.mode,previous.controller,handoff);
}

void UnrealReader::arm_story_walk(UObject* pawn,bool controller,const NavigationUpdate& started) {
    story_walk_.reset();
    if(started.mode!=NavigationMode::Automatic&&!started.reached_target)return;
    const auto here=loaded_travel_location(pawn);
    const auto& snapshot=story_.snapshot();
    if(!here||here->point.map!=snapshot.map_id)return;
    const auto destinations=story_destinations(snapshot);
    const auto selected=std::find_if(destinations.begin(),destinations.end(),[&](const auto& step) {
        return story_target_id(snapshot,step)==target_id_;
    });
    if(selected==destinations.end())return;
    std::optional<TravelPassage> expected;
    UObject* exit_source=nullptr;
    if(navigation_room_door_.empty()&&!selected->exit_source.empty())
        exit_source=UObjectGlobals::StaticFindObject(selected->exit_source.c_str());
    else {
        // Direct quest goals can refer to a transition actor without the
        // related-exit flag used by filtered goal discovery.
        const auto& gate=navigation_room_door_.empty()?selected->actor_id:navigation_room_door_;
        for(auto* actor:find(L"CPP_MapTransitionTrigger"))if(utf8(path(actor))==gate) {
            exit_source=actor;
            break;
        }
    }
    if(!exit_source&&here->point==TravelPoint{3250,13}&&here->area==12&&snapshot.main_progress==6130) {
        // Holding Up on the Dragon trial's ladder can carry the player
        // straight through the walk-in passage before Story sees the top.
        for(auto* ladder:find(L"CPP_MapGimmickLadder")) {
            if(actor_world(ladder)!=actor_world(pawn)||read<int>(ladder,L"m_UniqueId")!=80||
                utf8(path(ladder))!=selected->actor_id)continue;
            auto* gate=UObjectGlobals::StaticFindObject(
                L"/Game/Map/Dng/08Gro/08Gro_43/GD/Dng_08Gro_43_01_GD.Dng_08Gro_43_01_GD:"
                L"PersistentLevel.BP_MapTransitionTrigger_C_10");
            const auto route=capture_story_exit_route(pawn,gate);
            if(route&&route->destination_map==3250&&route->destination_point==15&&read<int>(gate,L"m_UniqueId")==2)
                exit_source=gate;
            break;
        }
    }
    if(const auto route=capture_story_exit_route(pawn,exit_source);route&&route->destination_point&&!route->event_only) {
        const auto trigger=read<int>(route->actor,L"m_UniqueId",L"IntProperty");
        if(trigger&&*trigger>=0)expected=TravelPassage{here->point.map,*trigger,{route->destination_map,*route->destination_point}};
    }
    story_walk_=StoryWalk{*here,expected,selected->id,snapshot.destination_epoch,snapshot.main_progress,
        controller,started.reached_target,{}};
    log("STORY WALK armed map="+std::to_string(snapshot.map_id)+" target="+selected->label+
        (expected?" passage="+std::to_string(expected->trigger):""));
}

void UnrealReader::observe_story_walk(UObject* pawn,bool battle) {
    if(!story_walk_||navigation_category_!=story_category)return;
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    // Native scenes can return focus in a different frame from field control.
    // Active movement already cancels on focus loss in movement_tick.
    if(foreground!=GetCurrentProcessId())return;
    if(!speech_.ready()) {
        story_walk_.reset();return;
    }
    if(battle)suspend_auto_walk_trip();
    auto& trip=*story_walk_;
    const auto& snapshot=story_.snapshot();
    auto current=!battle&&!description_busy_&&pawn?loaded_travel_location(pawn):std::nullopt;
    if(current&&(!snapshot.ready||snapshot.map_id!=current->point.map||snapshot.destination_epoch.empty()||
       snapshot.world!=utf8(path(actor_world(pawn)))||snapshot.player!=utf8(path(pawn))))current.reset();
    const bool recover_route=trip.recovering_route&&!battle&&current&&current->point==trip.departure.point&&
        current->area==trip.departure.area&&snapshot.destination_epoch==trip.epoch&&snapshot.main_progress==trip.progress;
    if(trip.recovering_route&&!battle&&!recover_route) {
        story_walk_.reset();speech_.say(tr("Automatic walking stopped. A fresh route could not be verified."));return;
    }
    const bool progressed=snapshot.main_progress&&trip.progress&&*snapshot.main_progress>*trip.progress&&
        (trip.at_target||trip.continuation.pending());
    // Sakura's barrier scene moves the pawn into the lower hall while keeping
    // the map manager's original entry point and the same objective progress.
    const bool crossed_tower_entrance=current&&trip.continuation.pending()&&
        trip.selected.ends_with("|step:tower-barrier")&&trip.departure.point.map==3280&&
        trip.departure.area==0&&current->area==3&&trip.progress==8000&&snapshot.main_progress==8000;
    const bool next_step=(trip.at_target||crossed_tower_entrance)&&current&&current->point==trip.departure.point&&
        !snapshot.next_step.empty()&&snapshot.destination_epoch!=trip.epoch;
    if(current&&(trip.continuation.pending()||progressed||next_step||recover_route||current->point!=trip.departure.point||
       (trip.passage&&travel_arrived(trip.departure,*trip.passage,*current)))) {
        const auto guard=capture_navigation_guard();
        if(guard.pawn!=pawn||!guard.eligible)current.reset();
        else if(recover_route&&guard.physical_input) {
            story_walk_.reset();speech_.say(tr("Automatic walking off."));return;
        } else if(!ordinary_walking(pawn)) {
            story_walk_.reset();speech_.say(story_status(snapshot));return;
        } else if(trip.at_target&&trip.departure.point==TravelPoint{3250,13}&&trip.departure.area==12&&
                  trip.passage&&trip.passage->trigger==2&&travel_arrived(trip.departure,*trip.passage,*current)&&
                  (guard.physical_input||controller_walking_state(true).physical_input)) {
            // The same Up input that climbs the ladder can still be held
            // when the elevator room gives the player control again.
            const auto now=GetTickCount64();
            if(!trip.retry_until)trip.retry_until=now+750;
            if(now<trip.retry_until)return;
        }
    }
    const auto handoff=trip.continuation.observe(trip.departure,trip.passage,current,battle,progressed||next_step||recover_route);
    if(handoff==TravelHandoff::Wait)return;
    const auto now=GetTickCount64();
    if(trip.retry_at>now)return;
    auto previous=std::exchange(story_walk_,std::nullopt).value();
    stop_navigation("");
    if(handoff==TravelHandoff::Stopped||handoff==TravelHandoff::DifferentArrival) {
        speech_.say(tr("Automatic Story walking stopped. Start it again to continue."));return;
    }
    const auto next=continue_story_target(snapshot,previous.selected);
    if(!next) {
        speech_.say(trf("{0} Choose the next Story target to continue walking.",{story_status(snapshot)}));return;
    }
    const auto guard=capture_navigation_guard();
    if(guard.pawn!=pawn||!guard.foreground||!guard.eligible||!ordinary_walking(pawn)) {
        story_walk_=std::move(previous);
        return;
    }
    if(const auto issue=automatic_resume_issue(guard,previous.controller);!issue.empty()) {
        speech_.say(tr(issue));return;
    }
    target_id_=story_target_id(snapshot,*next);
    target_type_.clear();
    const auto route=navigation_frame(pawn,true,guard.eligible,guard.foreground,guard.physical_input,true);
    const auto started=navigation_.start(NavigationMode::Automatic,route);
    if(!recover_route&&handoff==TravelHandoff::StoryAdvance&&next->exit&&started.mode!=NavigationMode::Automatic&&
       !started.reached_target) {
        // A native door can report open before its collision has cleared.
        // Let the newly disclosed passage settle and recheck it with live control and collision.
        if(!previous.retry_until) {
            previous.retry_until=now+2400;
            log("STORY WALK waiting for passage clearance target="+next->label);
        }
        if(now<previous.retry_until) {
            previous.retry_at=now+200;
            story_walk_=std::move(previous);
            return;
        }
    }
    navigation_controller_=started.mode==NavigationMode::Automatic&&previous.controller;
    navigation_output(started);
    arm_story_walk(pawn,previous.controller,started);
    if(!story_walk_)return;
    if(recover_route)story_walk_->recovery_origin=previous.recovery_origin;
    const auto area=field_selection_context(pawn);
    if(navigation_selections_.size()>=64&&!navigation_selections_.contains(area))navigation_selections_.clear();
    navigation_selection_context_=area;
    navigation_selections_[area].targets[story_category]=target_id_;
    const auto reason=recover_route?"replan":handoff==TravelHandoff::BattleReturn?"battle":
        handoff==TravelHandoff::Crossed?"exit":"objective";
    log("STORY WALK resumed via="+std::string(reason)+" map="+std::to_string(snapshot.map_id)+" target="+next->label);
}

UnrealReader::UnrealReader(Speech& speech,RouteBeaconOutput& beacon,std::filesystem::path audio_settings_file)
    :speech_(speech),beacon_(beacon),audio_settings_file_(std::move(audio_settings_file)),
     audio_settings_(load_audio_settings(audio_settings_file_)) {
    beacon_.set_settings(audio_settings_);
}

bool UnrealReader::before_event(unreal::UObject* object,unreal::UFunction* function,void* params) {
    return accessibility_menu_.before_event(object,function,params);
}

void UnrealReader::tick(unsigned commands) {
    game_thread_=GetCurrentThreadId();
    stage="capture root and player";
    auto* top=top_widget(); auto* p=player();
    const auto previous_audio=audio_settings_;
    stage="capture Accessibility Options";
    accessibility_menu_.tick(top,audio_settings_);
    if(previous_audio!=audio_settings_) {
        beacon_.set_settings(audio_settings_);
        if(!save_audio_settings(audio_settings_file_,audio_settings_)) {
            log("OPTIONS accessibility audio settings could not be saved");
            speech_.say(tr("Audio setting applied for this session. Could not save audio settings."),false);
        }
    }
    const bool accessibility_options=accessibility_menu_.open();
    stage="capture battle presentation";
    constexpr unsigned battle_requests=PreviousCategory|NextCategory|BattleHp|BattleMp|BattleGauges|
        BattleBuffs|BattleDebuffs|BattlePanelOpen|BattlePanelClose|BattleVitals|BattleEffects;
    auto battle=battle_input_.capture(top,(commands&battle_requests)!=0);
    battle_control_layout_=battle.active?top:nullptr;
    const auto battle_status=battle_input_.diagnostic();
    const bool battle_status_changed=battle_status!=battle_status_;
    if(battle_status_changed){battle_status_=battle_status;log("BATTLE "+battle_status);}
    const bool continuing_battle=battle.active&&!battle.context.empty()&&battle_context_==battle.context;
    auto* review_layout=battle.active&&battle_layout(top)&&!battle.prompt&&
        std::any_of(battle.units.begin(),battle.units.end(),[](const auto& unit){return unit.side==BattleSide::Ally;})?top:nullptr;
    if(battle_review_layout_!=review_layout||(review_layout&&!continuing_battle)) {
        battle_review_layout_=review_layout;
        ++battle_review_epoch_;
        battle_binding_checked_=0;
        publish_battle_controller_permission(false,battle_review_epoch_);
    }
    // Selection position is refreshed before every route use; also clear it
    // while idle whenever its world or controlled player is lost.
    if(active_target_) {
        const auto current_world=p?utf8(path(actor_world(p))):std::string{};
        const auto current_player=p?utf8(path(p)):std::string{};
        if(current_world.empty()||current_world!=active_target_->world||
           current_player!=active_target_->player)active_target_.reset();
    }
    const auto world=p?utf8(path(p->GetOuterPrivate())):std::string{};
    const auto context=world+"|"+utf8(path(top));
    const bool battle_results=battle_result_layout(top);
    battle_results_layout_=battle_results?top:nullptr;
    const bool finishing_battle=battle_results&&!battle_context_.empty()&&!world.empty()&&context_.starts_with(world+"|");
    if(battle.active)battle_context_=battle.context;
    else if(!finishing_battle)battle_context_.clear();
    if(context_!=context) {
        context_=context;
        core_.reset();
        if(!continuing_battle&&!finishing_battle)speech_.stop();
        const bool title=context.ends_with(".WBP_TitleLayout_C")||context.find(".WBP_TitleLayout_C_")!=std::string::npos;
        targets_.clear();
        ++area_browse_revision_;
        if(title) {
            area_browse_.reset();
            cancel_auto_walk_trip();
        }
        if(title||!pause_field_guidance()) {
            target_id_.clear();target_type_.clear();
            if(!suspend_auto_walk_trip())stop_navigation("Navigation stopped. Screen or area changed.");
        }
        log("CONTEXT "+context);
    }
    stage="capture menu";
    auto* instance=p?object(actor_world(p),L"OwningGameInstance"):nullptr;
    auto menu=accessibility_menu_.snapshot()?accessibility_menu_.snapshot():
        battle.prompt?std::optional<MenuSnapshot>{}:menu_snapshot(top,instance,journal_scroll_,!battle_results);
    const bool result_focus=battle_results&&menu&&menu->ready&&!menu->label.empty();
    if(battle_results&&!menu)
        menu=MenuSnapshot{utf8(path(top)),"","",-1,0,true,false,true};
    stage="capture dialogue";
    Frame frame{accessibility_options?context+"|accessibility-options":context,
        std::move(menu),dialogue_snapshot(top,speakers_,lines_,read_subtitles_)};
    battle.command_visible=battle.active&&(battle.choosing_target||battle.prompt.has_value()||
        (frame.menu&&frame.menu->ready&&!frame.menu->label.empty()));
    const bool was_command_visible=std::exchange(battle_command_visible_,battle.command_visible)&&continuing_battle;
    const bool tutorial=is(top,L"/Script/mucha.CPP_WBP_Tutorial");
    const bool journal=frame.menu&&frame.menu->journal.has_value();
    if(!journal)journal_scroll_={};
    const auto tutorial_review=tutorial&&(commands&(Repeat|Screen))?current_review(frame,{}):std::string{};
    frame.menu_interrupts=!battle_layout(top)&&!battle_results;
    frame.observed_at=GetTickCount64();
    if(!tutorial&&!accessibility_options&&!battle_results&&frame.menu&&frame.menu->ready)
        frame.menu_hints=menu_guides(top,instance);
    if(accessibility_options)
        frame.introduction=tr("Accessibility Options. Left and right adjust the selected value. Back returns to System Menu.");
    else if(!journal&&!battle_results&&!battle_layout(top)&&frame.menu&&frame.menu->id!=utf8(path(top)))
        frame.introduction=widget_text(top,true,frame.dialogue&&frame.dialogue->voice==DialogueVoice::Voiced,true);
    BattleResultFrame result_frame;
    if(battle_results) {
        stage="capture visible battle result page";
        const auto result=battle_input_.result_page(top);
        result_frame={context,result.page,result.staging,widget_lines(top,result_focus,false,false),
            result_focus?current_review(frame,{}):std::string{},GetTickCount64(),result.counting_rewards};
        if(result.diagnostic!=battle_result_status_) {
            battle_result_status_=result.diagnostic;
            log("BATTLE RESULT "+battle_result_status_);
        }
    } else {
        battle_result_status_.clear();
        battle_result_core_.reset();
    }
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    stage="capture current story objective";
    story_dirty_=false;
    stage="capture Story input";
    auto story_capture=capture_story_input(p);
    stage="capture active narration clocks";
    narration_clocks_=capture_narration_clocks(foreground==GetCurrentProcessId());
    description_busy_=description_observer_&&description_observer_(frame);
    if(description_busy_)beacon_.publish(NavigationMode::Off,{});
    const auto proximity_now=std::chrono::steady_clock::now();
    if(proximity_world_!=story_capture.world||proximity_player_!=story_capture.player) {
        proximity_sample_={};
        beacon_.cancel_proximity();
        proximity_world_=story_capture.world;
        proximity_player_=story_capture.player;
    }
    const bool entity_cues=(audio_settings_.npc_enabled&&audio_settings_.npc_volume>0)||
        (audio_settings_.object_enabled&&audio_settings_.object_volume>0);
    const bool wall_cues=audio_settings_.wall_enabled&&audio_settings_.wall_volume>0;
    if((!entity_cues&&!wall_cues)||battle.active||battle_results||(top&&!navigation_field_surface(top))||
       !story_announcement_allowed(frame,foreground==GetCurrentProcessId(),beacon_.proximity_ready(),
           story_capture.field_control,description_busy_)) {
        beacon_.cancel_proximity();
        proximity_sample_={};
    } else if(proximity_now>=proximity_sample_) {
        stage="capture nearby field cues";
        proximity_sample_=proximity_now+std::chrono::milliseconds(120);
        if(const auto spatial=story_spatial_context(p)) {
            std::vector<ActivationBox> passages;
            const auto candidates=capture_field_proximity(p,*spatial,passages);
            auto entities=entity_cues?field_entity_cues(candidates,*spatial):FieldEntityCues{};
            stage="capture nearby walls";
            FieldWallCues walls;
            if(wall_cues)walls=capture_field_walls(p,*spatial,passages);
            beacon_.publish_proximity({std::move(entities),std::move(walls)});
        } else beacon_.cancel_proximity();
    }
    bool story_can_announce=story_announcement_allowed(frame,foreground==GetCurrentProcessId(),
        speech_.ready(),story_capture.field_control,description_busy_);
    if(story_can_announce&&story_.needs_announcement(story_capture,speech_.epoch())) {
        stage="capture story voice readiness";
        const auto voice=capture_narration_input(false);
        story_can_announce=story_announcement_allowed(frame,foreground==GetCurrentProcessId(),speech_.ready(),
            story_capture.field_control,description_busy_,voice.any_talk_voice_busy==NarrationFact::No);
    }
    const auto story_update=story_.observe(story_capture,story_can_announce,speech_.epoch());
    apply_story_update(story_update);
    if(speech_.ready() && foreground==GetCurrentProcessId()) {
        // Capture still runs while output is unavailable so battle/world end
        // discards stale sessions. Consume current results only for delivery.
        const auto battle_events=battle_input_.drain_events();
        core_.output_epoch(speech_.epoch());
        if(battle_output_epoch_!=speech_.epoch()){
            battle_output_epoch_=speech_.epoch();battle_core_.reset();battle_result_core_.reset();
        }
        if(battle.active&&(commands&(BattleInterrupt|BattleInputKey|battle_requests|Repeat|Screen))) {
            speech_.interrupt_battle();
            if(!battle.command_visible&&(!was_command_visible||(commands&BattleInterrupt))) {
                battle_core_.interrupt_action(battle);
                log("BATTLE hit speech interrupted actor="+std::to_string(battle.actor));
            }
        }
        auto spoken_frame=speech_frame(frame,description_busy_);
        if(battle_results)spoken_frame.menu.reset();
        for(const auto& event:core_.observe(spoken_frame)) {
            // A review key can also switch the device whose guides are drawn.
            const bool reviewing_guides=(commands&(Repeat|Screen))&&
                event.delivery==SpeechDelivery::SelectionSupplement&&event.text==frame.menu_hints;
            if(!reviewing_guides&&!(journal&&(commands&(Repeat|Screen)))&&
               (tutorial_review.empty()||event.text!=tutorial_review))speech_.say(event);
        }
        for(const auto& event:battle_core_.observe(battle))speech_.say(event);
        std::string hit_batch;
        const auto say_hits=[&] {
            if(hit_batch.empty())return;
            speech_.say(SpeechEvent{std::exchange(hit_batch,{}),false,SpeechDelivery::OrderedBattle});
        };
        for(const auto& event:battle_core_.events(battle,battle_events)) {
            if(!hit_batch.empty()&&hit_batch.size()+event.text.size()>4096)say_hits();
            if(!hit_batch.empty())hit_batch+=". ";hit_batch+=event.text;
        }
        say_hits();
        if(battle_results) {
            const auto events=battle_result_core_.observe(result_frame);
            if(commands&(ResultUp|ResultDown)) {
                speech_.interrupt_battle();
                const auto line=battle_result_core_.browse((commands&ResultUp)?-1:1);
                speech_.say({line,true,SpeechDelivery::LatestSelection});
            } else for(const auto& event:events)speech_.say(event);
        }
        if(battle_layout(top)&&!battle.active&&battle_status_changed)
            speech_.say(battle.unavailable_reason.empty()?tr("Battle information is not ready."):battle.unavailable_reason,false);
        if(!description_busy_&&!navigation_speech_stop_pending_.empty())
            speech_.say(tr(std::exchange(navigation_speech_stop_pending_,std::string{})),false);
        for(const auto& event:story_update.speech)speech_.say(event.text,event.interrupt);
    }
    const auto now=GetTickCount64();
    if(now-last_diagnostic_>10000) { last_diagnostic_=now; log("CAPTURE top="+utf8(path(top))+" menu="+(frame.menu?frame.menu->label:"unavailable")+" value="+(frame.menu?frame.menu->value:"")+" dialogue="+(frame.dialogue?"visible":"unavailable")+" player="+(p?"present":"unavailable")); }
    if(commands&Help) {
        auto help_story=story_.snapshot();help_story.field_control=story_can_announce;
        const auto keyboard_help=tr("Visions of Grandshelt. F5 repeat. Home and End change target category, including Story for the current main objective and Destinations for travel through connected areas. Page Up and Page Down choose a target in the field; F6 and F7 also choose targets. Delete toggles manual guidance. Shift Delete toggles automatic walking. Moving or opening a menu stops automatic walking. F8 reads a complete native route. F9 reads the current screen, or party, enemies and target information in battle. F10 help. You control interaction and Confirm.")+" ";
        const auto modifier=controller_modifier()==ControllerModifier::RightShoulder?"RB":
            controller_modifier()==ControllerModifier::RightTrigger?"RT":"LT";
        const auto controller_help=trf("Controller. Hold {0} for navigation. D-pad left and right change category; up and down choose a target. Press the left stick for manual guidance, or the right stick for automatic walking. B stops navigation; Y reads help. Release {0} after starting or stopping. Release held controls after a menu, focus or connection change.",{modifier})+" "+
            (controller_unavailable_reason_.empty()?tr("Controller navigation is available."):
                trf("Controller navigation unavailable. {0}",{tr(controller_unavailable_reason_)}))+" ";
        const auto beacon_help=tr(audio_settings_.beacon_volume==0?
            "Beacon volume is zero. Manual guidance will speak turns.":beacon_.ready()?
            "The stereo route beacon points toward the next turn and pulses faster as you approach. F8 speaks directions.":
            "Route beacon unavailable. Navigation will speak directions.")+" ";
        const auto travel_help=tr("Story and Destinations auto-walk resume after their exits and battles. Use Interact when prompted.")+" ";
        const auto accessibility_help=tr("Accessibility Options is in the System menu. In battle, Home and End choose an ally to review. H reads HP, M reads MP, L reads gauges, F2 reads buffs, and F3 reads debuffs. Press the left stick in battle for controller status.")+" ";
        const auto battle_help=tr("F4 or right-stick click stops combat announcements. Use the up and down arrows or D-pad to review victory results.")+" ";
        speech_.say(((commands&ControllerOrigin)?std::string{}:keyboard_help)+controller_help+accessibility_help+battle_help+travel_help+beacon_help+story_help_status(help_story));
    }
    if(commands&Repeat) {
        const auto review=battle.prompt?battle_core_.review(battle):
            battle_results?battle_result_core_.browse(0):current_review(frame,core_.repeat());
        speech_.say(review.empty()?tr("No captured selection or dialogue"):review);
    }
    if(commands&Screen) {
        auto screen=(battle.active||battle_layout(top))?
            (battle.active?battle_core_.review(battle):battle.unavailable_reason.empty()?tr("Battle information is unavailable."):battle.unavailable_reason):
            battle_results?battle_result_core_.review():
            tutorial?tutorial_review:
            (accessibility_options||is(top,L"/Script/mucha.CPP_OP_Layout"))?
            (frame.menu&&frame.menu->ready?frame.menu->label+(frame.menu->value.empty()?"":". "+frame.menu->value):tr("Current option is loading")):
            (journal||(frame.menu&&frame.menu->ready&&frame.menu->details&&!frame.menu->details->empty()))?
            current_review(frame,{}):widget_text(top,false,false,true);
        append_menu_text(screen,frame.menu_hints);
        if(const auto timer=displayed_timer();!timer.empty())screen+=(screen.empty()?"":". ")+timer;
        if(const auto& snapshot=story_.snapshot();snapshot.field_control&&!snapshot.nearby_clue.empty())
            screen+=(screen.empty()?"":". ")+snapshot.nearby_clue;
        if(const auto& snapshot=story_.snapshot();snapshot.field_control&&!snapshot.next_step.empty()&&
            (snapshot.crossing||navigation_category_==story_category)) {
            if(screen.empty())screen=story_status(snapshot);
            else screen+=". "+snapshot.next_step;
        }
        speech_.say(screen.empty()?tr("Current screen text is unavailable"):screen);
    }
    if(battle.active&&battle_layout(top)&&!battle.prompt&&(commands&battle_requests)&&
       foreground==GetCurrentProcessId()&&speech_.ready()) {
        if(commands&BattlePanelClose) {
            speech_.say(tr("Battle status closed."));
        } else {
            struct BattleReview {unsigned mask;const wchar_t* key;BattleStatusAction action;};
            constexpr BattleReview reviews[]={
                {BattlePanelOpen|BattleVitals,nullptr,BattleStatusAction::Vitals},
                {PreviousCategory,L"Home",BattleStatusAction::Previous},
                {NextCategory,L"End",BattleStatusAction::Next},
                {BattleHp,L"H",BattleStatusAction::Hp},{BattleMp,L"M",BattleStatusAction::Mp},
                {BattleGauges,L"L",BattleStatusAction::Gauges},{BattleBuffs,L"F2",BattleStatusAction::Buffs},
                {BattleDebuffs,L"F3",BattleStatusAction::Debuffs},{BattleEffects,nullptr,BattleStatusAction::Effects}
            };
            for(const auto& review:reviews) {
                if(!(commands&review.mask))continue;
                if(review.key&&!(commands&ControllerOrigin)&&!battle_review_binding_available(review.key)) {
                    speech_.say(tr("This battle status shortcut is unavailable with the current bindings."));
                    continue;
                }
                auto message=battle_core_.status(battle,review.action);
                if(commands&BattlePanelOpen)
                    message=trf("Battle status. D-pad left and right choose an ally. Up reads HP and MP. Down reads effects. Y reads gauges. Press B or the left stick to close. {0}",{message});
                if(!message.empty())speech_.say(message);
            }
        }
        commands&=~battle_requests;
    }
    if(destination_leg_&&!commands)observe_destination(p,battle.active||battle_results);
    if(story_walk_&&!commands)observe_story_walk(p,battle.active||battle_results);
    if(commands&StopNavigation) {
        area_browse_.reset();
        const bool trip=destination_leg_.has_value();
        const bool story=story_walk_.has_value();
        story_walk_.reset();
        destination_leg_.reset();
        destination_crossed_.clear();
        const bool active=navigation_.mode()!=NavigationMode::Off;
        stop_navigation("");speech_.say(tr(trip?"Destination guidance stopped.":story?"Story guidance stopped.":
            active?"Navigation stopped.":"Navigation is off."));return;
    }
    constexpr unsigned navigation_commands=Previous|Next|Route|PreviousCategory|NextCategory|ToggleGuidance|ToggleAutomatic|PreviousPageTarget|NextPageTarget|ReviewNavigation;
    if(!(commands&navigation_commands))return;
    // Active guidance has its own per-engine-tick guard. An idle reader needs
    // this native capture only for an explicit navigation command.
    const auto guard=capture_navigation_guard();
    if(!speech_.ready()){
        navigation_speech_stop_pending_=navigation_.mode()==NavigationMode::Off?
            "Navigation unavailable while speech output was disconnected.":"Navigation stopped while speech output was unavailable.";
        stop_navigation("");log("NAV command unavailable while speech output is disconnected");return;
    }
    if(!guard.foreground)return;
    // Do not borrow Page keys from scrolling, map zoom or text/key capture.
    // Explicit F8 can still explain why a field route is unavailable.
    if(!guard.eligible) {
        // Page keys retain native scrolling/map zoom outside field control.
        constexpr unsigned explicit_commands=Route|Previous|Next|PreviousCategory|NextCategory|ToggleGuidance|ToggleAutomatic|PreviousPageTarget|NextPageTarget;
        const bool legacy=(commands&(Route|Previous|Next))!=0;
        if(legacy||(guard.field_surface&&(commands&explicit_commands)))speech_.say(trf("Navigation unavailable. {0}",{tr(guard.reason)}));
        return;
    }
    const std::array<std::pair<unsigned,NavigationKey>,6> gestures={{{PreviousCategory,CategoryBack},{NextCategory,CategoryForward},
        {PreviousPageTarget,TargetBack},{NextPageTarget,TargetForward},{ToggleGuidance,GuidanceKey},{ToggleAutomatic,AutomaticKey}}};
    for(const auto& [command,key]:gestures)if(!(commands&ControllerOrigin)&&(commands&command)&&!guard.allowed[key]) {
        commands&=~command;
        speech_.say(guard.conflicts[key].empty()?tr("Navigation key unavailable because it conflicts with a game control."):tr(guard.conflicts[key]));
    }
    if((commands&ControllerOrigin)&&!guard.controller_allowed){speech_.say(tr("Controller navigation unavailable. Its current game binding could not be reserved."));return;}
    if(guard.rowing||guard.flying) {
        if(commands&ToggleAutomatic) {
            speech_.say(tr(guard.flying?"Steer the airship with the left stick. Use manual guidance for the beacon.":
                "Steer the raft with the game's movement controls. Use manual guidance for the Story crossing."));
            return;
        }
        if(commands&ToggleGuidance&&navigation_.mode()==NavigationMode::Manual) {
            if(commands&ControllerOrigin)speech_.say(tr("Guidance is already on."));
            else navigation_output(navigation_.toggle(NavigationMode::Manual,{}));
            return;
        }
        if(commands&(Route|ToggleGuidance)) {
            auto rowing=navigation_frame(guard.pawn,true,true,guard.foreground,false,false);
            if(commands&Route) {
                const auto directions=describe_route(rowing.path);
                speech_.say(rowing.activation_reached?rowing.arrival_message:
                    directions?rowing.target_label+". "+*directions:
                    !rowing.target_unavailable_reason.empty()?tr(rowing.target_unavailable_reason):
                    !rowing.route_unavailable_reason.empty()?tr(rowing.route_unavailable_reason):
                    tr("A route for the current Story crossing is unavailable."));
            }
            if(commands&ToggleGuidance) {
                const auto update=navigation_.toggle(NavigationMode::Manual,rowing);
                navigation_controller_=update.mode!=NavigationMode::Off&&(commands&ControllerOrigin);
                navigation_output(update);
            }
            return;
        }
        if(commands&navigation_commands) {
            const auto& story=story_.snapshot();
            if(guard.flying) {
                speech_.say(story.flight_destination?trf("Story. {0}",{story.next_step}):tr("Story. No current flight destination could be verified."));
            } else speech_.say(story.crossing?trf("Story. {0} Use manual guidance for the route.",{story.next_step}):
                tr("Story. No current crossing could be verified."));
        }
        return;
    }
    navigation_crossing_.reset();
    if(commands&PreviousPageTarget)commands|=Previous;
    if(commands&NextPageTarget)commands|=Next;
    if(!(commands&navigation_commands))return;
    if(navigation_category_==destination_category&&!(commands&(PreviousCategory|NextCategory))) {
        destination_commands(guard.pawn,commands,false);
        return;
    }
    const auto area=field_selection_context(guard.pawn);
    if(navigation_selections_.size()>=64&&!navigation_selections_.contains(area))navigation_selections_.clear();
    auto& memory=navigation_selections_.try_emplace(area).first->second;
    const auto& current_story=story_.snapshot();
    const bool active_story=current_story.ready&&!current_story.objective.empty()&&
        current_story.world==utf8(path(actor_world(guard.pawn)))&&current_story.player==utf8(path(guard.pawn));
    if(navigation_selection_context_!=area) {
        navigation_selection_context_=area;
        if(navigation_.mode()==NavigationMode::Off) {
            target_id_=memory.targets[navigation_category_];target_type_.clear();
        }
    }
    if(target_id_.empty())target_id_=memory.targets[navigation_category_];
    const bool category_changed=(commands&(PreviousCategory|NextCategory))!=0;
    if(commands==static_cast<unsigned>(ReviewNavigation|ControllerOrigin)&&navigation_category_!=story_category&&
       navigation_category_!=destination_category) {
        // Leave the game thread free for the next controller gesture.
        speech_.say(tr(target_categories[navigation_category_])+".");
        return;
    }
    std::string stopped;
    const auto stop_for_selection=[&] {
        story_walk_.reset();
        if(navigation_.mode()==NavigationMode::Automatic)stopped=tr("Automatic walking off.")+" ";
        else if(navigation_.mode()==NavigationMode::Manual)stopped=tr("Guidance off.")+" ";
        stop_navigation("");
    };
    if(category_changed) {
        destination_leg_.reset();
        destination_crossed_.clear();
        stop_for_selection();
        if(commands&PreviousCategory)navigation_category_=(navigation_category_+target_categories.size()-1)%target_categories.size();
        if(commands&NextCategory)navigation_category_=(navigation_category_+1)%target_categories.size();
        target_id_=memory.targets[navigation_category_];target_type_.clear();
    }
    if(navigation_category_==destination_category) {
        destination_commands(guard.pawn,commands,category_changed);
        return;
    }
    if(active_story&&!current_story.destination_epoch.empty()&&navigation_category_==story_category&&!target_id_.empty()) {
        const auto current=story_destinations(current_story);
        if(std::none_of(current.begin(),current.end(),[&](const auto& step){return story_target_id(current_story,step)==target_id_;})) {
            stop_for_selection();
            target_id_.clear();target_type_.clear();memory.targets[story_category].clear();
        }
    }
    const auto operate_navigation=[&]() {
        if(!(commands&(Route|ToggleGuidance|ToggleAutomatic)))return false;
        if(navigation_category_==story_category&&!target_id_.empty()) {
            const auto& snapshot=story_.snapshot();
            for(const auto& destination:story_destinations(snapshot))if(story_target_id(snapshot,destination)==target_id_&&
                !story_route_available(snapshot,destination)) {
                if(navigation_.mode()!=NavigationMode::Off)stop_navigation("",true);
                active_target_.reset();
                speech_.say(story_status(snapshot)+" "+tr(destination.unavailable_reason));return true;
            }
        }
        if(target_id_.empty())return false;
        auto* live_target=refresh_navigation_target(guard.pawn);
        if(!active_target_||active_target_->id!=target_id_||
           active_target_->world!=utf8(path(actor_world(guard.pawn)))||
           active_target_->player!=utf8(path(guard.pawn))) {
            stop_navigation("");
            if(!navigation_target_reason_.empty()){speech_.say(trf("{0} The selection is retained; try again.",{tr(navigation_target_reason_)}));return true;}
            target_id_.clear();target_type_.clear();memory.targets[navigation_category_].clear();
            speech_.say(tr("The selected target is no longer available in this area."));return true;
        }
        if(commands&Route) {
            if(nearby_navigation_target(guard.pawn,live_target)&&!is_story_shrine_bridge_goal(live_target,active_target_->position)) {
                speech_.say(trf("At {0}. Interaction available.",{active_target_->label}));
                if(!(commands&(ToggleGuidance|ToggleAutomatic)))return true;
            } else {
                std::string reason;
                const auto path=route_snapshot(guard.pawn,live_target,active_target_->position,false,&reason);
                const auto guidance=path?describe_route(path->snapshot):std::optional<std::string>{};
                log("NAV explicit route points="+std::to_string(path?path->snapshot.points.size():0)+" reason="+reason);
                const bool doorway=path&&!path->room_door.empty();
                speech_.say(path&&path->activation_reached?tr(doorway?hall_door_arrival:"At the target's activation area."):
                    guidance?(doorway?trf("First, go to the door to the hall. {0}",{*guidance}):*guidance):
                    reason.empty()?tr("A complete native walking route is unavailable."):tr(reason));
            }
        }
        if(commands&(ToggleGuidance|ToggleAutomatic)) {
            const auto requested=commands&ToggleAutomatic?NavigationMode::Automatic:NavigationMode::Manual;
            if(story_walk_&&requested==NavigationMode::Automatic&&!(commands&ControllerOrigin)) {
                story_walk_.reset();stop_navigation("");speech_.say(tr("Automatic Story walking off."));return true;
            }
            if(navigation_.mode()==requested) {
                if(commands&ControllerOrigin){speech_.say(tr(requested==NavigationMode::Automatic?"Automatic walking is already on.":"Guidance is already on."));return true;}
                navigation_output(navigation_.toggle(requested,{}));return true;
            }
            if(requested==NavigationMode::Automatic&&!guard.actuator) {
                speech_.say(trf("Automatic walking unavailable. {0}",{tr(guard.actuator_reason)}));return true;
            }
            const bool controller_start=requested==NavigationMode::Automatic&&(commands&ControllerOrigin);
            if(controller_start)controller_automatic_starter_grace();
            else clear_controller_starter_grace();
            auto nav=navigation_frame(guard.pawn,true,guard.eligible,guard.foreground,guard.physical_input,
                requested==NavigationMode::Automatic);
            const auto started=navigation_.toggle(requested,nav);
            navigation_controller_=navigation_.mode()!=NavigationMode::Off&&(commands&ControllerOrigin);
            controller_start_pending_=navigation_.mode()==NavigationMode::Automatic&&controller_start;
            navigation_output(started);
            if(navigation_category_==story_category) {
                if(requested==NavigationMode::Automatic)arm_story_walk(guard.pawn,controller_start,started);
                else story_walk_.reset();
            } else if(started.mode!=NavigationMode::Off) {
                navigation_field_exit_.reset();
                if(is(live_target,L"/Script/mucha.CPP_MapTransitionTrigger")) {
                    const auto here=loaded_travel_location(guard.pawn);
                    const auto exit=capture_story_exit_route(guard.pawn,live_target);
                    const auto trigger=read<int>(live_target,L"m_UniqueId",L"IntProperty");
                    if(here&&exit&&trigger&&*trigger>=0&&!exit->event_only&&exit->destination_point&&
                       target_id_==utf8(path(exit->actor))+exit->route_key)
                        navigation_field_exit_=FieldExit{*here,{here->point.map,*trigger,
                            {exit->destination_map,*exit->destination_point}}};
                }
            }
        }
        return true;
    };
    constexpr unsigned selection_commands=Previous|Next|PreviousCategory|NextCategory;
    if(!(commands&selection_commands)&&operate_navigation())return;
    bool selected_unreachable=false,checking_routes=false;
    AreaCaptureReport report;
    std::vector<Target> current;
    if(navigation_category_==story_category) {
        current=collect_story_targets(guard.pawn,story_.snapshot());
    } else {
        stage="capture live area entries";
        const auto origin=controlled_agent_position(guard.pawn);
        const auto area_now=GetTickCount64();
        if(!origin||!finite(*origin)) {
            area_browse_.reset();
            report.unavailable_reason="The player position could not be read.";
        } else {
            if(!area_browse_||area_browse_->area!=area||area_browse_->revision!=area_browse_revision_)
                area_browse_=AreaBrowse{.area=area,.revision=area_browse_revision_};
            auto& browse=*area_browse_;
            std::erase_if(browse.routes,[area_now](const auto& result) {
                return area_now-result.second.sampled>area_route_proof_ms;
            });
            const auto started=std::chrono::steady_clock::now();
            auto entries=capture_area_targets(guard.pawn);
            report=area_capture_report();
            const auto radius=getter<float>(object(guard.pawn,L"CapsuleComponent"),L"GetScaledCapsuleRadius").value_or(0.f);
            std::vector<std::string> unchecked;
            for(auto& entry:entries) {
                if(navigation_category_&&entry.category!=navigation_category_)continue;
                if(!entry.snapshot.interactable) {
                    const auto check=browse.routes.find(entry.snapshot.id);
                    if(check==browse.routes.end()||!same_area_position(check->second.position,entry.position)||
                       (!same_area_position(check->second.origin,*origin)&&
                        !(check->second.reachable&&nearby_area_origin(check->second.origin,*origin,radius)))) {
                        unchecked.push_back(entry.snapshot.id);
                        continue;
                    }
                    if(!check->second.reachable) {
                        selected_unreachable|=entry.snapshot.id==target_id_;
                        continue;
                    }
                }
                current.push_back({std::move(entry.snapshot),entry.actor,entry.distance,entry.category,
                    std::move(entry.type),entry.position});
            }
            checking_routes=!unchecked.empty();
            if(browse.category!=navigation_category_) {
                browse.announced_wait=false;
                browse.probes=browse.total_us=browse.max_us=0;
            }
            browse.category=navigation_category_;
            browse.checks=std::move(unchecked);
            browse.next=0;
            browse.request=0;
            const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now()-started).count();
            log("NAV area browse category="+std::to_string(browse.category)+" reachable="+std::to_string(current.size())+
                " pending="+std::to_string(browse.checks.size())+" capture_us="+std::to_string(elapsed));
        }
    }
    if(selected_unreachable&&navigation_.mode()==NavigationMode::Off) {
        target_id_.clear();target_type_.clear();memory.targets[navigation_category_].clear();
    }
    const auto issue=!report.unavailable_reason.empty()?report.unavailable_reason:
        report.unreadable[navigation_category_]?report.first_issue[navigation_category_]:std::string{};
    const auto issue_key=area+"|"+std::to_string(navigation_category_)+"|"+std::to_string(report.unreadable[navigation_category_])+"|"+issue;
    const auto partial_warning=!issue.empty()&&issue_key!=area_read_status_?
        trf("{0} Some targets could not be read.",{tr(issue)})+" ":std::string{};
    area_read_status_=issue_key;
    if(checking_routes) {
        auto& browse=*area_browse_;
        // Cycling a partial list can skip exits added before the selection.
        // Replay the selection only; never defer a command that starts walking.
        const auto selection=commands&(Previous|Next);
        browse.request=selection?selection:ReviewNavigation;
        if(!browse.announced_wait) {
            browse.announced_wait=true;
            speech_.say(stopped+trf("{0}. Checking reachable targets.",{tr(target_categories[navigation_category_])}));
        }
        return;
    }
    if(area_browse_)area_browse_->announced_wait=false;
    targets_.clear();for(const auto& target:current)targets_.push_back(target.snapshot);
    if(current.empty()) {
        if(issue.empty()&&navigation_.mode()==NavigationMode::Off){target_id_.clear();target_type_.clear();}
        speech_.say(stopped+tr(target_categories[navigation_category_])+". "+
            (navigation_category_==story_category?story_status(story_.snapshot()):
                issue.empty()?tr("No reachable targets in this area."):tr(issue)));return;
    }
    auto selected=std::find_if(current.begin(),current.end(),[&](const auto& target){return target.snapshot.id==target_id_;});
    int index=selected==current.end()?-1:static_cast<int>(selected-current.begin());
    const bool only_exit=navigation_category_==3&&!checking_routes&&current.size()==1&&
        (commands&(Route|ToggleGuidance|ToggleAutomatic));
    if(index<0&&!target_id_.empty()&&(commands&ReviewNavigation)&&!category_changed&&!(commands&(Previous|Next))) {
        if(navigation_category_==story_category) {
            const auto live=refresh_navigation_target(guard.pawn);
            if(!live&&!navigation_target_reason_.empty()) {
                stop_navigation("");speech_.say(trf("{0} The selection is retained; try again.",{tr(navigation_target_reason_)}));return;
            }
            if(live&&active_target_) {
                speech_.say(trf("{0} remains selected. Use Page Up or Page Down to choose another target.",{active_target_->label}));return;
            }
        }
        if(navigation_.mode()!=NavigationMode::Off) {
            speech_.say(tr("Waiting for a walking route."));
            return;
        }
    }
    if(index<0&&(category_changed||(commands&ReviewNavigation)||only_exit||(navigation_category_==story_category&&!(commands&(Previous|Next)))))index=0;
    if(commands&Next)index=(index+1)%static_cast<int>(current.size());
    if(commands&Previous)index=(index<=0?static_cast<int>(current.size()):index)-1;
    if(index<0){speech_.say(tr("Choose a target with Page Up or Page Down, or F6 and F7."));return;}
    const auto& target=current[static_cast<std::size_t>(index)];
    if(target_id_!=target.snapshot.id)stop_for_selection();
    target_id_=target.snapshot.id;target_type_=target.type;memory.targets[navigation_category_]=target_id_;
    if(target.position)active_target_=KnownTarget{target_id_,utf8(path(actor_world(guard.pawn))),utf8(path(guard.pawn)),
        target.snapshot.label,target.type,*target.position};
    else active_target_.reset();
    if(commands&(Previous|Next|PreviousCategory|NextCategory|ReviewNavigation)) {
        std::string selected_text=describe_target(target.snapshot).value_or(tr("Target unavailable"));
        if(target.story) {
            const auto& snapshot=story_.snapshot();
            if(category_changed||(commands&ReviewNavigation))
                selected_text=story_details_status(snapshot)+(target.snapshot.label==snapshot.objective?"":" "+selected_text);
            if(!target.story_detail.empty())selected_text+=". "+target.story_detail;
            for(const auto& destination:story_destinations(snapshot))if(story_target_id(snapshot,destination)==target_id_&&!destination.position)
                selected_text+=" "+tr(destination.unavailable_reason);
        } else if(target.position) {
            if(const auto spatial=story_spatial_context(guard.pawn)) {
                const auto detail=describe_field_position(*target.position,*spatial);
                if(!detail.empty())selected_text+=". "+detail;
            }
        }
        const auto position=checking_routes?std::string{}:". "+trf("{0} of {1}",{std::to_string(index+1),std::to_string(current.size())});
        speech_.say(SpeechEvent{stopped+((category_changed||(commands&ReviewNavigation))?
            tr(target_categories[navigation_category_])+". ":"")+partial_warning+selected_text+position,
            true,SpeechDelivery::LatestSelection});
    }
    operate_navigation();
}
}
