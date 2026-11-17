#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace resonance {

bool supported_language(std::string_view tag);
std::string_view current_language();

// English uses the source strings. Other game languages load their matching TSV.
// An invalid or incomplete load leaves the previous catalog active.
void use_language(std::string_view tag, const std::filesystem::path& catalog_directory);

std::string tr(std::string_view english);
std::string trf(std::string_view english, std::initializer_list<std::string_view> fields);

}
