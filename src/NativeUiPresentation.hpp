#pragma once

#include "resonance/UnrealApi.hpp"
#include <string>
#include <vector>

namespace resonance {
// Game-thread reads of a layer already mounted in the viewport, and of the
// key list already selected for a visible EnhancedUI glyph.
unreal::UObject* active_layout_layer(unreal::UObject* game_instance,const wchar_t* layer_tag);
std::vector<std::string> displayed_key_labels(unreal::UObject* glyph);
}
