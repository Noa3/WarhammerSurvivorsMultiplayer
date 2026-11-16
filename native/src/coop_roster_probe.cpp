#include "wsm/coop_roster_probe.h"
#include "wsm/coop_roster.h"
#include "wsm/player_observer.h"
#include "wsm/rewired_input_profile.h"
#include "wsm/rewired_control_pulse.h"
#include "wsm/upgrade_pass_hook.h"
#include "wsm/upgrade_pass_profile.h"
#include "wsm/upgrade_choice_controller.h"
#include <cmath>
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
enum class RosterAction { inspect, enroll, rollback, select_player, copy_character, upgrade_context, upgrade_selection, upgrade_items, upgrade_hint, equipment, input_profile, control_environment, control_open, control_pulse, control_close, upgrade_hook_inspect, upgrade_hook_install, upgrade_hook_remove, upgrade_offer, upgrade_choose, upgrade_choice_close };
// Keep patch/recovery ownership past the bounded diagnostic worker lifetime.
UpgradePassHook process_upgrade_hook;
struct RosterCommand {
    RosterAction action = RosterAction::inspect;
    std::int32_t player = -1;
    RewiredControlOptions control;
    float x = 0, y = 0;
    std::uint32_t duration = 0;
    std::uint64_t choice_token = 0;
    std::int32_t choice_index = -1, choice_weapon = 0;
};
bool control_opt_in() {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(L"WSM_COOP_CONTROL_OPT_IN",value,8)==1 && value[0]==L'1';
}
bool upgrade_hook_opt_in() {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(L"WSM_UPGRADE_PASS_HOOK_OPT_IN",value,8)==1 && value[0]==L'1';
}
bool upgrade_choice_opt_in() {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(L"WSM_UPGRADE_CHOICE_OPT_IN",value,8)==1 && value[0]==L'1';
}
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
    for(const auto& route:std::initializer_list<std::pair<std::string_view,RosterAction>>{
        {"controlEnvironment:",RosterAction::control_environment},{"controlClose:",RosterAction::control_close},
        {"upgradeHook:",RosterAction::upgrade_hook_inspect},{"upgradeHookInstall:",RosterAction::upgrade_hook_install},
        {"upgradeHookRemove:",RosterAction::upgrade_hook_remove},{"upgradeChoiceClose:",RosterAction::upgrade_choice_close}}) {
        if(text.substr(0,route.first.size())==route.first) {
            DWORD pid=0;if(!number(text.substr(route.first.size()),pid)||pid!=expected_pid)return false;
            result.action=route.second;output=result;return true;
        }
    }
    constexpr std::string_view offer_prefix="upgradeOffer:",choose_prefix="upgradeChoose:";
    const bool offer=text.substr(0,offer_prefix.size())==offer_prefix;
    const bool choose=text.substr(0,choose_prefix.size())==choose_prefix;
    if(offer||choose) {
        text.remove_prefix(offer?offer_prefix.size():choose_prefix.size());std::vector<std::string_view> parts;
        while(true){const auto p=text.find(':');parts.push_back(text.substr(0,p));if(p==std::string_view::npos)break;text.remove_prefix(p+1);if(parts.size()>5)return false;}
        DWORD pid=0;
        if(parts.size()!=(offer?2u:5u)||!number(parts[0],pid)||pid!=expected_pid)return false;
        if(offer) {
            if(!number(parts[1],result.player)||result.player<0)return false;
            result.action=RosterAction::upgrade_offer;
        } else {
            if(!number(parts[1],result.choice_token)||!result.choice_token||!number(parts[2],result.player)||result.player<0||
               !number(parts[3],result.choice_index)||result.choice_index<0||result.choice_index>=32||
               !number(parts[4],result.choice_weapon)||result.choice_weapon<=0)return false;
            result.action=RosterAction::upgrade_choose;
        }
        output=result;return true;
    }
    const bool control_open=text.substr(0,12)=="controlOpen:";
    const bool control_pulse=text.substr(0,13)=="controlPulse:";
    if(control_open||control_pulse) {
        text.remove_prefix(control_open?12:13);std::vector<std::string_view> parts;
        while(true){auto p=text.find(':');parts.push_back(text.substr(0,p));if(p==std::string_view::npos)break;text.remove_prefix(p+1);if(parts.size()>6)return false;}
        DWORD pid=0;if(parts.size()!=(control_open?6u:4u)||!number(parts[0],pid)||pid!=expected_pid)return false;
        if(control_open) {
            result.control.player_id=1;
            std::int32_t* fields[]={&result.control.source_id,&result.control.category_id,&result.control.layout_id,&result.control.horizontal_axis_id,&result.control.vertical_axis_id};
            for(unsigned i=0;i<5;++i)if(!number(parts[i+1],*fields[i])||*fields[i]<0)return false;
            if(result.control.horizontal_axis_id==result.control.vertical_axis_id)return false;
            result.action=RosterAction::control_open;
        } else {
            if(!number(parts[1],result.x)||!number(parts[2],result.y)||!number(parts[3],result.duration)||
                !std::isfinite(result.x)||!std::isfinite(result.y)||std::abs(result.x)>1||std::abs(result.y)>1||
                (result.x==0&&result.y==0)||result.duration<1||result.duration>250)return false;
            result.action=RosterAction::control_pulse;
        }
        output=result;return true;
    }
    constexpr std::string_view profile_prefix="inputProfile:";
    if(text.substr(0,profile_prefix.size())==profile_prefix) {
        DWORD pid=0;if(!number(text.substr(profile_prefix.size()),pid)||pid!=expected_pid)return false;
        result.action=RosterAction::input_profile;output=result;return true;
    }
    constexpr std::string_view equipment_prefix="equipment:";
    if(text.substr(0,equipment_prefix.size())==equipment_prefix) {
        DWORD pid=0;if(!number(text.substr(equipment_prefix.size()),pid)||pid!=expected_pid)return false;
        result.action=RosterAction::equipment;output=result;return true;
    }
    constexpr std::string_view context_prefix="upgradeContext:";
    constexpr std::string_view selection_prefix="upgradeSelection:";
    constexpr std::string_view items_prefix="upgradeItems:";
    constexpr std::string_view hint_prefix="upgradeHint:";
    const bool selection_query=text.substr(0,selection_prefix.size())==selection_prefix;
    const bool items_query=text.substr(0,items_prefix.size())==items_prefix;
    const bool hint_query=text.substr(0,hint_prefix.size())==hint_prefix;
    if(text.substr(0,context_prefix.size())==context_prefix || selection_query || items_query || hint_query) {
        DWORD pid=0;
        const auto prefix=hint_query?hint_prefix:items_query?items_prefix:selection_query?selection_prefix:context_prefix;
        if(!number(text.substr(prefix.size()),pid)||pid!=expected_pid)return false;
        result.action=hint_query?RosterAction::upgrade_hint:items_query?RosterAction::upgrade_items:selection_query?RosterAction::upgrade_selection:RosterAction::upgrade_context;output=result;return true;
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
    HMODULE module = nullptr;
    Il2CppApi api;
    Il2CppDomain* domain = nullptr;
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
    EquipmentSnapshot equipment;
    RewiredInputProfileSnapshot input_profile;
    std::unique_ptr<RewiredControllerSession> control;
    RewiredControlEnvironment control_environment;
    RewiredPulseResult pulse;
    PlayerReadStatus control_status = PlayerReadStatus::Error;
    std::string control_error;
    bool startup_patch_checked=false,startup_patch_verified=false;
    std::string startup_patch_error;
    std::unique_ptr<UpgradeChoiceController> choice;
    UpgradeChoiceOffer choice_offer;
    UpgradeChoiceResult choice_result;
    PlayerReadStatus choice_status = PlayerReadStatus::Error;

    MainThreadIdentity identity;
    CoopRosterStatus status = CoopRosterStatus::Error;
    PlayerReadStatus player_status = PlayerReadStatus::Error;
    PlayerReadStatus upgrade_status = PlayerReadStatus::Error;
    PlayerReadStatus equipment_status = PlayerReadStatus::Error;
    PlayerReadStatus input_profile_status = PlayerReadStatus::Error;
    bool action_succeeded = false;
    std::string error;
    std::string action_error, roster_error, configuration_error, player_error, upgrade_error;
};
bool ensure_control(ProbeContext& c) {
    if(c.control)return true;
    auto session=std::make_unique<RewiredControllerSession>();
    if(!session->initialize(c.module,c.api,c.domain,c.window.thread,0,c.action_error))return false;
    c.control=std::move(session);return true;
}
void execute_command(void* raw) {
    auto& c = *static_cast<ProbeContext*>(raw);
    c.choice_offer={};c.choice_result={};c.choice_status=PlayerReadStatus::Error;
    if(c.command.action==RosterAction::control_pulse) {
        // A deadline cannot clear an axis when the Unity thread is unresponsive.
        // Do not admit nonzero writes without a persistent cleanup/retry owner.
        c.action_succeeded=false;
        c.error=c.action_error="Input pulse disabled: persistent cleanup and retry owner unavailable";
        return;
    }
    if((c.command.action==RosterAction::upgrade_offer||c.command.action==RosterAction::upgrade_choose)&&!upgrade_choice_opt_in()) {
        c.action_succeeded=false;c.error=c.action_error="Upgrade selection requires separate WSM_UPGRADE_CHOICE_OPT_IN=1";return;
    }
    if(c.command.action==RosterAction::upgrade_hook_install || c.command.action==RosterAction::upgrade_hook_remove) {
        // Thread identity and a stopped run do not establish code quiescence.
        // Keep both writes unreachable until a verified safe window exists.
        c.action_succeeded=false;
        c.action_error=upgrade_hook_opt_in()?
            "Upgrade hook mutation disabled: no verified quiescent patch window":
            "Upgrade hook mutation requires separate WSM_UPGRADE_PASS_HOOK_OPT_IN=1";
        c.error=c.action_error;
        return;
    }
    try {
        if (!c.guard.verify(c.window.thread, 0, c.identity, c.action_error)) { c.error = c.action_error; return; }
        c.player_status = c.player.read(c.scene, c.player_error);
        if(c.command.action==RosterAction::upgrade_choice_close) {
            c.action_succeeded=!c.choice||c.choice->close(c.action_error);
            if(c.action_succeeded)c.choice.reset();
        } else if(c.command.action==RosterAction::upgrade_offer||c.command.action==RosterAction::upgrade_choose) {
            if(c.player_status!=PlayerReadStatus::Captured)c.action_error="A captured active scene is required for upgrade selection";
            else {
                if(!c.choice) {
                    auto controller=std::make_unique<UpgradeChoiceController>();
                    if(controller->initialize(c.module,c.api,c.domain,c.window.thread,0,c.action_error))c.choice=std::move(controller);
                }
                if(c.choice&&c.command.action==RosterAction::upgrade_offer) {
                    c.choice_status=c.choice->capture(c.command.player,c.choice_offer,c.action_error);
                    c.action_succeeded=c.choice_status==PlayerReadStatus::Captured;
                } else if(c.choice)c.action_succeeded=c.choice->select(c.command.choice_token,c.command.player,c.command.choice_index,c.command.choice_weapon,c.choice_result,c.action_error);
            }
        } else if(c.command.action==RosterAction::upgrade_hook_inspect) {
            c.startup_patch_checked=true;
            c.startup_patch_verified=verify_upgrade_pass_image(c.module,true,c.startup_patch_error);
            c.action_succeeded=true;
        } else if(c.command.action==RosterAction::control_environment) {
            if(ensure_control(c)) {c.control_status=c.control->inspect(c.control_environment,c.action_error);c.action_succeeded=c.control_status==PlayerReadStatus::Captured;}
        } else if(c.command.action==RosterAction::control_open) {
            if(!control_opt_in())c.action_error="Controller mutation requires explicit WSM_COOP_CONTROL_OPT_IN=1";
            else if(ensure_control(c))c.action_succeeded=c.control->open(c.command.control,c.action_error);
        } else if(c.command.action==RosterAction::control_close) {
            c.action_succeeded=!c.control||c.control->close(c.action_error);
        } else if(c.command.action==RosterAction::upgrade_context || c.command.action==RosterAction::upgrade_selection || c.command.action==RosterAction::upgrade_items || c.command.action==RosterAction::upgrade_hint) {
            c.upgrade_status=c.command.action==RosterAction::upgrade_hint?c.player.read_upgrade_hint(c.upgrade,c.upgrade_error):c.command.action==RosterAction::upgrade_items?c.player.read_upgrade_items(c.upgrade,c.upgrade_error):c.command.action==RosterAction::upgrade_selection?
                c.player.read_upgrade_selection(c.upgrade,c.upgrade_error):c.player.read_upgrade_context(c.upgrade,c.upgrade_error);
            c.action_succeeded=c.upgrade_status==PlayerReadStatus::Captured;
        } else if(c.command.action==RosterAction::input_profile) {
            c.input_profile_status=read_rewired_input_profile(c.module,c.api,c.domain,c.window.thread,0,c.input_profile,c.action_error);
            c.action_succeeded=c.input_profile_status==PlayerReadStatus::Captured;
        } else if(c.command.action==RosterAction::equipment) {
            c.equipment_status=c.player.read_equipment(c.equipment,c.action_error);
            c.action_succeeded=c.equipment_status==PlayerReadStatus::Captured;
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
    } catch (const std::exception& e) { c.error = c.action_error = e.what(); c.status = CoopRosterStatus::Error; c.upgrade_status=PlayerReadStatus::Error; c.equipment_status=PlayerReadStatus::Error; c.input_profile_status=PlayerReadStatus::Error; c.action_succeeded = false; c.configuration_captured = false; }
    catch (...) { c.error = c.action_error = "Unknown native roster callback failure"; c.status = CoopRosterStatus::Error; c.upgrade_status=PlayerReadStatus::Error; c.equipment_status=PlayerReadStatus::Error; c.input_profile_status=PlayerReadStatus::Error; c.action_succeeded = false; c.configuration_captured = false; }
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
    const char* action = c.command.action == RosterAction::inspect ? "inspect" : c.command.action == RosterAction::enroll ? "enroll" : c.command.action == RosterAction::select_player ? "select" : c.command.action == RosterAction::copy_character ? "copyCharacter" : c.command.action == RosterAction::upgrade_context ? "upgradeContext" : c.command.action == RosterAction::upgrade_selection ? "upgradeSelection" : c.command.action == RosterAction::upgrade_items ? "upgradeItems" : c.command.action == RosterAction::upgrade_hint ? "upgradeHint" : c.command.action==RosterAction::equipment?"equipment":c.command.action==RosterAction::input_profile?"inputProfile":"rollback";
    if(c.command.action==RosterAction::control_environment)action="controlEnvironment";
    if(c.command.action==RosterAction::control_open)action="controlOpen";
    if(c.command.action==RosterAction::control_pulse)action="controlPulse";
    if(c.command.action==RosterAction::control_close)action="controlClose";
    if(c.command.action==RosterAction::upgrade_hook_inspect)action="upgradeHook";
    if(c.command.action==RosterAction::upgrade_hook_install)action="upgradeHookInstall";
    if(c.command.action==RosterAction::upgrade_hook_remove)action="upgradeHookRemove";
    if(c.command.action==RosterAction::upgrade_offer)action="upgradeOffer";
    if(c.command.action==RosterAction::upgrade_choose)action="upgradeChoose";
    if(c.command.action==RosterAction::upgrade_choice_close)action="upgradeChoiceClose";
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
    const bool requested=c.command.action==RosterAction::upgrade_context || c.command.action==RosterAction::upgrade_selection || c.command.action==RosterAction::upgrade_items || c.command.action==RosterAction::upgrade_hint;
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
        if(c.command.action==RosterAction::upgrade_selection || c.command.action==RosterAction::upgrade_items || c.command.action==RosterAction::upgrade_hint) {
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
        if(c.command.action==RosterAction::upgrade_items || c.command.action==RosterAction::upgrade_hint) {
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
    if(captured && c.command.action==RosterAction::upgrade_hint) {
        out << ",\"fourthOptionHint\":{\"observed\":" << (c.upgrade.fourth_option_hint_observed?"true":"false")
            << ",\"referencePresent\":" << (c.upgrade.fourth_option_hint_present?"true":"false")
            << ",\"unityAlive\":" << (c.upgrade.fourth_option_hint_unity_alive?"true":"false")
            << ",\"childComparisonComplete\":" << (c.upgrade.fourth_option_hint_comparison_complete?"true":"false")
            << ",\"childSlot\":" << c.upgrade.fourth_option_hint_child_slot << '}';
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
    const bool equipment_requested=c.command.action==RosterAction::equipment;
    const bool equipment_captured=equipment_requested&&c.equipment_status==PlayerReadStatus::Captured;
    const char* equipment_state=!equipment_requested?"not_requested":equipment_captured?"captured":c.equipment_status==PlayerReadStatus::NotReady?"not_ready":"error";
    out << ",\"equipment\":{\"state\":\"" << equipment_state << "\",\"actors\":[";
    if(equipment_captured)for(std::size_t i=0;i<c.equipment.actors.size();++i) {
        const auto& actor=c.equipment.actors[i];if(i)out << ',';
        out << "{\"slot\":" << actor.slot << ",\"inputId\":" << actor.input_id << ",\"characterType\":" << actor.character_type
            << ",\"managerReferencePresent\":" << (actor.manager_present?"true":"false")
            << ",\"collectionReferencePresent\":" << (actor.collection_present?"true":"false") << ",\"items\":[";
        for(std::size_t j=0;j<actor.items.size();++j) {
            const auto& item=actor.items[j];if(j)out << ',';
            out << "{\"referencePresent\":" << (item.reference_present?"true":"false")
                << ",\"isWeapon\":" << (item.is_weapon?"true":"false")
                << ",\"fieldsObserved\":" << (item.fields_observed?"true":"false")
                << ",\"weaponType\":" << item.weapon_type
                << ",\"ownerReferencePresent\":" << (item.owner_present?"true":"false")
                << ",\"ownerSlot\":" << item.owner_slot << ",\"runtimeType\":" << quoted_json(item.runtime_type) << '}';
        }
        out << "]}";
    }
    out << "]}";
    const bool profile_requested=c.command.action==RosterAction::input_profile;
    const bool profile_captured=profile_requested&&c.input_profile_status==PlayerReadStatus::Captured;
    const char* profile_state=!profile_requested?"not_requested":profile_captured?"captured":c.input_profile_status==PlayerReadStatus::NotReady?"not_ready":"error";
    out << ",\"inputProfile\":{\"state\":\"" << profile_state << '"';
    if(profile_captured) {
        out << ",\"managerCount\":" << c.input_profile.manager_count
            << ",\"horizontalActionId\":" << c.input_profile.horizontal_action_id
            << ",\"verticalActionId\":" << c.input_profile.vertical_action_id << ",\"definitions\":[";
        for(std::size_t i=0;i<c.input_profile.definitions.size();++i) {
            const auto& definition=c.input_profile.definitions[i];if(i)out << ',';
            out << "{\"sourceId\":" << definition.source_id << ",\"axisIds\":[";
            for(std::size_t j=0;j<definition.axis_ids.size();++j){if(j)out << ',';out << definition.axis_ids[j];}
            out << "]}";
        }
        out << ']';
    }
    out << '}';
    const bool environment_requested=c.command.action==RosterAction::control_environment;
    out << ",\"controlEnvironment\":{\"state\":" << quoted_json(!environment_requested?"not_requested":c.control_status==PlayerReadStatus::Captured?"captured":c.control_status==PlayerReadStatus::NotReady?"not_ready":"error");
    if(environment_requested&&c.control_status==PlayerReadStatus::Captured) {
        out<<",\"horizontalActionId\":"<<c.control_environment.horizontal_action_id<<",\"verticalActionId\":"<<c.control_environment.vertical_action_id;
        const auto named=[&](const char* label,const std::vector<RewiredNamedId>& values){out<<",\""<<label<<"\":[";for(std::size_t i=0;i<values.size();++i){if(i)out<<',';out<<"{\"id\":"<<values[i].id<<",\"name\":"<<quoted_json(values[i].name)<<'}';}out<<']';};
        named("categories",c.control_environment.categories);named("layouts",c.control_environment.layouts);
        out<<",\"players\":[";
        for(std::size_t i=0;i<c.control_environment.players.size();++i){if(i)out<<',';const auto& p=c.control_environment.players[i];out<<"{\"playerId\":"<<p.player_id<<",\"x\":"<<p.horizontal<<",\"y\":"<<p.vertical<<",\"maps\":[";
            for(std::size_t j=0;j<p.maps.size();++j){if(j)out<<',';const auto& m=p.maps[j];out<<"{\"id\":"<<m.id<<",\"controllerId\":"<<m.controller_id<<",\"controllerType\":"<<m.controller_type<<",\"categoryId\":"<<m.category_id<<",\"layoutId\":"<<m.layout_id<<",\"enabled\":"<<(m.enabled?"true":"false")<<",\"bindings\":[";
                for(std::size_t n=0;n<m.bindings.size();++n){if(n)out<<',';const auto& b=m.bindings[n];out<<"{\"actionId\":"<<b.action_id<<",\"elementId\":"<<b.element_id<<",\"elementType\":"<<b.element_type<<",\"axisRange\":"<<b.axis_range<<",\"inverted\":"<<(b.inverted?"true":"false")<<'}';}out<<"]}";}out<<"]}";}
        out<<']';
    }
    out<<'}';
    if(c.control){const auto s=c.control->state();out<<",\"ownedControl\":{\"playerId\":"<<s.player_id<<",\"controllerId\":"<<s.controller_id<<",\"mapId\":"<<s.map_id<<",\"created\":"<<(s.controller_created?"true":"false")<<",\"attached\":"<<(s.controller_attached?"true":"false")<<",\"registered\":"<<(s.map_registered?"true":"false")<<",\"cleanupRequired\":"<<(s.cleanup_required?"true":"false")<<",\"x\":"<<s.horizontal<<",\"y\":"<<s.vertical<<'}';}
    if(c.command.action==RosterAction::control_pulse){out<<",\"pulse\":{\"nonzeroWritten\":"<<(c.pulse.nonzero_written?"true":"false")<<",\"neutralized\":"<<(c.pulse.neutralized?"true":"false")<<",\"focusLost\":"<<(c.pulse.focus_lost?"true":"false")<<",\"resourcesClosed\":"<<(c.pulse.resources_closed?"true":"false")<<",\"updates\":"<<c.pulse.updates;
        out<<",\"before\":"<<player_observation_json(c.pulse.before.players.empty()?PlayerReadStatus::NotReady:PlayerReadStatus::Captured,c.pulse.before,c.identity,sample)<<",\"after\":"<<player_observation_json(c.pulse.after.players.empty()?PlayerReadStatus::NotReady:PlayerReadStatus::Captured,c.pulse.after,c.identity,sample)<<'}';}
    if(c.command.action==RosterAction::upgrade_hook_inspect || c.command.action==RosterAction::upgrade_hook_install || c.command.action==RosterAction::upgrade_hook_remove) {
        const auto hook=process_upgrade_hook.state();
        out<<",\"upgradeHook\":{\"prepared\":"<<(hook.prepared?"true":"false")
           <<",\"ownershipRetained\":"<<(hook.ownership_retained?"true":"false")
           <<",\"lastVerifiedApplied\":"<<(hook.last_verified_applied?"true":"false")
           <<",\"protectionPending\":"<<(hook.protection_pending?"true":"false")<<'}';
    }
    if(c.command.action==RosterAction::upgrade_hook_inspect) {
        out<<",\"startupUpgradeGuard\":{\"checked\":"<<(c.startup_patch_checked?"true":"false")
           <<",\"mappedPatchedMethodVerified\":"<<(c.startup_patch_verified?"true":"false")
           <<",\"validationError\":"<<quoted_json(c.startup_patch_error)<<'}';
    }
    if(c.command.action==RosterAction::upgrade_offer||c.command.action==RosterAction::upgrade_choose||c.command.action==RosterAction::upgrade_choice_close) {
        const bool offer=c.command.action==RosterAction::upgrade_offer;
        const char* choice_state=offer?(c.choice_status==PlayerReadStatus::Captured?"captured":c.choice_status==PlayerReadStatus::NotReady?"not_ready":"error"):
            c.action_succeeded?(c.command.action==RosterAction::upgrade_choose?"callback_returned":"closed"):"error";
        out<<",\"upgradeChoice\":{\"state\":"<<quoted_json(choice_state)
           <<",\"invocationAttempted\":"<<(c.choice_result.invocation_attempted?"true":"false")
           <<",\"invocationReturned\":"<<(c.choice_result.invocation_returned?"true":"false");
        if(offer&&c.choice_status==PlayerReadStatus::Captured) {
            out<<",\"token\":"<<quoted_json(std::to_string(c.choice_offer.token))<<",\"inputId\":"<<c.choice_offer.input_id
               <<",\"actorLevel\":"<<c.choice_offer.actor_level<<",\"cards\":[";
            for(std::size_t i=0;i<c.choice_offer.cards.size();++i) {
                const auto& card=c.choice_offer.cards[i];if(i)out<<',';
                out<<"{\"childIndex\":"<<card.child_index<<",\"weaponType\":"<<card.weapon_type<<",\"currentLevel\":"<<card.current_level
                   <<",\"isNew\":"<<(card.is_new?"true":"false")<<",\"interactable\":"<<(card.interactable?"true":"false")<<'}';
            }
            out<<']';
        }
        out<<'}';
    }
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
        c.module=game;c.api=api;c.domain=domain;
        attachment.detach();
        log("WSM_ROSTER_READY: metadata worker detached; 32 bounded requests; roster changes require outside-run phase, input changes require separate opt-in");
        const auto deadline = GetTickCount64() + 15ull * 60 * 1000;
        for (unsigned sample = 1; sample <= 32 && IsWindow(c.window.hwnd); ++sample) {
            std::ostringstream number;
            number << std::setw(6) << std::setfill('0') << sample;
            const auto request = directory / ("request-" + number.str());
            while (IsWindow(c.window.hwnd) && GetTickCount64() < deadline && !request_present(request)) Sleep(50);
            if (!IsWindow(c.window.hwnd) || GetTickCount64() >= deadline) break;
            if (!parse_command(read_command(request), GetCurrentProcessId(), c.command))
                throw std::runtime_error("Invalid roster command or mismatched own-process ID");
            c.snapshot = {}; c.configuration = {}; c.configuration_captured = false; c.scene = {}; c.upgrade = {}; c.equipment = {}; c.input_profile = {}; c.identity = {}; c.error.clear();
            c.action_error.clear(); c.roster_error.clear(); c.configuration_error.clear(); c.player_error.clear(); c.upgrade_error.clear();
            c.status = CoopRosterStatus::Error; c.player_status = PlayerReadStatus::Error; c.upgrade_status = PlayerReadStatus::Error; c.equipment_status = PlayerReadStatus::Error; c.input_profile_status = PlayerReadStatus::Error; c.action_succeeded = false;
            c.control_environment={};c.control_status=PlayerReadStatus::Error;c.pulse={};c.control_error.clear();
            c.startup_patch_checked=false;c.startup_patch_verified=false;c.startup_patch_error.clear();
            c.choice_offer={};c.choice_result={};c.choice_status=PlayerReadStatus::Error;

            const auto status = run_window_hook(bridge, c.window.hwnd, c.window.thread, execute_command, &c, 2000);
            if (status != HookResult::completed) { c.action_succeeded = false; c.status = CoopRosterStatus::Error; c.player_status = PlayerReadStatus::Error; c.upgrade_status=PlayerReadStatus::Error; c.equipment_status=PlayerReadStatus::Error; c.input_profile_status=PlayerReadStatus::Error; c.error = c.action_error = "Owned Unity roster callback did not complete"; }
            publish_result(directory / ("state-" + number.str() + ".json"), result_json(c, sample));
            log("WSM_ROSTER_RESULT=" + std::to_string(sample) + " success=" + std::to_string(c.action_succeeded) +
                " pending=" + std::to_string(c.roster.has_pending_enrollment()));
            if (!c.error.empty()) log("WSM_ROSTER_ERROR: " + c.error);
        }
        log("WSM_ROSTER_STOP: request limit, deadline or owned-window exit");
        if(process_upgrade_hook.state().ownership_retained)
            log("WSM_UPGRADE_HOOK_PROCESS_LIFETIME: retained until explicit outside-run removal or process exit");
    } catch (const std::exception& e) { log(std::string("ERROR: roster probe stopped: ") + e.what()); }
    if(context&&context->control&&context->control->state().cleanup_required&&IsWindow(context->window.hwnd)) {
        auto cleanup=[](void* raw){auto& c=*static_cast<ProbeContext*>(raw);c.action_succeeded=c.control->close(c.control_error);};
        const auto done=run_window_hook(bridge,context->window.hwnd,context->window.thread,cleanup,context.get(),2000);
        log(done==HookResult::completed&&context->action_succeeded?"WSM_CONTROL_CLEANUP: owned resources removed":"ERROR: owned control cleanup unverified; resources retained until process exit");
    }
    if(context&&context->choice&&IsWindow(context->window.hwnd)) {
        auto cleanup=[](void* raw){auto& c=*static_cast<ProbeContext*>(raw);c.action_succeeded=c.choice->close(c.action_error);if(c.action_succeeded)c.choice.reset();};
        const auto done=run_window_hook(bridge,context->window.hwnd,context->window.thread,cleanup,context.get(),2000);
        log(done==HookResult::completed&&context->action_succeeded?"WSM_CHOICE_CLEANUP: retained offers released":"ERROR: choice cleanup unverified; roots retained until process exit");
    }
    if (context && context->roster.has_pending_enrollment())
        log("WSM_ROSTER_PENDING: no automatic engine removal; owned roots retained until process exit");
}
}
