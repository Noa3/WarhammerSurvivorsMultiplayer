#include "wsm/coop_roster_probe.h"
#include "wsm/coop_roster.h"
#include "wsm/player_observer.h"
#include "wsm/window_hook.h"
#include <charconv>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace wsm {
namespace {
enum class RosterAction { inspect, enroll, rollback, select_player, copy_character, upgrade_context, upgrade_selection, upgrade_items };
struct RosterCommand { RosterAction action = RosterAction::inspect; std::int32_t player = -1; };
bool mutates_roster(RosterAction action) {
    return action==RosterAction::enroll || action==RosterAction::rollback ||
           action==RosterAction::select_player || action==RosterAction::copy_character;
}
template<class T> bool number(std::string_view text, T& output) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool parse_command(std::string_view text, DWORD expected_pid, RosterCommand& output) {
    if (!expected_pid || text.empty() || text.size() > 96) return false;
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
    RosterCommand result;
    if (text == "inspect") { output = result; return true; }
    constexpr std::string_view context_prefix="upgradeContext:";
    constexpr std::string_view selection_prefix="upgradeSelection:";
    constexpr std::string_view items_prefix="upgradeItems:";
    const bool selection_query=text.substr(0,selection_prefix.size())==selection_prefix;
    const bool items_query=text.substr(0,items_prefix.size())==items_prefix;
    if(text.substr(0,context_prefix.size())==context_prefix || selection_query || items_query) {
        DWORD pid=0;
        const auto prefix=items_query?items_prefix:selection_query?selection_prefix:context_prefix;
        if(!number(text.substr(prefix.size()),pid)||pid!=expected_pid)return false;
        result.action=items_query?RosterAction::upgrade_items:selection_query?RosterAction::upgrade_selection:RosterAction::upgrade_context;output=result;return true;
    }
    const bool enroll = text.substr(0, 7) == "enroll:";
    const bool select = text.substr(0, 7) == "select:";
    const bool copy = text.substr(0, 5) == "copy:";
    const bool rollback = text.substr(0, 9) == "rollback:";
    if (!enroll && !select && !copy && !rollback) return false;
    text.remove_prefix(rollback ? 9 : copy ? 5 : 7);
    const auto separator = text.find(':');
    const auto pid_text = rollback ? text : text.substr(0, separator);
    DWORD pid = 0;
    if (!number(pid_text, pid) || pid != expected_pid) return false;
    if (enroll || select || copy) {
        if (separator == std::string_view::npos || !number(text.substr(separator + 1), result.player) || result.player < 0) return false;
        result.action = enroll ? RosterAction::enroll : copy ? RosterAction::copy_character : RosterAction::select_player;
    } else result.action = RosterAction::rollback;
    output = result;
    return true;
}
struct ProbeWindow { HWND hwnd = nullptr; DWORD thread = 0; };
BOOL CALLBACK find_probe_window(HWND hwnd, LPARAM raw) {
    DWORD pid = 0;
    const auto thread = GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t name[128]{};
    if (GetClassNameW(hwnd, name, 128) && std::wcscmp(name, L"UnityWndClass") == 0) {
        *reinterpret_cast<ProbeWindow*>(raw) = {hwnd, thread};
        return FALSE;
    }
    return TRUE;
}
std::filesystem::path probe_directory() {
    const DWORD size = GetEnvironmentVariableW(L"WSM_COOP_ROSTER_DIR", nullptr, 0);
    if (!size || size > 32768) throw std::invalid_argument("Roster directory is missing or too long");
    std::vector<wchar_t> text(size);
    if (GetEnvironmentVariableW(L"WSM_COOP_ROSTER_DIR", text.data(), size) != size - 1)
        throw std::invalid_argument("Roster directory changed during setup");
    std::filesystem::path path(text.data());
    if (!path.is_absolute()) throw std::invalid_argument("Roster directory must be absolute");
    for (auto ancestor = path; !ancestor.empty();) {
        const auto flags = GetFileAttributesW(ancestor.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::invalid_argument("Roster directory needs existing non-linked directory ancestors");
        const auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    if (!std::filesystem::is_empty(path)) throw std::invalid_argument("Choose a fresh empty roster directory");
    return path;
}
bool request_present(const std::filesystem::path& path) {
    const auto flags = GetFileAttributesW(path.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return false;
        throw std::runtime_error("Roster request path is unavailable");
    }
    if (flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Roster request must be a regular file");
    return true;
}
std::string read_command(const std::filesystem::path& path) {
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open roster request");
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER length{};
    const bool valid = GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= 96;
    char buffer[96]{};
    DWORD count = 0;
    const bool complete = valid && ReadFile(file, buffer, static_cast<DWORD>(length.QuadPart), &count, nullptr) &&
        count == static_cast<DWORD>(length.QuadPart);
    const bool closed = CloseHandle(file) != FALSE;
    if (!complete || !closed) throw std::runtime_error("Roster request must be a complete bounded regular file");
    return {buffer, count};
}
void publish_result(const std::filesystem::path& path, const std::string& text) {
    auto temporary = path;
    temporary += L".tmp";
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot exclusively create roster output");
    DWORD count = 0;
    const bool complete = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) &&
        count == text.size() && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    if (!complete || !closed) throw std::runtime_error("Roster write failed; partial output retained");
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish roster output without replacing existing bytes");
}
struct ProbeContext {
    CoopRoster roster;
    PlayerStateReader player;
    MainThreadGuard guard;
    ProbeWindow window;
    RosterCommand command;
    CoopRosterSnapshot snapshot;
    CoopCharacterConfiguration configuration;
    bool configuration_captured = false;
    SceneStateSnapshot scene;
    UpgradeContextSnapshot upgrade;
    MainThreadIdentity identity;
    CoopRosterStatus status = CoopRosterStatus::Error;
    PlayerReadStatus player_status = PlayerReadStatus::Error;
    PlayerReadStatus upgrade_status = PlayerReadStatus::Error;
    bool action_succeeded = false;
    std::string error;
    std::string action_error, roster_error, configuration_error, player_error, upgrade_error;
};
void execute_command(void* raw) {
    auto& c = *static_cast<ProbeContext*>(raw);
    try {
        if (!c.guard.verify(c.window.thread, 0, c.identity, c.action_error)) { c.error = c.action_error; return; }
        c.player_status = c.player.read(c.scene, c.player_error);
        if(c.command.action==RosterAction::upgrade_context || c.command.action==RosterAction::upgrade_selection || c.command.action==RosterAction::upgrade_items) {
            c.upgrade_status=c.command.action==RosterAction::upgrade_items?c.player.read_upgrade_items(c.upgrade,c.upgrade_error):c.command.action==RosterAction::upgrade_selection?
                c.player.read_upgrade_selection(c.upgrade,c.upgrade_error):c.player.read_upgrade_context(c.upgrade,c.upgrade_error);
            c.action_succeeded=c.upgrade_status==PlayerReadStatus::Captured;
        } else if (mutates_roster(c.command.action)) {
            // This supplements, but does not replace, the trusted operator's
            // explicit outside-run precondition: NotReady can have other causes.
            if (c.player_status != PlayerReadStatus::NotReady) {
                c.action_error = "Roster mutation requires operator-established outside-run phase; active or failed player read refused";
            } else {
                c.action_succeeded = c.command.action == RosterAction::enroll ?
                    c.roster.enroll(c.command.player, c.action_error) : c.command.action == RosterAction::select_player ?
                    c.roster.select_menu_player(c.command.player, c.action_error) : c.command.action == RosterAction::copy_character ?
                    c.roster.copy_menu_character(c.command.player, c.action_error) : c.roster.rollback(c.action_error);
            }
        }
        c.status = c.roster.inspect(c.snapshot, c.roster_error);
        if (c.status == CoopRosterStatus::NotReady && c.roster_error.empty()) c.roster_error = "Roster not ready";
        c.configuration_captured = c.roster.inspect_character_configuration(c.configuration, c.configuration_error);
        if (!c.configuration_captured && c.configuration_error.empty()) c.configuration_error = "Character configuration unavailable";
        if (c.command.action == RosterAction::inspect) c.action_succeeded = c.status == CoopRosterStatus::Captured;
        c.error = !c.action_error.empty() ? c.action_error : !c.roster_error.empty() ? c.roster_error :
            !c.configuration_error.empty() ? c.configuration_error : !c.upgrade_error.empty() ? c.upgrade_error : c.player_error;
    } catch (const std::exception& e) { c.error = c.action_error = e.what(); c.status = CoopRosterStatus::Error; c.upgrade_status=PlayerReadStatus::Error; c.action_succeeded = false; c.configuration_captured = false; }
    catch (...) { c.error = c.action_error = "Unknown native roster callback failure"; c.status = CoopRosterStatus::Error; c.upgrade_status=PlayerReadStatus::Error; c.action_succeeded = false; c.configuration_captured = false; }
}
std::string quoted_json(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 0x20) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    result += '"';
    return result;
}
std::string result_json(const ProbeContext& c, unsigned sample) {
    const char* state = c.status == CoopRosterStatus::Captured ? "captured" : c.status == CoopRosterStatus::NotReady ? "not_ready" : "error";
    const char* action = c.command.action == RosterAction::inspect ? "inspect" : c.command.action == RosterAction::enroll ? "enroll" : c.command.action == RosterAction::select_player ? "select" : c.command.action == RosterAction::copy_character ? "copyCharacter" : c.command.action == RosterAction::upgrade_context ? "upgradeContext" : c.command.action == RosterAction::upgrade_selection ? "upgradeSelection" : c.command.action == RosterAction::upgrade_items ? "upgradeItems" : "rollback";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schemaVersion\":1,\"sample\":" << sample << ",\"action\":\"" << action
        << "\",\"actionSucceeded\":" << (c.action_succeeded ? "true" : "false")
        << ",\"state\":\"" << state << "\",\"pendingEnrollment\":" << (c.roster.has_pending_enrollment() ? "true" : "false");
    auto ids = [&](const char* name, const std::vector<std::int32_t>& values) {
        out << ",\"" << name << "\":[";
        if (c.status == CoopRosterStatus::Captured)
            for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; out << values[i]; }
        out << ']';
    };
    ids("configuredIds", c.snapshot.configured_ids);
    ids("enrolledIds", c.snapshot.enrolled_ids);
    out << ",\"characterConfiguration\":{\"captured\":" << (c.configuration_captured ? "true" : "false");
    if (c.configuration_captured) {
        out << ",\"coopConfigReferencePresent\":" << (c.configuration.coop_config_present ? "true" : "false")
            << ",\"isMultiplayer\":" << (c.configuration.is_multiplayer ? "true" : "false")
            << ",\"selectedPlayerId\":" << c.configuration.selected_player_id
            << ",\"selectedPlayerIndex\":" << c.configuration.selected_player_index << ",\"selectedTypes\":[";
        for (std::size_t i = 0; i < c.configuration.selected_types.size(); ++i) { if (i) out << ','; out << c.configuration.selected_types[i]; }
        out << ']';
    }
    out << '}';
    const bool requested=c.command.action==RosterAction::upgrade_context || c.command.action==RosterAction::upgrade_selection || c.command.action==RosterAction::upgrade_items;
    const bool captured=requested && c.upgrade_status==PlayerReadStatus::Captured;
    const char* upgrade_state=!requested?"not_requested":captured?"captured":c.upgrade_status==PlayerReadStatus::NotReady?"not_ready":"error";
    out << ",\"upgradeContext\":{\"state\":\"" << upgrade_state << '"';
    if(captured) {
        out << ",\"levelUpPageCount\":" << c.upgrade.level_up_page_count
            << ",\"gameSessionReferencePresent\":" << (c.upgrade.page_game_session_present?"true":"false")
            << ",\"activeCharacterReferencePresent\":" << (c.upgrade.active_character_present?"true":"false")
            << ",\"activeCharacterSlot\":" << c.upgrade.active_character_slot
            << ",\"levelUpFactoryReferencePresent\":" << (c.upgrade.level_up_factory_present?"true":"false")
            << ",\"choiceContainerGroupReferencePresent\":" << (c.upgrade.choice_group_present?"true":"false")
            << ",\"choiceContainerItemsReferencePresent\":" << (c.upgrade.choice_group_items_present?"true":"false")
            << ",\"choiceContainerItemReferences\":[";
        for(std::size_t i=0;i<c.upgrade.choice_group_item_references.size();++i) {
            if(i)out << ',';
            out << (c.upgrade.choice_group_item_references[i]?"true":"false");
        }
        out << ']';
        if(c.command.action==RosterAction::upgrade_selection || c.command.action==RosterAction::upgrade_items) {
            out << ",\"choiceSelection\":{\"observed\":" << (c.upgrade.choice_selection_observed?"true":"false")
                << ",\"selectedContainerSlot\":" << c.upgrade.selected_choice_container_slot << ",\"unityAlive\":[";
            for(std::size_t i=0;i<c.upgrade.choice_group_item_unity_alive.size();++i) {
                if(i)out << ',';out << (c.upgrade.choice_group_item_unity_alive[i]?"true":"false");
            }
            out << "],\"activeInHierarchy\":[";
            for(std::size_t i=0;i<c.upgrade.choice_group_item_active.size();++i) {
                if(i)out << ',';out << (c.upgrade.choice_group_item_active[i]?"true":"false");
            }
            out << "]}";
        }
        if(c.command.action==RosterAction::upgrade_items) {
            out << ",\"choiceItems\":{\"observed\":" << (c.upgrade.choice_items_observed?"true":"false")
                << ",\"childCount\":" << c.upgrade.choice_child_count << ",\"items\":[";
            for(std::size_t i=0;i<c.upgrade.choice_items.size();++i) {
                const auto& item=c.upgrade.choice_items[i];if(i)out << ',';
                out << "{\"childReferencePresent\":" << (item.child_present?"true":"false")
                    << ",\"childUnityAlive\":" << (item.child_unity_alive?"true":"false")
                    << ",\"componentReferencePresent\":" << (item.component_present?"true":"false")
                    << ",\"componentUnityAlive\":" << (item.component_unity_alive?"true":"false")
                    << ",\"fieldsObserved\":" << (item.fields_observed?"true":"false")
                    << ",\"weaponType\":" << item.weapon_type
                    << ",\"dataReferencePresent\":" << (item.data_present?"true":"false")
                    << ",\"isNew\":" << (item.is_new?"true":"false") << '}';
            }
            out << "]}";
        }
    }
    out << ",\"actors\":[";
    if(captured)for(std::size_t i=0;i<c.upgrade.actors.size();++i) {
        const auto& actor=c.upgrade.actors[i];if(i)out << ',';
        out << "{\"slot\":" << actor.slot << ",\"inputId\":" << actor.input_id << ",\"characterType\":" << actor.character_type
            << ",\"weaponsManagerReferencePresent\":" << (actor.weapons_manager_present?"true":"false")
            << ",\"accessoriesManagerReferencePresent\":" << (actor.accessories_manager_present?"true":"false")
            << ",\"accessoriesCollectionReferencePresent\":" << (actor.accessories_collection_present?"true":"false") << '}';
    }
    out << "]}";
    out << ",\"playerObservation\":" << player_observation_json(c.player_status, c.scene, c.identity, sample)
        << ",\"errors\":{\"action\":" << quoted_json(c.action_error)
        << ",\"roster\":" << quoted_json(c.roster_error)
        << ",\"characterConfiguration\":" << quoted_json(c.configuration_error)
        << ",\"playerObservation\":" << quoted_json(c.player_error)
        << ",\"upgradeContext\":" << quoted_json(c.upgrade_error) << "}}";
    return out.str();
}
}
void run_coop_roster_probe(HMODULE bridge, HMODULE game, Il2CppApi api,
                           Il2CppDomain* domain, Il2CppThread* thread,
                           void (*log)(const std::string&)) {
    struct Attachment {
        Il2CppApi& api; Il2CppThread* thread;
        void detach() { if (thread) { api.thread_detach(thread); thread = nullptr; } }
        ~Attachment() { detach(); }
    } attachment{api, thread};
    std::unique_ptr<ProbeContext> context;
    try {
        const auto directory = probe_directory();
        context = std::make_unique<ProbeContext>();
        auto& c = *context;
        const auto startup = GetTickCount64() + 45000;
        while (!c.window.hwnd && GetTickCount64() < startup) {
            EnumWindows(find_probe_window, reinterpret_cast<LPARAM>(&c.window));
            if (!c.window.hwnd) Sleep(50);
        }
        if (!c.window.hwnd) throw std::runtime_error("No owned Unity window for roster probe");
        if (!api.resolve_probe(game, c.error) || !MainThreadGuard::create(api, domain, c.guard, c.error) ||
            !c.roster.initialize(game, api, domain, c.window.thread, 0, c.error) ||
            !c.player.initialize(game, api, domain, c.window.thread, 0, c.error))
            throw std::runtime_error("Roster/player metadata contract: " + c.error);
        attachment.detach();
        log("WSM_ROSTER_READY: metadata worker detached; eight bounded requests; mutation requires explicit operator-established outside-run phase");
        const auto deadline = GetTickCount64() + 15ull * 60 * 1000;
        for (unsigned sample = 1; sample <= 8 && IsWindow(c.window.hwnd); ++sample) {
            std::ostringstream number;
            number << std::setw(6) << std::setfill('0') << sample;
            const auto request = directory / ("request-" + number.str());
            while (IsWindow(c.window.hwnd) && GetTickCount64() < deadline && !request_present(request)) Sleep(50);
            if (!IsWindow(c.window.hwnd) || GetTickCount64() >= deadline) break;
            if (!parse_command(read_command(request), GetCurrentProcessId(), c.command))
                throw std::runtime_error("Invalid roster command or mismatched own-process ID");
            c.snapshot = {}; c.configuration = {}; c.configuration_captured = false; c.scene = {}; c.upgrade = {}; c.identity = {}; c.error.clear();
            c.action_error.clear(); c.roster_error.clear(); c.configuration_error.clear(); c.player_error.clear(); c.upgrade_error.clear();
            c.status = CoopRosterStatus::Error; c.player_status = PlayerReadStatus::Error; c.upgrade_status = PlayerReadStatus::Error; c.action_succeeded = false;
            const auto status = run_window_hook(bridge, c.window.hwnd, c.window.thread, execute_command, &c, 2000);
            if (status != HookResult::completed) { c.action_succeeded = false; c.status = CoopRosterStatus::Error; c.player_status = PlayerReadStatus::Error; c.upgrade_status=PlayerReadStatus::Error; c.error = c.action_error = "Owned Unity roster callback did not complete"; }
            publish_result(directory / ("state-" + number.str() + ".json"), result_json(c, sample));
            log("WSM_ROSTER_RESULT=" + std::to_string(sample) + " success=" + std::to_string(c.action_succeeded) +
                " pending=" + std::to_string(c.roster.has_pending_enrollment()));
            if (!c.error.empty()) log("WSM_ROSTER_ERROR: " + c.error);
        }
        log("WSM_ROSTER_STOP: request limit, deadline or owned-window exit");
    } catch (const std::exception& e) { log(std::string("ERROR: roster probe stopped: ") + e.what()); }
    if (context && context->roster.has_pending_enrollment())
        log("WSM_ROSTER_PENDING: no automatic engine removal; owned roots retained until process exit");
}
}
