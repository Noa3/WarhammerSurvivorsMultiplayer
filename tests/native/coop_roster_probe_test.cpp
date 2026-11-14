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
    command = {RosterAction::enroll, 41};
    check("wrong PID leaves command unchanged", !parse_command("enroll:1235:0", 1234, command) && command.action == RosterAction::enroll && command.player == 41);
    for (const auto* text : {"enroll:1234:-1", "enroll:1234:2147483648", "enroll:1234:41:0", "enroll:1234", "enroll:1234:", "rollback:1234:0", "rollback:-1234", "inspect\nrollback:1234", "unknown"})
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
    text = result_json(c, 3);
    check("configuration preserves selected enum values", text.find("\"coopConfigReferencePresent\":true") != std::string::npos && text.find("\"selectedTypes\":[37,0]") != std::string::npos);
    std::cout << "ROSTER_ERROR_JSON " << text << '\n';
    const auto directory = std::filesystem::path(argv[1]) / ("roster-command-" + std::to_string(GetCurrentProcessId()));
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
