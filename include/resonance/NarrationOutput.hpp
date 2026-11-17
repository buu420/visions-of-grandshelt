#pragma once
#include "resonance/Narration.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace resonance {
struct NarrationWave {
    std::uint16_t channels{};
    std::uint32_t sample_rate{};
    std::vector<std::byte> samples;
    double duration() const;
};

// Throw std::runtime_error with a useful reason. Only bounded, nonempty RIFF
// PCM16 mono/stereo audio is accepted (8-192 kHz, at most 64 MiB per file).
NarrationWave read_narration_wave(const std::filesystem::path&);
// Exact UTF-8 TSV header: scene, id, start, end, audio, text. Numeric fields
// are locale-independent seconds. Validate the entire scheduler table and every
// WAV before returning; no partial successful table. Assets must resolve inside
// asset_root, including through symlinks/junctions, and fit their cue interval.
std::vector<NarrationCue> load_narration_manifest(const std::filesystem::path& manifest,
                                                const std::filesystem::path& asset_root);

class NarrationAudioDevice {
public:
    virtual ~NarrationAudioDevice() = default;
    virtual void play(NarrationWave, double offset_seconds) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    // Called on the output worker only. Return true after the device has actually
    // finished its buffer; throw on a polling failure. Legacy fakes stay active.
    virtual bool completed() { return false; }
};

struct NarrationPlaybackStatus {
    std::uint64_t generation{};
    std::string scene, cue_id;
    bool active{}, paused{}, completed{}, error{}, queued{};
    std::string message;
};

class NarrationOutput {
public:
    using ErrorCallback = std::function<void(const NarrationAction&, const std::string&)>;
    NarrationOutput(std::filesystem::path asset_root, ErrorCallback);
    // Inject a fake/non-audible device for lifecycle tests.
    NarrationOutput(std::filesystem::path asset_root, ErrorCallback,
                    std::unique_ptr<NarrationAudioDevice>);
    ~NarrationOutput();
    NarrationOutput(const NarrationOutput&) = delete;
    NarrationOutput& operator=(const NarrationOutput&) = delete;
    void submit(const std::vector<NarrationAction>&);
    // Pointer-free snapshot. A queued Play is active immediately; a pending Stop
    // stays active until the worker actually releases an active device. queued
    // clears on acknowledgement. Newer Play/Stop invalidates old completion/error.
    NarrationPlaybackStatus status() const;
private:
    // Error callbacks are serialized against submit/Stop and never called for
    // superseded generations. They must be short and must not re-enter output.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
