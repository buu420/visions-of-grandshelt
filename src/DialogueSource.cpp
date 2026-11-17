#include "resonance/DialogueSource.hpp"
#include "resonance/NativeBoundary.hpp"
#include "resonance/UnrealApi.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace resonance {
namespace {
using namespace unreal;

struct NativeKey {
    wchar_t* data{};
    std::int32_t count{}, capacity{};
};

struct alignas(16) SourceArguments {
    std::array<std::byte, 16> text{};
    FName table_id{};
    NativeKey key{};
    std::uint8_t found{};
};
static_assert(sizeof(SourceArguments) == 48 &&
    offsetof(SourceArguments, text) == 0 && offsetof(SourceArguments, table_id) == 16 &&
    offsetof(SourceArguments, key) == 24 && offsetof(SourceArguments, found) == 40);

struct SourceCall {
    SourceArguments args;
    const FProperty& text;
    const FProperty& key;

    SourceCall(const FProperty& text_property, const FProperty& key_property)
        : text(text_property), key(key_property) {
        text.InitializeValue(args.text.data());
        key.InitializeValue(&args.key);
    }
    ~SourceCall() {
        key.DestroyValue(&args.key);
        text.DestroyValue(args.text.data());
    }
    SourceCall(const SourceCall&) = delete;
    SourceCall& operator=(const SourceCall&) = delete;
};

constexpr auto native_function = static_cast<EFunctionFlags>(0x400);
constexpr auto static_function = static_cast<EFunctionFlags>(0x2000);
constexpr std::uint64_t parameter = 0x80, output_parameter = 0x100, return_parameter = 0x400;
constexpr int parameter_extent = sizeof(SourceArguments);
constexpr std::size_t max_identity_component = 512;

bool checked_parameter(FProperty* property, int offset, int size, const wchar_t* type) {
    if (!property || !property->HasAnyPropertyFlags(parameter) ||
        property->GetOffset_Internal() != offset || property->GetSize() != size) return false;
    const auto* field_type = property->field_class();
    return field_type && field_type->GetName() == type;
}

std::string identity_component(std::wstring_view value, bool fold_name_case) {
    if (value.empty() || value.size() > max_identity_component) return {};
    std::string result;
    result.reserve(value.size());
    for (auto ch : value) {
        if (!((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'.' || ch == L'/' || ch == L'-'))
            return {};
        if (fold_name_case && ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
        result.push_back(static_cast<char>(ch));
    }
    return result;
}
}

std::string capture_dialogue_source_event(const unreal::FText& caption, std::string* reason) {
    if (reason) reason->clear();
    const auto unavailable = [reason](std::string_view detail) {
        if (reason) reason->assign(detail);
        return std::string{};
    };
    auto* library = UObjectGlobals::StaticFindObject(L"/Script/Engine.Default__KismetTextLibrary");
    if (!UObjectGlobals::IsValidObjectForFindXOf(library)) return unavailable("text library unavailable");
    auto* function = library->GetFunctionByNameInChain(L"StringTableIdAndKeyFromText");
    if (!function || function->GetOuterPrivate() != library->GetClassPrivate() ||
        !function->HasAnyFunctionFlags(native_function) || !function->HasAnyFunctionFlags(static_function))
        return unavailable("unexpected source function");
    // Exact-build reflected allocation is 48 bytes. The native bool at
    // offset 40 may end ParmsSize at byte 41 before alignment padding.
    if (!native_call_extent(function->GetParmsSize(), function->GetPropertiesSize(), parameter_extent,
        function->GetReturnValueOffset(), 40, 41, parameter_extent)) {
        if (reason) *reason = "unexpected source function extent " +
            std::to_string(function->GetParmsSize()) + "/" + std::to_string(function->GetPropertiesSize()) +
            "/" + std::to_string(function->GetReturnValueOffset());
        return {};
    }

    auto* text = function->FindProperty(FName(L"Text", FNAME_Find));
    auto* table = function->FindProperty(FName(L"OutTableId", FNAME_Find));
    auto* key = function->FindProperty(FName(L"OutKey", FNAME_Find));
    auto* result = function->GetReturnProperty();
    if (!checked_parameter(text, 0, 16, L"TextProperty") ||
        !checked_parameter(table, 16, 8, L"NameProperty") ||
        !checked_parameter(key, 24, 16, L"StrProperty") ||
        !checked_parameter(result, 40, 1, L"BoolProperty") ||
        !table->HasAnyPropertyFlags(output_parameter) || !key->HasAnyPropertyFlags(output_parameter) ||
        !result->HasAnyPropertyFlags(return_parameter)) return unavailable("unexpected source fields");

    SourceCall call(*text, *key);
    text->CopyCompleteValue(call.args.text.data(), &caption);
    library->ProcessEvent(function, &call.args);
    const auto& args = call.args;
    if (args.found != 1) return unavailable("caption has no StringTable history");
    if (args.table_id == FName{} || !args.key.data || args.key.count <= 1 ||
        args.key.count > static_cast<std::int32_t>(max_identity_component + 1) ||
        args.key.capacity < args.key.count || args.key.data[args.key.count - 1] != L'\0')
        return unavailable("caption source key unavailable");

    const auto table_id = identity_component(args.table_id.ToString(), true);
    const auto string_key = identity_component(
        std::wstring_view(args.key.data, static_cast<std::size_t>(args.key.count - 1)), false);
    if (!table_id.starts_with('/') || string_key.empty()) return unavailable("unexpected source identity");
    return "table:" + table_id + "|" + string_key;
}
}
