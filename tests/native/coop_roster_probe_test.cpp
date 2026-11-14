// Execute the production parser/serializer/writer without a game or runtime.
#include "../../native/src/coop_roster_probe.cpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    int failed = 0, count = 0;
    auto check = [&](const char* name, bool ok) { ++count; if (!ok) ++failed; std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; };
    using namespace wsm;
    RosterCommand command;
    check("inspect command", parse_command("inspect\r\n", 1234, command) && command.action == RosterAction::inspect);
    check("own PID enrollment", parse_command("enroll:1234:41", 1234, command) && command.action == RosterAction::enroll && command.player == 41);
    check("own PID rollback", parse_command("rollback:1234", 1234, command) && command.action == RosterAction::rollback);
    check("own PID menu player selection", parse_command("select:1234:41", 1234, command) && command.action == RosterAction::select_player && command.player == 41);
    check("wrong PID menu player selection", !parse_command("select:1235:41", 1234, command));
    check("own PID stock character copy", parse_command("copy:1234:41", 1234, command) && command.action == RosterAction::copy_character && command.player == 41);
    check("wrong PID stock character copy", !parse_command("copy:1235:41", 1234, command));
    check("own PID upgrade context query", parse_command("upgradeContext:1234", 1234, command) && command.action==RosterAction::upgrade_context && command.player==-1);
    check("wrong PID upgrade context query", !parse_command("upgradeContext:1235", 1234, command));
    check("own PID explicit selection query", parse_command("upgradeSelection:1234",1234,command) && command.action==RosterAction::upgrade_selection && command.player==-1);
    check("wrong PID explicit selection query", !parse_command("upgradeSelection:1235",1234,command));
    check("selection query cannot enter mutation dispatch", !mutates_roster(RosterAction::upgrade_selection));
    RosterCommand item_command;
    check("own PID explicit item query",parse_command("upgradeItems:1234",1234,item_command) && item_command.player==-1);
    check("wrong PID explicit item query",!parse_command("upgradeItems:1235",1234,item_command));
    check("item query cannot enter mutation dispatch",item_command.action==RosterAction::upgrade_items && !mutates_roster(item_command.action));
    RosterCommand hint_command;
    check("own PID explicit hint query",parse_command("upgradeHint:1234",1234,hint_command) && hint_command.action==RosterAction::upgrade_hint);
    check("wrong PID explicit hint query",!parse_command("upgradeHint:1235",1234,hint_command));
    check("hint query cannot enter mutation dispatch",!mutates_roster(RosterAction::upgrade_hint));
    RosterCommand equipment_command;
    check("own PID explicit equipment query",parse_command("equipment:1234",1234,equipment_command) && equipment_command.action==RosterAction::equipment && equipment_command.player==-1);
    check("wrong PID explicit equipment query",!parse_command("equipment:1235",1234,equipment_command));
    check("equipment query cannot enter mutation dispatch",!mutates_roster(RosterAction::equipment));
    RosterCommand profile_command;
    check("own PID explicit input profile query",parse_command("inputProfile:1234",1234,profile_command)&&profile_command.action==RosterAction::input_profile&&profile_command.player==-1);
    check("wrong PID explicit input profile query",!parse_command("inputProfile:1235",1234,profile_command));
    check("input profile cannot enter mutation dispatch",!mutates_roster(RosterAction::input_profile));
    for(const auto* value:{"inputProfile:","inputProfile:-1234","inputProfile:1234:0","inputProfile:1234\nrollback:1234"})
        check(value,!parse_command(value,1234,profile_command));
    for(const auto* value:{"equipment:","equipment:-1234","equipment:1234:0","equipment:1234\nrollback:1234"})
        check(value,!parse_command(value,1234,equipment_command));
    for(const auto* text:{"upgradeHint:","upgradeHint:-1234","upgradeHint:1234:0","upgradeHint:1234\nrollback:1234"})
        check(text,!parse_command(text,1234,hint_command));
    for(const auto* text:{"upgradeItems:","upgradeItems:-1234","upgradeItems:1234:0","upgradeItems:1234\nrollback:1234"})
        check(text,!parse_command(text,1234,item_command));
    for(const auto* text:{"upgradeSelection:","upgradeSelection:-1234","upgradeSelection:1234:0","upgradeSelection:1234\nrollback:1234"})
        check(text,!parse_command(text,1234,command));
    check("upgrade query cannot enter mutation dispatch", !mutates_roster(RosterAction::upgrade_context) && !mutates_roster(RosterAction::inspect));
    check("all explicit mutation actions remain classified", mutates_roster(RosterAction::enroll) && mutates_roster(RosterAction::rollback) && mutates_roster(RosterAction::select_player) && mutates_roster(RosterAction::copy_character));
    command = {RosterAction::enroll, 41};
    check("wrong PID leaves command unchanged", !parse_command("enroll:1235:0", 1234, command) && command.action == RosterAction::enroll && command.player == 41);
    for (const auto* text : {"enroll:1234:-1", "enroll:1234:2147483648", "enroll:1234:41:0", "enroll:1234", "enroll:1234:", "rollback:1234:0", "rollback:-1234", "inspect\nrollback:1234", "upgradeContext:", "upgradeContext:-1234", "upgradeContext:1234:0", "upgradeContext:1234\nrollback:1234", "unknown"})
        check(text, !parse_command(text, 1234, command));
    check("bounded command", !parse_command(std::string(97, 'x'), 1234, command));
    check("Int32 maximum ID", parse_command("enroll:1234:" + std::to_string((std::numeric_limits<std::int32_t>::max)()), 1234, command));
    ProbeContext c;
    c.status = CoopRosterStatus::Captured;
    c.snapshot.configured_ids = {0, 41}; c.snapshot.enrolled_ids = {0};
    auto text = result_json(c, 1);
    check("captured roster includes actual supplied IDs", text.find("\"configuredIds\":[0,41]") != std::string::npos && text.find("\"enrolledIds\":[0]") != std::string::npos);
    c.status = CoopRosterStatus::Error;
    text = result_json(c, 2);
    check("error suppresses stale roster and player state", text.find("\"configuredIds\":[]") != std::string::npos && text.find("\"enrolledIds\":[]") != std::string::npos && text.find("\"players\":[]") != std::string::npos);
    check("unavailable configuration is explicit", text.find("\"characterConfiguration\":{\"captured\":false}") != std::string::npos);
    c.configuration_captured = true; c.configuration.selected_types = {37, 0}; c.configuration.coop_config_present = true;
    c.action_error = "action \"quoted\"\\path\n\t";
    c.roster_error = "\b\f\r";
    c.configuration_error = std::string(1, '\x01');
    c.player_error = "player observation failed";
    c.upgrade_error = "upgrade \"context\"\\reference\n";
    text = result_json(c, 3);
    check("configuration preserves selected enum values", text.find("\"coopConfigReferencePresent\":true") != std::string::npos && text.find("\"selectedTypes\":[37,0]") != std::string::npos);
    std::cout << "ROSTER_ERROR_JSON " << text << '\n';
    c.command={RosterAction::upgrade_context,-1};c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.level_up_page_count=1;c.upgrade.page_game_session_present=true;c.upgrade.active_character_present=true;
    c.upgrade.active_character_slot=1;c.upgrade.level_up_factory_present=true;
    c.upgrade.actors={{0,41,37,true,false,false},{1,52,91,false,true,true}};
    c.upgrade.choice_group_present=true;c.upgrade.choice_group_items_present=true;
    c.upgrade.choice_group_item_references={false,true};
    auto context_json=result_json(c,4);
    check("captured upgrade query serializes reference identity", context_json.find("\"activeCharacterSlot\":1")!=std::string::npos && context_json.find("\"inputId\":52")!=std::string::npos);
    check("captured upgrade query serializes choice group structure", context_json.find("\"choiceContainerGroupReferencePresent\":true")!=std::string::npos &&
          context_json.find("\"choiceContainerItemsReferencePresent\":true")!=std::string::npos &&
          context_json.find("\"choiceContainerItemReferences\":[false,true]")!=std::string::npos);
    std::cout << "ROSTER_UPGRADE_JSON " << context_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;context_json=result_json(c,5);
    check("upgrade error suppresses stale context", context_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    std::cout << "ROSTER_UPGRADE_ERROR_JSON " << context_json << '\n';
    c.upgrade_status=PlayerReadStatus::NotReady;context_json=result_json(c,6);
    check("upgrade not ready suppresses stale context", context_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};c.upgrade_status=PlayerReadStatus::Captured;context_json=result_json(c,7);
    check("ordinary inspection does not pretend upgrade query ran", context_json.find("\"upgradeContext\":{\"state\":\"not_requested\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::upgrade_selection,-1};c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.choice_selection_observed=true;c.upgrade.selected_choice_container_slot=1;
    c.upgrade.choice_group_item_unity_alive={false,true};c.upgrade.choice_group_item_active={false,true};
    auto selection_json=result_json(c,8);
    check("selection query exposes explicit engine observation", selection_json.find("\"choiceSelection\":{\"observed\":true,\"selectedContainerSlot\":1")!=std::string::npos);
    std::cout << "ROSTER_SELECTION_JSON " << selection_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;selection_json=result_json(c,9);
    check("selection error suppresses stale engine flags", selection_json.find("\"choiceSelection\"")==std::string::npos && selection_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.upgrade_status=PlayerReadStatus::NotReady;selection_json=result_json(c,10);
    check("selection not ready suppresses stale engine flags", selection_json.find("\"choiceSelection\"")==std::string::npos && selection_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::upgrade_context,-1};c.upgrade_status=PlayerReadStatus::Captured;selection_json=result_json(c,11);
    check("field query never exposes a stale engine observation", selection_json.find("\"choiceSelection\"")==std::string::npos);
    c.command=item_command;c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.choice_items_observed=true;c.upgrade.choice_child_count=2;
    c.upgrade.choice_items={{true,true,false,false,false,false,false,0},{true,true,true,true,true,false,true,7002}};
    auto items_json=result_json(c,12);
    check("item query serializes explicit action and ordered content",items_json.find("\"action\":\"upgradeItems\"")!=std::string::npos &&
          items_json.find("\"choiceItems\":{\"observed\":true,\"childCount\":2,\"items\":[")!=std::string::npos &&
          items_json.find("\"weaponType\":7002,\"dataReferencePresent\":false,\"isNew\":true")!=std::string::npos);
    std::cout << "ROSTER_ITEMS_JSON " << items_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;items_json=result_json(c,13);
    check("item error suppresses all stale observations",items_json.find("\"choiceItems\"")==std::string::npos && items_json.find("\"choiceSelection\"")==std::string::npos &&
          items_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.upgrade_status=PlayerReadStatus::NotReady;items_json=result_json(c,14);
    check("item not ready suppresses all stale observations",items_json.find("\"choiceItems\"")==std::string::npos &&
          items_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    for(auto action:{RosterAction::upgrade_context,RosterAction::upgrade_selection}) {
        c.command={action,-1};c.upgrade_status=PlayerReadStatus::Captured;items_json=result_json(c,15);
        check("less detailed query never exposes stale item observations",items_json.find("\"choiceItems\"")==std::string::npos);
    }
    const auto directory = std::filesystem::path(argv[1]) / ("roster-command-" + std::to_string(GetCurrentProcessId()));
    c.command={RosterAction::upgrade_hint,-1};c.upgrade_status=PlayerReadStatus::Captured;
    c.upgrade.fourth_option_hint_observed=true;c.upgrade.fourth_option_hint_present=true;
    c.upgrade.fourth_option_hint_unity_alive=true;c.upgrade.fourth_option_hint_comparison_complete=true;
    c.upgrade.fourth_option_hint_child_slot=1;
    auto hint_json=result_json(c,16);
    check("hint query serializes explicit action and identity",hint_json.find("\"action\":\"upgradeHint\"")!=std::string::npos &&
          hint_json.find("\"fourthOptionHint\":{\"observed\":true,\"referencePresent\":true,\"unityAlive\":true,\"childComparisonComplete\":true,\"childSlot\":1}")!=std::string::npos);
    std::cout << "ROSTER_HINT_JSON " << hint_json << '\n';
    c.upgrade_status=PlayerReadStatus::Error;hint_json=result_json(c,17);
    check("hint error suppresses all stale observations",hint_json.find("\"fourthOptionHint\"")==std::string::npos &&
          hint_json.find("\"upgradeContext\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.upgrade_status=PlayerReadStatus::NotReady;hint_json=result_json(c,18);
    check("hint not ready suppresses all stale observations",hint_json.find("\"fourthOptionHint\"")==std::string::npos &&
          hint_json.find("\"upgradeContext\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    for(auto action:{RosterAction::upgrade_context,RosterAction::upgrade_selection,RosterAction::upgrade_items}) {
        c.command={action,-1};c.upgrade_status=PlayerReadStatus::Captured;hint_json=result_json(c,19);
        check("less detailed query never exposes stale hint observation",hint_json.find("\"fourthOptionHint\"")==std::string::npos);
    }
    c.command={RosterAction::equipment,-1};c.equipment_status=PlayerReadStatus::Captured;
    EquipmentActorSnapshot inventory;inventory.slot=1;inventory.input_id=52;inventory.character_type=91;
    inventory.manager_present=true;inventory.collection_present=true;
    inventory.items={{true,true,true,true,7002,1,"Weapons.Quoted\"Name"},{false,false,false,false,0,-1,""}};
    c.equipment.actors={inventory};
    auto equipment_json=result_json(c,20);
    check("equipment query serializes own action and owner",equipment_json.find("\"action\":\"equipment\"")!=std::string::npos &&
        equipment_json.find("\"equipment\":{\"state\":\"captured\",\"actors\":[")!=std::string::npos &&
        equipment_json.find("\"ownerSlot\":1")!=std::string::npos);
    std::cout << "ROSTER_EQUIPMENT_JSON " << equipment_json << '\n';
    c.equipment_status=PlayerReadStatus::Error;equipment_json=result_json(c,21);
    check("equipment error suppresses stale inventory",equipment_json.find("\"equipment\":{\"state\":\"error\",\"actors\":[]}")!=std::string::npos);
    c.equipment_status=PlayerReadStatus::NotReady;equipment_json=result_json(c,22);
    check("equipment not ready suppresses stale inventory",equipment_json.find("\"equipment\":{\"state\":\"not_ready\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};c.equipment_status=PlayerReadStatus::Captured;equipment_json=result_json(c,23);
    check("ordinary inspection never exposes stale equipment",equipment_json.find("\"equipment\":{\"state\":\"not_requested\",\"actors\":[]}")!=std::string::npos);
    c.command={RosterAction::input_profile,-1};c.input_profile_status=PlayerReadStatus::Captured;
    c.input_profile.manager_count=1;c.input_profile.horizontal_action_id=101;c.input_profile.vertical_action_id=103;
    c.input_profile.definitions={{7,{2,9}},{11,{}}};
    auto profile_json=result_json(c,24);
    check("input profile serializes definitions and actual actions",profile_json.find("\"action\":\"inputProfile\"")!=std::string::npos&&
        profile_json.find("\"inputProfile\":{\"state\":\"captured\",\"managerCount\":1,\"horizontalActionId\":101,\"verticalActionId\":103,\"definitions\":[{\"sourceId\":7,\"axisIds\":[2,9]},{\"sourceId\":11,\"axisIds\":[]}]}")!=std::string::npos);
    std::cout << "ROSTER_INPUT_PROFILE_JSON " << profile_json << '\n';
    c.input_profile_status=PlayerReadStatus::Error;profile_json=result_json(c,25);
    check("input profile error suppresses stale definitions",profile_json.find("\"inputProfile\":{\"state\":\"error\"}")!=std::string::npos);
    c.input_profile_status=PlayerReadStatus::NotReady;profile_json=result_json(c,26);
    check("input profile not ready suppresses stale definitions",profile_json.find("\"inputProfile\":{\"state\":\"not_ready\"}")!=std::string::npos);
    c.command={RosterAction::inspect,-1};c.input_profile_status=PlayerReadStatus::Captured;profile_json=result_json(c,27);
    check("ordinary inspection never exposes stale input profile",profile_json.find("\"inputProfile\":{\"state\":\"not_requested\"}")!=std::string::npos);
    if (!std::filesystem::create_directory(directory)) return 2;
    try {
        const auto request = directory / "request";
        { std::ofstream out(request, std::ios::binary); out << "inspect"; }
        check("bounded request read", read_command(request) == "inspect");
        const auto output = directory / "state.json";
        publish_result(output, text);
        bool refused = false;
        try { publish_result(output, "replacement"); } catch (const std::exception&) { refused = true; }
        std::ifstream in(output, std::ios::binary);
        const std::string actual(std::istreambuf_iterator<char>{in}, {});
        check("published output cannot be replaced", refused && actual == text);
        in.close();
        std::filesystem::remove_all(directory);
    } catch (...) { std::filesystem::remove_all(directory); throw; }
    std::cout << "Roster command checks: " << count - failed << " passed, " << failed << " failed, " << count << " total\n";
    return failed ? 1 : 0;
}
