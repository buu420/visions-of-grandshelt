#pragma once
namespace resonance {
inline bool query_collision(int mode) {return mode==1||mode==3||mode==5;}
struct OverlapEvidence {
    bool owned_components{},actors_enabled{},trigger_generates{},player_generates{};
    int trigger_mode{-1},player_mode{-1},trigger_response{-1},player_response{-1};
};
// Both query components generate overlaps. Ignore on either side vetoes it;
// one side may Block when the other requests Overlap.
inline bool enabled_overlap(const OverlapEvidence& e) {
    return e.owned_components&&e.actors_enabled&&e.trigger_generates&&e.player_generates&&
        query_collision(e.trigger_mode)&&query_collision(e.player_mode)&&
        e.trigger_response>=1&&e.trigger_response<=2&&e.player_response>=1&&e.player_response<=2&&
        (e.trigger_response==1||e.player_response==1);
}
}
