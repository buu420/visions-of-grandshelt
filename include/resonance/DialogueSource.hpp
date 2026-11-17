#pragma once

#include <string>

namespace resonance {
namespace unreal { class FText; }

// Uses the already matched, live native caption on the game thread.
std::string capture_dialogue_source_event(const unreal::FText& caption, std::string* reason = nullptr);
}
