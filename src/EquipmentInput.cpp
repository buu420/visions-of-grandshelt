#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "resonance/EquipmentInput.hpp"
#include "resonance/Localization.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace resonance {
namespace {
using namespace unreal;

constexpr auto equipment_scene = L"/Game/UI/MainMenu/ActionEquip/WBP_ActionEquipmentLayout.WBP_ActionEquipmentLayout_C";
constexpr auto equipped_list = L"/Game/UI/MainMenu/ActionEquip/Widget/Equipped/WBP_UnitEquippedList.WBP_UnitEquippedList_C";
constexpr auto slot_root = L"/Game/UI/MainMenu/ActionEquip/Widget/Equipped/WBP_UnitEquippedActionEntryRoot.WBP_UnitEquippedActionEntryRoot_C";
constexpr auto item_entry = L"/Game/UI/MainMenu/ActionEquip/Widget/Equipped/WBP_UnitEquippedItemEntry.WBP_UnitEquippedItemEntry_C";
constexpr auto item_row = L"/Game/UI/MainMenu/ActionEquip/Widget/Equipped/WBP_UnitEquippedItem.WBP_UnitEquippedItem_C";
constexpr auto vision_row = L"/Game/UI/MainMenu/ActionEquip/Widget/Equipped/WBP_UnitEquippedVision.WBP_UnitEquippedVision_C";
constexpr auto skill_row = L"/Game/UI/MainMenu/ActionEquip/Widget/Equipped/WBP_UnitEquippedSkillEntry.WBP_UnitEquippedSkillEntry_C";
constexpr auto item_candidate = L"/Game/UI/MainMenu/ActionEquip/Widget/Equippable/WBP_UnitEquippableItemEntry.WBP_UnitEquippableItemEntry_C";
constexpr auto skill_candidate = L"/Game/UI/MainMenu/ActionEquip/Widget/Equippable/WBP_UnitEquippableSkillEntry.WBP_UnitEquippableSkillEntry_C";
constexpr auto vision_candidate = L"/Game/UI/MainMenu/ActionEquip/Widget/Equippable/WBP_UnitEquippableVisionEntry.WBP_UnitEquippableVisionEntry_C";
constexpr auto item_slot = L"/Script/mucha.CPP_VM_UnitEquippedItemSlot";
constexpr auto skill_slot = L"/Script/mucha.CPP_VM_UnitEquippedSkillSlot";
constexpr auto button_path = L"/Script/EnhancedUI.CPP_WBP_ButtonPath";
constexpr auto text_block = L"/Script/UMG.TextBlock";
constexpr int max_slots = 64;

enum class EquipmentType : unsigned char { None, Weapon, Armor, Accessory, Vision };
template<class T> struct NativeArray { T* data; int count, capacity; };
static_assert(sizeof(NativeArray<UObject*>) == 16);

std::wstring path(const UObject* object) {
    if (!object) return {};
    auto name = object->GetFullName();
    const auto space = name.find(L' ');
    return space == name.npos ? name : name.substr(space + 1);
}

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), bytes, nullptr, nullptr)) return {};
    return result;
}

bool live(UObject* object) { return UObjectGlobals::IsValidObjectForFindXOf(object); }

bool is(UObject* object, std::wstring_view expected) {
    if (!live(object)) return false;
    for (auto* type = static_cast<UStruct*>(object->GetClassPrivate()); type; type = type->GetSuperStruct())
        if (path(type) == expected) return true;
    return false;
}

FProperty* field(UStruct* type, const wchar_t* name, const wchar_t* kind, int size, int offset = -1) {
    auto* property = type->FindProperty(FName(name, FNAME_Find));
    if (!property || property->GetSize() != size || !property->field_class() ||
        property->field_class()->GetName() != kind) return nullptr;
    const auto found = property->GetOffset_Internal();
    if (found < 0 || found + size > type->GetPropertiesSize() || (offset >= 0 && found != offset)) return nullptr;
    return property;
}

template<class T>
std::optional<T> read(UObject* object, const wchar_t* name, const wchar_t* kind, int offset = -1) {
    if (!live(object)) return {};
    auto* p = field(object->GetClassPrivate(), name, kind, sizeof(T), offset);
    if (!p) return {};
    T out{};
    std::memcpy(&out, reinterpret_cast<const std::byte*>(object) + p->GetOffset_Internal(), sizeof(T));
    return out;
}

UObject* object(UObject* owner, const wchar_t* name, int offset = -1) {
    auto result = read<UObject*>(owner, name, L"ObjectProperty", offset);
    return result && live(*result) ? *result : nullptr;
}

bool visible(UObject* widget) {
    const auto visibility = read<unsigned char>(widget, L"Visibility", L"EnumProperty", 0xdc);
    const auto opacity = read<float>(widget, L"RenderOpacity", L"FloatProperty", 0xe0);
    return visibility && (*visibility == 0 || *visibility == 3 || *visibility == 4) &&
        opacity && *opacity > .01f;
}

std::optional<bool> get_bool(UObject* target, const wchar_t* function) {
    if (!live(target)) return {};
    auto* fn = target->GetFunctionByNameInChain(function);
    if (!fn || fn->GetParmsSize() != 1 || fn->GetPropertiesSize() < 1 || fn->GetPropertiesSize() > 512) return {};
    auto* result = field(fn, L"ReturnValue", L"BoolProperty", 1, 0);
    if (!result || result != fn->GetReturnProperty()) return {};
    alignas(16) std::array<std::byte, 512> storage{};
    target->ProcessEvent(fn, storage.data());
    return storage.front() != std::byte{};
}

