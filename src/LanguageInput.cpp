#include "resonance/LanguageInput.hpp"

#include "resonance/UnrealApi.hpp"

#include <array>
#include <cstdint>

namespace resonance
{
    std::optional<std::string_view> capture_game_language()
    {
        using namespace unreal;

        // The native getter returns the applied FInternationalization language
        // using the game's ELanguage ordinals. StringKey, Max and Unsupported
        // have no player-facing language pack.
        constexpr std::array<std::string_view, 8> tags{
            "ja", "en", "de", "fr", "es-ES", "ko", "zh-Hant", "zh-Hans",
        };

        auto* defaults = UObjectGlobals::StaticFindObject(L"/Script/mucha.Default__CPP_BFL_Localize");
        if (!defaults || !UObjectGlobals::IsValidObjectForFindXOf(defaults))
        {
            return std::nullopt;
        }
        constexpr std::uint32_t class_default_object = 0x10u;
        constexpr std::uint32_t dying_object = 0x8000u | 0x10000u | 0x40000000u;
        const auto object_flags = static_cast<std::uint32_t>(defaults->GetObjectFlags());
        if (!(object_flags & class_default_object) || (object_flags & dying_object)
            || defaults->GetFullName() != L"CPP_BFL_Localize /Script/mucha.Default__CPP_BFL_Localize")
        {
            return std::nullopt;
        }

        auto* function = defaults->GetFunctionByNameInChain(L"GetLanguage");
        constexpr auto native = static_cast<EFunctionFlags>(0x400u);
        constexpr auto static_function = static_cast<EFunctionFlags>(0x2000u);
        if (!function || function->GetOuterPrivate() != defaults->GetClassPrivate()
            || !function->HasAnyFunctionFlags(native) || !function->HasAnyFunctionFlags(static_function)
            || function->GetParmsSize() != 1 || function->GetPropertiesSize() != 1
            || function->GetReturnValueOffset() != 0)
        {
            return std::nullopt;
        }

        auto* result = function->GetReturnProperty();
        const auto* result_class = result ? result->field_class() : nullptr;
        constexpr std::uint64_t parameter = 0x80u;
        constexpr std::uint64_t return_parameter = 0x400u;
        if (!result || result->GetSize() != 1 || result->GetOffset_Internal() != 0
            || !result->HasAnyPropertyFlags(parameter) || !result->HasAnyPropertyFlags(return_parameter)
            || !result_class || result_class->GetName() != L"EnumProperty")
        {
            return std::nullopt;
        }

        alignas(16) std::uint8_t ordinal = 0xff;
        defaults->ProcessEvent(function, &ordinal);
        return ordinal < tags.size() ? std::optional{tags[ordinal]} : std::nullopt;
    }
}
