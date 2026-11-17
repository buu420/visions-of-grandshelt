#include "resonance/InputContextPolicy.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace resonance;
namespace {
void require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
bool allowed(const InputContextPolicy& p) {
    return p.known&&p.map_unit_context==InputFact::Yes&&p.blocking_context==InputFact::No&&p.text_entry==InputFact::No;
}
InputContext field(){return {1,{2,26,14,17,18,23,12}};}
void logged_field_capabilities() {
    const std::array<InputContext,1> contexts{field()};
    require(allowed(evaluate_input_contexts(contexts)),"the actual post-tutorial MapUnit capability row is ordinary field input, not open menus");
}
void scoped_child_exceptions() {
    for(std::uint8_t parent:{std::uint8_t{12},std::uint8_t{17}})
        for(std::uint8_t child:{std::uint8_t{18},std::uint8_t{23},std::uint8_t{26}}) {
            const std::array<InputContext,2> contexts{field(),InputContext{parent,{child}}};
            const auto policy=evaluate_input_contexts(contexts);
            require(policy.known&&policy.blocking_context==InputFact::Yes&&!allowed(policy),
                "the same MapCommon/SystemMenu/ChatList child outside MapUnit must still block");
        }
}
void shipped_menu_stacks() {
    // Exact DT_MainMenuScene: Root, Item, Equip, Recovery, Quest, world map.
    const std::array<InputContext,6> menus{{{12,{17,21}},{12,{16,17,33}},{12,{13,16,17}},
        {12,{20,17}},{12,{17,31,18}},{17,{18,24,12,31}}}};
    for(const auto& menu:menus) {
        const std::array<InputContext,2> contexts{field(),menu};
        require(evaluate_input_contexts(contexts).blocking_context==InputFact::Yes,
            "actual shipped menu/world-map child contexts must remain blocking");
    }
}
void tutorial_battle_travel_modes() {
    for(std::uint8_t parent:{std::uint8_t{3},std::uint8_t{4},std::uint8_t{5},std::uint8_t{6},
        std::uint8_t{7},std::uint8_t{8},std::uint8_t{10},std::uint8_t{11},std::uint8_t{28},std::uint8_t{29}}) {
        const std::array<InputContext,2> contexts{InputContext{parent,{12,32}},field()};
        require(evaluate_input_contexts(contexts).blocking_context==InputFact::Yes,
            "tutorial/events, battle, race/travel, options and shop parents must remain blocking");
    }
}
void text_edit_and_unexpected_child() {
    for(const auto& modal:std::array<InputContext,3>{{{25,{}},{1,{25}},{1,{24}}}}) {
        const std::array<InputContext,2> contexts{field(),modal};
        const auto policy=evaluate_input_contexts(contexts);
        require(!allowed(policy)&&policy.blocking_context==InputFact::Yes,
            "unexpected modal children of MapUnit cannot use the persistent-capability exception");
        if(modal.parent==25||modal.children==std::vector<std::uint8_t>{25})
            require(policy.text_entry==InputFact::Yes,"native UITextEdit still requires text suppression");
    }
}
void missing_field_parent() {
    const std::array<InputContext,1> child_only{InputContext{12,{1}}},dash_only{InputContext{2,{12}}};
    require(!allowed(evaluate_input_contexts(child_only)),"a MapUnit child is not an actual MapUnit parent mode");
    require(!allowed(evaluate_input_contexts(dash_only)),"dash capability alone is not an ordinary field mode");
    require(!allowed(evaluate_input_contexts({})),"an empty context capture must deny field navigation");
}
void unknown_or_oversized_contexts() {
    for(const auto& unknown:std::array<InputContext,3>{{{34,{}},{1,{34}},{1,{255}}}}) {
        const std::array<InputContext,2> contexts{field(),unknown};
        const auto policy=evaluate_input_contexts(contexts);
        require(!policy.known&&!allowed(policy),"MAX/unknown native enum values must fail closed rather than authorize input");
    }
    std::vector<InputContext> too_many(65,InputContext{1,{2}});
    require(!evaluate_input_contexts(too_many).known,"oversized native stack capture must fail closed");
    const std::array<InputContext,1> huge_child{InputContext{1,std::vector<std::uint8_t>(65,2)}};
    require(!evaluate_input_contexts(huge_child).known,"oversized child capture must fail closed");
}
void duplicate_capabilities_and_shared_ui() {
    const std::array<InputContext,2> contexts{InputContext{12,{}},InputContext{1,{2,26,14,17,18,23,12,18,23}}};
    require(allowed(evaluate_input_contexts(contexts)),"native concatenation may duplicate ordinary capabilities; shared UI alone is not a menu-open fact");
}
}
int main() {
    const std::array<std::pair<const char*,void(*)()>,8> tests{{
        {"logged_field_capabilities",logged_field_capabilities},{"scoped_child_exceptions",scoped_child_exceptions},
        {"shipped_menu_stacks",shipped_menu_stacks},{"tutorial_battle_travel_modes",tutorial_battle_travel_modes},
        {"text_edit_and_unexpected_child",text_edit_and_unexpected_child},{"missing_field_parent",missing_field_parent},
        {"unknown_or_oversized_contexts",unknown_or_oversized_contexts},{"duplicate_capabilities_and_shared_ui",duplicate_capabilities_and_shared_ui}}};
    int failures=0;
    for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failures;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    std::cout<<tests.size()-failures<<"/"<<tests.size()<<" input context groups passed\n";
    return failures?1:0;
}