bool usable_entry(UObject* row) {
    return visible(row) && get_bool(row, L"GetIsEnabled").value_or(false) &&
        !get_bool(row, L"GetIsInvalid").value_or(false);
}

std::span<UObject* const> equipped_entries(UObject* menu, UObject* row, int index, int count) {
    auto* prop = field(menu->GetClassPrivate(), L"EntryList", L"ArrayProperty", 16);
    if (!prop) return {};
    auto* inner = static_cast<FArrayProperty*>(prop)->GetInner();
    if (!inner || !inner->field_class() || inner->field_class()->GetName() != L"ObjectProperty" ||
        inner->GetSize() != sizeof(UObject*)) return {};
    const auto list = read<NativeArray<UObject*>>(menu, L"EntryList", L"ArrayProperty");
    if (!list || !list->data || list->count != count || list->capacity < count || list->data[index] != row)
        return {};
    return {list->data, static_cast<std::size_t>(list->count)};
}

UObject* path_button(UObject* row) {
    return is(row, button_path) ? object(row, L"PathButton", 0x4a0) : nullptr;
}

UObject* slot_content(UObject* row) {
    return is(row, slot_root) ? path_button(row) : row;
}

std::optional<EquipmentType> item_slot_type(UObject* row) {
    if (!is(row, item_entry) && !is(row, item_row) && !is(row, vision_row)) return {};
    auto* vm = object(row, L"VM_UnitEquippedItemSlot");
    if (!is(vm, item_slot)) return {};
    return read<EquipmentType>(vm, L"EquipmentType", L"EnumProperty", 0x100);
}

std::optional<int> displayed_subslot(std::span<UObject* const> entries, EquipmentType kind, int focused) {
    // Number the matching slots in the live presentation list. Each slot
    // contributes its visible row type, independent of what it contains.
    int total = 0;
    int number = 0;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!visible(entries[index])) continue;
        auto* content = slot_content(entries[index]);
        if (!visible(content) || item_slot_type(content) != kind) continue;
        ++total;
        if (index == static_cast<std::size_t>(focused)) number = total;
    }
    return number > 0 && total > 1 ? std::optional{number} : std::nullopt;
}

std::string numbered(std::string_view key, std::optional<int> number) {
    auto label = tr(key);
    if (number && *number > 0) return trf("{0} {1}", {label, std::to_string(*number)});
    return label;
}

std::string slot_label(UObject* row, std::span<UObject* const> entries, int index) {
    row = slot_content(row);
    if (!usable_entry(row)) return {};
    if (is(row, skill_row)) {
        auto* vm = object(row, L"VM_UnitEquippedSkillSlot");
        if (!is(vm, skill_slot)) return {};
        const auto slot = read<int>(vm, L"SlotIndex", L"IntProperty", 0xc0);
        return slot && *slot >= 0 && *slot < max_slots ? numbered("Ability", *slot + 1) : tr("Ability");
    }
    const auto kind = item_slot_type(row);
    if (!kind) return {};
    if (*kind == EquipmentType::Weapon) return tr("Weapon");
    if (*kind == EquipmentType::Armor) return tr("Armor");
    if (*kind != EquipmentType::Accessory && *kind != EquipmentType::Vision) return {};
    return numbered(*kind == EquipmentType::Accessory ? "Accessory" : "Vision",
        displayed_subslot(entries, *kind, index));
}

std::string direct_row_text(UObject* row) {
    // Prefer the resolved visible Text_Name field for selected controls whose
    // names are rendered inside native button paths or named slots.
    for (int depth = 0; depth < 4 && visible(row); ++depth) {
        auto* name = object(row, L"Text_Name");
        if (visible(name) && is(name, text_block)) {
            auto* text = field(name->GetClassPrivate(), L"Text", L"TextProperty", 16);
            if (!text) return {};
            const auto result = reinterpret_cast<const FText*>(reinterpret_cast<const std::byte*>(name) +
                text->GetOffset_Internal())->ToString();
            return result == L"Name" ? std::string{} : utf8(result);
        }
        row = path_button(row);
    }
    return {};
}
}

std::optional<MenuSnapshot> equipment_row_snapshot(UObject* scene, UObject* menu, UObject* focused,
    int index, int count, std::string_view displayed) {
    if (index < 0 || index >= count || count > max_slots || !is(scene, equipment_scene) ||
        !visible(scene) || !visible(menu) || !usable_entry(focused)) return {};

    if (menu == object(scene, L"WBP_UnitEquippedList") && is(menu, equipped_list)) {
        const auto entries = equipped_entries(menu, focused, index, count);
        if (entries.empty()) return {};
        auto label = slot_label(focused, entries, index);
        if (label.empty()) return {};
        auto value = displayed.empty() ? direct_row_text(focused) : std::string(displayed);
        return MenuSnapshot{utf8(path(menu)), std::move(label), std::move(value), index, count, true, true, true};
    }

    // Stock candidate names, costs and quantities already read through the
    // ordinary menu reader. Recover only an actual blank focused candidate.
    if (!displayed.empty() ||
        (!is(focused, item_candidate) && !is(focused, skill_candidate) && !is(focused, vision_candidate))) return {};
    auto name = direct_row_text(focused);
    if (name.empty()) return {};
    return MenuSnapshot{utf8(path(menu)), std::move(name), "", index, count, true, true, true};
}
}
