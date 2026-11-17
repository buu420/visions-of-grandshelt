#pragma once
#include <cstdint>
namespace resonance {
// Physical edges only. Focus changes cannot turn a held key into a deliberate
// game command; callers still apply modifier and native binding rules.
class KeyEdges {
    std::uint32_t down_{};
    bool foreground_{};
public:
    std::uint32_t observe(std::uint32_t down,bool foreground) {
        const auto pressed=foreground&&foreground_?(down&~down_):0;
        down_=down;foreground_=foreground;return pressed;
    }
};
}
