#include "wsm/player_state_reader.h"
#include <windows.h>
#include <cstdio>
#include <string>
// A Windows GUI player has no console, so printf never surfaces. The last
// reader decision is exported so the managed driver can log it.
namespace { std::string g_reason;
void note(const char* tag,const std::string& detail,int status){ g_reason=std::string(tag)+" status="+std::to_string(status)+" detail="+(detail.empty()?"-":detail); }
std::string player(const wsm::PlayerStateSnapshot& p){ return "("+std::to_string(p.x)+","+std::to_string(p.y)+",hp="+std::to_string(p.hp)+",xp="+std::to_string(p.xp)+",level="+std::to_string(p.level)+",input="+std::to_string(p.input_id)+",kind="+std::to_string(p.character_type)+",dead="+(p.dead?"1":"0")+")"; } }
extern "C" __declspec(dllexport) const char* __cdecl fixture_reason() { return g_reason.c_str(); }
extern "C" __declspec(dllexport) int __cdecl fixture_test(int managed, unsigned os, int mode) {
    g_reason.clear();
    auto module = GetModuleHandleW(L"GameAssembly.dll");
    wsm::Il2CppApi api; std::string error;
    if (!module || !wsm::Il2CppApi::resolve(module, api, error) || !api.resolve_probe(module, error)) { g_reason="api detail="+error; return 10; }
    wsm::PlayerStateReader reader;
    if (!reader.initialize(module, api, api.domain_get(), os, managed, error)) { g_reason="initialize detail="+error; return 11; }
    if (mode == 3 || mode == 4) reader.test_fail_next_scene_root(mode == 4);
    wsm::SceneStateSnapshot snapshot; snapshot.stage_type = -111;
    snapshot.players = {{-11, 92, 15, 21, -17.5f, 18.25f, 19.75f, 20.5f, true},
                        {-12, 93, 16, 22, -23.5f, 24.25f, 25.75f, 26.5f, false}};
    const auto before = snapshot;
    const auto unchanged = [&] {
        if (snapshot.stage_type != before.stage_type || snapshot.players.size() != before.players.size()) return false;
        for (std::size_t i = 0; i < before.players.size(); ++i) {
            const auto &a = snapshot.players[i], &b = before.players[i];
            if (a.slot != b.slot || a.input_id != b.input_id || a.character_type != b.character_type ||
                a.level != b.level || a.x != b.x || a.y != b.y || a.hp != b.hp || a.xp != b.xp || a.dead != b.dead) return false;
        }
        return true;
    };
    auto status = reader.read(snapshot, error);
    auto code = static_cast<int>(status);
    // mode 1: outside a scene -> NotReady with the caller snapshot untouched (no fake success).
    if (mode == 1) { if (status == wsm::PlayerReadStatus::NotReady && unchanged()) return 0; note("not_ready",error,code); return 12; }
    // mode 2: malformed state -> Error with the caller snapshot untouched.
    if (mode >= 2) { if (status == wsm::PlayerReadStatus::Error && unchanged()) return 0; note("expected_error",error,code); return 13; }
    if (status != wsm::PlayerReadStatus::Captured) { note("captured",error,code); return 14; }
    if (snapshot.stage_type != 7 || snapshot.players.size()!=2) { note("scene","stage="+std::to_string(snapshot.stage_type)+" players="+std::to_string(snapshot.players.size()),code); return 15; }
    const auto &a=snapshot.players[0], &b=snapshot.players[1];
    if (a.slot!=0 || a.input_id!=41 || a.character_type!=3 || a.level!=5 || a.x!=1.25f || a.y!=-2.5f || a.hp!=83.5f || a.xp!=12.25f || a.dead || b.slot!=1 || b.input_id!=52 || b.character_type!=4 || b.level!=8 || b.x!=-9.0f || b.y!=6.5f || b.hp!=0.0f || b.xp!=77.0f || !b.dead) { note("players","a"+player(a)+" b"+player(b),code); return 16; }
    return 0;
}
