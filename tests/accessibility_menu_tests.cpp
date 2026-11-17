#include "../src/AccessibilityMenuModel.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

using namespace resonance;
using namespace resonance::accessibility_menu_detail;

namespace {
void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}
}

int main() {
    const std::array normal{1, 2, 3, 4, 5};
    const auto with_entry = insert_menu_entry<int>(normal, 3);
    check(with_entry && *with_entry == std::vector<int>({1, 2, 3, 0, 4, 5}),
        "Accessibility Options should follow Options and preserve native rows");
    check(!insert_menu_entry<int>(normal, 9), "a menu without Options cannot be extended");
    check(!insert_menu_entry<int>(std::array{1, 3, 3, 4}, 3), "ambiguous Options must be rejected");
    check(!insert_menu_entry<int>(std::array{1, 3, 0, 4}, 3), "a live placeholder must not be inserted twice");

    AudioSettings settings;
    check(!adjust(settings, 0, Action::Increase) && settings.beacon_volume == 100,
        "volume cannot exceed 100");
    check(adjust(settings, 0, Action::Decrease) && settings.beacon_volume == 90,
        "beacon volume must step down");
    check(adjust(settings, 1, Action::Confirm) && !settings.wall_enabled,
        "Confirm must toggle wall tones");
    check(adjust(settings, 2, Action::Decrease) && settings.wall_volume == 90 && !settings.wall_enabled,
        "wall tone volume and enabled state must be independent");
    check(adjust(settings, 3, Action::Decrease) && !settings.npc_enabled && settings.object_enabled,
        "NPC and object switches must be independent");
    check(adjust(settings, 5, Action::Decrease) && !settings.object_enabled && !settings.npc_enabled,
        "object toggle must preserve NPC toggle");
    check(adjust(settings, 5, Action::Increase) && settings.object_enabled,
        "right must enable a disabled switch");
    check(!adjust(settings, 7, Action::Decrease), "unknown row must have no action");
    settings.object_volume = 4;
    check(adjust(settings, 6, Action::Decrease) && settings.object_volume == 0,
        "volume must clamp at zero");
    check(value(settings, 2) == "90 percent" && value(settings, 3) == "Off",
        "only the current value should be provided to speech");
}
