#include "wsm/rewired_controller_session.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <string>

namespace {
std::unique_ptr<wsm::RewiredControllerSession> control;
std::string reason;
const wsm::RewiredControlOptions valid{1, 7, 17, 23, 2, 9};
int fail(int code, const std::string& text) { reason = text; return code; }
}
extern "C" __declspec(dllexport) const char* __cdecl fixture_control_reason() { return reason.c_str(); }
extern "C" __declspec(dllexport) int __cdecl fixture_control(int managed, unsigned os, int mode) {
    reason.clear();
    auto module = GetModuleHandleW(L"GameAssembly.dll");
    wsm::Il2CppApi api;
    if (!module || !wsm::Il2CppApi::resolve(module, api, reason) || !api.resolve_probe(module, reason)) return 70;
    if (!control) {
        control = std::make_unique<wsm::RewiredControllerSession>();
        if (!control->initialize(module, api, api.domain_get(), os, managed, reason)) return 71;
    }
    if(mode>=21 && mode<=31) {
        wsm::RewiredControlEnvironment result;
        result.horizontal_action_id=808; result.vertical_action_id=909;
        result.custom_sources_captured=true;
        result.players.push_back({}); result.players[0].player_id=456;
        result.players[0].custom_sources.push_back({123,456,{789},{987}});
        const auto before=control->state();
        auto status=mode==23?control->inspect(result,reason):control->inspect_sources(result,reason);
        const auto after=control->state();
        if(before.controller_id!=after.controller_id || before.map_id!=after.map_id || before.cleanup_required!=after.cleanup_required)
            return fail(120,"Source inspection changed ownership");
        if(mode!=21 && mode!=23 && mode!=30) {
            const char* expected=mode==22?"get_sourceControllerId":mode==24||mode==25?"Invalid or duplicate custom axis":
                mode==26?"Map/controller reference mismatch":mode==27||mode==28?"Invalid or duplicate custom controller identity":
                mode==29?"Invalid or duplicate custom map identity":"Invalid controller kind or custom type mismatch";
            return status==wsm::PlayerReadStatus::Error && reason.find(expected)!=std::string::npos && result.horizontal_action_id==808 &&
            result.vertical_action_id==909 && result.custom_sources_captured && result.players.size()==1 &&
            result.players[0].player_id==456 && result.players[0].custom_sources.size()==1 &&
            result.players[0].custom_sources[0].controller_id==123 && result.players[0].custom_sources[0].source_id==456 &&
            result.players[0].custom_sources[0].axis_ids==std::vector<std::int32_t>{789} &&
            result.players[0].custom_sources[0].map_ids==std::vector<std::int32_t>{987} ? 0 : fail(121,"Source error reason/output mismatch: "+reason);
        }
        if(status!=wsm::PlayerReadStatus::Captured) return fail(122,"Source inspect failed: "+reason);
        if(mode==23) return !result.custom_sources_captured && result.players.size()==2 && result.players[0].custom_sources.empty() ? 0 : fail(123,"Default inspect exposed sources");
        if(mode==30) return result.custom_sources_captured && result.players.size()==2 && result.players[0].custom_sources.empty() && result.players[1].custom_sources.empty() ? 0 : fail(129,"Empty source observation mismatch");
        if(!result.custom_sources_captured || result.players.size()!=2 || result.players[0].custom_sources.size()!=2 || !result.players[1].custom_sources.empty()) return fail(124,"Source inventory shape mismatch");
        const auto& a=result.players[0].custom_sources[0]; const auto& b=result.players[0].custom_sources[1];
        return a.controller_id==901 && b.controller_id==902 && a.source_id==7 && b.source_id==7 &&
            a.axis_ids==std::vector<std::int32_t>({2,9}) && b.axis_ids==std::vector<std::int32_t>({9,2}) &&
            a.map_ids.size()==1 && b.map_ids.empty() && result.players[0].maps.size()==2 &&
            a.map_ids[0]==result.players[0].maps[1].id && result.players[0].maps[1].controller_id==901 ? 0 : fail(125,"Source identity/axis/map join mismatch");
    }
    if (mode == 1) {
        if (!control->open(valid, reason)) return 72;
        const auto s = control->state();
        return s.player_id == 1 && s.controller_created && s.controller_attached && s.map_registered &&
            s.cleanup_required && s.horizontal == 0 && s.vertical == 0 && s.primary_verified && !s.release_verified &&
            !s.factory_outcome_unknown && s.last_controller_id==s.controller_id && s.last_map_id==s.map_id &&
            s.global_customs_before==0 && s.global_customs_after==1 ? 0 : fail(73, "Owned registration/state mismatch");
    }
    if (mode == 2 || mode == 3) {
        const float x = mode == 2 ? 0.75f : 0, y = mode == 2 ? -0.25f : 0;
        if (!control->set_axes(x, y, reason)) return 74;
        wsm::RewiredControlEnvironment observed;
        if (control->inspect(observed, reason) != wsm::PlayerReadStatus::Captured) return 75;
        const auto first = std::find_if(observed.players.begin(), observed.players.end(), [](const auto& p) { return p.player_id == 0; });
        const auto second = std::find_if(observed.players.begin(), observed.players.end(), [](const auto& p) { return p.player_id == 1; });
        return first != observed.players.end() && second != observed.players.end() &&
            first->horizontal == 0 && first->vertical == 0 && second->horizontal == x && second->vertical == y ?
            0 : fail(76, "Actual mapped player-axis values or primary-player isolation mismatch");
    }
    if (mode == 4) {
        if (!control->close(reason)) return 77;
        const auto s = control->state();
        return !s.controller_created && !s.controller_attached && !s.map_registered && !s.cleanup_required &&
            s.release_verified && s.primary_verified && s.last_controller_id>=0 && s.last_map_id>=0 && s.global_customs_after==0 ?
            0 : fail(78, "Cleanup proof or historical identity mismatch");
    }
    if (mode == 5 || mode == 6 || mode == 7 || mode == 8 || mode == 9) {
        auto options = valid;
        if (mode == 5) options.player_id = 0;
        if (mode == 6) options.source_id = 999;
        if (mode == 7) options.category_id = 999;
        if (mode == 8) options.layout_id = 999;
        if (mode == 9) options.vertical_axis_id = options.horizontal_axis_id;
        const auto prior = control->state();
        if (control->open(options, reason) || reason.empty()) return fail(79, "Invalid control options accepted without a refusal");
        const auto after = control->state();
        return after.controller_id == prior.controller_id && after.cleanup_required == prior.cleanup_required &&
            after.controller_created == prior.controller_created && after.map_registered == prior.map_registered ? 0 : 80;
    }
    if (mode == 10) {
        const auto before = control->state();
        if (control->open(valid, reason)) return fail(81, "Duplicate open accepted");
        const auto after = control->state();
        return before.controller_id == after.controller_id && before.map_id == after.map_id && after.map_registered ? 0 : 82;
    }
    if (mode == 11) {
        if (control->set_axes(std::numeric_limits<float>::quiet_NaN(), 0, reason)) return 83;
        const auto state = control->state();
        return state.horizontal == 0 && state.vertical == 0 ? 0 : 84;
    }
    if(mode==12) {
        if(control->open(valid,reason)||reason.empty())return fail(89,"Faulted factory unexpectedly opened");
        const auto s=control->state();
        return !s.cleanup_required&&!s.controller_created&&!s.controller_attached&&!s.map_registered?0:fail(90,"Partial-open failure retained resources");
    }
    if(mode==20) {
        wsm::RewiredControllerSession other;
        if(!other.initialize(module,api,api.domain_get(),os,managed,reason))return 111;
        if(other.open(valid,reason)||reason.empty())return fail(112,"Faulted factory unexpectedly opened");
        auto s=other.state();
        if(!s.cleanup_required||!s.factory_outcome_unknown||s.global_customs_before!=2||s.global_customs_after!=-1||s.observation_error.empty())return fail(113,"Failed global query hid uncertainty");
        if(other.close(reason)||!other.state().cleanup_required)return fail(114,"Failed global query lost ownership on close");
        return 0;
    }
    if(mode==19) {
        if(!control->close(reason))return fail(109,"Release blocked by primary observation: "+reason);
        const auto s=control->state();
        return s.release_verified&&!s.cleanup_required&&!s.primary_verified&&!s.observation_error.empty()?0:fail(110,"Primary change was not separated from release proof");
    }
    if(mode==18) {
        if(control->open(valid,reason)||reason.empty())return fail(104,"Throwing factory unexpectedly opened");
        const auto s=control->state();
        if(!s.cleanup_required||!s.factory_outcome_unknown||s.release_verified||s.last_controller_id!=-1||s.global_customs_before!=1||s.global_customs_after!=2)return fail(105,"Factory exception lost uncertain ownership or observations");
        if(control->close(reason)||reason.empty())return fail(106,"Ambiguous factory cleanup claimed success");
        return control->state().cleanup_required?0:fail(107,"Ambiguous factory cleanup lost ownership");
    }
    if(mode==13||mode==14) {
        const auto before=control->state();
        if(control->set_axes(0.5f,0,reason)||reason.empty())return fail(91,"Invalid runtime/input write accepted");
        const auto after=control->state();
        if(before.controller_id!=after.controller_id||!after.cleanup_required||!after.map_registered||after.horizontal!=before.horizontal||after.vertical!=before.vertical)return fail(92,"Refused write changed ownership/axes");
        if(mode==13&&reason!="Run actor/input identity changed during owned control")return fail(93,"Actor identity test refused for unrelated reason: "+reason);
        return 0;
    }
    if(mode==17) {
        const auto before=control->state();
        if(control->close(reason)||reason.empty())return fail(98,"Collision cleanup unexpectedly destroyed a foreign map");
        if(reason!="Ambiguous or foreign maps on owned controller; retain ownership")return fail(102,"Collision refused for unrelated reason: "+reason);
        const auto after=control->state();
        return after.cleanup_required&&after.controller_id==before.controller_id&&after.map_id==before.map_id?0:fail(99,"Collision refusal released ownership");
    }
    if(mode==15) {
        wsm::RewiredControllerSession wrong;
        if(!wrong.initialize(module,api,api.domain_get(),os+1,managed,reason))return 94;
        wsm::RewiredControlEnvironment seeded;seeded.horizontal_action_id=808;seeded.vertical_action_id=909;
        const auto status=wrong.inspect(seeded,reason);
        return status==wsm::PlayerReadStatus::Error&&!reason.empty()&&seeded.horizontal_action_id==808&&seeded.vertical_action_id==909?0:fail(95,"Wrong-thread observation published or changed seeded output");
    }
    wsm::RewiredControlEnvironment result;
    if (control->inspect(result, reason) != wsm::PlayerReadStatus::Captured) return 85;
    const bool category = std::any_of(result.categories.begin(), result.categories.end(), [](const auto& c) { return c.id == 17 && c.name == "Gameplay"; });
    const bool layout = std::any_of(result.layouts.begin(), result.layouts.end(), [](const auto& l) { return l.id == 23 && l.name == "CustomDefault"; });
    return category && layout && result.horizontal_action_id == 101 && result.vertical_action_id == 103 && result.players.size() == 2 ?
        0 : fail(86, "Nondefault category/layout/action/player inventory mismatch");
}
