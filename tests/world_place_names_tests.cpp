#include "resonance/WorldPlaceNames.hpp"
#include "../src/MapTitles.hpp"

#include <array>
#include <iostream>
#include <string_view>

using namespace std::literals;
using resonance::localized_map_title;

int main() {
    constexpr std::array locales{"en"sv, "fr"sv, "de"sv, "es-ES"sv, "zh-Hans"sv, "zh-Hant"sv, "ja"sv, "ko"sv};
    for (const auto& title : resonance::area_input_detail::map_titles) {
        if (localized_map_title(title.id, "en") != title.name) {
            std::cerr << "English baseline changed for " << title.id << '\n';
            return 1;
        }
        for (auto locale : locales) {
            if (localized_map_title(title.id, locale).empty()) {
                std::cerr << "Missing title for " << title.id << " in " << locale << '\n';
                return 1;
            }
        }
    }
    if (localized_map_title(2000, "fr") != "Ville de Mitra" ||
        localized_map_title(1000, "es-ES") != "Mapamundi" ||
        localized_map_title(2000, "zh-Hans") != "米特拉镇" ||
        localized_map_title(2000, "zh-Hant") != "米特拉鎮" ||
        localized_map_title(2000, "ja") != "ミトラの町" ||
        localized_map_title(2000, "ko") != "미트라 마을" ||
        localized_map_title(3000, "de") != localized_map_title(3001, "de")) {
        std::cerr << "Native place-title or map-alias mismatch\n";
        return 1;
    }
    if (!localized_map_title(2000, "is").empty() || !localized_map_title(999999, "fr").empty()) {
        std::cerr << "Unresolved locale or map returned a title\n";
        return 1;
    }
    std::cout << "Verified names for the supported map IDs and eight locales\n";
}
