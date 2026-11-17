#include "../src/ChapterSixShrines.hpp"

#include <iostream>
#include <stdexcept>

using namespace chapter_six_shrines;

namespace {
void require(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}

void expect_leg(Location location, TrialState state, Kind kind, int id) {
    const auto leg = next_leg(location, state);
    require(leg && leg->kind == kind && leg->id == id, "unexpected shrine continuation");
}

void gronoa_routes_each_floor_and_reopens_the_return() {
    expect_leg({3240, 6110, 0, 180}, {}, Kind::Passage, 3);
    const auto down = next_leg({3240, 6110, 1, 40});
    require(down && down->kind == Kind::Ladder && down->id == 0 && down->from_top,
        "the upper stair section leads to the down ladder on its connected side");
    expect_leg({3240, 6110, 3, -3190}, {}, Kind::Lift, 14);
    expect_leg({3240, 6110, 5, -5480}, {}, Kind::Lift, 14);
    require(!next_leg({3240, 6110, 6, -6400}), "the bottom story trigger waits for the bottom floor");
    expect_leg({3240, 6110, 6, -7970}, {}, Kind::WalkEvent, 25);
    expect_leg({3240, 6120, 6, -7980}, {}, Kind::ReturnSave, 15);
    expect_leg({3240, 6120, 1, -390}, {}, Kind::Passage, 4);
    const auto world = next_leg({3240, 6120, 0, 100});
    require(world && world->id == 0 && world->destination_map == 1000 && world->point == 3241,
        "after Elfdrit the entrance returns to the appropriate World point");
    require(!next_leg({3240, 6100, 0, 180}), "entrance sequence must settle before shrine guidance");
}

void shrine_trials_require_the_current_check() {
    const auto opening = next_leg({3250, 6121, 0, 80});
    require(opening && opening->id == 11 && opening->door == 87 && opening->point == 1,
        "the first room uses the authored entrance door and connector");
    expect_leg({3250, 6122, 1, 120}, {false, {}}, Kind::WalkEvent, 72);
    require(!next_leg({3250, 6123, 2, 120}, {{}, false}), "unknown completion does not route to a trial");
    require(!next_leg({3250, 6123, 2, 120}, {false, {}}), "unknown stele state withholds the next trial leg");
    expect_leg({3250, 6123, 2, 120}, {false, false}, Kind::Stele, 101);
    expect_leg({3250, 6123, 2, 120}, {false, true}, Kind::WalkEvent, 73);
    expect_leg({3250, 6123, 2, 120}, {true, {}}, Kind::Passage, 19);
    expect_leg({3250, 6123, 3, 120}, {false, false}, Kind::Stele, 102);
    expect_leg({3250, 6123, 10, 400}, {false, true}, Kind::WalkEvent, 75);
    expect_leg({3250, 6123, 11, 400}, {true, {}}, Kind::Passage, 13);
    require(!next_leg({3250, 6123, 12, 190}, {false, {}}), "the Dragon room needs the second trial segment");
    expect_leg({3250, 6130, 12, 190}, {false, {}}, Kind::WalkEvent, 77);
    expect_leg({3250, 6130, 12, 450}, {true, {}}, Kind::Ladder, 80);
    expect_leg({3250, 6130, 12, 1350}, {true, {}}, Kind::Passage, 2);
}

void lifts_and_courtyard_follow_the_authored_sequence() {
    require(!next_leg({3250, 6123, 13, 220}), "lift route is unavailable before entering the last trial segment");
    expect_leg({3250, 6130, 13, 220}, {}, Kind::Lift, 116);
    expect_leg({3250, 6130, 14, 760}, {}, Kind::Lift, 117);
    expect_leg({3250, 6130, 18, 2760}, {}, Kind::Lift, 119);
    expect_leg({3250, 6130, 16, 1760}, {}, Kind::Lift, 118);
    expect_leg({3250, 6130, 15, 1250}, {}, Kind::Lift, 96);
    expect_leg({3250, 6130, 25, 1250}, {}, Kind::Lift, 96);
    expect_leg({3250, 6130, 26, 2260}, {}, Kind::Lift, 120);
    expect_leg({3250, 6130, 19, 3260}, {}, Kind::Passage, 7);
    require(!next_leg({3250, 6130, 18, 1750}), "area/height mismatch cannot route to a lift on another floor");
    expect_leg({3250, 6140, 21, 100}, {}, Kind::Passage, 8);
    require(!next_leg({3250, 6150, 21, 100}), "the game's native duel NPC takes priority at 6150");
    expect_leg({3250, 6160, 21, 100}, {}, Kind::Passage, 23);
    const auto final = next_leg({3250, 6160, 22, 100});
    require(final && final->id == 3 && final->door == 88 && final->point == 23,
        "the final route is the native chamber passage");
    require(!next_leg({3250, 6170, 22, 100}), "the old chamber route expires on the chapter handoff");
}
}

int main() {
    try {
        gronoa_routes_each_floor_and_reopens_the_return();
        shrine_trials_require_the_current_check();
        lifts_and_courtyard_follow_the_authored_sequence();
        std::cout << "Chapter 6 shrine route tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
