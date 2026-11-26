#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "EngineHooks.hpp"
#include "resonance/UnrealReader.hpp"
#include "resonance/NativeWalking.hpp"
#include "resonance/NarrationOutput.hpp"
#include "resonance/Descriptions.hpp"
#include "resonance/NarrationInput.hpp"
#include "resonance/Hotkeys.hpp"
#include "resonance/ControllerInput.hpp"
#include "resonance/LanguageInput.hpp"
#include "resonance/Localization.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace resonance {
namespace {
std::string format_sha256(const std::array<unsigned char,32>& digest) {
    constexpr std::string_view digits="0123456789abcdef";
    std::string result;
    result.reserve(digest.size()*2);
    for(auto byte:digest) {
        result.push_back(digits[byte>>4]);
        result.push_back(digits[byte&0xf]);
    }
    return result;
}
std::string sha256(const std::filesystem::path& file) {
    std::ifstream stream(file,std::ios::binary); if(!stream) return {};
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) {BCryptCloseAlgorithmProvider(algorithm,0);return {};}
    std::array<char,65536> block{}; bool ok=true;
    while(stream) {stream.read(block.data(),block.size());const auto n=stream.gcount();if(n>0&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(block.data()),static_cast<ULONG>(n),0)<0) {ok=false;break;}}
    std::array<unsigned char,32> digest{};
    ok=ok&&!stream.bad()&&BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
    return ok?format_sha256(digest):std::string{};
}
std::string caption_sha256(std::string_view caption) {
    BCRYPT_ALG_HANDLE algorithm{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    std::array<unsigned char,32> digest{};
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(caption.data())),
        static_cast<ULONG>(caption.size()),digest.data(),static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    return status>=0?format_sha256(digest):std::string{};
}
struct State {
    Speech speech;
    DescriptionCoordinator descriptions;
    std::mutex failure_mutex;
    std::optional<std::pair<NarrationAction,std::string>> failure;
    std::unique_ptr<NarrationOutput> recorded;
    bool describe_with_screen_reader{};
    RouteBeaconOutput beacon;
    UnrealReader reader;
    std::atomic<bool> active{true};
    std::atomic<unsigned> commands{};
    bool announced{};
    std::filesystem::path folder;
    std::string_view language;
    unsigned long long next_language_probe{};
    std::string narration_status,gate_status;
    DescriptionFrame captured_description;
    bool dialogue_deferred{};
    std::set<std::string> paced_scenes;
    std::vector<std::string> notices;
    unsigned long long last_capture{};
    NativeWalkingStartup walking_startup;
    void setup_walking(bool requested) {
        if(!walking_startup.due(GetTickCount64(),requested))return;
        const auto result=install_native_walking();
        walking_startup.complete(result,GetTickCount64());
        if(result==NativeWalkingInstall::Ready)log("HOOK native walking and gait ready");
    }
    explicit State(std::filesystem::path path):speech(path),reader(speech,beacon,path/L"AudioSettings.ini"),
        folder(std::move(path)) {
        reader.set_description_observer([this](const Frame& frame){return observe_descriptions(frame);});
    }
    void load_descriptions() {
        auto assets=folder/L"Descriptions";
        const bool localized=language!="en";
        if(localized)assets/=std::string(language);
        const auto source=localized?DescriptionSource::ScreenReader:DescriptionSource::Recording;
        stop_descriptions();
        recorded.reset();
        describe_with_screen_reader=false;
        {
            std::lock_guard lock(failure_mutex);
            failure.reset();
        }
        paced_scenes.clear();
        descriptions.set_cues({},{});
        std::vector<NarrationCue> timed;
        std::vector<PacedNarrationCue> paced;
        try {timed=load_narration_manifest(assets/L"cues.tsv",assets,source);}
        catch(const std::exception& e){log(std::string("NARRATION MOVIE LOAD ERROR ")+e.what());}
        try {paced=load_paced_narration_manifest(assets/L"paced.tsv",assets,source);}
        catch(const std::exception& e){log(std::string("NARRATION SCRIPTED LOAD ERROR ")+e.what());}
        const auto timed_count=timed.size(),paced_count=paced.size();
        for(const auto& cue:paced)paced_scenes.insert(cue.scene);
        try {
            if(timed.empty()&&paced.empty())throw std::runtime_error("No validated description cues");
            descriptions.set_cues(std::move(timed),std::move(paced));
            if(localized) {
                describe_with_screen_reader=true;
                narration_status=tr("Screen reader descriptions ready.");
            } else {
                recorded=std::make_unique<NarrationOutput>(assets,[this](const NarrationAction& action,const std::string& error) {
                    log("NARRATION ERROR "+action.cue_id+" "+error);
                    std::lock_guard lock(failure_mutex);failure=std::make_pair(action,error);
                });
                narration_status=tr(timed_count?"Recorded movie descriptions ready.":"Recorded movie descriptions unavailable.")+
                    " "+tr(paced_count?"Scripted scene descriptions ready.":"Scripted scene descriptions unavailable.");
            }
            log("NARRATION loaded clocked="+std::to_string(timed_count)+" paced="+std::to_string(paced_count)+
                " language="+std::string(language)+" output="+(localized?"screen reader":"recording"));
        } catch(const std::exception& error) {
            narration_status=tr(localized?"Screen reader descriptions unavailable.":"Recorded descriptions unavailable.");
            log(std::string("NARRATION LOAD ERROR ")+error.what());
        }
    }
    bool refresh_language() {
        const auto now=GetTickCount64();
        if(now<next_language_probe)return false;
        next_language_probe=now+150;
        const auto observed=capture_game_language();
        if(!observed||*observed==language)return false;
        language=*observed;
        speech.stop();
        try {
            use_language(language,folder/L"Localization");
            log("LANGUAGE text="+std::string(language));
        } catch(const std::exception& error) {
            use_language("en",folder/L"Localization");
            log("LANGUAGE catalog error text="+std::string(language)+" "+error.what());
        }
        reader.language_changed(language!="en");
        load_descriptions();
        return true;
    }
    void announce_startup() {
        if(announced||language.empty()||!speech.ready())return;
        announced=true;
        speech.say(trf("Visions of Grandshelt loaded. F10 for help. {0}",{narration_status}));
    }
    bool observe_descriptions(const Frame& frame) {
        DescriptionFrame capture;capture.ui=frame;capture.clocks=reader.narration_clocks();
        DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        capture.foreground=foreground==GetCurrentProcessId();
        capture.screen_reader=describe_with_screen_reader;
        capture.output_ready=(recorded||describe_with_screen_reader)&&speech.ready();
        if(recorded)capture.audio=recorded->status();
        else if(describe_with_screen_reader)capture.audio=speech.description_status();
        if(capture.audio.active&&!capture.audio.queued&&
            (capture.audio.generation!=captured_description.audio.generation||captured_description.audio.queued))
            log("NARRATION OUTPUT "+capture.audio.scene+" "+capture.audio.cue_id);
        const bool relevant=std::any_of(capture.clocks.begin(),capture.clocks.end(),[this](const auto& c){return descriptions.has_scene(c.scene);});
        if(relevant||descriptions.has_context()) {
            const bool dialogue_ready=frame.dialogue&&frame.dialogue->visible&&frame.dialogue->ready&&!frame.dialogue->text.empty();
            const auto native=capture_narration_input(dialogue_ready);
            capture.native_scene=native.scene;capture.native_session=native.session;
            capture.native_event_known=native.event_active!=NarrationFact::Unknown;
            capture.native_event_active=native.event_active==NarrationFact::Yes;
            capture.pause_known=native.user_pause!=NarrationFact::Unknown;
            capture.user_paused=native.user_pause==NarrationFact::Yes;
            capture.voice_known=native.event_voice_busy!=NarrationFact::Unknown&&native.any_talk_voice_busy!=NarrationFact::Unknown;
            capture.voice_busy=native.event_voice_busy==NarrationFact::Yes||native.any_talk_voice_busy==NarrationFact::Yes;
            if(dialogue_ready&&frame.dialogue->source_event.empty()&&descriptions.has_caption_fallback(native.scene)) {
                const auto& previous=captured_description.ui.dialogue;
                capture.caption_sha256=previous&&previous->text==frame.dialogue->text&&!captured_description.caption_sha256.empty()?
                    captured_description.caption_sha256:caption_sha256(frame.dialogue->text);
            }
            const auto diagnostic=native.scene+" pause="+std::to_string(static_cast<int>(native.user_pause))+
                " voice="+std::to_string(static_cast<int>(native.event_voice_busy))+
                " any_voice="+std::to_string(static_cast<int>(native.any_talk_voice_busy))+" reason="+native.reason;
            if(diagnostic!=gate_status){gate_status=diagnostic;log("NARRATION gate "+diagnostic);}
        }
        captured_description=capture;
        const auto result=descriptions.observe(capture);
        if(!result.trace.empty())log("NARRATION TRACE "+result.trace);
        if(!result.caption_end_cue.empty())log("NARRATION CAPTION END "+result.caption_end_cue);
        dialogue_deferred=result.defer_dialogue;
        // Localized descriptions share Prism with subtitles. The description
        // actions own their playback and must not flush a subtitle when a cue starts.
        if(result.stop_speech&&!describe_with_screen_reader)speech.stop();
        apply_descriptions(result.actions);
        if(!result.notice.empty())notices.push_back(result.notice);
        return result.defer_dialogue;
    }
    void apply_descriptions(const std::vector<NarrationAction>& actions) {
        if(recorded) {
            auto contextual=actions;
            for(auto& action:contextual) {
                unsigned matches=0;
                for(const auto& clock:captured_description.clocks)if(clock.valid&&clock.scene==action.scene&&(clock.playing||clock.paused)) {
                    ++matches;action.session=clock.session;
                }
                if(matches!=1)action.session.clear();
                if(action.dialogue_id.empty()&&paced_scenes.contains(action.scene)&&captured_description.ui.dialogue)
                    action.dialogue_id=captured_description.ui.dialogue->id;
            }
            recorded->submit(contextual);
        } else if(describe_with_screen_reader) {
            for(const auto& action:actions)speech.describe(action);
        }
        for(const auto& action:actions)if(action.kind==NarrationActionKind::Play)
            log("NARRATION PLAY "+action.scene+" "+action.cue_id);
    }
    void after_capture(unsigned requested_commands) {
        DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        auto capture=captured_description;
        capture.foreground=foreground==GetCurrentProcessId();
        capture.output_ready=(recorded||describe_with_screen_reader)&&speech.ready();
        std::optional<std::pair<NarrationAction,std::string>> failed;
        DescriptionFailureDisposition disposition=DescriptionFailureDisposition::Discard;
        {
            std::lock_guard lock(failure_mutex);
            if(failure) {
                disposition=classify_description_failure(failure->first,capture,dialogue_deferred);
                if(disposition!=DescriptionFailureDisposition::Wait){failed=std::move(failure);failure.reset();}
            }
        }
        if(failed&&disposition==DescriptionFailureDisposition::Speak) {
            const auto& action=failed->first;
            notices.push_back(action.kind==NarrationActionKind::Play&&!action.text.empty()?
                trf("Recorded description unavailable. Description: {0}",{action.text}):tr("Recorded description playback failed."));
        }
        // Use the observer's frozen audio status. A fast worker failure arriving
        // after it must wait for a capture that releases/queues the native caption.
        if(requested_commands&Help)notices.push_back(narration_status);
        if(capture.foreground&&speech.ready())for(const auto& notice:notices)speech.say(notice,false);
        notices.clear();
    }
    void stop_descriptions() {
        apply_descriptions(descriptions.reset());
        if(recorded) {
            const auto status=recorded->status();
            if(status.active)recorded->submit({{NarrationActionKind::Stop,status.scene,status.cue_id,{},{},0}});
        } else if(describe_with_screen_reader) {
            const auto status=speech.description_status();
            if(status.active)speech.describe({NarrationActionKind::Stop,status.scene,status.cue_id});
        }
        notices.clear();std::lock_guard lock(failure_mutex);failure.reset();
    }
};
struct Fault { unsigned long code{}; void* instruction{}; std::uintptr_t access{}; unsigned long long kind{}; std::array<void*,32> stack{}; unsigned short frames{}; };
Fault fault;
int capture_fault(EXCEPTION_POINTERS* exception) {
    fault.code=exception->ExceptionRecord->ExceptionCode;
    fault.instruction=exception->ExceptionRecord->ExceptionAddress;
    fault.frames=CaptureStackBackTrace(0,static_cast<DWORD>(fault.stack.size()),fault.stack.data(),nullptr);
    if(exception->ExceptionRecord->NumberParameters>=2) {fault.kind=exception->ExceptionRecord->ExceptionInformation[0];fault.access=exception->ExceptionRecord->ExceptionInformation[1];}
    return EXCEPTION_EXECUTE_HANDLER;
}
bool guarded_tick(State* state,unsigned& commands,bool& capture) {
    __try {
        if(state->refresh_language())capture=true;
        state->announce_startup();
        const auto controller=state->reader.controller_commands();
        if(!state->reader.battle_commands_available())commands&=~battle_only_commands;
        const auto read_started=GetTickCount64();
        begin_controller_field_read(read_started);
        if(!commands&&controller){commands=controller;capture=true;}
        if(!commands) {
            commands=state->reader.area_browse_commands();
            if(commands)capture=true;
        }
        if(commands&ToggleAutomatic)state->setup_walking(true);
        if(capture) state->reader.tick(commands);
        state->reader.movement_tick();
        renew_controller_after_field_read(read_started);
        return true;
    }
    __except(capture_fault(GetExceptionInformation())) {
        publish_controller_permission(false,0);
        return false;
    }
}
bool guarded_event(State* state,resonance::unreal::UObject* object,resonance::unreal::UFunction* function,void* params) {
    __try {state->reader.on_event(object,function,params);return true;}
    __except(capture_fault(GetExceptionInformation())) {return false;}
}
bool guarded_before_event(State* state,unreal::UObject* object,unreal::UFunction* function,void* params,bool& consumed) {
    __try {consumed=state->reader.before_event(object,function,params);return true;}
    __except(capture_fault(GetExceptionInformation())) {return false;}
}
void report_native_fault(State* state,const char* source) {
    state->active=false;
    clear_native_walking();
    publish_controller_permission(false,0);
    publish_battle_controller_permission(false,0);
    state->stop_descriptions();
    std::ostringstream message;message<<"ERROR reader suspended after native fault source="<<source<<" code=0x"<<std::hex<<fault.code<<" instruction="<<fault.instruction<<" access=0x"<<fault.access<<" operation="<<fault.kind;
    log(message.str());log("FAULT stage="+reader_stage());
    for(unsigned short i=0;i<fault.frames;++i) {
        HMODULE module{};wchar_t file[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(fault.stack[i]),&module);
        GetModuleFileNameW(module,file,MAX_PATH);
        std::ostringstream frame;frame<<"STACK "<<i<<" "<<std::filesystem::path(file).filename().string()<<" +0x"<<std::hex<<(reinterpret_cast<std::uintptr_t>(fault.stack[i])-reinterpret_cast<std::uintptr_t>(module));log(frame.str());
    }
    state->speech.say(tr("Visions of Grandshelt reader stopped after an error. See its log."));
}
void event(std::weak_ptr<State> weak,resonance::unreal::UObject* object,resonance::unreal::UFunction* function,void* params) {
    auto state=weak.lock();if(!state||!state->active)return;
    try {if(!guarded_event(state.get(),object,function,params))report_native_fault(state.get(),"presentation event");}
    catch(const std::exception& e) {
        state->active=false;clear_native_walking();publish_controller_permission(false,0);
        publish_battle_controller_permission(false,0);state->stop_descriptions();
        log(std::string("ERROR event reader suspended: ")+e.what());
        state->speech.say(tr("Visions of Grandshelt reader stopped after an error. See its log."));
    }
}
void tick(std::weak_ptr<State> weak,float) {
    auto state=weak.lock(); if(!state||!state->active) return;
    auto commands=state->commands.exchange(0);
    const auto now=GetTickCount64();
    const bool menu_focus_changed=state->reader.take_menu_focus_change();
    bool capture=commands||menu_focus_changed||now-state->last_capture>=80;
    if(capture) state->last_capture=now;
    try {
        if(!guarded_tick(state.get(),commands,capture)) {
            report_native_fault(state.get(),"tick");
        } else if(capture) {state->last_capture=now;state->after_capture(commands);}
    } catch(const std::exception& e) {state->active=false;clear_native_walking();publish_controller_permission(false,0);
        publish_battle_controller_permission(false,0);state->stop_descriptions();log(std::string("ERROR reader suspended: ")+e.what());state->speech.say(tr("Visions of Grandshelt reader stopped after an error. See its log."));}
}
struct Runtime {
    std::filesystem::path folder;
    std::shared_ptr<State> state;
    KeyEdges key_edges;
    std::atomic<DWORD> game_thread{};
    unsigned long long next_probe{};
    unsigned probe_count{};
    bool disabled{};
};

