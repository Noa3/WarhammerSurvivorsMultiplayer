#include "wsm/steam_persona_reader.h"
#include <windows.h>
#include <string>
#include <cstring>
#include <memory>
namespace {
std::unique_ptr<wsm::SteamPersonaReader> reader;
std::string reason;
const char* (*image_name)(const Il2CppImage*)=nullptr;
const char* hide_steam(const Il2CppImage* image) {
    auto name=image_name(image);
    return name && std::strcmp(name,"com.rlabrecque.steamworks.net.dll")==0 ? "fixture-hidden.dll" : name;
}
}
extern "C" __declspec(dllexport) const char* __cdecl fixture_reason(){return reason.c_str();}
extern "C" __declspec(dllexport) int __cdecl fixture_open(unsigned os,int managed,int hide) {
    reader.reset();reason.clear();auto module=GetModuleHandleW(L"GameAssembly.dll");wsm::Il2CppApi a;
    if(!module||!wsm::Il2CppApi::resolve(module,a,reason)||!a.resolve_probe(module,reason))return 1;
    if(hide){image_name=a.image_get_name;a.image_get_name=hide_steam;}
    auto candidate=std::make_unique<wsm::SteamPersonaReader>();
    if(!candidate->initialize(module,a,a.domain_get(),os,managed,reason))return 2;
    reader=std::move(candidate);return 0;
}
// Expected is a fixture-only constant, never a real user name.
extern "C" __declspec(dllexport) int __cdecl fixture_read(int expected_status,const char* expected) {
    if(!reader)return 1;std::string output="sentinel";reason.clear();
    auto status=reader->read(output,reason);
    if(static_cast<int>(status)!=expected_status)return 2;
    if(status==wsm::PlayerReadStatus::Captured) {if(!expected||output!=expected||!reason.empty())return 3;}
    else if(output!="sentinel"||(status==wsm::PlayerReadStatus::Error && reason.empty()))return 4;
    return 0;
}
extern "C" __declspec(dllexport) void __cdecl fixture_close(){reader.reset();}
