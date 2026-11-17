#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resonance/Localization.hpp"
#include "resonance/AccessibilityMenu.hpp"
#include "resonance/Speech.hpp"
#include "AccessibilityMenuModel.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace resonance {
namespace {
using namespace unreal;
using namespace accessibility_menu_detail;

constexpr auto system_layout = L"/Game/UI/System/SystemMenu/Widget/WBP_SystemMenuLayout.WBP_SystemMenuLayout_C";
constexpr auto event_layout = L"/Game/UI/Event/SystemMenu/WBP_TalkEvent_SystemMenuLayout.WBP_TalkEvent_SystemMenuLayout_C";
constexpr auto main_menu_layout = L"/Game/UI/MainMenu/Widget/WBP_MainMenuLayout.WBP_MainMenuLayout_C";
constexpr auto main_menu_system = L"/Game/UI/MainMenu/System/WBP_MainMenuSystem.WBP_MainMenuSystem_C";
constexpr auto main_menu_stack = L"/Script/EnhancedUI.CPP_EU_ActivatableWidgetStack";
constexpr auto system_widget = L"/Game/UI/System/SystemMenu/Widget/WBP_SystemMenu.WBP_SystemMenu_C";
constexpr auto system_item = L"/Game/UI/System/SystemMenu/Widget/WBP_SystemList_Item.WBP_SystemList_Item_C";
constexpr auto title_panel = L"/Game/UI/Common/Layout/Panel/WBP_TitlePanelLayout.WBP_TitlePanelLayout_C";
constexpr auto manager = L"/Script/mucha.CPP_GameWidgetInputManager";
constexpr auto system_manager = L"/Script/mucha.CPP_SystemWidgetManager";
constexpr auto gameplay_tag = L"/Script/GameplayTags.GameplayTag";
constexpr auto menu_title = "Accessibility Options";
constexpr int maximum_entries = 12;

template<class T> struct NativeArray { T* data{}; int num{}, max{}; };
static_assert(sizeof(NativeArray<FName>) == 16);

bool live(UObject* object) {
    return UObjectGlobals::IsValidObjectForFindXOf(object);
}

std::wstring path(const UObject* object) {
    if (!object) return {};
    auto name = object->GetFullName();
    const auto space = name.find(L' ');
    return space == name.npos ? name : name.substr(space + 1);
}

bool is(UObject* object, std::wstring_view expected) {
    if (!live(object)) return false;
    for (auto* type = static_cast<UStruct*>(object->GetClassPrivate()); type; type = type->GetSuperStruct()) {
        if (path(type) == expected) return true;
    }
    return false;
}

bool owned_by(UObject* child, UObject* owner) {
    for (int depth = 0; child && depth < 5; ++depth, child = child->GetOuterPrivate()) {
        if (child == owner) return true;
    }
    return false;
}

FProperty* field(UStruct* type, const wchar_t* name, const wchar_t* kind, int offset, int size) {
    auto* property = type->FindProperty(FName(name, FNAME_Find));
    if (!property || property->GetSize() != size || !property->field_class() ||
        property->field_class()->GetName() != kind) return nullptr;
    const auto found = property->GetOffset_Internal();
    if (found < 0 || found + size > type->GetPropertiesSize() || (offset >= 0 && found != offset)) return nullptr;
    return property;
}

FProperty* member(UObject* object, const wchar_t* name, const wchar_t* type, int offset, int size) {
    if (!live(object)) return nullptr;
    return field(object->GetClassPrivate(), name, type, offset, size);
}

template<class T>
std::optional<T> read(UObject* object, const wchar_t* name, const wchar_t* type, int offset = -1) {
    auto* property = member(object, name, type, offset, sizeof(T));
    if (!property) return {};
    T value{};
    std::memcpy(&value, reinterpret_cast<const std::byte*>(object) + property->GetOffset_Internal(), sizeof(T));
    return value;
}

UObject* child(UObject* owner, const wchar_t* name, const wchar_t* expected) {
    const auto value = read<UObject*>(owner, name, L"ObjectProperty");
    return value && is(*value, expected) && owned_by(*value, owner) ? *value : nullptr;
}

bool ready_to_use(UObject* layout) {
    const auto visibility = read<unsigned char>(layout, L"Visibility", L"EnumProperty", 0xdc);
    const auto closing = read<unsigned char>(layout, L"IsClosingProcess", L"BoolProperty", 0x543);
    const auto opening = read<unsigned char>(layout, L"IsOpeningProcess", L"BoolProperty", 0x542);
    return visibility && (*visibility == 0 || *visibility == 3 || *visibility == 4) &&
        closing && !*closing && opening && !*opening;
}

UFunction* function(UObject* target, const wchar_t* name, int bytes) {
    if (!live(target)) return nullptr;
    auto* fn = target->GetFunctionByNameInChain(name);
    if (!fn || fn->GetParmsSize() != bytes || fn->GetPropertiesSize() < bytes ||
        fn->GetPropertiesSize() > 2048) return nullptr;
    return fn;
}

UObject* system_layout_for(UObject* root) {
    if (is(root, system_layout) || is(root, event_layout)) return ready_to_use(root) ? root : nullptr;
    if (!is(root, main_menu_layout) || !ready_to_use(root)) return nullptr;
    auto* stack = child(root, L"CPP_EU_ActivatableWidgetStack_51", main_menu_stack);
    auto* active = function(stack, L"GetActiveWidget", sizeof(UObject*));
    if (!active || path(active) != L"/Script/CommonUI.CommonActivatableWidgetContainerBase:GetActiveWidget" ||
        !field(active, L"ReturnValue", L"ObjectProperty", 0, sizeof(UObject*))) return nullptr;
    // Scene widgets are pooled. Use the native switcher's active child so a
    // hidden System page cannot take focus in another part of the Main Menu.
    UObject* scene{};
    stack->ProcessEvent(active, &scene);
    return is(scene, main_menu_system) && ready_to_use(scene) ? scene : nullptr;
}

class OwnedParameter {
public:
    OwnedParameter(FProperty* type, void* buffer) : type_(type), buffer_(buffer) { type_->InitializeValue(buffer_); }
    ~OwnedParameter() { type_->DestroyValue(buffer_); }
    OwnedParameter(const OwnedParameter&) = delete;
    OwnedParameter& operator=(const OwnedParameter&) = delete;
    void assign(const void* source) { type_->CopyCompleteValue(buffer_, source); }
private:
    FProperty* type_;
    void* buffer_;
};

bool tag_field(FProperty* property) {
    if (!property || !property->field_class() || property->field_class()->GetName() != L"StructProperty" ||
        property->GetSize() != sizeof(FName)) return false;
    auto* shape = static_cast<FStructProperty*>(property)->GetStruct().pointer;
    return shape && path(shape) == gameplay_tag && shape->GetPropertiesSize() == sizeof(FName);
}

bool tag_array(FProperty* property) {
    return property && tag_field(static_cast<FArrayProperty*>(property)->GetInner());
}

std::vector<FName> game_entries(UObject* widget) {
    const auto model = read<UObject*>(widget, L"VM_SystemMenu", L"ObjectProperty");
    if (!model || !is(*model, L"/Script/mucha.CPP_VM_SystemMenu")) return {};
    auto* property = member(*model, L"EnableEntryList", L"ArrayProperty", 0x70, sizeof(NativeArray<FName>));
    if (!tag_array(property)) return {};
    const auto entries = read<NativeArray<FName>>(*model, L"EnableEntryList", L"ArrayProperty", 0x70);
    if (!entries || entries->num < 1 || entries->num > maximum_entries || entries->max < entries->num ||
        !entries->data) return {};
    return {entries->data, entries->data + entries->num};
}

bool set_entries(UObject* widget, std::span<const FName> tags) {
    auto* fn = function(widget, L"Set Entry List", sizeof(NativeArray<FName>));
    if (!fn) return false;
    auto* in = field(fn, L"InList", L"ArrayProperty", 0, sizeof(NativeArray<FName>));
    if (!tag_array(in) || !in->HasAnyPropertyFlags(0x80) || !in->HasAnyPropertyFlags(0x2) ||
        !in->HasAnyPropertyFlags(0x8000000)) return false;
    // Copy the POD tag list through the engine property so all resulting
    // TArray storage belongs to the game, including any Blueprint copies.
    alignas(16) std::array<std::byte, 2048> storage{};
    OwnedParameter input(in, storage.data());
    const NativeArray<FName> source{const_cast<FName*>(tags.data()), static_cast<int>(tags.size()),
        static_cast<int>(tags.size())};
    input.assign(&source);
    widget->ProcessEvent(fn, storage.data());
    return true;
}

void* interface_slot(UObject* menu, int slot) {
    if (!is(menu, L"/Script/EnhancedUI.CPP_WBP_MenuBase") ||
        !member(menu, L"FirstFocusItemIndex", L"IntProperty", 0x460, 4) ||
        !member(menu, L"SelectedItemList", L"ArrayProperty", 0x4b8, 16)) return nullptr;
    // Same native menu interface and vtable slots used by the selected-row reader.
    auto** table = *reinterpret_cast<void***>(reinterpret_cast<std::byte*>(menu) + 1048);
    MEMORY_BASIC_INFORMATION memory{};
    if (!table || !VirtualQuery(table[slot], &memory, sizeof(memory)) ||
        memory.AllocationBase != GetModuleHandleW(nullptr) ||
        !(memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return nullptr;
    return table[slot];
}

int focus(UObject* menu) {
    using GetFocus = int(*)(void*, unsigned char);
    auto* fn = interface_slot(menu, 6);
    return fn ? reinterpret_cast<GetFocus>(fn)(reinterpret_cast<std::byte*>(menu) + 1048, 0) : -1;
}

UObject* focused_widget(UObject* menu) {
    using GetFocused = UObject*(*)(void*, unsigned char);
    auto* fn = interface_slot(menu, 7);
    const auto result = fn ? reinterpret_cast<GetFocused>(fn)(reinterpret_cast<std::byte*>(menu) + 1048, 0) : nullptr;
    return live(result) ? result : nullptr;
}

void set_focus(UObject* menu, int index, const std::vector<UObject*>& items) {
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    auto* fn = function(menu, L"SetFocusedIndex", 4);
    if (fn && field(fn, L"InIndex", L"IntProperty", 0, 4)) menu->ProcessEvent(fn, &index);
    // SetFocusedIndex updates the stored index. The separately stored focused
    // widget can survive a list rebuild or the native same-index early return.
    // Rebind it to the new native child before presenting the focused entry.
    if (focused_widget(menu) == items[index]) return;
    auto* set_widget = function(menu, L"SetFocuedItemWidget", 16);
    if (!set_widget || !field(set_widget, L"InTimeType", L"EnumProperty", 0, 1) ||
        !field(set_widget, L"InWidget", L"ObjectProperty", 8, 8)) return;
    alignas(8) std::array<std::byte, 16> params{};
    std::memcpy(params.data() + 8, &items[index], 8);
    menu->ProcessEvent(set_widget, params.data());
}

std::vector<UObject*> items(UObject* menu) {
    using GetItems = void*(*)(void*, void*);
    auto* fn = interface_slot(menu, 3);
    if (!fn) return {};
    NativeArray<UObject*> list{};
    reinterpret_cast<GetItems>(fn)(reinterpret_cast<std::byte*>(menu) + 1048, &list);
    std::vector<UObject*> result;
    if (list.data && list.num > 0 && list.num <= maximum_entries && list.max >= list.num)
        result.assign(list.data, list.data + list.num);
    if (list.data) FMemory::Free(list.data);
    if (!std::ranges::all_of(result, [](UObject* row) { return is(row, system_item); })) return {};
    return result;
}

bool matches(const std::vector<UObject*>& list, std::span<const FName> tags) {
    if (list.empty() || list.size() != tags.size()) return false;
    for (std::size_t index = 0; index < list.size(); ++index) {
        auto* property = member(list[index], L"EntryType", L"StructProperty", -1, sizeof(FName));
        if (!tag_field(property) || read<FName>(list[index], L"EntryType", L"StructProperty") != tags[index]) return false;
    }
    return true;
}

UObject* row_label(UObject* row) {
    return child(row, L"CommonTextBlock_49", L"/Script/CommonUI.CommonTextBlock");
}

UObject* title_label(UObject* title) {
    return child(title, L"Text_Title", L"/Script/CommonUI.CommonTextBlock");
}

std::wstring read_text(UObject* label) {
    auto* property = member(label, L"Text", L"TextProperty", -1, 16);
    if (!property) return {};
    return reinterpret_cast<const FText*>(reinterpret_cast<const std::byte*>(label) +
        property->GetOffset_Internal())->ToString();
}

bool set_text(UObject* label, const void* text) {
    auto* fn = function(label, L"SetText", 16);
    if (!fn || !text || path(fn) != L"/Script/UMG.TextBlock:SetText") return false;
    auto* type = field(fn, L"InText", L"TextProperty", 0, 16);
    if (!type) return false;
    alignas(16) std::array<std::byte, 16> params{};
    OwnedParameter input(type, params.data());
    input.assign(text);
    label->ProcessEvent(fn, params.data());
    return true;
}

std::optional<std::wstring> wide_text(std::string_view text) {
    if (text.empty() || text.size() > 4096 || text.find('\0') != text.npos) return {};
    const auto bytes = static_cast<int>(text.size());
    const auto chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, nullptr, 0);
    if (chars <= 0) return {};
    std::wstring wide(chars, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, wide.data(), chars) != chars) return {};
    return wide;
}

bool set_text(UObject* label, std::string_view text) {
    auto wide = wide_text(text);
    if (!wide) return false;
    auto* library = UObjectGlobals::StaticFindObject(L"/Script/Engine.Default__KismetTextLibrary");
    auto* fn = function(library, L"Conv_StringToText", 32);
    if (!fn || path(fn) != L"/Script/Engine.KismetTextLibrary:Conv_StringToText") return false;
    auto* type = field(fn, L"InString", L"StrProperty", 0, 16);
    auto* result = field(fn, L"ReturnValue", L"TextProperty", 16, 16);
    if (!type || !result || fn->GetReturnProperty() != result) return false;
    alignas(16) std::array<std::byte, 32> params{};
    OwnedParameter input(type, params.data());
    OwnedParameter output(result, params.data() + 16);
    const auto count = static_cast<int>(wide->size() + 1);
    const NativeArray<wchar_t> source{wide->data(), count, count};
    input.assign(&source);
    library->ProcessEvent(fn, params.data());
    return set_text(label, params.data() + 16);
}

bool restore_title(UObject* title) {
    auto* property = member(title, L"TitleText", L"TextProperty", -1, 16);
    return property && set_text(title_label(title), reinterpret_cast<std::byte*>(title) + property->GetOffset_Internal());
}

bool show_setting(UObject* item, const AudioSettings& settings, int index) {
    return set_text(row_label(item), tr(rows[index].label) + "   " + value(settings, index, false));
}

bool action_shape(UFunction* fn) {
    if (fn->GetParmsSize() != 32) return false;
    auto* p = field(fn, L"InputActionValue", L"StructProperty", 0, 32);
    if (!p) return false;
    auto* shape = static_cast<FStructProperty*>(p)->GetStruct().pointer;
    return shape && path(shape) == L"/Script/EnhancedInput.InputActionValue" && shape->GetPropertiesSize() == 32;
}

class UpdateGuard {
public:
    explicit UpdateGuard(bool& updating) : updating_(updating) { updating_ = true; }
    ~UpdateGuard() { updating_ = false; }
private:
    bool& updating_;
};
}

