#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/NarrationInput.hpp"
#include "resonance/NativeBoundary.hpp"
#include "resonance/UnrealApi.hpp"
#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

// Offsets and signatures: exact-build JMAP plus the pinned-build Ghidra bodies in
// D:\FFRSAccess\evidence\narration-input-audit-20261031\NARRATION-INPUT.md.
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
    return o&&UObjectGlobals::IsValidObjectForFindXOf(o)&&(static_cast<std::uint32_t>(o->GetObjectFlags())&excluded_flags)==0;
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
// nullopt: property mismatch. Inner nullptr: property valid but empty/dead.
std::optional<UObject*> reflected_object(UObject* o,const wchar_t* name,int offset) {
    auto* p=reflected(o,name,offset,8); if(!p) return std::nullopt;
    UObject* value=nullptr; std::memcpy(&value,p,sizeof value);
    return live(value)?value:nullptr;
}

class Call {
    UFunction* fn_{};
    FProperty* result_{};
    int result_offset_{};
    std::vector<std::max_align_t> storage_;
public:
    Call(UObject* object,const wchar_t* name,int parms,int return_offset) {
        if(!live(object)) return;
        auto* fn=object->GetFunctionByNameInChain(name);
        if(!fn)return;
        auto* returned=fn->GetReturnProperty();
        if(!returned||returned->GetOffset_Internal()!=return_offset||
           !returned->HasAnyPropertyFlags(0x80)||!returned->HasAnyPropertyFlags(0x400)||
           !native_call_extent(fn->GetParmsSize(),fn->GetPropertiesSize(),parms,
               fn->GetReturnValueOffset(),return_offset,return_offset+returned->GetSize(),256))return;
        const int bytes=std::max<int>(parms,fn->GetPropertiesSize());
        if(bytes<=0||bytes>256) return;
        storage_.assign((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t)+1,std::max_align_t{});
        std::memset(storage_.data(),0,storage_.size()*sizeof(std::max_align_t));
        fn_=fn; result_=fn->GetReturnProperty();
        if(!result_) {fn_=nullptr;return;}
        result_offset_=result_->GetOffset_Internal();
        if(result_offset_!=return_offset||result_offset_+result_->GetSize()>bytes) {fn_=nullptr;result_=nullptr;return;}
        result_->InitializeValue(data()+result_offset_);
    }
    ~Call() { if(result_) result_->DestroyValue(data()+result_offset_); }
    Call(const Call&)=delete; Call& operator=(const Call&)=delete;
    explicit operator bool() const { return fn_!=nullptr; }
    std::byte* data() { return reinterpret_cast<std::byte*>(storage_.data()); }
    void invoke(UObject* o) { o->ProcessEvent(fn_,storage_.data()); }
    template<class T> T get(size_t offset) { T v{}; std::memcpy(&v,data()+offset,sizeof(T)); return v; }
    template<class T> void put(size_t offset,const T& v) { std::memcpy(data()+offset,&v,sizeof(T)); }
};
NarrationFact fact(bool v) { return v?NarrationFact::Yes:NarrationFact::No; }
NarrationFact get_bool(UObject* o,const wchar_t* name) {
    Call c(o,name,1,0); if(!c) return NarrationFact::Unknown;
    c.invoke(o); return fact(c.get<unsigned char>(0)!=0);
}
UObject* get_object(UObject* o,const wchar_t* name) {
    Call c(o,name,8,0); if(!c) return nullptr;
    c.invoke(o); auto* r=c.get<UObject*>(0); return live(r)?r:nullptr;
}

// CPP_TalkVoice native int id @+0x28 (class size 0x30), read by both the widget
// pause body FUN_145217f60 and auto-proceed FUN_145212950 (<0 means no voice).
std::optional<int> voice_id(UObject* talk_widget) {
    auto voice=reflected_object(talk_widget,L"m_pVoice",0x5c8);
    if(!voice) return std::nullopt;
    // A valid empty voice property owns no character voice. Hidden/preloaded
    // talk widgets must not make every unrelated description unavailable.
    if(!*voice) return -1;
    if(!is(*voice,L"/Script/mucha.CPP_TalkVoice")) return std::nullopt;
    int id=-1; std::memcpy(&id,reinterpret_cast<std::byte*>(*voice)+0x28,sizeof id); return id;
}
// CPP_SoundManager::IsExistPlaySound: parms 8, ID int @0, bool @4. Its body calls
// FUN_14514ed20 (m_PlaySoundList lookup), the same test auto-proceed uses.
NarrationFact sound_exists(UObject* sound_manager,int id) {
    if(id<0) return NarrationFact::No;
    Call c(sound_manager,L"IsExistPlaySound",8,4); if(!c) return NarrationFact::Unknown;
    c.put(0,id); c.invoke(sound_manager); return fact(c.get<unsigned char>(4)!=0);
}
NarrationFact voice_busy(UObject* talk_widget,UObject* sound_manager,int* out_id=nullptr) {
    const auto id=voice_id(talk_widget); if(!id) return NarrationFact::Unknown;
    if(out_id) *out_id=*id;
    if(*id<0) return NarrationFact::No;
    return sound_manager?sound_exists(sound_manager,*id):NarrationFact::Unknown;
}
UObject* single(const wchar_t* type) { auto all=instances(type); return all.size()==1?all.front():nullptr; }
}

