#include "resonance/Localization.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    namespace fs=std::filesystem;
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir=fs::temp_directory_path()/("resonance-localization-"+std::to_string(stamp));
    const auto file=dir/"fr.tsv";
    fs::create_directory(dir);
    try {
        using namespace resonance;
        for (const auto tag : {"en","fr","de","es-ES","zh-Hans","zh-Hant","ja","ko"})
            check(supported_language(tag),"A supported game language was rejected.");
        for (const auto tag : {"es-MX","ru","zh","pt-BR","../fr",""})
            check(!supported_language(tag),"Accepted a language outside the game's locale list.");

        check(tr("Objects")=="Objects","English source speech changed.");
        {
            std::ofstream out(file,std::ios::binary);
            out<<"key\ttext\nObjects\tObjets\n{0} has {1} HP\tPV : {1} pour {0}\n";
        }
        use_language("fr",dir);
        check(tr("Objects")=="Objets","French catalog was not selected.");
        check(tr("An unchanged native label")=="An unchanged native label","Missing values need source fallback.");
        check(trf("{0} has {1} HP",{"Naya {1}","32"})=="PV : 32 pour Naya {1}",
            "Placeholders were not reordered or a native value was interpreted as a template.");
        {
            std::ofstream out(file,std::ios::binary);
            out<<"key\ttext\n{0} has {1} HP\tPV : {1}\n";
        }
        bool rejected=false;
        try { use_language("fr",dir); } catch (const std::runtime_error&) { rejected=true; }
        check(rejected&&tr("Objects")=="Objets","Invalid placeholders replaced the active catalog.");
        use_language("en",dir);
        check(trf("{0} has {1} HP",{"Naya","32"})=="Naya has 32 HP","English switch retained the previous translation.");
        fs::remove(file);
        fs::remove(dir);
        return 0;
    } catch(const std::exception& error) {
        fs::remove(file);
        fs::remove(dir);
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
