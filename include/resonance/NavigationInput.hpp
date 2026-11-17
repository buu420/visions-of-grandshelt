#pragma once
// Read-only native input facts for navigation commands. Call on the game thread
// (engine post-tick). Every capture re-resolves its objects; nothing is cached,
// loaded, written, consumed or synthesised.
#include "resonance/UnrealApi.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace resonance {
enum class InputFact : unsigned char { Unknown, No, Yes };

struct InputContext { std::uint8_t parent{}; std::vector<std::uint8_t> children; };

struct KeyBindingFact {
    std::string key;                    // Engine key name, e.g. "Home".
    InputFact conflict{InputFact::Unknown};
    std::vector<std::string> mappings;  // Player-mappable mapping names bound to the key.
};

struct NativeNavigationInput {
    bool known{};          // Every required native query succeeded.
    bool field_control{};  // Conservative ordinary field-play eligibility.
    std::string reason;    // First reason field_control is false (diagnostic).
    InputFact selecting_key{InputFact::Unknown}; // Options key selector capturing.
    InputFact text_entry{InputFact::Unknown};    // Focused UMG editable text or UI_TextEdit.
    // Individual facts behind field_control.
    InputFact local_player_pawn{}, unit_input_enabled{}, paused{}, move_input_ignored{},
        input_manager_enabled{}, ui_blocked{}, talk_event{}, map_unit_context{}, blocking_context{};
    std::vector<InputContext> contexts; // GetAllIMCStackList order (see report).
    // Candidate keys: Home, End, PageUp, PageDown, Delete, LeftShift, RightShift.
    // PageUp/PageDown ignore only UICommon_ScrollUp/UICommon_ScrollDown.
    std::vector<KeyBindingFact> keys;
    bool page_keys_borrowable{}; // field_control and no other PageUp/PageDown mapping.
    // Physical movement from the native action or its current held digital
    // bindings. AddMovementInput does not write either source.
    InputFact native_move_input{InputFact::Unknown};
};

// controlled_pawn: the local player's pawn. active_widget: nullptr unless root has
// a menu/dialogue widget on top; any non-null value denies field control.
NativeNavigationInput capture_navigation_input(resonance::unreal::UObject* controlled_pawn,
                                               resonance::unreal::UObject* active_widget,
                                               bool controller_gate_only=false);
// Default battle review keys are checked against the live remapping profile.
bool battle_review_binding_available(const wchar_t* key);
}
