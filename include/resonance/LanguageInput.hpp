#pragma once

#include <optional>
#include <string_view>

namespace resonance
{
    // Call on the game thread inside an Unreal object frame. An unavailable or
    // unsupported native language has no tag until the game applies a supported one.
    std::optional<std::string_view> capture_game_language();
}
