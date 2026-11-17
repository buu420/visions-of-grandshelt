#pragma once

#include "resonance/AudioSettings.hpp"
#include "resonance/Core.hpp"
#include "resonance/UnrealApi.hpp"

namespace resonance {
namespace accessibility_menu_detail { enum class Action; }
class AccessibilityMenu {
public:
    void tick(unreal::UObject* active, AudioSettings& settings);
    bool before_event(unreal::UObject*, unreal::UFunction*, void* params);
    bool open() const { return open_; }
    const std::optional<MenuSnapshot>& snapshot() const { return snapshot_; }

private:
    unreal::UObject* root_{};
    unreal::UObject* layout_{};
    unreal::UObject* widget_{};
    unreal::UObject* menu_{};
    unreal::UObject* title_{};
    unreal::UObject* entry_{};
    std::optional<MenuSnapshot> snapshot_;
    accessibility_menu_detail::Action request_{};
    int request_index_{-1};
    bool open_{};
    bool updating_{};
    bool title_changed_{};
    bool rebuild_failed_{};
    bool unavailable_logged_{};
    bool added_logged_{};
};
}
