#include "resonance/Localization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace resonance {
namespace {

constexpr std::array<std::string_view,8> languages{
    "en","fr","de","es-ES","zh-Hans","zh-Hant","ja","ko"
};
struct Catalog {
    std::string_view language;
    std::map<std::string,std::string,std::less<>> entries;
};
std::atomic<std::shared_ptr<const Catalog>> current;

bool utf8(std::string_view text) {
    for (std::size_t offset=0; offset<text.size();) {
        const auto lead=static_cast<unsigned char>(text[offset++]);
        if (lead<=0x7f) {
            if (lead<0x20 || lead==0x7f) return false;
            continue;
        }
        if (lead<0xc2 || lead>0xf4) return false;
        const auto trailing=lead<0xe0?1u:lead<0xf0?2u:3u;
        if (trailing>text.size()-offset) return false;
        const auto first=static_cast<unsigned char>(text[offset]);
        if ((lead==0xe0&&first<0xa0)||(lead==0xed&&first>=0xa0)||
            (lead==0xf0&&first<0x90)||(lead==0xf4&&first>=0x90)) return false;
        for (unsigned n=0; n<trailing; ++n)
            if ((static_cast<unsigned char>(text[offset++])&0xc0)!=0x80) return false;
    }
    return true;
}

struct Field {
    std::size_t index;
    std::size_t length;
};

std::optional<Field> field_at(std::string_view text,std::size_t offset) {
    if (text[offset]!='{' || offset+2>=text.size()) return {};
    const auto* first=text.data()+offset+1;
    const auto* end=text.data()+text.size();
    std::size_t number{};
    const auto parsed=std::from_chars(first,end,number);
    if (parsed.ec!=std::errc{} || parsed.ptr==end || *parsed.ptr!='}') return {};
    return Field{number,static_cast<std::size_t>(parsed.ptr-first)+2};
}

std::vector<std::size_t> fields_in(std::string_view text) {
    std::vector<std::size_t> fields;
    for (std::size_t at=0; at<text.size(); ++at)
        if (const auto field=field_at(text,at)) {
            fields.push_back(field->index);
            at+=field->length-1;
        }
    std::sort(fields.begin(),fields.end());
    return fields;
}

std::shared_ptr<const Catalog> load(std::string_view language,const std::filesystem::path& file) {
    constexpr std::uintmax_t maximum_catalog_bytes=2*1024*1024;
    const auto bytes=std::filesystem::file_size(file);
    if (!bytes||bytes>maximum_catalog_bytes) throw std::runtime_error("Invalid language catalog size.");
    std::ifstream input(file,std::ios::binary);
    std::string line;
    const auto read_line=[&] {
        if (!std::getline(input,line)) return false;
        if (line.ends_with('\r')) line.pop_back();
        return true;
    };
    if (!read_line()) throw std::runtime_error("Cannot read language catalog.");
    if (line.starts_with("\xef\xbb\xbf")) line.erase(0,3);
    if (line!="key\ttext") throw std::runtime_error("Invalid language catalog header.");
    auto next=std::make_shared<Catalog>();
    next->language=language;
    while (read_line()) {
        const auto tab=line.find('\t');
        if (tab==line.npos || !tab || tab+1>=line.size()) throw std::runtime_error("Invalid language catalog row.");
        const auto key=std::string_view(line).substr(0,tab);
        const auto value=std::string_view(line).substr(tab+1);
        if (!utf8(key)||!utf8(value)||fields_in(key)!=fields_in(value)||!next->entries.emplace(key,value).second)
            throw std::runtime_error("Invalid or duplicate language catalog entry.");
    }
    if (input.bad()||next->entries.empty()) throw std::runtime_error("Language catalog is empty or incomplete.");
    return next;
}
}

bool supported_language(std::string_view tag) {
    return std::find(languages.begin(),languages.end(),tag)!=languages.end();
}

std::string_view current_language() {
    const auto selected=current.load();
    return selected?selected->language:"en";
}

void use_language(std::string_view tag,const std::filesystem::path& catalog_directory) {
    const auto language=std::find(languages.begin(),languages.end(),tag);
    if (language==languages.end()) throw std::runtime_error("Unsupported game language.");
    if (tag=="en") current.store(nullptr);
    else current.store(load(*language,catalog_directory/(std::string(tag)+".tsv")));
}

std::string tr(std::string_view english) {
    if (const auto catalog=current.load()) {
        const auto found=catalog->entries.find(english);
        if (found!=catalog->entries.end()) return found->second;
    }
    return std::string(english);
}

std::string trf(std::string_view english,std::initializer_list<std::string_view> fields) {
    const auto text=tr(english);
    std::string result;
    result.reserve(text.size());
    for (std::size_t at=0; at<text.size();) {
        const auto field=field_at(text,at);
        if (field&&field->index<fields.size()) {
            result.append(fields.begin()[field->index]);
            at+=field->length;
        } else result.push_back(text[at++]);
    }
    return result;
}

}