std::atomic<Runtime*> active_runtime{};

void poll_keyboard(Runtime& runtime) {
    static constexpr int keys[] = {
        VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_DELETE,
        'H','M','L',VK_F2,VK_F3,VK_F4,VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_RETURN,VK_ESCAPE,VK_SPACE,
        'W','A','S','D','C'};
    static constexpr unsigned commands[] = {
        Repeat, Previous, Next, Route, Screen, Help, PreviousCategory, NextCategory,
        PreviousPageTarget, NextPageTarget, ToggleGuidance, BattleHp, BattleMp, BattleGauges, BattleBuffs, BattleDebuffs,
        BattleInterrupt,ResultUp|BattleInputKey,ResultDown|BattleInputKey,BattleInputKey,BattleInputKey,BattleInputKey,
        BattleInputKey,BattleInputKey,
        BattleInputKey,BattleInputKey,BattleInputKey,BattleInputKey,BattleInputKey};
    static_assert(std::size(keys)==std::size(commands)&&std::size(keys)<=32);
    const auto held = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    DWORD pid{};
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool foreground = pid == GetCurrentProcessId();
    std::uint32_t down{};
    for (unsigned i = 0; i < std::size(keys); ++i) if (held(keys[i])) down |= 1u << i;
    const auto pressed = runtime.key_edges.observe(down, foreground);
    if (!foreground) return;

    const bool modified = held(VK_CONTROL) || held(VK_MENU) || held(VK_LWIN) ||
        held(VK_RWIN) || held(VK_INSERT) || held(VK_CAPITAL);
    const bool shift = held(VK_SHIFT);
    for (unsigned i = 0; i < std::size(keys); ++i) {
        const bool delete_key = keys[i] == VK_DELETE;
        if ((pressed & (1u << i)) && !modified && (!shift || delete_key)) {
            runtime.state->commands.fetch_or(delete_key && shift ? ToggleAutomatic : commands[i]);
        }
    }
}

