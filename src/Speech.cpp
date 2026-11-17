#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#pragma warning(push)
#pragma warning(disable:4309) // Upstream enum uses a 64-bit feature bit under MSVC.
#include <prism.h>
#pragma warning(pop)
#include "resonance/Speech.hpp"
#include <fstream>
#include <chrono>
namespace resonance {
namespace { std::mutex log_mutex; std::filesystem::path log_path; }
void set_log_path(const std::filesystem::path& path) { std::lock_guard lock(log_mutex); log_path=path; }
void log(const std::string& message) {
    std::lock_guard lock(log_mutex);
    std::ofstream stream(log_path,std::ios::app|std::ios::binary);
    stream<<GetTickCount64()<<" "<<message<<'\n';
}
Speech::Speech(std::filesystem::path folder):folder_(std::move(folder)),thread_([this]{run();}) {}
Speech::~Speech() {
    { std::lock_guard lock(mutex_); quit_=true; queue_.clear(); }
    cv_.notify_one(); if(thread_.joinable()) thread_.join();
}
void Speech::say(std::string text,bool interrupt) {
    say(SpeechEvent{std::move(text),interrupt});
}
void Speech::say(SpeechEvent event) {
    if(event.text.empty())return;
    { std::lock_guard lock(mutex_); queue_.push(std::move(event)); }
    cv_.notify_one();
}
void Speech::stop() {
    { std::lock_guard lock(mutex_); queue_.clear(); stop_=true; }
    cv_.notify_one();
}
void Speech::run() {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::vector<HMODULE> dependencies;
    for(const auto* name:{L"SpeechCore.dll",L"SRAL.dll",L"tolk.dll",L"UniversalSpeech.dll"}) {
        if(auto h=LoadLibraryExW((folder_/name).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) dependencies.push_back(h);
    }
    auto module=LoadLibraryExW((folder_/L"prism.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) { log("ERROR Prism load failed, Win32="+std::to_string(GetLastError())); }
    else {
#define RESOLVE(name) auto name##_fn=reinterpret_cast<decltype(&name)>(GetProcAddress(module,#name))
        RESOLVE(prism_config_init); RESOLVE(prism_init); RESOLVE(prism_shutdown);
        RESOLVE(prism_registry_acquire_best); RESOLVE(prism_backend_name);
        RESOLVE(prism_backend_initialize); RESOLVE(prism_backend_speak);
        RESOLVE(prism_backend_stop); RESOLVE(prism_backend_free); RESOLVE(prism_backend_is_speaking);
        if(!prism_config_init_fn||!prism_init_fn||!prism_shutdown_fn||!prism_registry_acquire_best_fn||!prism_backend_name_fn||!prism_backend_initialize_fn||!prism_backend_speak_fn||!prism_backend_stop_fn||!prism_backend_free_fn) {
            log("ERROR Prism ABI exports missing");
        } else {
            auto config=prism_config_init_fn();
            auto* context=prism_init_fn(&config);
            PrismBackend* backend=nullptr;
            // acquire_best already initializes its backend (Prism FrozenRegistry::acquire_best).
            if(!context) log("ERROR Prism context initialization failed");
            else {
                std::unique_lock lock(mutex_);
                bool unavailable_reported=false;
                enum class OrderedPlayback { None, Submitted, Speaking };
                OrderedPlayback ordered=OrderedPlayback::None;
                auto last_delivery=SpeechDelivery::Normal;
                auto ordered_submitted=std::chrono::steady_clock::time_point{};
                bool can_query_playback=false;
                constexpr auto probe_interval=std::chrono::milliseconds(25);
                constexpr auto startup_probe_window=std::chrono::milliseconds(250);
                const auto work_ready=[&]{return quit_||stop_||queue_.size()!=0;};
                while(!quit_) {
                    if(!backend) {
                        lock.unlock();backend=prism_registry_acquire_best_fn(context);lock.lock();
                        if(quit_) break;
                        if(!backend) {
                            ready_=false;
                            if(!unavailable_reported) {log("ERROR Prism has no available speech backend; retrying");unavailable_reported=true;}
                            cv_.wait_for(lock,std::chrono::seconds(1),[&]{return quit_;});
                            continue;
                        }
                        unavailable_reported=false;
                        ordered=OrderedPlayback::None;
                        last_delivery=SpeechDelivery::Normal;
                        can_query_playback=prism_backend_is_speaking_fn!=nullptr;
                        if(epoch_.load()!=0) queue_.clear();
                        epoch_.fetch_add(1);ready_=true;
                        log(std::string("Prism ready: ")+prism_backend_name_fn(backend)+" output_epoch="+std::to_string(epoch_.load()));
                    }
                    // Watch for the start of queued hit speech without postponing a
                    // selection when the backend cannot establish playback.
                    const auto age=std::chrono::steady_clock::now()-ordered_submitted;
                    if(can_query_playback&&(ordered==OrderedPlayback::Speaking||
                       (ordered==OrderedPlayback::Submitted&&age<startup_probe_window))) {
                        cv_.wait_for(lock,probe_interval,work_ready);
                    } else cv_.wait(lock,work_ready);
                    if(quit_) break;
                    if(stop_) {
                        stop_=false;
                        ordered=OrderedPlayback::None;
                        last_delivery=SpeechDelivery::Normal;
                        lock.unlock();
                        const auto e=prism_backend_stop_fn(backend);
                        if(e!=PRISM_OK&&e!=PRISM_ERROR_NOT_SPEAKING)log("Prism stop="+std::to_string(e));
                        lock.lock();
                    }
                    if(can_query_playback&&ordered!=OrderedPlayback::None) {
                        bool speaking=false;
                        lock.unlock();
                        const auto status=prism_backend_is_speaking_fn(backend,&speaking);
                        lock.lock();
                        if(quit_||stop_)continue;
                        if(status!=PRISM_OK)can_query_playback=false;
                        else if(speaking)ordered=OrderedPlayback::Speaking;
                        else if(ordered==OrderedPlayback::Speaking)ordered=OrderedPlayback::None;
                    }
                    const auto* pending=queue_.front();
                    if(pending&&pending->delivery==SpeechDelivery::LatestSelection&&
                       can_query_playback&&ordered==OrderedPlayback::Speaking) {
                        // Keep the selection in the app queue while known hit
                        // speech plays so new focus can replace it here.
                        cv_.wait_for(lock,probe_interval,[&] {
                            const auto* next=queue_.front();
                            return quit_||stop_||!next||next->delivery!=SpeechDelivery::LatestSelection;
                        });
                        continue;
                    }
                    auto event=queue_.pop();
                    if(!event)continue;
                    bool interrupt=event->interrupt;
                    if(event->delivery==SpeechDelivery::LatestSelection) {
                        // Prism interruption cancels the entire backend queue.
                        // Replace prior selection speech only after protecting hits.
                        interrupt=ordered==OrderedPlayback::None&&
                            (last_delivery==SpeechDelivery::LatestSelection||
                             (interrupt&&last_delivery!=SpeechDelivery::OrderedBattle));
                    } else if(event->delivery==SpeechDelivery::OrderedBattle)interrupt=false;
                    lock.unlock();
                    const auto result=prism_backend_speak_fn(backend,event->text.c_str(),interrupt);
                    log("SPEECH result="+std::to_string(result)+" "+event->text);
                    lock.lock();
                    if(interrupt)ordered=OrderedPlayback::None;
                    if(event->delivery==SpeechDelivery::OrderedBattle&&result==PRISM_OK) {
                        ordered=OrderedPlayback::Submitted;
                        ordered_submitted=std::chrono::steady_clock::now();
                    }
                    last_delivery=event->delivery;
                    if(result!=PRISM_OK) {
                        ready_=false;queue_.clear();
                        lock.unlock();prism_backend_free_fn(backend);backend=nullptr;lock.lock();
                        // A new output epoch makes the observer replay the CURRENT
                        // native capture after reconnection, instead of stale queued text.
                        cv_.wait_for(lock,std::chrono::seconds(1),[&]{return quit_;});
                    }
                }
                lock.unlock();
                if(backend) {const auto ignored=prism_backend_stop_fn(backend);(void)ignored;}
                ready_=false;
            }
            if(backend) prism_backend_free_fn(backend);
            if(context) prism_shutdown_fn(context);
        }
        FreeLibrary(module);
    }
    for(auto it=dependencies.rbegin();it!=dependencies.rend();++it) FreeLibrary(*it);
    if(SUCCEEDED(com)) CoUninitialize();
}
}
