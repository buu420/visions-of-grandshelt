#pragma once
#include "resonance/Navigation.hpp"
#include <cstdint>

namespace resonance::unreal { class UObject; }

namespace resonance {
enum class NativeWalkingInstall { Ready, Retryable, Unavailable };

// Retry a temporary startup failure only on a fresh walking request.
class NativeWalkingStartup {
public:
    bool due(std::uint64_t now, bool requested) const {
        if (attempts_ == 0) return true;
        return retryable_ && attempts_ < 3 && requested &&
            now >= last_attempt_ && now - last_attempt_ >= 750;
    }
    void complete(NativeWalkingInstall result, std::uint64_t now) {
        ++attempts_;
        last_attempt_ = now;
        retryable_ = result == NativeWalkingInstall::Retryable;
    }
private:
    unsigned attempts_{};
    std::uint64_t last_attempt_{};
    bool retryable_{};
};

struct NativeWalkingPulse {
    resonance::unreal::UObject* pawn{};
    void* input{};
    const void* movement_slot{};
    resonance::unreal::UObject* movement{};
    const void* max_speed{};
    const void* velocity{};
    const void* movement_mode{};
    const void* accumulated_input{};
    const void* root_slot{};
    resonance::unreal::UObject* root{};
    const void* position{};
    Vec3 sampled_position{};
    Vec3 world_direction{},stick{};
    double clearance{},side_margin{},acceleration{},braking{},tick{};
    std::optional<double> min_analog_speed;
};

// Install only after the executable and loader hashes have been verified.
NativeWalkingInstall install_native_walking();
bool native_walking_ready();
void disable_native_walking();
void queue_native_walking(NativeWalkingPulse);
void clear_native_walking();
bool owns_native_walking_input(resonance::unreal::UObject*,Vec3);
}
