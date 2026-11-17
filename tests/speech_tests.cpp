#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/Speech.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace resonance;
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
template<class F> bool until(F predicate) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<end) {if(predicate()) return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));} return predicate();
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"test backend directory missing");
        const std::filesystem::path folder=argv[1];
        auto dll=LoadLibraryW((folder/L"prism.dll").c_str());require(dll!=nullptr,"test backend must load");
        auto available=reinterpret_cast<void(*)(bool)>(GetProcAddress(dll,"test_set_available"));
        auto fail=reinterpret_cast<void(*)()>(GetProcAddress(dll,"test_fail_next"));
        auto read=reinterpret_cast<void(*)(char*,size_t)>(GetProcAddress(dll,"test_output"));
        auto speaking=reinterpret_cast<void(*)(bool)>(GetProcAddress(dll,"test_speaking"));
        auto supported=reinterpret_cast<void(*)(bool)>(GetProcAddress(dll,"test_speaking_supported"));
        auto read_interrupts=reinterpret_cast<void(*)(char*,size_t)>(GetProcAddress(dll,"test_interruptions"));
        auto query_count=reinterpret_cast<unsigned(*)()>(GetProcAddress(dll,"test_speaking_queries"));
        auto stops=reinterpret_cast<unsigned(*)()>(GetProcAddress(dll,"test_stops"));
        require(available&&fail&&read&&speaking&&supported&&read_interrupts&&query_count&&stops,"test backend control exports missing");
        auto output=[&]{char value[4096]{};read(value,sizeof(value));return std::string(value);};
        auto interrupts=[&]{char value[4096]{};read_interrupts(value,sizeof(value));return std::string(value);};
        set_log_path(folder/L"speech-test.log");
        {
            Speech speech(folder);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            require(!speech.ready(),"initial unavailable backend must remain unavailable");
            available(true);
            require(until([&]{return speech.ready();}),"backend becoming available must restore automatic speech");
            ReaderCore core;Frame frame{"title",MenuSnapshot{"title","Continue","",0,3,true,true,true},{}};
            core.output_epoch(speech.epoch());
            for(auto e:core.observe(frame)) speech.say(e.text,e.interrupt);
            require(until([&]{return output().find("Continue. 1 of 3")!=std::string::npos;}),"native capture must reach speech output");
            const auto old_epoch=speech.epoch();
            fail();speech.say("failure trigger",true);speech.say("stale queued line",false);
            require(until([&]{return !speech.ready();}),"failed backend must invalidate readiness");
            speech.say("queued during outage",false);
            available(true);
            require(until([&]{return speech.ready()&&speech.epoch()>old_epoch;}),"transient speech failure must recover with a new output epoch");
            core.output_epoch(speech.epoch());auto replay=core.observe(frame);
            require(replay.size()==1,"unchanged current capture must replay after speech recovery");
            for(auto e:replay) speech.say(e.text,e.interrupt);
            require(until([&]{auto all=output();auto a=all.find("Continue. 1 of 3");return a!=std::string::npos&&all.find("Continue. 1 of 3",a+1)!=std::string::npos;}),"recovered automatic reader must deliver the current selection again");
            require(output().find("stale queued line")==std::string::npos,"recovery must discard stale queued output");
            require(output().find("queued during outage")==std::string::npos,"reconnection must discard text queued during the outage before replaying the current capture");
            auto select=[&](const char* text){speech.say(SpeechEvent{text,false,SpeechDelivery::LatestSelection});};
            select("Before rapid selection");
            require(until([&]{return output().find("Before rapid selection")!=std::string::npos;}),"first selection submitted");
            select("Current rapid selection");
            require(until([&]{return interrupts().find("Current rapid selection")!=std::string::npos;}),
                "an active disposable selection is interrupted by the latest selection");

            speaking(true);
            speech.say(SpeechEvent{"Ordered hit",false,SpeechDelivery::OrderedBattle});
            require(until([&]{return output().find("Ordered hit")!=std::string::npos;}),"hit submitted to backend");
            const auto before_queries=query_count();
            select("Obsolete while hit plays");
            select("Selection after hit");
            require(until([&]{return query_count()>=before_queries+2;}),"pending selection is held while repeated playback probes report busy");
            require(output().find("Obsolete while hit plays")==std::string::npos,"no obsolete selection submitted during known hit playback");
            require(output().find("Selection after hit")==std::string::npos,"newest selection waits for known hit playback");
            speaking(false);
            require(until([&]{return output().find("Selection after hit")!=std::string::npos;}),"newest selection follows the hit");
            require(output().find("Obsolete while hit plays")==std::string::npos,"only the newest selection survives the wait");
            require(interrupts().find("Selection after hit")==std::string::npos,"first selection advances without cancelling the preceding hit");

            select("New focus after playback drains");
            require(until([&]{return interrupts().find("New focus after playback drains")!=std::string::npos;}),
                "a new focused row can interrupt the prior selection after playback drains");

            speaking(true);
            speech.say({"Multi-hit to cut",false,SpeechDelivery::OrderedBattle});
            require(until([&]{return output().find("Multi-hit to cut")!=std::string::npos;}),"interruption fixture hit plays");
            const auto before_stop=stops();
            speech.interrupt_battle();
            select("Target after interruption");
            require(until([&]{return stops()>before_stop&&output().find("Target after interruption")!=std::string::npos;}),
                "deliberate interruption cancels backend hit speech and lets current focus speak");

            speaking(true);
            speech.describe({NarrationActionKind::Play,"scene","localized","Una luz cruza la sala."});
            require(until([&]{return output().find("Una luz cruza la sala.")!=std::string::npos;}),
                "localized description text reaches Prism");
            const auto description=speech.description_status();
            require(description.active&&!description.queued&&description.generation>0&&description.cue_id=="localized",
                "a spoken description is tracked in its native scene");
            speech.describe({NarrationActionKind::Stop,"scene","prior-cue"});
            require(speech.description_status().active,"another cue's stop cannot clear the current description");
            const auto before_description_stop=stops();
            speech.describe({NarrationActionKind::Pause,"scene","localized"});
            require(until([&]{return stops()>before_description_stop&&!speech.description_status().active;}),
                "pausing a scene stops its screen-reader description");
            speech.describe({NarrationActionKind::Resume,"scene","localized"});
            require(!speech.description_status().active,"resuming cannot replay a partially spoken line");

            supported(false);
            speech.say(SpeechEvent{"Legacy ordered hit",false,SpeechDelivery::OrderedBattle});
            require(until([&]{return output().find("Legacy ordered hit")!=std::string::npos;}),"legacy hit submitted");
            select("Legacy first selection");
            require(until([&]{return output().find("Legacy first selection")!=std::string::npos;}),"legacy backends receive selected text");
            select("Legacy latest selection");
            require(until([&]{return output().find("Legacy latest selection")!=std::string::npos;}),"legacy backends receive the next selected text");
            require(interrupts().find("Legacy latest selection")==std::string::npos,
                "backends without playback status cannot cancel a possibly pending hit");

            fail();speech.say("fail before shutdown");
            require(until([&]{return !speech.ready();}),"shutdown fixture must reach unavailable state");
        }
        FreeLibrary(dll);
        std::cout<<"Speech startup recovery, transient recovery, current capture replay, stale queue and shutdown passed\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
