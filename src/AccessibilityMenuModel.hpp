#pragma once

#include "resonance/AudioSettings.hpp"
#include "resonance/Localization.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace resonance::accessibility_menu_detail {
enum class Action { None, Open, Back, Decrease, Increase, Confirm };
struct Row {
    const char* label;
    int AudioSettings::*volume{};
    bool AudioSettings::*enabled{};
};
inline constexpr std::array rows{
    Row{"Beacon volume", &AudioSettings::beacon_volume},
    Row{"Wall tones", nullptr, &AudioSettings::wall_enabled},
    Row{"Wall tone volume", &AudioSettings::wall_volume},
    Row{"NPC sounds", nullptr, &AudioSettings::npc_enabled},
    Row{"NPC sound volume", &AudioSettings::npc_volume},
    Row{"Object sounds", nullptr, &AudioSettings::object_enabled},
    Row{"Object sound volume", &AudioSettings::object_volume},
};

template<class Tag>
std::optional<std::vector<Tag>> insert_menu_entry(std::span<const Tag> original, Tag options) {
    const auto where = std::find(original.begin(), original.end(), options);
    if (where == original.end() || options == Tag{} ||
        std::find(where + 1, original.end(), options) != original.end() ||
        std::find(original.begin(), original.end(), Tag{}) != original.end()) return {};
    std::vector<Tag> result(original.begin(), original.end());
    result.insert(result.begin() + (where - original.begin()) + 1, Tag{});
    return result;
}

inline std::string value(const AudioSettings& settings, int index, bool spoken = true) {
    if (index < 0 || index >= static_cast<int>(rows.size())) return {};
    const auto& row = rows[index];
    if (row.volume)
        return trf(spoken ? "{0} percent" : "{0}%", {std::to_string(settings.*row.volume)});
    return tr(settings.*row.enabled ? "On" : "Off");
}

inline bool adjust(AudioSettings& settings, int index, Action action) {
    if (index < 0 || index >= static_cast<int>(rows.size())) return false;
    const auto& row = rows[index];
    if (row.volume) {
        if (action != Action::Decrease && action != Action::Increase) return false;
        auto& volume = settings.*row.volume;
        const auto previous = volume;
        volume = std::clamp(volume + (action == Action::Increase ? 10 : -10), 0, 100);
        return previous != volume;
    }
    auto& enabled = settings.*row.enabled;
    const auto previous = enabled;
    if (action == Action::Confirm) enabled = !enabled;
    if (action == Action::Increase) enabled = true;
    if (action == Action::Decrease) enabled = false;
    return previous != enabled;
}
}
