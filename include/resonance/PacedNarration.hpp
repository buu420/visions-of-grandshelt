#pragma once
#include "resonance/NarrationOutput.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace resonance {
struct PacedNarrationCue {
    std::string scene, event, id, text, audio;
    bool finish_on_advance{true};
    std::string caption_sha256;
    DescriptionSource source{DescriptionSource::Recording};
};
struct PacedNarrationEvent {
    std::string key;
    std::uint64_t occurrence{}; // Positive, increasing within one scene/run.
};
// UTF-8 TSV: scene, event, id, audio, text, finish_on_advance (0 or 1).
// A trailing caption_sha256 column may carry scene-unique rendered-caption
// fingerprints for native StringTable events.
// With Recording, validate every relative PCM asset and its fifteen-second
// duration limit. ScreenReader keeps the text and triggers without loading WAVs.
std::vector<PacedNarrationCue> load_paced_narration_manifest(const std::filesystem::path& manifest,
    const std::filesystem::path& asset_root, DescriptionSource source=DescriptionSource::Recording);
struct PacedNarrationFrame {
    std::string map, session, scene;
    // Supply only an event actually observed firing. Do not fill this from a
    // cached pose or inferred sequence/wall time. Repeated occurrences dedupe.
    std::optional<PacedNarrationEvent> event;
    NarrationPlaybackStatus audio;
    bool active{}, ended{}, skipped{}, foreground{true}, paused{}, modal{}, may_start{};
    bool discard_unstarted{};
};

// No clock drives this policy: TalkWait holds do not pause an active recording.
// Native voice/dialogue readiness is supplied through may_start. True pause or
// focus loss pauses the recording. Modal, end, skip and context changes expire it.
// Keep at most the latest foreground event pending. A short current cue may finish
// across event advances if configured. Root must arbitrate the shared output and
// synchronously submit returned actions before observing its next status snapshot.
class PacedNarrationScheduler {
public:
    explicit PacedNarrationScheduler(std::vector<PacedNarrationCue> cues = {});
    std::vector<NarrationAction> set_cues(std::vector<PacedNarrationCue>);
    std::vector<NarrationAction> observe(const PacedNarrationFrame&);
    std::vector<NarrationAction> reset();
    bool has_scene(std::string_view) const;
    const std::vector<PacedNarrationCue>& cues() const {return cues_;}
private:
    struct Pending {std::size_t cue;std::uint64_t occurrence;};
    struct Playing {
        std::size_t cue;
        std::uint64_t after_generation;
        std::optional<std::uint64_t> generation;
        bool paused{};
    };
    std::vector<PacedNarrationCue> cues_;
    std::optional<Pending> pending_;
    std::optional<Playing> playing_;
    std::string map_,session_,scene_;
    std::uint64_t occurrence_{},audio_generation_{};
    bool terminated_{};
    NarrationAction action(NarrationActionKind,std::size_t) const;
    void expire(std::vector<NarrationAction>&);
};
}
