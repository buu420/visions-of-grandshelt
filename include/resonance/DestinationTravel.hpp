#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace resonance {
struct TravelPoint {
    int map{-1};
    int entry{-2}; // The game uses -1 for an authored default entrance.
    auto operator<=>(const TravelPoint&) const = default;
};

struct TravelLocation {
    TravelPoint point;
    int previous_map{-1};
    int area{-1};
    bool operator==(const TravelLocation&) const = default;
};

struct TravelPassage {
    int from_map{-1};
    int trigger{-1};
    TravelPoint arrival;
    bool operator==(const TravelPassage&) const = default;
};

struct TravelLink {
    TravelPoint departure;
    TravelPassage passage;
};

struct DestinationRoute {
    int map;
    std::size_t first_leg;
    std::size_t leg_count;
};

// Each first leg is live and reachable. Later links start at a specific
// authored arrival point so opposite shores and separate floors stay apart.
std::vector<DestinationRoute> plan_destinations(std::span<const TravelPassage> first,
    std::span<const TravelLink> onward,std::span<const TravelPassage> completed);

// Call this only once the current player's field and map have settled.
bool travel_arrived(const TravelLocation& departure,const TravelPassage& passage,const TravelLocation& current);

enum class TravelHandoff { Wait, Crossed, BattleReturn, StoryAdvance, DifferentArrival, Stopped };

class TravelContinuation {
public:
    void interrupt() { interrupted_=true; }
    bool pending() const { return interrupted_||battle_seen_; }
    // The caller supplies a field location only when the player can walk.
    TravelHandoff observe(const TravelLocation& departure,const std::optional<TravelPassage>& passage,
        const std::optional<TravelLocation>& field,bool battle,bool story_advanced=false);
private:
    bool interrupted_{},battle_seen_{};
    std::optional<TravelLocation> candidate_;
    TravelHandoff candidate_handoff_{TravelHandoff::Wait};
};
}
