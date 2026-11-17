#include "resonance/Hotkeys.hpp"
#include <iostream>
#include <stdexcept>
using resonance::KeyEdges;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try {
    KeyEdges first;
    need(first.observe(1,true)==0,"a held key on the first foreground capture is not a fresh command");
    first.observe(0,true);need(first.observe(1,true)==1,"a fresh deliberate foreground press works");
    need(first.observe(1,true)==0,"a held key never repeats");
    first.observe(1,false);need(first.observe(1,true)==0,"returning with a held key never activates navigation");
    first.observe(0,true);need(first.observe(2,true)==2,"release and a new press after focus return works");
    KeyEdges returning;returning.observe(0,true);returning.observe(0,false);
    need(returning.observe(4,true)==0,"a key first observed during focus gain cannot activate auto-walk");
    returning.observe(0,true);need(returning.observe(4,true)==4,"fresh press re-arms after focus gain");
    std::cout<<"Foreground key edge checks passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
