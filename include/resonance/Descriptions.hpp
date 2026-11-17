#pragma once
#include "resonance/PacedNarration.hpp"
#include "resonance/Core.hpp"
#include <optional>
#include <set>
namespace resonance {
struct DescriptionFrame {
    Frame ui;
    std::vector<NarrationClock> clocks;
    NarrationPlaybackStatus audio;
    std::string native_scene,native_session;
    bool foreground{true},output_ready{true},pause_known{},user_paused{},voice_known{},voice_busy{};
    bool native_event_known{},native_event_active{};
    std::string caption_sha256;
    bool screen_reader{};
};
struct DescriptionDecision {
    std::vector<NarrationAction> actions;
    bool defer_dialogue{},stop_speech{};
    std::string notice;
    std::string caption_end_cue;
    std::string trace;
};
enum class DescriptionFailureDisposition { Wait, Discard, Speak };
// Use the exact frozen capture which the Reader just processed. Wait keeps a
// current failure pending until that capture observes its error and releases the
// native caption. Discard rejects superseded, background, ended or wrong-run data.
DescriptionFailureDisposition classify_description_failure(const NarrationAction&,
                                                           const DescriptionFrame&,
                                                           bool dialogue_deferred);
// Arbitrates one description output before the reader emits current dialogue.
// Only fresh completed native dialogue occurrences trigger paced cues. Unknown
// native gates release dialogue and report unavailable; they never hold it forever.
// Missing/ambiguous captures suspend output while preserving same-run occurrence
// tombstones and clocked consumption. Only positive context/end evidence resets.
class DescriptionCoordinator {
public:
    std::vector<NarrationAction> set_cues(std::vector<NarrationCue>,std::vector<PacedNarrationCue>);
    DescriptionDecision observe(const DescriptionFrame&);
    std::vector<NarrationAction> reset();
    bool has_scene(std::string_view) const;
    bool has_caption_fallback(std::string_view scene) const;
    bool has_context() const;
private:
    struct Reservation {
        std::string cue_id, dialogue_key, dialogue_id;
        std::uint64_t after_generation{};
        std::optional<std::uint64_t> generation;
        bool started{},may_start_at_caption_end{};
    };
    NarrationScheduler clocked_;
    PacedNarrationScheduler paced_;
    std::string map_,scene_,session_,last_dialogue_;
    std::string traced_cue_;
    std::optional<Reservation> reserved_;
    std::set<std::string> seen_;
    std::uint64_t occurrence_{};
    bool unavailable_reported_{};
    std::vector<NarrationAction> suspend(const DescriptionFrame&);
    void reconcile_reservation(const NarrationPlaybackStatus&);
};
}
