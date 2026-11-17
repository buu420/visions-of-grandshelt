#pragma once

#include <string_view>

namespace resonance {
// Display name for a map already accepted by the route reader.
std::string_view localized_map_title(int map, std::string_view locale);
}