bool AccessibilityMenu::before_event(UObject* object, UFunction* fn, void* params) {
    if (updating_ || !widget_ || !fn || !params) return false;
    static const FName set_list(L"Set Entry List", FNAME_Find), clicked(L"BP_OnClicked", FNAME_Find);
    static const FName cancel(L"HandleInputCancel", FNAME_Find), escape(L"HandleInputEscape", FNAME_Find);
    static const FName toggle(L"OnInputOpenSystemMenu", FNAME_Find), direction(L"HandleInputDirection", FNAME_Find);
    const auto name = fn->GetNamePrivate();
    if (object == widget_ && name == set_list) {
        // The MVVM can repopulate the widget. Revalidate the actual rows
        // before accepting another action.
        request_ = Action::None;
        snapshot_.reset();
        return false;
    }
    if (object == widget_ && name == clicked && fn->GetParmsSize() == 24 &&
        (open_ || entry_) && system_layout_for(root_) == layout_) {
        auto* property = field(fn, L"InTriggeredData", L"StructProperty", 0, 24);
        if (!property) return false;
        auto* shape = static_cast<FStructProperty*>(property)->GetStruct().pointer;
        if (!shape || path(shape) != L"/Script/EnhancedUI.ButtonEventTriggeredData" ||
            !field(shape, L"Button", L"ObjectProperty", 8, 8)) return false;
        const auto list = items(menu_);
        const auto index = focus(menu_);
        if (index < 0 || index >= static_cast<int>(list.size())) return open_;
        if (!open_ && list[index] != entry_) return false;
        if (request_ != Action::Back) {
            request_ = open_ ? Action::Confirm : Action::Open;
            request_index_ = index;
        }
        return true;
    }
    if (!open_) return false;
    if (name != cancel && name != escape && name != toggle && name != direction) return false;
    const auto* owner = name == toggle ? system_manager : manager;
    if (!is(object, owner) || !action_shape(fn) || system_layout_for(root_) != layout_) return false;
    if (name != direction) {
        request_ = Action::Back;
        return true;
    }
    std::array<double, 2> axes{};
    std::memcpy(axes.data(), params, sizeof(axes));
    // The shipped input manager chooses the dominant axis. Let Up and Down
    // change native focus while the mod handles the horizontal value changes.
    if (!std::isfinite(axes[0]) || !std::isfinite(axes[1]) || std::abs(axes[0]) <= std::abs(axes[1])) return false;
    if (request_ != Action::Back) {
        request_ = axes[0] > 0 ? Action::Increase : Action::Decrease;
        request_index_ = focus(menu_);
    }
    return true;
}