NativeNarrationInput capture_narration_input(bool dialogue_ready) {
    NativeNarrationInput r;
    auto deny=[&](const char* why){ if(r.reason.empty()) r.reason=why; };
    UObject* sound_manager=single(L"CPP_SoundManager");

    // Authoritative current event: the TalkEventSubsystem's manager and its scene.
    UObject* manager=nullptr; UObject* scene=nullptr; UObject* player=nullptr;
    if(auto* subsystem=single(L"CPP_TalkEventSubsystem")) {
        if(auto m=reflected_object(subsystem,L"m_pEventManager",0x58)) {
            manager=*m;
            if(!manager) r.event_active=NarrationFact::No;
        }
    }
    if(manager) {
        auto s=reflected_object(manager,L"m_pScene",0x30);
        if(s&&*s&&is(*s,L"/Script/mucha.CPP_TalkEventScene")) {
            auto back=reflected_object(*s,L"m_pEventManager",0x28);
            if(back&&*back==manager) scene=*s;
        } else if(s&&!*s) r.event_active=NarrationFact::No;
    }
    if(scene) {
        // HandlePause (0x144eab850) writes 1 and HandleResume (0x144eab890) writes 0.
        unsigned char flag=0xff; std::memcpy(&flag,reinterpret_cast<std::byte*>(scene)+0x78,1);
        r.user_pause=flag==0?NarrationFact::No:flag==1?NarrationFact::Yes:NarrationFact::Unknown;
        auto p=reflected_object(scene,L"m_pEventPlayer",0x38);
        if(p&&*p) {
            auto back=reflected_object(*p,L"m_pEventManager",0x2b0);
            if(back&&*back==manager) player=*p;
        }
        r.event_active=player?NarrationFact::Yes:(p&&!*p)?NarrationFact::No:NarrationFact::Unknown;
    }
    if(player&&is(player,L"/Script/mucha.CPP_TalkEventSequenceBase")) {
        auto sp=reflected_object(player,L"m_pSequencePlayer",0x2e8);
        if(sp&&*sp) {
            if(auto* asset=get_object(*sp,L"GetSequence")) {
                r.session=narrow(path(scene))+"|"+narrow(path(*sp));
                r.scene="sequence:"+narrow(path(asset));
            }
            r.sequence_paused=get_bool(*sp,L"IsPaused");
            r.sequence_playing=get_bool(*sp,L"IsPlaying");
        }
    }
    // Event talk widget voice and window.
    if(manager) if(auto w=reflected_object(manager,L"m_pWidget",0x28); w&&*w) {
        r.event_voice_busy=voice_busy(*w,sound_manager,&r.voice_id);
        if(auto wm=reflected_object(*w,L"WindowManager",0x598); wm&&*wm) {
            auto window=reflected_object(*wm,L"m_pWindow",0x190);
            r.talk_window_present=window?fact(*window!=nullptr):NarrationFact::Unknown;
        }
    }
    // Every live talk widget, including ATC chat, so narration never overlaps a voice.
    {
        bool busy=false,unknown=false;
        for(auto* w:instances(L"CPP_WBP_Talk")) {
            const auto f=voice_busy(w,sound_manager);
            if(f==NarrationFact::Yes) busy=true; else if(f==NarrationFact::Unknown) unknown=true;
        }
        r.any_talk_voice_busy=busy?NarrationFact::Yes:unknown?NarrationFact::Unknown:NarrationFact::No;
    }

    if(r.event_active!=NarrationFact::Yes) deny("no active talk event");
    if(r.session.empty()) deny("event is not a sequence");
    if(r.user_pause!=NarrationFact::No) deny("event paused by player or unknown");
    if(r.event_voice_busy!=NarrationFact::No) deny("event voice playing or unknown");
    if(r.any_talk_voice_busy!=NarrationFact::No) deny("talk voice playing or unknown");
    if(!dialogue_ready) deny("visible dialogue not ready");
    r.may_start=r.reason.empty();
    return r;
}
}
