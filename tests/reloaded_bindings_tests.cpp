#include "resonance/UnrealApi.hpp"
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace resonance::unreal;

namespace {
template <class T, std::size_t Size>
void put(std::array<std::byte, Size>& storage, std::size_t offset, T value) {
    std::memcpy(storage.data() + offset, &value, sizeof(value));
}

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

unsigned initialized{}, destroyed{}, copied{};
void initialize(const FProperty*, void* destination) {
    ++initialized;
    *static_cast<int*>(destination) = 41;
}
void destroy(const FProperty*, void*) { ++destroyed; }
void copy(const FProperty*, void* destination, const void* source, int count) {
    copied = count;
    std::memcpy(destination, source, sizeof(int) * count);
}
}

int main() {
    alignas(8) std::array<std::byte, 0xe0> function{};
    alignas(8) std::array<std::byte, 0x80> argument{}, returned{};
    auto* fn = reinterpret_cast<UFunction*>(function.data());
    auto* input = reinterpret_cast<FProperty*>(argument.data());
    auto* output = reinterpret_cast<FProperty*>(returned.data());
    FName parameter_name;
    parameter_name.ComparisonIndex = 16;
    parameter_name.Number = 3;
    put(function, 0x50, input);
    put(function, 0x58, 12);
    put(function, 0xb0, std::uint32_t{0x400});
    put(function, 0xb6, std::uint16_t{12});
    put(function, 0xb8, std::uint16_t{8});
    put(argument, 0x18, output);
    put(argument, 0x20, parameter_name);
    put(argument, 0x30, 2);
    put(argument, 0x34, 4);
    put(argument, 0x38, std::uint64_t{0x80});
    put(returned, 0x30, 1);
    put(returned, 0x34, 4);
    put(returned, 0x38, std::uint64_t{0x480});
    put(returned, 0x44, 8);
    check(fn->FindProperty(parameter_name) == input, "find exact reflected FName");
    ++parameter_name.Number;
    check(!fn->FindProperty(parameter_name), "keep numbered field names distinct");
    check(fn->GetReturnProperty() == output, "find reflected return field");
    check(fn->GetParmsSize() == 12 && fn->GetReturnValueOffset() == 8 &&
          fn->GetPropertiesSize() == 12, "read native function extents");
    check(fn->HasAnyFunctionFlags(static_cast<EFunctionFlags>(0x400)), "read native function flag");
    check(input->GetSize() == 8 && output->GetOffset_Internal() == 8, "read reflected field extent");

    std::array<void*, 43> slots{};
    slots[30] = reinterpret_cast<void*>(&copy);
    slots[39] = reinterpret_cast<void*>(&destroy);
    slots[42] = reinterpret_cast<void*>(&initialize);
    put(argument, 0, slots.data());
    std::array<int, 2> destination{}, source{17, 22};
    input->InitializeValue(destination.data());
    check(initialized == 1 && destination[0] == 41, "use native nontrivial initializer");
    input->CopyCompleteValue(destination.data(), source.data());
    check(copied == 2 && destination == source, "use native nontrivial copy for the complete fixed array");
    input->DestroyValue(destination.data());
    check(destroyed == 1, "use native nontrivial destructor");
    put(argument, 0x38, std::uint64_t{0x1040000200});
    input->InitializeValue(destination.data());
    check(initialized == 1 && destination == std::array<int, 2>{}, "zero-initialize trivially constructed values");
    input->CopyCompleteValue(destination.data(), source.data());
    input->DestroyValue(destination.data());
    check(destination == source && destroyed == 1, "copy and release trivially owned values");
}
