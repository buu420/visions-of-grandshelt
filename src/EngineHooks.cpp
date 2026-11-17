#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "NativeHook.hpp"
#include "EngineHooks.hpp"
#include "resonance/Speech.hpp"

#include <array>
#include <cstring>

namespace resonance {
namespace {
using Tick = void(*)(unreal::UEngine*, float, bool);
using Event = void(*)(unreal::UObject*, unreal::UFunction*, void*);

Tick original_tick{};
Event original_event{};
EnginePostTick post_tick{};
PresentationPreEvent pre_event{};
PresentationPostEvent post_event{};

void tick_hook(unreal::UEngine* engine, float seconds, bool idle) {
    original_tick(engine, seconds, idle);
    post_tick(seconds);
}

void event_hook(unreal::UObject* object, unreal::UFunction* function, void* args) {
    if (pre_event(object, function, args)) return;
    original_event(object, function, args);
    post_event(object, function, args);
}

template<std::size_t N, std::size_t Count>
MH_STATUS create_hook(void* target, void* hook, void** original,
                      const std::array<UINT8, N>& bytes,
                      const std::array<UINT8, Count>& boundaries) {
    if (std::memcmp(target, bytes.data(), bytes.size()) != 0) return MH_ERROR_UNSUPPORTED_FUNCTION;
    auto status = MH_CreateHook(target, hook, original);
    if (status == MH_ERROR_MEMORY_ALLOC) {
        status = ResonanceCreateAbsoluteHook(target, hook, original,
            boundaries.data(), static_cast<UINT>(boundaries.size()));
    }
    return status;
}

bool failed(const char* operation, MH_STATUS status) {
    log(std::string("ERROR native engine hook ") + operation + " " + MH_StatusToString(status));
    return false;
}
}

bool install_engine_hooks(std::uintptr_t image, EnginePostTick tick, PresentationPreEvent before, PresentationPostEvent after) {
    post_tick = tick;
    pre_event = before;
    post_event = after;

    auto* tick_address = reinterpret_cast<void*>(image + 0x3c75120);
    auto* event_address = reinterpret_cast<void*>(image + 0x1592e30);
    // Complete position-independent instructions verified in the exact executable.
    constexpr std::array<UINT8, 14> tick_bytes{
        0x40,0x55,0x53,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe0};
    constexpr std::array<UINT8, 7> tick_boundaries{0,2,3,5,7,9,14};
    constexpr std::array<UINT8, 19> event_bytes{
        0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0,1,0,0};
    constexpr std::array<UINT8, 9> event_boundaries{0,2,3,4,6,8,10,12,19};

    auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return failed("initialize", status);
    status = create_hook(tick_address, reinterpret_cast<void*>(&tick_hook),
        reinterpret_cast<void**>(&original_tick), tick_bytes, tick_boundaries);
    if (status != MH_OK) return failed("engine tick", status);
    status = create_hook(event_address, reinterpret_cast<void*>(&event_hook),
        reinterpret_cast<void**>(&original_event), event_bytes, event_boundaries);
    if (status != MH_OK) {
        MH_RemoveHook(tick_address);
        return failed("ProcessEvent", status);
    }

    status = MH_QueueEnableHook(tick_address);
    if (status == MH_OK) status = MH_QueueEnableHook(event_address);
    if (status == MH_OK) status = MH_ApplyQueued();
    if (status != MH_OK) {
        MH_RemoveHook(event_address);
        MH_RemoveHook(tick_address);
        return failed("enable", status);
    }
    return true;
}
}
