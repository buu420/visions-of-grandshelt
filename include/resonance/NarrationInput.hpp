#pragma once
// Read-only native facts for paced narration in scripted talk events. Call on the
// game thread (engine post-tick). Objects are re-resolved every call; nothing is
// cached, loaded, written, played, paused or otherwise changed.
#include <string>

namespace resonance {
enum class NarrationFact : unsigned char { Unknown, No, Yes };

struct NativeNarrationInput {
    // Same identifiers as UnrealReader's sequence clocks: session is
    // "<scene path>|<sequence player path>", scene is "sequence:<asset path>".
    // Empty when the current event player is not a sequence.
    std::string session, scene;
    NarrationFact event_active{};    // Subsystem's manager has a live scene and event player.
    // HandlePause/HandleResume flag (CPP_TalkEventScene native byte +0x78). TalkWait
    // does not set it. Yes means the player's pause/skip menu holds the event.
    NarrationFact user_pause{};
    NarrationFact sequence_paused{}, sequence_playing{}; // Sequence player; TalkWait also pauses it.
    // The game's own auto-proceed voice test: TalkVoice id (+0x28) >= 0 and
    // CPP_SoundManager::IsExistPlaySound(id).
    NarrationFact event_voice_busy{};  // The event talk widget's character voice.
    NarrationFact any_talk_voice_busy{}; // Any live talk widget (events or ATC chat).
    int voice_id{-1};
    NarrationFact talk_window_present{}; // Event talk widget has a live window.
    bool may_start{};
    std::string reason; // First reason may_start is false (diagnostic).
};

// dialogue_ready: root's own fact that the visible dialogue text has completely
// appeared and has been captured. may_start needs it plus a known quiet voice.
NativeNarrationInput capture_narration_input(bool dialogue_ready);
}