void AccessibilityMenu::tick(UObject* active, AudioSettings& settings) {
    snapshot_.reset();
    auto* layout = system_layout_for(active);
    if (active != root_ || layout != layout_) {
        root_ = active;
        layout_ = layout;
        widget_ = menu_ = title_ = entry_ = nullptr;
        request_ = Action::None;
        open_ = false;
        title_changed_ = rebuild_failed_ = false;
        unavailable_logged_ = added_logged_ = false;
    }
    if (!layout_ || rebuild_failed_) return;

    UpdateGuard guard(updating_);
    widget_ = child(layout_, L"WBP_SystemMenu", system_widget);
    menu_ = child(widget_, L"PathMenu", L"/Script/EnhancedUI.CPP_WBP_MenuBase");
    title_ = child(widget_, L"WBP_TitlePanelLayout", title_panel);
    if (!widget_ || !menu_ || !title_) {
        if (!unavailable_logged_) log("ACCESS OPTIONS unavailable: native System Menu widgets not found");
        unavailable_logged_ = true;
        return;
    }
    const auto native = game_entries(widget_);
    const auto options_tag = FName(L"UI.System.SystemMenu.Entry.Option", FNAME_Find);
    const auto expanded = insert_menu_entry<FName>(native, options_tag);
    if (!expanded) {
        if ((open_ || entry_) && !native.empty()) set_entries(widget_, native);
        if (title_changed_) restore_title(title_);
        open_ = title_changed_ = false;
        entry_ = nullptr;
        request_ = Action::None;
        return;
    }
    const auto entry_index = static_cast<int>(std::find(expanded->begin(), expanded->end(), FName{}) - expanded->begin());
    const auto title = tr(menu_title);
    std::array<FName, rows.size()> page{};
    auto list = items(menu_);

    const auto present_system_entry = [&] {
        if (!entry_ || focus(menu_) != entry_index || focused_widget(menu_) != entry_) return;
        auto* label = row_label(entry_);
        const auto expected = wide_text(title);
        if (!expected) return;
        if (read_text(label) != *expected && !set_text(label, std::string_view(title))) return;
        if (read_text(label) != *expected) return;
        snapshot_ = MenuSnapshot{"accessibility-system-entry", title, "",
            entry_index, static_cast<int>(list.size()), true, true, true};
    };
    const auto rebuild_system = [&] {
        open_ = false;
        entry_ = nullptr;
        if (title_changed_ || matches(list, page)) {
            restore_title(title_);
            title_changed_ = false;
        }
        if (!set_entries(widget_, *expanded)) return false;
        list = items(menu_);
        if (!matches(list, *expanded) || !set_text(row_label(list[entry_index]), std::string_view(title))) {
            set_entries(widget_, native);
            rebuild_failed_ = true;
            return false;
        }
        entry_ = list[entry_index];
        return true;
    };
    const auto request = std::exchange(request_, Action::None);
    if (open_ && !matches(list, page)) open_ = false;
    if (open_ && request == Action::Back) {
        if (rebuild_system()) {
            set_focus(menu_, entry_index, list);
            present_system_entry();
            log(std::string("ACCESS OPTIONS restored entry focus=") + (snapshot_ ? "yes" : "no"));
        }
        log("ACCESS OPTIONS closed");
        return;
    }

    if (!open_ && request == Action::Open && matches(list, *expanded) && list[entry_index] == entry_) {
        if (set_entries(widget_, page)) {
            list = items(menu_);
            if (matches(list, page) && set_text(title_label(title_), std::string_view(title))) {
                open_ = true;
                title_changed_ = true;
                for (int index = 0; index < static_cast<int>(list.size()); ++index) {
                    if (!show_setting(list[index], settings, index)) {
                        open_ = false;
                        break;
                    }
                }
            }
        }
        if (open_) {
            set_focus(menu_, 0, list);
            entry_ = nullptr;
            log("ACCESS OPTIONS opened");
        } else {
            if (rebuild_system()) set_focus(menu_, entry_index, list);
            log("ACCESS OPTIONS unavailable: could not build the settings page");
        }
    }
    if (!open_) {
        if (entry_ && matches(list, *expanded) && list[entry_index] == entry_) {
            present_system_entry();
            return;
        }
        if (!matches(list, native) && !matches(list, *expanded) && !matches(list, page)) return;
        auto index = focus(menu_);
        if (matches(list, native) && index >= entry_index) ++index;
        if (rebuild_system()) {
            set_focus(menu_, index, list);
            present_system_entry();
            if (!added_logged_)
                log(layout_ == root_ ? "ACCESS OPTIONS inserted below the native Options row" :
                    "ACCESS OPTIONS inserted below the native Options row in Main Menu System");
            added_logged_ = true;
        } else if (!unavailable_logged_) {
            log("ACCESS OPTIONS unavailable: native System Menu rows could not be extended");
            unavailable_logged_ = true;
        }
        return;
    }
    if (request == Action::Increase || request == Action::Decrease || request == Action::Confirm) {
        auto changed = settings;
        if (adjust(changed, request_index_, request) &&
            show_setting(list[request_index_], changed, request_index_)) settings = changed;
    }
    const auto index = focus(menu_);
    if (index < 0 || index >= static_cast<int>(rows.size())) return;
    snapshot_ = MenuSnapshot{"accessibility-options", tr(rows[index].label), value(settings, index),
        index, static_cast<int>(rows.size()), true, true, true};
}
}
