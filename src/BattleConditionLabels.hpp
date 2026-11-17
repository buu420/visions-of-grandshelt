#pragma once
#include <array>

namespace resonance::battle_condition_detail {
struct Label {
    const char* label{};
    bool harmful{};
};
// Index is eUnitStatusCondition in the supported shipping build. DA_UI_StatusCondition
// decides which icons the current HUD can show before a label is used. Polarity follows
// DT_StatusConditionDetailData, with the detrimental Doom and Overheat effects called out
// by their skill data. English names come from the status tutorials and skill captions,
// or describe the condition when the HUD only has an icon.
constexpr std::array<Label, 87> labels{{
    {}, // 0 None
    {"Poison", true},
    {"Blind", true},
    {"Sleep", true},
    {"Silence", true},
    {"Paralysis", true},
    {"Confuse", true},
    {"Petrify", true},
    {"Charm", true},
    {"Stop", true},
    {"Berserk", true}, // 10
    {"Doom", true},
    {"Slow", true},
    {}, // Instant Death
    {}, // Gravity
    {"Debrave", true},
    {"Deprotect", true},
    {"Defaith", true},
    {"Deshell", true},
    {"Stagger power down", true},
    {"Stagger defense down", true}, // 20
    {"Overheat", true},
    {"Moon Slayer"},
    {"Crash Blade"},
    {"Brave"},
    {"Protect"},
    {"Faith"},
    {"Shell"},
    {"Stagger power up"},
    {"Stagger defense up"},
    {"Regen"}, // 30
    {"Reraise"},
    {"Reflect"},
    {"Haste"},
    {"Cover"},
    {"Cover all"},
    {"Guard"},
    {"Boost"},
    {"Good Luck"},
    {"Veil defense up"},
    {}, // 40 Divine Radiance
    {"Honing Sense"},
    {"Light Veil"},
    {"Superior Guard"},
    {"Decoy"},
    {"Status ailment guard"},
    {"Overclock"},
    {"Overclock Zero"},
    {"Camouflage"},
    {"Taunt"},
    {"Demon Form"}, // 50
    {"Awakening Power"},
    {"Grand Magic"},
    {"Mind Focus"},
    {"Paradise Lost"},
    {"Duality Knight"},
    {"Vital point attack"},
    {"Royal Guard"},
    {"Great Wall"},
    {"Magic Barrier"},
    {"Enemy attention up"}, // 60
    {"Fire immunity"},
    {"Ice immunity"},
    {"Wind immunity"},
    {"Earth immunity"},
    {"Lightning immunity"},
    {"Water immunity"},
    {"Light immunity"},
    {"Dark immunity"},
    {"Heat"},
    {"Meteoroid"}, // 70
    {"Fire damage up"},
    {"Ice damage up"},
    {"Wind damage up"},
    {"Lightning damage up"},
    {"Masamune"},
    {"Swift"},
    {"Sage's Eye"},
    {"Fury"},
    {"Valiant Advance formation"},
    {"Offensive formation"}, // 80
    {"Charge formation"},
    {"Iron Wall formation"},
    {"Breath Recovery formation"},
    {"Fluid Tactics"},
    {"Refraction Arts"},
    {"Tactical Assault"}
}};
constexpr const Label* lookup(unsigned char condition) {
    return condition < labels.size() && labels[condition].label ? &labels[condition] : nullptr;
}
}
