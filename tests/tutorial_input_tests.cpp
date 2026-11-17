#include "../src/TutorialInputValidation.hpp"
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>
using namespace resonance;
using namespace resonance::tutorial_input_detail;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Observation ready() {
    Observation o;o.supported=o.owner_valid=o.readiness_known=o.can_input=o.input_enabled=true;
    o.pages_known=true;o.page_index=1;o.page_count=2;o.guide_owner_valid=o.guide_list_known=true;
    o.guides={{"page","Change page",true,true,true,false,true,{"Custom previous","Custom next"}},
        {"close","Close",true,true,true,true,true,{"Custom cancel"}}};return o;
}
void owner_and_readiness() {
    require(!controls_for({}).supported,"unrelated layouts must stay unsupported");
    require(controls_for(ready()).ready,"a verified native-ready tutorial must remain readable");
    for(int guard=0;guard<6;++guard) {
        auto o=ready();if(guard==0)o.owner_valid=false;if(guard==1)o.readiness_known=false;
        if(guard==2)o.can_input=false;if(guard==3)o.opening=true;if(guard==4)o.closing=true;if(guard==5)o.input_enabled=false;
        const auto r=controls_for(o);require(r.supported&&!r.ready&&r.instructions.empty(),"wrong owners or actual native open/close/input locks must not advertise active controls");
    }
}
void current_binding_and_guide_order() {
    auto o=ready();auto r=controls_for(o);
    require(r.instructions=="Change page: Custom previous or Custom next. Close: Custom cancel.","announce current native keys in displayed guide order without hardcoded defaults");
    std::swap(o.guides[0],o.guides[1]);r=controls_for(o);
    require(r.instructions.starts_with("Close: Custom cancel."),"native displayed order must be preserved");
}
void final_page_only_close() {
    auto o=ready();o.page_index=0;auto r=controls_for(o);
    require(r.ready&&r.instructions.find("Custom cancel")==std::string::npos&&r.instructions.find("Go to the last page to close.")!=std::string::npos,
        "stale close guides on nonfinal pages must not promise a working close key");
    o.page_count=1;r=controls_for(o);
    require(r.instructions.find("Custom cancel")!=std::string::npos,"a single final page can close when its native guide is enabled");
}
void disabled_or_hidden_close() {
    auto o=ready();o.guides[1].enabled=false;auto r=controls_for(o);
    require(r.ready&&r.instructions.find("Close:")==std::string::npos,"a greyed out close hint must not be advertised as enabled");
    o.guides[1].enabled=true;o.guides[1].visible=false;r=controls_for(o);
    require(r.instructions.find("Close:")==std::string::npos,"hidden close guides are not instructions");
}
void unknown_guides_do_not_block_text() {
    auto o=ready();o.guide_owner_valid=false;auto r=controls_for(o);
    require(r.ready&&r.instructions=="Tutorial controls are unavailable.","unrelated/stale action-guide owners must not block native-ready tutorial text");
    o=ready();o.guides[1].bindings_known=false;o.guides[1].keys.clear();r=controls_for(o);
    require(r.ready&&r.instructions.find("Close: current binding unavailable.")!=std::string::npos,
        "missing custom bindings must be explicit, never filled with shipped defaults");
    o=ready();o.guides[0].known=false;r=controls_for(o);
    require(r.ready&&r.instructions.find("Close: Custom cancel.")!=std::string::npos&&r.instructions.find("unavailable")!=std::string::npos,
        "one unknown guide must not discard other current controls or tutorial text");
}
void malformed_observations() {
    for(const auto page:std::array<std::array<int,2>,4>{{{-1,2},{2,2},{0,0},{0,65}}}) {
        auto o=ready();o.page_index=page[0];o.page_count=page[1];auto r=controls_for(o);
        require(r.ready&&r.instructions=="Tutorial controls are unavailable.","malformed page metadata cannot authorize close or last-page promises");
    }
    auto o=ready();o.guides.push_back(o.guides[0]);auto r=controls_for(o);
    require(r.ready&&r.instructions=="Tutorial controls are unavailable.","duplicate displayed guide identities fail closed without suppressing text");
    o=ready();o.guides.resize(17,o.guides[0]);r=controls_for(o);
    require(r.ready&&r.instructions=="Tutorial controls are unavailable.","native guide capture remains bounded");
}
void native_array_bounds() {
    std::array<int,2> data{};
    require(valid_array(data.data(),1,2,64)&&valid_array(nullptr,0,0,64),"valid bounded native arrays are usable");
    require(!valid_array(nullptr,1,1,64)&&!valid_array(data.data(),-1,2,64)&&
        !valid_array(data.data(),3,2,64)&&!valid_array(data.data(),65,65,64)&&!valid_array(nullptr,0,4,64),
        "malformed native arrays must be rejected before reading keys/pages/tags");
}
void visible_text_projection() {
    const MenuSnapshot original{"tutorial","Visible caption and body","old hints",0,2,true,true,true};
    auto menu=original;apply_tutorial_controls(menu,{});
    require(menu.ready&&menu.label==original.label&&menu.value==original.value,"unsupported capture must leave existing text, readiness and hints unchanged");
    auto unknown=ready();unknown.owner_valid=false;
    auto controls=controls_for(unknown);menu=original;apply_tutorial_controls(menu,controls);
    require(!controls.readiness_known&&menu.ready&&menu.label==original.label&&menu.value=="Tutorial controls are unavailable.",
        "an unknown owner/readiness lookup must not silence an already readable tutorial body");
    auto locked=ready();locked.opening=true;controls=controls_for(locked);menu=original;apply_tutorial_controls(menu,controls);
    require(controls.readiness_known&&!menu.ready&&menu.label==original.label&&menu.value.empty(),
        "a positively known native input lock withholds speech and active-control hints");
    controls=controls_for(ready());menu=original;apply_tutorial_controls(menu,controls);
    require(controls.readiness_known&&menu.ready&&menu.label==original.label&&menu.value==controls.instructions,
        "native-ready capture appends current hints without replacing the visible body");
    menu=original;menu.ready=false;apply_tutorial_controls(menu,controls);
    require(!menu.ready,"positive native readiness must preserve the existing Reader opening/closing gate");
}
struct Owned {virtual ~Owned()=default;std::shared_ptr<int> key;std::string text;};
struct Property {
    int initialized{},copied{},destroyed{};bool fail{};
    void InitializeValue(void* p){if(fail)throw std::runtime_error("init");++initialized;new(p)Owned;}
    void CopyCompleteValue(void* to,const void* from){++copied;*static_cast<Owned*>(to)=*static_cast<const Owned*>(from);}
    void DestroyValue(void* p){++destroyed;static_cast<Owned*>(p)->~Owned();}
};
void native_key_copy_lifetime() {
    Property p;alignas(Owned)std::array<std::byte,sizeof(Owned)> storage{};
    Owned source;source.key=std::make_shared<int>(17);source.text="localized key";
    {NativeValue owned(p,storage.data());owned.copy_from(&source);
     require(source.key.use_count()==2&&p.initialized==1&&p.copied==1,"FKey-like shared storage must be initialized and property-copied before native key-display calls");}
    require(source.key.use_count()==1&&p.destroyed==1,"copied native input values must release exactly once after capture");
    Property outer,failed;failed.fail=true;alignas(Owned)std::array<std::byte,sizeof(Owned)> a{},b{};
    try{NativeValue first(outer,a.data());NativeValue second(failed,b.data());}catch(const std::runtime_error&){}
    require(outer.destroyed==1&&failed.destroyed==0,"later native initialization failure must unwind only previously constructed values");
}
}
int main() {
    const std::array<std::pair<const char*,void(*)()>,9> tests{{{"owner_and_readiness",owner_and_readiness},
        {"current_binding_and_guide_order",current_binding_and_guide_order},{"final_page_only_close",final_page_only_close},
        {"disabled_or_hidden_close",disabled_or_hidden_close},{"unknown_guides_do_not_block_text",unknown_guides_do_not_block_text},
        {"malformed_observations",malformed_observations},{"native_array_bounds",native_array_bounds},
        {"visible_text_projection",visible_text_projection},
        {"native_key_copy_lifetime",native_key_copy_lifetime}}};
    int failed=0;for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    std::cout<<tests.size()-failed<<"/"<<tests.size()<<" tutorial groups passed\n";return failed?1:0;
}
