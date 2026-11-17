#pragma once
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace resonance {
struct NarrationCue {
    std::string scene, id, text, audio;
    double start{}, end{};
    double duration{}; // Validated PCM duration; zero keeps the 150 ms legacy policy.
};
struct NarrationClock {
    std::string session, scene;
    double seconds{}, sample_seconds{}, rate{1};
    bool valid{}, playing{}, paused{}, foreground{true}, seeking{};
};
enum class NarrationActionKind { Play, Pause, Resume, Stop };
struct NarrationAction {
    NarrationActionKind kind{};
    std::string scene, cue_id, text, audio;
    double offset{};
    // Immutable native context added before enqueueing; generation is filled by
    // the output worker when it reports an error. No engine pointers cross threads.
    std::string session, dialogue_id;
    std::uint64_t generation{};
};

// Clocks and cue times use seconds relative to the same native scene origin.
// session must identify a particular player/run, not just the scene asset.
// Audio starts at offset zero after a naturally crossed cue start only if its
// measured recording still fits with 50 ms margin. The asset loader requires
// 200 ms at the authored start, allowing normal capture latency within that gap.
// Without a measured duration,
// the start window is 150 ms. Missed starts are skipped; an active clip is cut
// off at end. Non-unit playback rates
// are unsupported while playing. No clock or action contains engine pointers.
// Feed one authoritative active source, not interleaved movie/sequence clocks.
// A native/wall-time discrepancy over 350 ms invalidates active synchronization;
// backward native jumps over 50 ms rearm future cues. If paused audio's native
// clock advances over 100 ms, discard it instead of resuming out of sync.
// A playing custom clock with no >5 ms native progress for 240 ms pauses audio.
// Resume after genuine progress catches its estimated played position within
// 30 ms; a >100 ms overshoot stops instead. Recovery cannot start another cue.
class NarrationScheduler {
public:
    explicit NarrationScheduler(std::vector<NarrationCue> cues = {});
    // Strong exception guarantee. Reject invalid intervals, overlapping cues in
    // one scene, repeated (scene,id), and unsafe relative audio paths. On success
    // apply returned Stop before using the replacement table.
    std::vector<NarrationAction> set_cues(std::vector<NarrationCue>);
    std::vector<NarrationAction> observe(const NarrationClock&);
    std::vector<NarrationAction> reset();
    bool has_scene(std::string_view) const;
    const std::vector<NarrationCue>& cues() const { return cues_; }
private:
    std::vector<NarrationCue> cues_;
    std::vector<bool> consumed_;
    std::optional<NarrationClock> previous_;
    std::optional<std::size_t> active_;
    std::string session_, scene_;
    bool output_paused_{}, initial_window_{};
    double paused_at_{};
    bool clock_tracking_{}, clock_held_{}, hold_paused_{};
    double progress_native_{}, progress_sample_{}, audio_native_{};
    NarrationAction action(NarrationActionKind,std::size_t) const;
    void stop(std::vector<NarrationAction>&);
};
}