bool binding_probe(bool& ready) {
    __try {
        ready = unreal::verify_live_bindings();
        return true;
    } __except(capture_fault(GetExceptionInformation())) {
        return false;
    }
}

bool initialize_reader(Runtime& runtime) {
    const auto now = GetTickCount64();
    if (now < runtime.next_probe) return false;
    runtime.next_probe = now + 500;
    bool verified{};
    if (!binding_probe(verified)) {
        runtime.disabled = true;
        std::ostringstream message;
        message << "ERROR native Unreal binding fault code=0x" << std::hex << fault.code
                << " instruction=" << fault.instruction << " access=0x" << fault.access;
        log(message.str());
        return false;
    }
    if (!verified) {
        if (++runtime.probe_count == 40) {
            runtime.disabled = true;
            log("ERROR exact-build Unreal object and text anchors did not match");
        }
        return false;
    }

    runtime.state = std::make_shared<State>(runtime.folder);
    runtime.state->setup_walking(false);
    initialize_controller_input([] {
        DWORD foreground{};
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
        return foreground == GetCurrentProcessId();
    }, ControllerModifier::RightShoulder);
    log("HOOK native Unreal bindings verified on the game thread");
    return true;
}

void engine_tick(float delta) {
    auto* runtime = active_runtime.load(std::memory_order_acquire);
    if (!runtime || runtime->disabled) return;
    runtime->game_thread.store(GetCurrentThreadId(), std::memory_order_release);
    try {
        const unreal::ObjectFrame frame;
        if (!runtime->state && !initialize_reader(*runtime)) return;
        if (!runtime->state->active) return;
        poll_keyboard(*runtime);
        tick(runtime->state, delta);
    } catch (const std::exception& error) {
        runtime->disabled = true;
        if (runtime->state) runtime->state->active = false;
        clear_native_walking();
        publish_controller_permission(false, 0);
        publish_battle_controller_permission(false,0);
        log(std::string("ERROR native reader initialization stopped: ") + error.what());
    }
}

