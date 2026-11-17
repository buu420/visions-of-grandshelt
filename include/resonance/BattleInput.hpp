#pragma once
// Read-only, game-thread capture of the presented battle UI for BattleCore.
// Every call re-resolves the current world's CPP_UI_BattleSubsystem; native
// pointers are never returned, queued or kept between calls. Events carry
// copied display text only. Nothing is written, invoked as an action, or loaded.
#include "resonance/Battle.hpp"
#include "resonance/UnrealApi.hpp"
#include <memory>
#include <string>
#include <vector>

namespace resonance {
class BattleInput {
public:
    BattleInput();
    ~BattleInput();
    BattleInput(const BattleInput&)=delete;
    BattleInput& operator=(const BattleInput&)=delete;
    // top: the reader's current top widget. Active only while the local
    // player's current-world battle (bDuringBattle) shows its battle layout or
    // battle sequencer as that top. HUD roles are read only from the layout.
    // Optional party detail is read only when a status shortcut requests it.
    BattleFrame capture(resonance::unreal::UObject* top, bool include_party_status = false);
    // ProcessEvent post hook. Rejects by function name first and ignores the
    // nested ProcessEvent calls this class makes for its own getters.
    void on_event(resonance::unreal::UObject* object,resonance::unreal::UFunction* function,void* params);
    // Presented events of the latest active battle context, in sequence order.
    // Kept while the same battle is shortly inactive; dropped when it ends.
    std::vector<BattleEvent> drain_events();
    // Forgets the session, queued and pending events; sequences never restart.
    void reset();
    // top: the reader's current top widget. The presented page of a battle
    // result screen (the innermost result page shown through its switchers)
    // and whether that page's current staging step is still playing.
    BattleResultPage result_page(resonance::unreal::UObject* top);
    // Latest capture summary for the reader log; never contains pointers.
    std::string diagnostic() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
