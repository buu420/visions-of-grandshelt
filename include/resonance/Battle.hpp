#pragma once
#include "resonance/Core.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace resonance {
enum class BattleSide : unsigned char { Unknown, Ally, Enemy };
enum class BattleStatusAction : unsigned char { Previous, Next, Hp, Mp, Gauges, Buffs, Debuffs, Vitals, Effects };
struct BattleConditions {
    std::vector<std::string> buffs, debuffs;
};
struct BattlePartyStatus {
    std::optional<int> limit_percent, limit_level;
    std::optional<BattleConditions> effects;
};
// These are already presentation-filtered values, not raw unit parameters.
struct BattleUnit {
    int id{-1};
    BattleSide side{BattleSide::Unknown};
    std::string name,letter;
    int level{-1};
    std::optional<int> hp,mp;
    // Attached to rows of the presented party HUD. Missing inner values mean unavailable.
    std::optional<BattlePartyStatus> party_status;
    std::string target_prediction, stagger, health_indicator;
    std::optional<int> stagger_percent;
};
struct BattleLibra {
    int unit{-1};
    std::optional<int> hp,hp_max;
    std::vector<std::string> details;
};
struct BattlePrompt {
    std::string id;
    std::string text;
    std::string selection;
};
struct BattleFrame {
    std::string context;
    bool active{};
    std::vector<BattleUnit> units;
    int actor{-1};
    bool choosing_target{};
    std::vector<int> targets;
    std::optional<BattleLibra> libra;
    std::string banner,unavailable_reason;
    int focused_target{-1};
    std::optional<BattlePrompt> prompt;
    bool command_visible{};
};
struct BattleEvent {
    std::string context;
    // One increasing sequence across all recipients in this battle session,
    // assigned when ready for delivery. Never a per-widget counter.
    std::uint64_t sequence{};
    int unit{-1};
    BattleSide side{BattleSide::Unknown};
    std::string name,letter;
    // Exact rendered number/title text, copied after the popup is presented.
    // Never an HP difference or an unfiltered calculation event.
    std::string text;
    bool presented{};
};
class BattleCore {
public:
    std::vector<SpeechEvent> observe(const BattleFrame&);
    std::vector<SpeechEvent> events(const BattleFrame&,const std::vector<BattleEvent>&);
    void interrupt_action(const BattleFrame&);
    std::string review(const BattleFrame&) const;
    std::string status(const BattleFrame&, BattleStatusAction);
    void reset();
private:
    std::string context_,actor_,targets_,banner_,unavailable_,prompt_,prompt_selection_;
    std::vector<BattleUnit> known_units_;
    std::uint64_t last_event_{};
    int reviewed_ally_{-1};
    struct MutedAction {
        int actor;
        std::string banner;
        bool command_seen;
    };
    std::optional<MutedAction> muted_action_;
};
// The presented page of the battle result screen (WBP_Btl_ResultLayout).
struct BattleResultPage {
    std::string page;              // native page identity; empty when unreadable
    std::optional<bool> staging;   // the page still has presentation steps to finish
    std::string diagnostic;
    bool counting_rewards{};
};
struct BattleResultFrame {
    std::string context;           // the shown result screen; empty when none is shown
    std::string page;
    std::optional<bool> staging;
    std::vector<std::string> lines; // presented rows; menu controls use focus below
    std::string focus;             // the focused control's speech; empty when nothing has focus
    std::uint64_t now_ms{};
    bool counting_rewards{};
};
// Speaks each result page once, after its count-up and slide-in presentation
// has settled, with the focused control; focus moves on a spoken page follow.
class BattleResultCore {
public:
    std::vector<SpeechEvent> observe(const BattleResultFrame&);
    std::string browse(int direction);
    std::string review() const;
    void reset();
private:
    std::string context_,key_,focus_;
    std::vector<std::string> lines_;
    std::optional<std::size_t> position_;
    bool readable_{};
    std::uint64_t since_{};
    std::vector<std::string> spoken_;
};
}
