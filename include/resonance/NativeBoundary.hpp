#pragma once
#include <cstdint>
#include <string_view>
namespace resonance {
// UFunction::ParmsSize stops at the last parameter; PropertiesSize includes
// native alignment padding. Only the independently audited extent/allocation
// pair is accepted, never an arbitrary smaller parameter block.
inline bool native_call_extent(int parms,int allocation,int expected_allocation,int actual_return,
        int expected_return,int consumed_end,int capacity) {
    return expected_allocation>0&&allocation==expected_allocation&&allocation<=capacity&&
        consumed_end>0&&consumed_end<=allocation&&
        (expected_return<0||(expected_return>=0&&expected_return<consumed_end))&&
        actual_return==(expected_return<0?65535:expected_return)&&
        (parms==consumed_end||parms==expected_allocation);
}
enum class NativeMetadataKind {Class,ScriptStruct,Function};
inline bool getter_call_extent(bool native,int parms,int allocation,int expected_allocation,int actual_return,
        int expected_return,int consumed_end,int capacity) {
    if(native)return native_call_extent(parms,allocation,expected_allocation,actual_return,expected_return,consumed_end,capacity);
    // Blueprint overrides retain locals after their parameters. Do not confuse
    // those locals with an allocation change in a pinned native signature.
    return expected_allocation>0&&allocation>=expected_allocation&&allocation<=capacity&&
        consumed_end>0&&consumed_end<=expected_allocation&&
        (expected_return<0||expected_return<consumed_end)&&
        actual_return==(expected_return<0?65535:expected_return)&&
        (parms==consumed_end||parms==expected_allocation);
}
inline bool native_metadata_shape(std::uint32_t flags,std::wstring_view metaclass,NativeMetadataKind kind) {
    constexpr std::uint32_t excluded=0x10u|0x20u|0x8000u|0x10000u|0x40000000u;
    if(flags&excluded)return false;
    const auto expected=kind==NativeMetadataKind::Class?L"/Script/CoreUObject.Class":
        kind==NativeMetadataKind::ScriptStruct?L"/Script/CoreUObject.ScriptStruct":L"/Script/CoreUObject.Function";
    return metaclass==expected;
}
}
