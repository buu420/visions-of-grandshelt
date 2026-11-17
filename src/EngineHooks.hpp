#pragma once
#include "resonance/UnrealApi.hpp"

namespace resonance {
using EnginePostTick = void(*)(float);
using PresentationPreEvent = bool(*)(unreal::UObject*, unreal::UFunction*, void*);
using PresentationPostEvent = void(*)(unreal::UObject*, unreal::UFunction*, void*);
bool install_engine_hooks(std::uintptr_t image, EnginePostTick tick, PresentationPreEvent before, PresentationPostEvent after);
}
