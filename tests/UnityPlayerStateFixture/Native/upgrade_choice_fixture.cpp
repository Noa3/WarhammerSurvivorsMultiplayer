#include "wsm/upgrade_choice_controller.h"
#include <memory>
#include <string>
#include <thread>

namespace {
std::unique_ptr<wsm::UpgradeChoiceController> controller;
wsm::UpgradeChoiceOffer offer;
wsm::UpgradeChoiceResult result;
std::string reason;
bool equal(const wsm::UpgradeChoiceOffer& a, const wsm::UpgradeChoiceOffer& b) {
    if(a.token!=b.token || a.input_id!=b.input_id || a.actor_level!=b.actor_level || a.cards.size()!=b.cards.size())return false;
    for(std::size_t i=0;i<a.cards.size();++i) {
        const auto& x=a.cards[i];const auto& y=b.cards[i];
        if(x.child_index!=y.child_index || x.weapon_type!=y.weapon_type || x.current_level!=y.current_level ||
           x.is_new!=y.is_new || x.interactable!=y.interactable)return false;
    }
    return true;
}
}
extern "C" __declspec(dllexport) const char* __cdecl fixture_choice_reason() { return reason.c_str(); }
extern "C" __declspec(dllexport) int __cdecl fixture_choice_reset(int managed,unsigned os) {
    reason.clear();
    if(controller && !controller->close(reason))return 1;
    controller.reset();offer={};result={};
    auto module=GetModuleHandleW(L"GameAssembly.dll");wsm::Il2CppApi api;
    if(!module || !wsm::Il2CppApi::resolve(module,api,reason) || !api.resolve_probe(module,reason))return 2;
    controller=std::make_unique<wsm::UpgradeChoiceController>();
    return controller->initialize(module,api,api.domain_get(),os,managed,reason)?0:3;
}
extern "C" __declspec(dllexport) int __cdecl fixture_choice_capture(int input) {
    reason.clear();if(!controller){reason="Fixture controller not initialized";return 1;}
    auto before=offer;auto status=controller->capture(input,offer,reason);
    if(status!=wsm::PlayerReadStatus::Captured && !equal(before,offer)){
        reason="Refused capture modified prior output";return 9;
    }
    if(status==wsm::PlayerReadStatus::Captured) {
        if(!offer.token || offer.input_id!=input || offer.actor_level!=5 || offer.cards.size()!=2 ||
           offer.cards[0].child_index!=0 || offer.cards[0].weapon_type!=7001 ||
           offer.cards[1].child_index!=2 || offer.cards[1].weapon_type!=7002){reason="Unexpected captured offer";return 8;}
        return 0;
    }
    return 1;
}
// Direct-library negatives must not be hidden by the positive fixture's
// expected two-card shape or by the diagnostic command parser.
extern "C" __declspec(dllexport) int __cdecl fixture_choice_capture_raw(int input) {
    reason.clear();if(!controller){reason="Fixture controller not initialized";return 1;}
    auto before=offer;auto status=controller->capture(input,offer,reason);
    if(status!=wsm::PlayerReadStatus::Captured&&!equal(before,offer)){reason="Refused raw capture changed output";return 9;}
    return status==wsm::PlayerReadStatus::Captured?0:1;
}
extern "C" __declspec(dllexport) int __cdecl fixture_choice_offer_count() {
    return static_cast<int>(offer.cards.size());
}
extern "C" __declspec(dllexport) int __cdecl fixture_choice_offer_has(int child,int weapon) {
    for(const auto& card:offer.cards)if(card.child_index==child&&card.weapon_type==weapon)return 1;
    return 0;
}
extern "C" __declspec(dllexport) std::uint64_t __cdecl fixture_choice_token() { return offer.token; }
extern "C" __declspec(dllexport) int __cdecl fixture_choice_attempted() { return result.invocation_attempted?1:0; }
extern "C" __declspec(dllexport) int __cdecl fixture_choice_returned() { return result.invocation_returned?1:0; }
extern "C" __declspec(dllexport) int __cdecl fixture_choice_submit(std::uint64_t token,int input,int index,int weapon) {
    reason.clear();result={};
    if(!controller){reason="Fixture controller not initialized";return 1;}
    return controller->select(token,input,index,weapon,result,reason)?0:1;
}
extern "C" __declspec(dllexport) int __cdecl fixture_choice_offthread_submit(std::uint64_t token,int input,int index,int weapon) {
    int status=0;
    std::thread worker([&]{status=fixture_choice_submit(token,input,index,weapon);});worker.join();return status;
}
extern "C" __declspec(dllexport) int __cdecl fixture_choice_close() {
    reason.clear();return !controller || controller->close(reason)?0:1;
}
