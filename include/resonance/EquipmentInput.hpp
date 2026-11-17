#pragma once

#include "resonance/Core.hpp"
#include "resonance/UnrealApi.hpp"

#include <string_view>

namespace resonance {
// The caller passes the active Equipment & Abilities scene and the visible
// native selection already obtained for the current input target.
std::optional<MenuSnapshot> equipment_row_snapshot(unreal::UObject* scene, unreal::UObject* menu,
    unreal::UObject* focused, int index, int count, std::string_view displayed);
}