void presentation_event(unreal::UObject* object, unreal::UFunction* function, void* args) {
    auto* runtime = active_runtime.load(std::memory_order_acquire);
    if (!runtime || runtime->game_thread.load(std::memory_order_acquire) != GetCurrentThreadId() ||
        !runtime->state || runtime->disabled) return;
    event(runtime->state, object, function, args);
}
bool before_presentation_event(unreal::UObject* object,unreal::UFunction* function,void* args) {
    auto* runtime=active_runtime.load(std::memory_order_acquire);
    if(!runtime||runtime->game_thread.load(std::memory_order_acquire)!=GetCurrentThreadId()||
       !runtime->state||runtime->disabled||!runtime->state->active)return false;
    bool consumed=false;
    try {
        if(!guarded_before_event(runtime->state.get(),object,function,args,consumed))
            report_native_fault(runtime->state.get(),"accessibility menu input");
    } catch(const std::exception& error) {
        runtime->state->active=false;
        clear_native_walking();
        publish_controller_permission(false,0);
        publish_battle_controller_permission(false,0);
        runtime->state->stop_descriptions();
        log(std::string("ERROR accessibility menu input stopped: ")+error.what());
        runtime->state->speech.say(tr("Visions of Grandshelt reader stopped after an error. See its log."));
    }
    return consumed;
}

