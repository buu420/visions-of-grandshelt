#include "resonance/Core.hpp"
#include "resonance/Story.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace resonance;
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
Frame line(DialogueVoice voice,std::string id="line-1",std::string text="A spoken story line.") {
    return {"scene",{},DialogueSnapshot{std::move(id),"Narrator",std::move(text),true,true,voice}};
}
}
int main() {
    int failed=0;
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"voice assignment uses the current line rather than playback timing",[] {
            require(assigned_dialogue_voice({true,true,true,true,17})==DialogueVoice::Voiced,"the current line's successful playback ID stays set after natural voice finish");
            require(assigned_dialogue_voice({true,true,true,true,-1})==DialogueVoice::Unvoiced,"failed or skipped native playback must retain text");
            require(assigned_dialogue_voice({true,true,true,false,17})==DialogueVoice::Unvoiced,"a designer-marked unvoiced line must remain readable");
            require(assigned_dialogue_voice({true,true,false,true,17})==DialogueVoice::Unknown,"pre-reflect state cannot borrow a playback ID");
            require(assigned_dialogue_voice({true,false,true,true,17})==DialogueVoice::Unknown,"inactive stale data cannot prove a voice belongs to a live line");
            require(assigned_dialogue_voice({false,true,true,true,17})==DialogueVoice::Unknown,"unverified native state cannot suppress text");
            ReaderCore reader;
            const auto pending=assigned_dialogue_voice({true,true,false,true,-1});
            require(!reader.observe(line(pending)).empty(),"already-readable text must never wait indefinitely for a voice marker");
        }},
        {"voiced lines stay quiet but remain explicitly repeatable",[] {
            ReaderCore reader;auto frame=line(DialogueVoice::Voiced);
            require(reader.observe(frame).empty(),"native voiced dialogue must not be spoken automatically");
            require(reader.repeat()=="Narrator: A spoken story line.","suppressed dialogue must remain available to explicit repeat");
            require(reader.observe(frame).empty(),"a completed native voice must not cause a late duplicate");
            reader.output_epoch(2);
            require(reader.observe(frame).empty(),"backend recovery must not replay positively voiced dialogue");
        }},
        {"localized subtitles read once even while visual descriptions are active",[] {
            ReaderCore reader;
            auto frame=line(DialogueVoice::Voiced,"localized-1","Le texte du sous-titre.");
            frame.dialogue->read_as_subtitle=true;
            frame.dialogue->ready=false;
            require(reader.observe(speech_frame(frame,true)).empty(),"an unfinished subtitle must stay quiet");
            frame.dialogue->ready=true;
            frame.dialogue->visible=false;
            require(reader.observe(speech_frame(frame,true)).empty(),"a hidden subtitle must stay quiet");
            frame.dialogue->visible=true;
            const auto speech=reader.observe(speech_frame(frame,true));
            require(speech.size()==1&&speech[0].interrupt&&speech[0].text=="Narrator: Le texte du sous-titre.",
                "a displayed localized subtitle must be spoken promptly despite voice or descriptions");
            frame.dialogue->voice=DialogueVoice::Unknown;
            require(reader.observe(speech_frame(frame,true)).empty(),"voice state changes must not replay the subtitle");
            frame.dialogue->id="localized-2";
            require(reader.observe(speech_frame(frame,true)).size()==1,"the next displayed subtitle must speak immediately");
        }},
        {"unvoiced and unknown lines remain accessible",[] {
            for(auto voice:{DialogueVoice::Unvoiced,DialogueVoice::Unknown}) {
                ReaderCore reader;auto frame=line(voice);
                const auto speech=reader.observe(frame);
                require(speech.size()==1&&speech[0].text=="Narrator: A spoken story line.","missing or unknown native voice must retain dialogue text");
                require(reader.observe(frame).empty(),"unvoiced dialogue must still deduplicate");
            }
        }},
        {"explicit review reads the current line while automatic dialogue is deferred",[] {
            auto frame=line(DialogueVoice::Voiced);
            frame.introduction="Story screen";
            require(current_review(frame,"Previous line")=="Narrator: A spoken story line.","F5 must use the current complete caption while AD defers automatic speech");
            frame.menu=MenuSnapshot{"choices","Wait","",0,2,true,true,true};
            require(current_review(frame,"Previous line")=="Wait. 1 of 2","current native choices retain explicit-review priority");
            frame.menu.reset();frame.dialogue->ready=false;
            require(current_review(frame,"Previous line")=="Previous line","unfinished current text must not leak through explicit repeat");
        }},
        {"a following unvoiced line is not lost",[] {
            ReaderCore reader;reader.observe(line(DialogueVoice::Voiced));
            auto frame=line(DialogueVoice::Unvoiced,"line-2","Read this note.");
            const auto speech=reader.observe(frame);
            require(speech.size()==1&&speech[0].text=="Narrator: Read this note.","voiced suppression cannot consume the following unvoiced line");
            frame.dialogue->id="line-3";
            require(reader.observe(frame).size()==1,"repeated words in a new native line must remain readable");
        }},
        {"description deferral records proven voiced lines without late duplicates",[] {
            ReaderCore reader;auto frame=line(DialogueVoice::Voiced);
            require(reader.observe(speech_frame(frame,true)).empty(),"voiced text must stay quiet during a recorded description");
            frame.dialogue->voice=DialogueVoice::Unknown;
            require(reader.observe(speech_frame(frame,true)).empty(),"native talk ending while AD is still busy must not clear the voiced line identity");
            require(reader.observe(speech_frame(frame,true)).empty(),"multiple deferred captures must retain that identity");
            require(reader.observe(speech_frame(frame,false)).empty(),"the same proven-voiced caption cannot be re-read when native talk ends during AD");
            frame=line(DialogueVoice::Unvoiced,"line-2","Read this note.");
            require(reader.observe(speech_frame(frame,true)).empty(),"new unvoiced dialogue must retain normal AD deferral");
            const auto speech=reader.observe(speech_frame(frame,false));
            require(speech.size()==1&&speech[0].text=="Narrator: Read this note.","unvoiced dialogue must read once AD finishes");
            ReaderCore never_classified;frame=line(DialogueVoice::Unknown);
            frame.introduction="Unvoiced caption";
            never_classified.observe(speech_frame(frame,true));
            require(!never_classified.observe(speech_frame(frame,false)).empty(),"a line never proven voiced must remain accessible after AD");
        }},
        {"menus retain priority beside voiced dialogue",[] {
            ReaderCore reader;auto frame=line(DialogueVoice::Voiced);
            frame.menu=MenuSnapshot{"choices","Wait","",0,2,true,true,true};
            const auto speech=reader.observe(frame);
            require(speech.size()==1&&speech[0].text=="Wait. 1 of 2","voiced dialogue must not duplicate or hide native choices");
            require(reader.repeat()=="Wait. 1 of 2","repeat should follow the current selected choice");
        }},
        {"a voiced-only frame cannot emit its context introduction",[] {
            ReaderCore reader;auto frame=line(DialogueVoice::Voiced);
            frame.introduction="A spoken story line.";
            require(reader.observe(frame).empty(),"context introduction cannot reintroduce a suppressed caption");
            frame.dialogue->voice=DialogueVoice::Unvoiced;frame.dialogue->id="line-2";
            require(!reader.observe(frame).empty(),"a later unvoiced frame must remain eligible");
        }},
        {"field HUD hints are not an open menu",[] {
            ScreenTextObservation hud{"field","Mini-Map. System Menu",true,true,true,false};
            Frame frame{"field",screen_text_menu(hud),{}};
            ReaderCore reader;
            require(!frame.menu,"persistent field hints must not become a modal MenuSnapshot");
            require(reader.observe(frame).empty(),"entering the field must not auto-read persistent HUD hints as a menu");
            require(story_announcement_allowed(frame,true,true,true,false),"field hints must not block an otherwise eligible objective announcement");
        }},
        {"tutorial and startup question fallback stays readable",[] {
            for(const std::string text:{"Mini-Map. Page 2 of 2.","Quit the game?"}) {
                ReaderCore reader;
                Frame frame{"modal",screen_text_menu({"modal",text,true,true,false,false}),{}};
                const auto speech=reader.observe(frame);
                require(speech.size()==1&&speech[0].text==text,"real tutorial/question fallback must remain readable");
                require(!story_announcement_allowed(frame,true,true,true,false),"actual modal feedback must continue to block passive story announcements");
            }
        }},
        {"hidden and unfinished fallback never speaks",[] {
            ScreenTextObservation s{"modal","Hidden question",false,true,false,false};
            require(!screen_text_menu(s),"hidden screen must not speak");
            s.visible=true;s.ready=false;require(!screen_text_menu(s),"opening screen must wait");
            s.ready=true;s.dialogue_window=true;require(!screen_text_menu(s),"dialogue must not be re-read as generic screen text");
        }}
    };
    for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    std::cout<<tests.size()-failed<<'/'<<tests.size()<<" dialogue/HUD groups passed\n";
    return failed?1:0;
}
