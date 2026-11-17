#pragma once
#include "resonance/ControllerNavigation.hpp"
#include <cstdint>
#include <vector>
namespace resonance {
enum class ControllerModifier {LeftTrigger,RightTrigger,RightShoulder};
struct ControllerBackendStatus {
    bool ready{};
    unsigned connected{};
    bool modifier_down{};
    std::uint16_t buttons{};
};
struct ControllerWalkingState {
    bool ready{};
    unsigned connected{};
    bool physical_input{};
    bool read_failed{};
    bool starter_held{};
};
// The focus callback is a platform observation, never an Unreal call. The
// native permission/epoch is published separately from the game thread.
bool initialize_controller_input(bool (*foreground)(),ControllerModifier modifier=ControllerModifier::LeftTrigger);
bool retry_controller_input(); // Game thread; bounded retry only while the provider is not yet loaded.
ControllerModifier controller_modifier();
void disable_controller_input();
ControllerBackendStatus controller_backend_status();
// A verified field surface can still reserve the replaced Journal shortcut
// while movement or gesture capture is temporarily unavailable.
void publish_controller_permission(bool permitted,std::uint64_t epoch,bool reserve_field_rb=false);
// Bracket reader work within one post-game-tick callback. Input providers can
// poll concurrently while the verified field snapshot is still current.
void begin_controller_field_read(std::uint64_t read_started);
void renew_controller_after_field_read(std::uint64_t read_started);
std::vector<ControllerAction> take_controller_actions(std::uint64_t epoch);
enum class BattleControllerAction { None, Opened, Closed, Previous, Next, Vitals, Effects, Gauges };
void publish_battle_controller_permission(bool permitted,std::uint64_t epoch);
std::vector<BattleControllerAction> take_battle_controller_actions(std::uint64_t epoch);
// Read the provider directly before applying movement, without consuming or
// generating navigation actions. The game's own poll may have aged during a query.
ControllerWalkingState controller_walking_state(bool refresh_connections=false);
void controller_automatic_starter_grace();
void clear_controller_starter_grace();
}
