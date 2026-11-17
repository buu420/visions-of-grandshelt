#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
std::byte* test_image{};
using TestEvent = void(*)(int*);
TestEvent original_event{};
void test_event(int* value) { original_event(value); }
HMODULE WINAPI test_module(LPCWSTR) { return reinterpret_cast<HMODULE>(test_image); }
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}

namespace resonance {
void log(const std::string& line) { std::cout << line << '\n'; }
}

#define GetModuleHandleW test_module
#include "NativeWalking.cpp"
#undef GetModuleHandleW

int main() {
    try {
        // Reserve all relative-jump addresses in this test process without
        // committing them. The actual game hook entry bytes live in the middle.
        constexpr std::size_t radius = 0x80000000, center_size = 0x20000;
        auto* reservation = static_cast<std::byte*>(
            VirtualAlloc(nullptr, radius * 2 + center_size, MEM_RESERVE, PAGE_NOACCESS));
        require(reservation != nullptr, "reserve the relative-jump window");
        auto* center = reservation + radius;
        auto* code = center + 0x8000;
        require(VirtualAlloc(code, 0x2000, MEM_COMMIT, PAGE_EXECUTE_READWRITE) == code, "commit test code");
        test_image = center - 0x50e0000;
        auto* tick = center + 0x8830;
        auto* apply = center + 0x97e0;
        auto* event = center + 0x9a00;
        const unsigned char tick_bytes[] = {
            0x40,0x55,0x53,0x48,0x8d,0x6c,0x24,0xc8,0x48,0x81,0xec,0x38,0x01,0x00,0x00,
            0x83,0x01,0x01,0x48,0x81,0xc4,0x38,0x01,0x00,0x00,0x5b,0x5d,0xc3};
        const unsigned char apply_bytes[] = {
            0x48,0x8b,0xc4,0x53,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x41,0x80,0x38,0x00,
            0x83,0x01,0x02,0x48,0x81,0xc4,0xa0,0x00,0x00,0x00,0x5b,0xc3};
        const unsigned char event_bytes[] = {
            0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0,1,0,0,
            0x83,0x01,0x04,0x48,0x81,0xc4,0,1,0,0,0x41,0x5f,0x41,0x5e,0x41,0x5d,0x41,0x5c,0x5f,0x5e,0x5d,0xc3};
        std::memcpy(event, event_bytes, sizeof event_bytes);
        std::memcpy(tick, tick_bytes, sizeof tick_bytes);
        std::memcpy(apply, apply_bytes, sizeof apply_bytes);
        DWORD previous{};
        require(VirtualProtect(code, 0x2000, PAGE_EXECUTE_READ, &previous) != 0, "seal test code");
        require(FlushInstructionCache(GetCurrentProcess(), code, 0x2000) != 0, "publish test code");

        const auto installed = resonance::install_native_walking();
        require(installed == resonance::NativeWalkingInstall::Ready && resonance::native_walking_ready(),
                "the native walking hooks must install with the relative-jump window reserved");
        int value = 0;
        unsigned char input = 1;
        auto* pawn = reinterpret_cast<resonance::unreal::UObject*>(&value);
        reinterpret_cast<void (*)(resonance::unreal::UObject*, float)>(tick)(pawn, 0.1f);
        reinterpret_cast<void (*)(resonance::unreal::UObject*, float, const void*)>(apply)(pawn, 0.1f, &input);
        require(value == 3, "both detours must call the native bodies with the complete prologues");

        constexpr std::array<UINT8, 9> boundaries{0,2,3,4,6,8,10,12,19};
        require(ResonanceCreateAbsoluteHook(event, reinterpret_cast<void*>(&test_event),
                    reinterpret_cast<void**>(&original_event), boundaries.data(), static_cast<UINT>(boundaries.size())) == MH_OK,
                "the presentation hook needs nine instruction boundaries");
        require(MH_EnableHook(event) == MH_OK, "enable the absolute presentation hook");
        reinterpret_cast<TestEvent>(event)(&value);
        require(value == 7, "the complete presentation prologue must run");

        resonance::disable_native_walking();
        require(MH_Uninitialize() == MH_OK, "remove test hooks");
        require(std::memcmp(tick, tick_bytes, sizeof tick_bytes) == 0 &&
                std::memcmp(apply, apply_bytes, sizeof apply_bytes) == 0 &&
                std::memcmp(event, event_bytes, sizeof event_bytes) == 0, "restore all overwritten instructions");
        require(VirtualFree(reservation, 0, MEM_RELEASE) != 0, "release the test reservation");
        std::cout << "PASS native walking with the relative-jump address space reserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
