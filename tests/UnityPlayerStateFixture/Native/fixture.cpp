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

extern "C" __declspec(dllexport) int __cdecl fixture_upgrade(int managed, unsigned os, int mode) {
    g_reason.clear();
    auto module=GetModuleHandleW(L"GameAssembly.dll");
    wsm::Il2CppApi api; std::string error;
    if(!module || !wsm::Il2CppApi::resolve(module,api,error) || !api.resolve_probe(module,error)) return 20;
    wsm::PlayerStateReader reader;
    if(!reader.initialize(module,api,api.domain_get(),os,managed,error)) { note("upgrade_init",error,21); return 21; }
    if(mode==11)reader.test_fail_next_scene_root(false);
    if(mode==14)reader.test_use_wrong_equipment_collection();
    wsm::UpgradeContextSnapshot snapshot;
    snapshot.level_up_page_count=-44; snapshot.page_game_session_present=true;
    snapshot.active_character_present=true; snapshot.level_up_factory_present=false; snapshot.active_character_slot=99;
    snapshot.actors={{-4,88,-7,true,false,true},{-8,89,-9,false,true,false}};
    snapshot.choice_group_present=true;snapshot.choice_group_items_present=false;
    snapshot.choice_group_item_references={true,false,true};
    snapshot.choice_selection_observed=true;snapshot.selected_choice_container_slot=77;
    snapshot.choice_group_item_unity_alive={true,false};snapshot.choice_group_item_active={false,true};
    snapshot.choice_items_observed=true;snapshot.choice_child_count=77;
    snapshot.choice_items={{true,false,true,false,true,false,true,-99}};
    const auto before=snapshot;
    const auto unchanged=[&] {
        if(snapshot.level_up_page_count!=before.level_up_page_count ||
           snapshot.page_game_session_present!=before.page_game_session_present ||
           snapshot.active_character_present!=before.active_character_present ||
           snapshot.level_up_factory_present!=before.level_up_factory_present ||
           snapshot.active_character_slot!=before.active_character_slot || snapshot.actors.size()!=before.actors.size() ||
           snapshot.choice_group_present!=before.choice_group_present || snapshot.choice_group_items_present!=before.choice_group_items_present ||
           snapshot.choice_group_item_references!=before.choice_group_item_references ||
           snapshot.choice_selection_observed!=before.choice_selection_observed || snapshot.selected_choice_container_slot!=before.selected_choice_container_slot ||
           snapshot.choice_group_item_unity_alive!=before.choice_group_item_unity_alive || snapshot.choice_group_item_active!=before.choice_group_item_active ||
           snapshot.choice_items_observed!=before.choice_items_observed || snapshot.choice_child_count!=before.choice_child_count ||
           snapshot.choice_items.size()!=before.choice_items.size())return false;
        for(std::size_t i=0;i<before.choice_items.size();++i) {
            const auto &a=snapshot.choice_items[i], &b=before.choice_items[i];
            if(a.child_present!=b.child_present || a.child_unity_alive!=b.child_unity_alive ||
               a.component_present!=b.component_present || a.component_unity_alive!=b.component_unity_alive ||
               a.fields_observed!=b.fields_observed || a.data_present!=b.data_present || a.is_new!=b.is_new || a.weapon_type!=b.weapon_type)return false;
        }
        for(std::size_t i=0;i<before.actors.size();++i) {
            const auto &a=snapshot.actors[i], &b=before.actors[i];
            if(a.slot!=b.slot || a.input_id!=b.input_id || a.character_type!=b.character_type ||
               a.weapons_manager_present!=b.weapons_manager_present || a.accessories_manager_present!=b.accessories_manager_present ||
               a.accessories_collection_present!=b.accessories_collection_present)return false;
        }
        return true;
    };
    auto status=mode>=29?reader.read_upgrade_items(snapshot,error):mode>=20?reader.read_upgrade_selection(snapshot,error):reader.read_upgrade_context(snapshot,error);
    if(mode==1 || mode==9 || mode==12 || mode==28 || mode==36) {
        if(status==wsm::PlayerReadStatus::NotReady && error.empty() && unchanged())return 0;
        note("upgrade_not_ready",error,static_cast<int>(status)); return 22;
    }
    if(mode==7 || mode==8 || mode==10 || mode==11 || mode==13 || mode==14 || mode==19 || mode==25 || mode==26 || mode==27 || mode==33 || mode==34 || mode==35) {
        if(status==wsm::PlayerReadStatus::Error && !error.empty() && unchanged() &&
           (mode!=14 || error=="ActiveEquipment must be List of canonical Equipment") &&
           ((mode!=19 && mode!=27) || error=="Choice container item count outside supported bound") &&
           (mode!=33 || error=="Choice child count outside supported bound"))return 0;
        note("upgrade_error",error,static_cast<int>(status)); return 23;
    }
    if(status!=wsm::PlayerReadStatus::Captured || !error.empty()) { note("upgrade_capture",error,static_cast<int>(status)); return 24; }
    const bool session=mode!=2, active=session && mode!=3, factory=mode!=6;
    const int slot=active?(mode==4?-2:1):-1;
    if(snapshot.level_up_page_count!=1 || snapshot.page_game_session_present!=session ||
       snapshot.active_character_present!=active || snapshot.level_up_factory_present!=factory ||
       snapshot.active_character_slot!=slot || snapshot.actors.size()!=2) { note("upgrade_context_values",error,25); return 25; }
    const auto &a=snapshot.actors[0], &b=snapshot.actors[1];
    const bool item_group=mode==29||mode==31||mode==32||mode==37||mode==38||mode==39;
    const bool group_present=(mode>=15 && mode<=18)||(mode>=20 && mode<=23)||item_group, items_present=group_present && mode!=16;
    const auto entries=(mode==15||(mode>=20 && mode<=22))?std::vector<bool>{false,true}:mode==18?std::vector<bool>{false,true,false,true,false,true,false,true}:mode==23?std::vector<bool>{true,true,true}:mode==37?std::vector<bool>{true,true}:item_group?std::vector<bool>{true}:std::vector<bool>{};
    if(snapshot.choice_group_present!=group_present || snapshot.choice_group_items_present!=items_present || snapshot.choice_group_item_references!=entries) {
        note("upgrade_choice_group_values",error,27);return 27;
    }
    const auto alive=(mode==20||mode==21)?std::vector<bool>{false,true}:mode==22?std::vector<bool>{false,false}:mode==23?std::vector<bool>{true,true,true}:item_group?entries:std::vector<bool>{};
    const auto active_items=mode==20?std::vector<bool>{false,true}:(mode==21||mode==22)?std::vector<bool>{false,false}:mode==23?std::vector<bool>{false,true,true}:item_group?entries:std::vector<bool>{};
    if(snapshot.choice_selection_observed!=(mode>=20) || snapshot.selected_choice_container_slot!=((mode==20||mode==23)?1:item_group?0:-1) ||
       snapshot.choice_group_item_unity_alive!=alive || snapshot.choice_group_item_active!=active_items) {
        note("upgrade_choice_selection_values",error,28);return 28;
    }
    if(item_group) {
        const int expected_count=mode==29?4:mode==31?0:mode==32?32:1;
        if(!snapshot.choice_items_observed || snapshot.choice_child_count!=expected_count || snapshot.choice_items.size()!=static_cast<std::size_t>(expected_count)) {
            note("upgrade_choice_item_count",error,29);return 29;
        }
        for(std::size_t i=0;i<snapshot.choice_items.size();++i) {
            const auto& item=snapshot.choice_items[i];const bool component=mode==29?i!=0:mode==37;
            const int type=mode==29?(i==2?7001:i==3?7002:0):mode==37?7001:0;
            const bool data=mode==29?i==2:mode==37;
            if(!item.child_present || !item.child_unity_alive || item.component_present!=component ||
               item.component_unity_alive!=component || item.fields_observed!=component ||
               item.data_present!=data || item.is_new!=data || item.weapon_type!=type) {
                note("upgrade_choice_item_values",error,30);return 30;
            }
        }
    } else if(snapshot.choice_items_observed || snapshot.choice_child_count!=-1 || !snapshot.choice_items.empty()) {
        note("upgrade_items_must_be_opt_in",error,31);return 31;
    }
    if(a.slot!=0 || a.input_id!=41 || a.character_type!=3 || !a.weapons_manager_present || a.accessories_manager_present || a.accessories_collection_present ||
       b.slot!=1 || b.input_id!=52 || b.character_type!=4 || b.weapons_manager_present || !b.accessories_manager_present || b.accessories_collection_present!=(mode!=5)) {
        note("upgrade_actor_values",error,26);return 26;
    }
    return 0;
}
