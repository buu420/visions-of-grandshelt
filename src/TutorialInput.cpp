#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/TutorialInput.hpp"
#include "resonance/NativeMetadata.hpp"
#include "NativeUiPresentation.hpp"
#include "TutorialInputValidation.hpp"
#include "StoryInputValidation.hpp"
#include <cstring>
#include <cwctype>
#include <memory>
#include <set>

// Exact-build TUTORIAL.md, jmap-activatable-actionguide.txt and pinned JMAP.
// All native observations stay on the game thread; only instructions leave it.
namespace resonance {
namespace {
using namespace resonance::unreal;
using namespace tutorial_input_detail;
constexpr std::uint32_t excluded=0x10u|0x20u|0x8000u|0x10000u|0x40000000u;
bool live(UObject* o){return o&&UObjectGlobals::IsValidObjectForFindXOf(o)&&!(static_cast<std::uint32_t>(o->GetObjectFlags())&excluded);}
std::wstring path(UObject* o) {
    if(!o)return {};auto result=o->GetFullName();const auto space=result.find(L' ');
    return space==std::wstring::npos?result:result.substr(space+1);
}
bool is(UObject* o,const wchar_t* type) {
    if(!o)return false;int depth=0;
    for(auto* c=static_cast<UStruct*>(o->GetClassPrivate());c&&depth++<64;c=c->GetSuperStruct())if(path(c)==type)return true;
    return false;
}
std::string text(std::wstring value) {
    if(value.size()>16384)throw std::runtime_error("Oversized tutorial text");
    std::wstring clean;bool tag=false,space=false;
    for(auto c:value) {
        if(c==L'<'){tag=true;continue;}if(c==L'>'&&tag){tag=false;continue;}if(tag)continue;
        if(iswspace(c)){space=!clean.empty();continue;}if(c<32||c==127)continue;
        if(space){clean+=L' ';space=false;}clean+=c;
    }
    if(clean.empty())return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,clean.data(),static_cast<int>(clean.size()),nullptr,0,nullptr,nullptr);
    if(count<1)throw std::runtime_error("Invalid tutorial text");
    std::string result(static_cast<std::size_t>(count),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,clean.data(),static_cast<int>(clean.size()),result.data(),count,nullptr,nullptr)!=count)
        throw std::runtime_error("Invalid tutorial text");
    return result;
}
bool same(FName a,FName b){return a.ComparisonIndex==b.ComparisonIndex&&a.Number==b.Number;}
bool named(FName name,const wchar_t* expected) {
    FName known(expected,FNAME_Find);return (known.ComparisonIndex||known.Number)&&same(name,known);
}
FProperty* field(UObject* o,const wchar_t* name,int offset,int size) {
    if(!live(o))return nullptr;auto* p=o->GetPropertyByNameInChain(name);
    return p&&p->GetSize()==size&&(offset<0||p->GetOffset_Internal()==offset)&&p->GetOffset_Internal()>=0&&
        p->GetOffset_Internal()+size<=o->GetClassPrivate()->GetPropertiesSize()?p:nullptr;
}
void* address(UObject* o,const wchar_t* name,int offset,int size) {
    return field(o,name,offset,size)?o->GetValuePtrByPropertyNameInChain(name):nullptr;
}
template<class T> std::optional<T> read(UObject* o,const wchar_t* name,int offset) {
    auto* p=address(o,name,offset,sizeof(T));if(!p)return {};T result{};std::memcpy(&result,p,sizeof result);return result;
}
UObject* object(UObject* o,const wchar_t* name,int offset=-1) {
    const auto value=read<UObject*>(o,name,offset);return value&&live(*value)?*value:nullptr;
}
template<class T> T pod(const std::byte* p,std::size_t offset){T result{};std::memcpy(&result,p+offset,sizeof result);return result;}
template<class T> struct Array {T* data{};int num{},max{};};
UScriptStruct* structure(FProperty* property,const wchar_t* expected,int size) {
    auto& handle=static_cast<FStructProperty*>(property)->GetStruct();UScriptStruct* result{};
    std::memcpy(&result,std::addressof(handle),sizeof result);
    return native_metadata(result,NativeMetadataKind::ScriptStruct)&&path(result)==expected&&
        result->GetPropertiesSize()==size?result:nullptr;
}
struct Param {const wchar_t* name;int offset,size;const wchar_t* type{};int stride{};};
class Call {
    UFunction* function_{};
    alignas(16)std::array<std::byte,512> data_{};
    std::array<std::unique_ptr<NativeValue<FProperty>>,8> values_{};
public:
    Call(UObject* receiver,const wchar_t* name,const wchar_t* expected,int allocation,int return_offset,
            std::initializer_list<Param> params,bool library=false) {
        if(library) {
            if(!receiver||
               (static_cast<std::uint32_t>(receiver->GetObjectFlags())&(0x8000u|0x10000u|0x40000000u))||
               !(static_cast<std::uint32_t>(receiver->GetObjectFlags())&0x10u)||
               path(receiver)!=L"/Script/Engine.Default__KismetInputLibrary"||
               path(receiver->GetClassPrivate())!=L"/Script/Engine.KismetInputLibrary")return;
        } else if(!live(receiver))return;
        auto* function=receiver->GetFunctionByNameInChain(name);
        if(!function||path(function)!=expected||!function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400))||
           (library&&!function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x2000)))||params.size()>values_.size())return;
        int return_size=0;for(const auto& p:params)if(std::wstring_view(p.name)==L"ReturnValue")return_size=p.size;
        if(story_input_detail::native_call_layout(function->GetParmsSize(),function->GetPropertiesSize(),function->GetReturnValueOffset(),
            allocation,return_offset,return_size,static_cast<int>(data_.size()))!=story_input_detail::NativeLayoutResult::Valid)return;
        std::size_t index=0;
        for(const auto& spec:params) {
            FName name_key(spec.name,FNAME_Find);if(!name_key.ComparisonIndex&&!name_key.Number)return;
            auto* p=function->FindProperty(name_key);
            if(!p||p->GetOffset_Internal()!=spec.offset||p->GetSize()!=spec.size||!p->HasAnyPropertyFlags(0x80)||
               spec.offset<0||spec.size<1||spec.offset>allocation-spec.size)return;
            if(std::wstring_view(spec.name)==L"ReturnValue"&&
               (p!=function->GetReturnProperty()||!p->HasAnyPropertyFlags(0x400)))return;
            if(std::wstring_view(spec.name)==L"OutData"&&(!p->HasAnyPropertyFlags(0x100)||
               !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400000))||
               !function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(1))))return;
            if(spec.stride) {
                auto* inner=static_cast<FArrayProperty*>(p)->GetInner();
                if(!inner||inner->GetSize()!=spec.stride||(spec.type&&!structure(inner,spec.type,spec.stride)))return;
            } else if(spec.type&&!structure(p,spec.type,spec.size))return;
            values_[index++]=std::make_unique<NativeValue<FProperty>>(*p,data_.data()+spec.offset);
        }
        function_=function;
    }
    Call(const Call&)=delete;Call& operator=(const Call&)=delete;
    explicit operator bool()const{return function_!=nullptr;}
    void invoke(UObject* receiver){receiver->ProcessEvent(function_,data_.data());}
    template<class T> T get(std::size_t offset)const{return pod<T>(data_.data(),offset);}
    template<class T> void put(std::size_t offset,const T& value){std::memcpy(data_.data()+offset,&value,sizeof value);}
    std::byte* data(){return data_.data();}
};
template<class T> std::optional<T> getter(UObject* receiver,const wchar_t* name,const wchar_t* expected) {
    Call call(receiver,name,expected,sizeof(T),0,{{L"ReturnValue",0,sizeof(T)}});if(!call)return {};
    call.invoke(receiver);return call.get<T>(0);
}
UObject* get_object(UObject* receiver,const wchar_t* name,const wchar_t* expected) {
    const auto value=getter<UObject*>(receiver,name,expected);return value&&live(*value)?*value:nullptr;
}
std::vector<UObject*> instances(const wchar_t* type) {
    std::vector<UObject*> result;UObjectGlobals::FindAllOf(type,result);
    std::erase_if(result,[](auto* o){return !live(o);});return result;
}
UObject* scoped(const wchar_t* type,UObject* owner) {
    UObject* result=nullptr;
    for(auto* candidate:instances(type))if(candidate->GetOuterPrivate()==owner){if(result)return nullptr;result=candidate;}
    return result;
}
struct WorldAccess {
    bool valid(UObject* o)const{return live(o);}bool is_world(UObject* o)const{return is(o,L"/Script/Engine.World");}
    bool is_level(UObject* o)const{return is(o,L"/Script/Engine.Level");}
    UObject* level_world(UObject* o)const{return object(o,L"OwningWorld",192);}UObject* outer(UObject* o)const{return o->GetOuterPrivate();}
};
UObject* input_manager(UObject* world) {
    auto all=instances(L"CPP_InputManager");
    return story_input_detail::unique_world_member<UObject>(all,world,[](auto* o){return story_input_detail::resolved_world(o,WorldAccess{});}).object;
}
struct RenderAccess {
    UObject* controller;
    UObject* active_root;
    bool valid(UObject* o)const{return live(o)&&is(o,L"/Script/UMG.Widget");}
    story_input_detail::RenderFacts facts(UObject* widget)const {
        story_input_detail::RenderFacts f;f.user_widget=is(widget,L"/Script/UMG.UserWidget");
        const auto visibility=getter<unsigned char>(widget,L"GetVisibility",L"/Script/UMG.Widget:GetVisibility");
        const auto opacity=getter<float>(widget,L"GetRenderOpacity",L"/Script/UMG.Widget:GetRenderOpacity");
        if(!visibility||!opacity)return f;f.visibility=*visibility;f.opacity=*opacity;f.alpha=1;f.owner_matches=true;
        if(f.user_widget) {
            auto* owner=get_object(widget,L"GetOwningPlayer",L"/Script/UMG.Widget:GetOwningPlayer");
            auto* color=address(widget,L"ColorAndOpacity",416,16);if(!owner||!color)return f;
            f.owner_matches=owner==controller;f.alpha=pod<float>(static_cast<std::byte*>(color),12);
        }
        f.known=true;return f;
    }
    std::optional<UObject*> parent(UObject* widget)const {
        if(widget==active_root)return static_cast<UObject*>(nullptr);
        auto value=getter<UObject*>(widget,L"GetParent",L"/Script/UMG.Widget:GetParent");
        return value&&(!*value||(live(*value)&&is(*value,L"/Script/UMG.PanelWidget")))?value:std::nullopt;
    }
    std::optional<UObject*> tree_owner(UObject* widget)const {
        if(widget==active_root)return static_cast<UObject*>(nullptr);
        std::set<UObject*> seen;
        for(auto* o=widget->GetOuterPrivate();live(o);o=o->GetOuterPrivate()) {
            if(seen.size()>=16||!seen.insert(o).second)return {};
            if(is(o,L"/Script/UMG.WidgetTree")) {
                auto* owner=o->GetOuterPrivate();
                if(!live(owner)||!is(owner,L"/Script/UMG.UserWidget")||object(owner,L"WidgetTree",728)!=o||object(o,L"RootWidget",48)!=widget)return {};
                return owner;
            }
            if(is(o,L"/Script/UMG.Widget"))return {};
        }
        return static_cast<UObject*>(nullptr);
    }
    std::optional<bool> in_viewport(UObject* widget)const {
        if(widget==active_root)return true;
        const auto value=getter<unsigned char>(widget,L"IsInViewport",L"/Script/UMG.Widget:IsInViewport");
        return value?std::optional<bool>(*value!=0):std::nullopt;
    }
};
bool rendered(UObject* widget,UObject* controller,UObject* active_root) {
    return story_input_detail::rendered_widget_chain(widget,RenderAccess{controller,active_root},active_root);
}
UObject* viewmodel(UObject* widget,const wchar_t* name,const wchar_t* type) {
    auto* cls=UObjectGlobals::StaticFindObject(L"/Script/ModelViewViewModel.MVVMView");
    if(!native_metadata(cls,NativeMetadataKind::Class)||path(cls)!=L"/Script/ModelViewViewModel.MVVMView")return nullptr;
    Call extension(widget,L"GetExtension",L"/Script/UMG.UserWidget:GetExtension",16,8,{{L"ExtensionType",0,8},{L"ReturnValue",8,8}});
    if(!extension)return nullptr;extension.put(0,cls);extension.invoke(widget);auto* view=extension.get<UObject*>(8);
    if(!live(view)||!is(view,L"/Script/ModelViewViewModel.MVVMView")||view->GetOuterPrivate()!=widget)return nullptr;
    Call call(view,L"GetViewModel",L"/Script/ModelViewViewModel.MVVMView:GetViewModel",24,8,{{L"ViewModelName",0,8},{L"ReturnValue",8,16}});
    if(!call)return nullptr;FName key(name,FNAME_Find);if(!key.ComparisonIndex&&!key.Number)return nullptr;
    call.put(0,key);call.invoke(view);auto* result=call.get<UObject*>(8);
    return live(result)&&call.get<void*>(16)&&is(result,type)?result:nullptr;
}
std::optional<std::vector<UObject*>> descendants(UObject* root) {
    std::vector<UObject*> result{root};
    for(std::size_t i=0;i<result.size();++i) {
        auto* widget=result[i];if(!live(widget)||!is(widget,L"/Script/UMG.Widget")||result.size()>1024)return {};
        std::vector<UObject*> children;
        if(is(widget,L"/Script/UMG.UserWidget")) {
            auto* tree=object(widget,L"WidgetTree",728);if(!tree||tree->GetOuterPrivate()!=widget)return {};
            auto* child=object(tree,L"RootWidget",48);if(!child)return {};children.push_back(child);
        } else if(is(widget,L"/Script/UMG.PanelWidget")) {
            const auto count=getter<int>(widget,L"GetChildrenCount",L"/Script/UMG.PanelWidget:GetChildrenCount");if(!count||*count<0||*count>128)return {};
            for(int n=0;n<*count;++n) {
                Call call(widget,L"GetChildAt",L"/Script/UMG.PanelWidget:GetChildAt",16,8,{{L"Index",0,4},{L"ReturnValue",8,8}});
                if(!call)return {};call.put(0,n);call.invoke(widget);auto* child=call.get<UObject*>(8);
                if(!live(child)||get_object(child,L"GetParent",L"/Script/UMG.Widget:GetParent")!=widget)return {};children.push_back(child);
            }
        }
        for(auto* child:children){if(std::find(result.begin(),result.end(),child)!=result.end())return {};result.push_back(child);}
    }
    return result;
}
bool capture_pages(UObject* layout,Observation& o) {
    auto* tutorial=object(layout,L"WBP_Tutorial");if(!tutorial)return false;
    const auto index=read<int>(tutorial,L"PageIndex",-1);
    auto* property=field(tutorial,L"Data",-1,48);auto* data=static_cast<std::byte*>(address(tutorial,L"Data",-1,48));
    if(!index||!property||!data)return false;auto* type=structure(property,L"/Script/mucha.TutorialAssetData",48);if(!type)return false;
    FName pages_name(L"Pages",FNAME_Find);auto* pages=type->FindProperty(pages_name);
    if(!pages||pages->GetOffset_Internal()!=24||pages->GetSize()!=16)return false;
    auto* inner=static_cast<FArrayProperty*>(pages)->GetInner();
    if(!inner||!structure(inner,L"/Script/mucha.TutorialPage",120))return false;
    const auto list=pod<Array<std::byte>>(data,24);if(!valid_array(list.data,list.num,list.max,64))return false;
    o.page_index=*index;o.page_count=list.num;return true;
}
std::optional<std::vector<FName>> display_tags(UObject* vm) {
    Call call(vm,L"GetDisplayKeyGuideTagList",L"/Script/mucha.CPP_VM_KeyGuideList:GetDisplayKeyGuideTagList",16,0,
        {{L"ReturnValue",0,16,L"/Script/GameplayTags.GameplayTag",8}});
    if(!call)return {};call.invoke(vm);const auto list=call.get<Array<FName>>(0);
    if(!valid_array(list.data,list.num,list.max,16))return {};
    std::vector<FName> result;for(int i=0;i<list.num;++i)result.push_back(list.data[i]);return result;
}
UObject* guide_layout(UObject* gi,UObject* controller,UObject* action_vm,UObject* list_vm) {
    auto* widget=active_layout_layer(gi,L"UI.Layer.ActionGuide");
    if(!is(widget,L"/Game/UI/System/ActionGuide/Widget/WBP_ActionGuideLayout.WBP_ActionGuideLayout_C")||
       !rendered(widget,controller,widget)||
       viewmodel(widget,L"CPP_VM_ActionGuide",L"/Script/mucha.CPP_VM_ActionGuide")!=action_vm||
       viewmodel(widget,L"CPP_VM_KeyGuideList",L"/Script/mucha.CPP_VM_KeyGuideList")!=list_vm)return nullptr;
    return widget;
}
UObject* guide_widget(UObject* root,UObject* controller,FName tag) {
    const auto all=descendants(root);if(!all)return nullptr;UObject* result=nullptr;
    for(auto* widget:*all)if(is(widget,L"/Script/mucha.CPP_WBP_TagKeyGuide")) {
        const auto current=read<FName>(widget,L"KeyGuideTag",1216);
        if(!current||!same(*current,tag)||!rendered(widget,controller,root))continue;
        if(result)return nullptr;result=widget;
    }
    return result;
}
std::string key_name(FName name,FName gamepad) {
    if(!name.ComparisonIndex&&!name.Number)return {};
    if(name.ToString()==L"Gamepad_FaceButton_Right"&&(gamepad.ComparisonIndex||gamepad.Number)) {
        auto device=gamepad.ToString();
        std::transform(device.begin(),device.end(),device.begin(),[](wchar_t c){return std::towlower(c);});
        if(device.starts_with(L"xbox")||device==L"xinput"||device==L"generic")return "B";
        if(device.starts_with(L"ps")||device.starts_with(L"playstation"))return tr("Circle");
    }
    auto* library=UObjectGlobals::StaticFindObject(L"/Script/Engine.Default__KismetInputLibrary");
    Call call(library,L"Key_GetDisplayName",L"/Script/Engine.KismetInputLibrary:Key_GetDisplayName",48,32,
        {{L"Key",0,24,L"/Script/InputCore.Key"},{L"bLongDisplayName",24,1},{L"ReturnValue",32,16}},true);
    auto* key_type=UObjectGlobals::StaticFindObject(L"/Script/InputCore.Key");
    if(!call||!native_metadata(key_type,NativeMetadataKind::ScriptStruct))return {};
    auto* key_name=static_cast<UStruct*>(key_type)->FindProperty(FName(L"KeyName",FNAME_Find));
    if(!key_name||key_name->GetOffset_Internal()!=0||key_name->GetSize()!=sizeof(FName)||
       !key_name->field_class()||key_name->field_class()->GetName()!=L"NameProperty")return {};
    // The native parameter initializer built the FKey. Only the FName is
    // copied from Slate; FKey resolves its own key details for the display name.
    call.put(0,name);call.put(24,static_cast<unsigned char>(1));call.invoke(library);
    return text(reinterpret_cast<const FText*>(call.data()+32)->ToString());
}
template<class T> std::optional<T> memory_value(const void* base,std::uintptr_t offset) {
    if(!base)return {};
    const auto address=reinterpret_cast<std::uintptr_t>(base)+offset;
    SIZE_T copied{};T value{};
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof value,&copied)||
       copied!=sizeof value)return {};
    return value;
}
std::vector<std::string> guide_keys(UObject* guide,UObject* root,UObject* controller,UObject* action) {
    const auto widgets=descendants(guide);if(!widgets)return {};
    std::vector<std::string> result;
    for(auto* widget:*widgets) {
        if(!is(widget,L"/Script/EnhancedUI.CPP_EU_WBP_CurrentDeviceKeyIcon")||
           object(widget,L"AssociatedInputAction",432)!=action||!rendered(widget,controller,root))continue;
        for(auto& key:displayed_key_labels(widget))
            if(std::find(result.begin(),result.end(),key)==result.end())result.push_back(std::move(key));
    }
    return result;
}
void capture_guides(UObject* layout,UObject* controller,UObject* gi,Observation& o) {
    auto* subsystem=scoped(L"CPP_ActionGuideSubsystem",gi);
    auto* owner=address(subsystem,L"GuideOwner",160,8);const auto tag=read<FName>(subsystem,L"GuideTag",88);
    const auto layout_tag=read<FName>(layout,L"ActionGuideTag",1376);
    if(!owner||reinterpret_cast<const FWeakObjectPtr*>(owner)->Get()!=layout||!tag||!layout_tag||!same(*tag,*layout_tag))return;
    auto* manager=object(subsystem,L"KeyGuideManager",72);auto* vm=object(manager,L"KeyGuideListVM",40);
    auto* action_vm=object(subsystem,L"ActionGuideVM",80);
    if(!vm||!is(vm,L"/Script/mucha.CPP_VM_KeyGuideList")||!action_vm||!is(action_vm,L"/Script/mucha.CPP_VM_ActionGuide")||
       !read<unsigned char>(action_vm,L"bDisplay",208).value_or(0))return;
    auto* root=guide_layout(gi,controller,action_vm,vm);if(!root)return;
    o.guide_owner_valid=true;
    const auto tags=display_tags(vm);if(!tags)return;o.guide_list_known=true;
    auto* data_subsystem=scoped(L"CPP_KeyGuideSubsystem",gi);
    for(auto current:*tags) {
        Guide guide;guide.tag=text(current.ToString());guide.close=named(current,L"UI.Common.Key.Guide.Declare.Common.Close");
        auto* widget=guide_widget(root,controller,current);
        Call state(vm,L"GetKeyGuideState",L"/Script/mucha.CPP_VM_KeyGuideList:GetKeyGuideState",12,8,
            {{L"InTag",0,8,L"/Script/GameplayTags.GameplayTag"},{L"ReturnValue",8,2,L"/Script/mucha.KeyGuideStateData"}});
        Call data(data_subsystem,L"FindKeyGuideData",L"/Script/mucha.CPP_KeyGuideSubsystem:FindKeyGuideData",168,160,
            {{L"InTag",0,8,L"/Script/GameplayTags.GameplayTag"},{L"OutData",8,152,L"/Script/mucha.KeyGuideData"},{L"ReturnValue",160,1}});
        if(!state||!data||!widget){o.guides.push_back(std::move(guide));continue;}
        state.put(0,current);state.invoke(vm);data.put(0,current);data.invoke(data_subsystem);
        if(!data.get<unsigned char>(160)){o.guides.push_back(std::move(guide));continue;}
        auto* model=viewmodel(widget,L"CPP_VM_KeyGuide",L"/Script/mucha.CPP_VM_KeyGuide");
        auto* state_data=address(model,L"StateData",316,2);const auto model_tag=read<FName>(model,L"GuideTag",120);
        const auto enabled=getter<unsigned char>(widget,L"GetIsEnabled",L"/Script/UMG.Widget:GetIsEnabled");
        if(!model||model->GetOuterPrivate()!=widget||!model_tag||!same(*model_tag,current)||!state_data||!enabled)
            {o.guides.push_back(std::move(guide));continue;}
        const auto visibility=state.get<unsigned char>(8),enable=state.get<unsigned char>(9);
        guide.known=true;guide.visible=visibility==0&&pod<unsigned char>(static_cast<std::byte*>(state_data),0)==0;
        guide.enabled=enable!=0&&pod<unsigned char>(static_cast<std::byte*>(state_data),1)!=0&&*enabled!=0;
        auto* action=data.get<UObject*>(8);guide.label=text(reinterpret_cast<const FText*>(data.data()+16)->ToString());
        auto* child=object(widget,L"WBP_KeyGuide");auto* live_data=static_cast<std::byte*>(address(child,L"GuideData",864,152));
        auto* live_field=field(child,L"GuideData",864,152);
        if(live_data&&live_field&&structure(live_field,L"/Script/mucha.KeyGuideData",152)&&
           rendered(child,controller,root)&&pod<UObject*>(live_data,0)==action&&live(action)&&is(action,L"/Script/EnhancedInput.InputAction")) {
            guide.keys=guide_keys(child,root,controller,action);
            guide.bindings_known=!guide.keys.empty();
        }
        o.guides.push_back(std::move(guide));
    }
}
}
UObject* active_layout_layer(UObject* gi,const wchar_t* layer_tag) {
    // The debug menu also registers a GameLayoutSubsystem in this instance.
    auto* subsystem=scoped(L"CPP_GameWidgetManagerSubsystem",gi);
    auto* viewport=object(subsystem,L"PrimaryGameLayout",48);
    if(!is(viewport,L"/Script/EnhancedUI.CPP_EU_WBP_GameLayout")||
       !getter<unsigned char>(viewport,L"IsInViewport",L"/Script/UMG.Widget:IsInViewport").value_or(0))return nullptr;
    Call active(viewport,L"GetActiveWidgetFromLayer",L"/Script/EnhancedUI.CPP_EU_WBP_GameLayout:GetActiveWidgetFromLayer",16,8,
        {{L"LayerTag",0,8,L"/Script/GameplayTags.GameplayTag"},{L"ReturnValue",8,8}});
    FName tag(layer_tag,FNAME_Find);
    if(!active||(!tag.ComparisonIndex&&!tag.Number))return nullptr;
    active.put(0,tag);active.invoke(viewport);
    auto* widget=active.get<UObject*>(8);
    return live(widget)?widget:nullptr;
}
std::vector<std::string> displayed_key_labels(UObject* glyph) {
    if(!live(glyph))return {};
    // Steam build 24250053. ApplyKeyInfo (0x4593e40 / 0x4593c30)
    // reads the Slate widget at +0x198. Its child at +0x270 owns the
    // FKeys selected by 0x45a75d0 and drawn by 0x45a87e0.
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto table=memory_value<std::uintptr_t>(glyph,0);
    if(!image||!table)return {};
    std::uintptr_t slate_table{};
    if(*table==image+0x83ad740)slate_table=image+0x83b3b20;
    if(*table==image+0x83aa350)slate_table=image+0x83b31d0;
    if(!slate_table)return {};
    const auto slate=memory_value<const void*>(glyph,0x198).value_or(nullptr);
    if(memory_value<std::uintptr_t>(slate,0)!=slate_table)return {};
    const auto display=memory_value<const void*>(slate,0x270).value_or(nullptr);
    if(memory_value<std::uintptr_t>(display,0)!=image+0x83b3818)return {};
    const auto keys=memory_value<Array<std::byte>>(display,0x350);
    const auto device=memory_value<unsigned char>(display,0x340);
    if(!keys||!device||*device>1||!valid_array(keys->data,keys->num,keys->max,4))return {};
    const auto gamepad=*device==1?memory_value<FName>(display,0x344).value_or(FName{}):FName{};
    std::vector<std::string> result;
    for(int i=0;i<keys->num;++i) {
        const auto key=memory_value<FName>(keys->data,static_cast<std::uintptr_t>(i)*24);
        if(!key)return {};
        auto label=key_name(*key,gamepad);
        if(!label.empty())result.push_back(std::move(label));
    }
    return result;
}
TutorialControls capture_tutorial_controls(UObject* layout) {
    Observation o;o.supported=live(layout)&&is(layout,L"/Script/mucha.CPP_WBP_Tutorial")&&
        layout->GetClassPrivate()->GetNamePrivate().ToString()==L"WBP_TutorialLayout_C";
    if(!o.supported)return {};
    try {
        auto* controller=get_object(layout,L"GetOwningPlayer",L"/Script/UMG.Widget:GetOwningPlayer");
        auto* world=story_input_detail::resolved_world(controller,WorldAccess{});
        auto* gi=object(world,L"OwningGameInstance",552);
        auto* local=get_object(layout,L"GetOwningLocalPlayer",L"/Script/UMG.Widget:GetOwningLocalPlayer");
        auto* tutorial=scoped(L"CPP_TutorialSubsystem",gi);
        o.owner_valid=gi&&is(gi,L"/Script/Engine.GameInstance")&&controller&&local&&is(local,L"/Script/Engine.LocalPlayer")&&
            object(controller,L"Player",840)==local&&object(tutorial,L"m_TutorialWidget",48)==layout;
        const auto can=read<unsigned char>(layout,L"IsCanInput",1345),opening=read<unsigned char>(layout,L"IsOpeningProcess",1346),
            closing=read<unsigned char>(layout,L"IsClosingProcess",1347);
        const auto input=getter<unsigned char>(input_manager(world),L"IsEnableInput",L"/Script/mucha.CPP_InputManager:IsEnableInput");
        o.readiness_known=can&&opening&&closing&&input;
        o.can_input=can.value_or(0)!=0;o.opening=opening.value_or(1)!=0;o.closing=closing.value_or(1)!=0;o.input_enabled=input.value_or(0)!=0;
        if(!o.owner_valid||!o.readiness_known||!o.can_input||o.opening||o.closing||!o.input_enabled)return controls_for(o);
        o.pages_known=capture_pages(layout,o);
        // Guide failures never turn a verified native-ready tutorial into a wait.
        try{capture_guides(layout,controller,gi,o);}catch(const std::exception&){o.guide_list_known=false;}
    } catch(const std::exception&) {o.guide_list_known=false;}
    return controls_for(o);
}
}
