#pragma once
#include <cstdint>

namespace resonance {
enum class ControllerAction {
    None, Opened, PreviousCategory, NextCategory, PreviousTarget, NextTarget,
    Guidance, Automatic, Stop, Help
};

struct ControllerPad {
    bool connected{};
    std::uint16_t buttons{};
    std::uint8_t left_trigger{}, right_trigger{};
    std::int16_t left_x{}, left_y{}, right_x{}, right_y{};
};
struct ControllerPolicy { bool permitted{}; std::uint64_t epoch{}; };
// Reported once per D-pad press, on the sample that resolved or abandoned it.
// RolledPress: the press began as another direction or an adjacent diagonal and
// settled into the command that was issued; dpad_chord is the first chord seen.
// AmbiguousAbandoned: the press ended without a command; dpad_chord is the chord
// (opposing or diagonal) that made it ambiguous.
enum class ControllerDpadDiagnostic { None, RolledPress, AmbiguousAbandoned };
struct ControllerDecision {
    ControllerAction action{};
    ControllerPad game; // Original controls, each either preserved or zeroed.
    bool capturing{}; // Entire sample consumed, including a closing command's sample.
    ControllerDpadDiagnostic dpad_diagnostic{};
    std::uint16_t dpad_chord{};
};
// A D-pad command is issued once its chord has been a single direction for this
// long (and at least one confirming poll). A rocker D-pad pressed off-centre can
// report the perpendicular direction or an adjacent diagonal for a poll or two.
inline constexpr std::uint64_t controller_dpad_settle_ms=45;

inline constexpr std::uint16_t ControllerDpadUp=0x0001, ControllerDpadDown=0x0002,
    ControllerDpadLeft=0x0004, ControllerDpadRight=0x0008,
    ControllerL3=0x0040, ControllerR3=0x0080, ControllerB=0x2000, ControllerY=0x8000;

// One instance per physical controller, sampled before filtering native input.
// Permission includes field eligibility and foreground/focus; epoch changes
// invalidate the current transaction. Keep observing denied contexts to release
// captured tails; reset abandons all history and requires a fresh LT release.
// No method generates controller input.
class ControllerNavigation {
public:
    // now_ms is a monotonic millisecond clock (GetTickCount64). D-pad presses
    // settle for controller_dpad_settle_ms before commanding.
    ControllerDecision observe(const ControllerPad&, ControllerPolicy, std::uint64_t now_ms);
    // Without a clock a D-pad press settles after one confirming poll.
    ControllerDecision observe(const ControllerPad&, ControllerPolicy);
    void reset();
private:
    ControllerDecision observe_sample(const ControllerPad&, ControllerPolicy, bool has_clock, std::uint64_t now_ms);
    ControllerPad previous_{};
    ControllerPolicy policy_{};
    std::uint16_t tail_buttons_{};
    std::uint8_t tail_axes_{};
    bool observed_{}, modifier_{}, waiting_for_release_{true}, capturing_{};
    // Current D-pad press while capturing: chord as last seen, the first chord of
    // the press, when the current chord became stable, and whether it is spent.
    std::uint16_t dpad_chord_{}, dpad_first_chord_{}, dpad_ambiguous_chord_{};
    std::uint64_t dpad_stable_since_{};
    unsigned dpad_stable_polls_{};
    bool dpad_pressed_{}, dpad_spent_{}, dpad_ambiguous_{};
};

// Connected raw buttons, triggers above .25, or either stick's radial magnitude
// above .25. Call before filtering when deciding whether physical input overrides.
bool controller_physical_input(const ControllerPad& raw);
}
