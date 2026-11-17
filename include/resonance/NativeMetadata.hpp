#pragma once
#include "resonance/NativeBoundary.hpp"
#include "resonance/UnrealApi.hpp"
namespace resonance {
// Only for metadata returned by a validated FProperty or StaticFindObject.
// The pinned IsValidObjectForFindXOf intentionally rejects these metaclasses;
// ordinary game objects must continue to use the ordinary-instance filter.
inline bool native_metadata(resonance::unreal::UObject* object,NativeMetadataKind kind) {
    if(!object)return false;
    auto* cls=object->GetClassPrivate();if(!cls)return false;
    auto name=cls->GetFullName();const auto space=name.find(L' ');
    const std::wstring_view path=space==std::wstring::npos?std::wstring_view(name):std::wstring_view(name).substr(space+1);
    return native_metadata_shape(static_cast<std::uint32_t>(object->GetObjectFlags()),path,kind);
}
}