void start_reloaded() {
    static std::once_flag start_once;
    std::call_once(start_once, [] {
        wchar_t dll_path[32768]{}, exe_path[32768]{};
        const auto image = GetModuleHandleW(nullptr);
        const auto dll_size = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), dll_path, std::size(dll_path));
        const auto exe_size = GetModuleFileNameW(image, exe_path, std::size(exe_path));
        if (!dll_size || dll_size >= std::size(dll_path) || !exe_size || exe_size >= std::size(exe_path)) return;

        const auto folder = std::filesystem::path(dll_path).parent_path();
        set_log_path(folder / L"ResonanceAccess.log");
        log("START Visions of Grandshelt 0.2.70-dev; Reloaded native mod");
        if (sha256(exe_path) != "b8c588e46ebadbe37feca7bdf2da0ae63a8538099d14a53ec0c2241b086e5ffa") {
            log("ERROR unsupported game executable; native reader disabled");
            return;
        }
        if (GetModuleHandleW(L"UE4SS.dll")) {
            log("ERROR UE4SS is already loaded; native reader disabled");
            return;
        }
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&__ImageBase), &pinned)) {
            log("ERROR native reader could not remain resident");
            return;
        }

        auto runtime = std::make_unique<Runtime>();
        runtime->folder = folder;
        const auto base = reinterpret_cast<std::uintptr_t>(image);
        unreal::bind_exact_image(base);
        if (!install_engine_hooks(base, engine_tick, before_presentation_event, presentation_event)) return;

        // The callbacks are resident until process exit. The owning state is
        // also resident so worker threads never join under the DLL loader lock.
        active_runtime.store(runtime.release(), std::memory_order_release);
        log("HOOK exact-build engine post-tick and presentation callbacks installed");
    });
}
}
}

extern "C" __declspec(dllexport) void ReloadedStart() {
    try {
        resonance::start_reloaded();
    } catch (const std::exception& error) {
        resonance::log(std::string("ERROR Reloaded start failed: ") + error.what());
    }
}
