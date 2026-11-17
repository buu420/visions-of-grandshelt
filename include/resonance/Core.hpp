#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <deque>

namespace resonance {
struct JournalPassage {
    std::string text;
    float offset{};
    unsigned long long scroll_input{};
};
struct MenuSnapshot {
    std::string id, label, value;
    int index{-1}, count{};
    bool visible{}, ready{}, enabled{true};
    // No value while the current detail panel is rebinding. An empty value
    // records a confirmed close, so reopening it can speak the same text.
    std::optional<std::string> details;
    std::optional<JournalPassage> journal;
};
struct OptionObservation {
    std::string label;
    std::vector<std::string> alternatives;
    int selected{-1};
    std::string presented_value;
    bool selection_expected{true}, popup_open{}, enabled{true}, binding_matches{true};
};
MenuSnapshot option_menu(const OptionObservation&,std::string id,int row,int rows);
struct ScreenTextObservation {
    std::string id,text;
    bool visible{},ready{},field_hud{},dialogue_window{};
};
std::optional<MenuSnapshot> screen_text_menu(const ScreenTextObservation&);
enum class DialogueVoice : unsigned char { Unknown, Unvoiced, Voiced };
struct VoiceAssignment {
    bool known{},active{},reflected{},voice_valid{};
    int playback_id{-1};
};
DialogueVoice assigned_dialogue_voice(const VoiceAssignment&);
struct DialogueSnapshot {
    std::string id, speaker, text;
    bool visible{}, ready{};
    DialogueVoice voice{DialogueVoice::Unknown};
    std::string source_event;
    bool read_as_subtitle{};
    bool automatically_read() const { return read_as_subtitle||voice!=DialogueVoice::Voiced; }
};
struct TargetSnapshot {
    std::string id, label, prompt;
    bool visible{}, interactable{};
};
struct Frame {
    std::string context;
    std::optional<MenuSnapshot> menu;
    std::optional<DialogueSnapshot> dialogue;
    std::string introduction;
    bool menu_interrupts{true};
    std::string menu_hints;
    unsigned long long observed_at{};
};
enum class SpeechDelivery : unsigned char { Normal, LatestSelection, SelectionSupplement, OrderedBattle, Description };
struct SpeechEvent {
    std::string text;
    bool interrupt{true};
    SpeechDelivery delivery{SpeechDelivery::Normal};
};
class ReaderCore {
public:
    std::vector<SpeechEvent> observe(const Frame&);
    void reset();
    void output_epoch(unsigned long long);
    const std::string& repeat() const { return last_; }
private:
    std::string context_, menu_, menu_text_, details_, hints_, dialogue_, last_;
    std::optional<JournalPassage> journal_page_;
    unsigned long long journal_moved_at_{}, journal_read_input_{};
    bool journal_opening_read_{};
    bool introduced_{};
    unsigned long long epoch_{};
};
std::string current_review(const Frame&,const std::string& previous);
Frame speech_frame(const Frame&,bool describing);
struct Vec3 { double x{}, y{}, z{}; };
struct PathSnapshot {
    std::vector<Vec3> points;
    Vec3 right{1,0,0}, up{0,1,0};
    double world_to_metres{};
    bool valid{}, partial{};
};
std::optional<std::string> describe_route(const PathSnapshot&,std::size_t max_legs=4096);
std::optional<std::string> describe_target(const TargetSnapshot&);
struct RenderedText { std::string text; bool visible{}, gradual{}, finished{}; };
std::optional<std::string> presented_text(const RenderedText&);
std::vector<int> presented_children(int count,bool switcher,int active_index);
class SpeechQueue {
public:
    void push(SpeechEvent);
    void discard(SpeechDelivery);
    bool contains(SpeechDelivery) const;
    std::optional<SpeechEvent> pop();
    const SpeechEvent* front() const { return queue_.empty()?nullptr:&queue_.front(); }
    void clear() { queue_.clear(); }
    size_t size() const { return queue_.size(); }
private:
    std::deque<SpeechEvent> queue_;
};
}
