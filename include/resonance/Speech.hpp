#pragma once
#include "resonance/Core.hpp"
#include "resonance/NarrationOutput.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
namespace resonance {
void log(const std::string&);
void set_log_path(const std::filesystem::path&);
class Speech {
public:
    explicit Speech(std::filesystem::path);
    ~Speech();
    void say(std::string,bool interrupt=true);
    void say(SpeechEvent);
    void stop();
    void interrupt_battle();
    void describe(const NarrationAction&);
    NarrationPlaybackStatus description_status() const;
    bool ready() const { return ready_; }
    unsigned long long epoch() const { return epoch_; }
private:
    void run();
    std::filesystem::path folder_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    SpeechQueue queue_;
    NarrationPlaybackStatus description_;
    bool quit_{}, stop_{}, battle_stop_{}, description_stop_{};
    std::atomic<bool> ready_{false};
    std::atomic<unsigned long long> epoch_{};
    std::thread thread_;
};
}
