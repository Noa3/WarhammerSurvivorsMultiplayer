#include "wsm/ingame_runtime.h"
#include "wsm/ingame_localization.h"
#include <array>
#include <filesystem>

namespace wsm {
bool InGameRuntime::load(const std::wstring& path, std::string& error) {
    error.clear();
    if (ready()) { error = "Networking runtime is already loaded."; return false; }
    if (path.empty() || !std::filesystem::path(path).is_absolute()) {
        error = "Networking runtime requires an absolute installation path."; return false;
    }
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        error = "Networking runtime is missing or is not a regular installation file."; return false;
    }
    HMODULE loaded = LoadLibraryExW(path.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!loaded) { error = "Networking runtime could not load; Windows error " + std::to_string(GetLastError()); return false; }
    // Native AOT does not support unloading. Keep even an incompatible loaded
    // runtime resident; a corrected binary requires restarting this process.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(loaded), &pinned) || pinned != loaded) {
        error = "Networking runtime lifetime could not be secured."; return false;
    }
    auto version = reinterpret_cast<decltype(version_)>(GetProcAddress(loaded, "wsm_ingame_version"));
    auto host = reinterpret_cast<decltype(host_)>(GetProcAddress(loaded, "wsm_ingame_host"));
    auto join = reinterpret_cast<decltype(join_)>(GetProcAddress(loaded, "wsm_ingame_join"));
    auto leave = reinterpret_cast<decltype(leave_)>(GetProcAddress(loaded, "wsm_ingame_leave"));
    auto status = reinterpret_cast<decltype(status_)>(GetProcAddress(loaded, "wsm_ingame_status"));
    if (!version || !host || !join || !leave || !status || version() != 1) {
        error = "Networking runtime ABI is missing or incompatible; restart after correcting installation."; return false;
    }
    // Additive ABI-1 exports; older backends retain the original five-export contract.
    state_ = reinterpret_cast<decltype(state_)>(GetProcAddress(loaded, "wsm_ingame_state"));
    peer_count_ = reinterpret_cast<decltype(peer_count_)>(GetProcAddress(loaded, "wsm_ingame_peer_count"));
    bound_port_ = reinterpret_cast<decltype(bound_port_)>(GetProcAddress(loaded, "wsm_ingame_bound_port"));
    last_error_ = reinterpret_cast<decltype(last_error_)>(GetProcAddress(loaded, "wsm_ingame_last_error"));
    module_ = loaded; version_ = version; host_ = host; join_ = join; leave_ = leave; status_ = status;
    return true;
}
int InGameRuntime::host(int port, bool lan) {
    if (!ready()) return -3;
    if (port < 0 || port > 65535) return -1;
    return host_(port, lan ? 1 : 0);
}
int InGameRuntime::join(const std::string& address, int port) {
    if (!ready()) return -3;
    if (address.empty() || address.size() > 15 || port < 1 || port > 65535) return -1;
    for (const auto value : address) if ((value < '0' || value > '9') && value != '.') return -1;
    return join_(reinterpret_cast<const unsigned char*>(address.data()), static_cast<int>(address.size()), port);
}
int InGameRuntime::leave() { return ready() ? leave_() : -3; }
std::string InGameRuntime::localized_status(const std::string& language, bool lan) const {
    using i18n::Text;
    const auto phrase = [&](Text id) { return std::string(i18n::text(id, language)); };
    if (!ready()) return phrase(Text::NetworkMissing);
    if (!state_ || !peer_count_ || !bound_port_ || !last_error_) return phrase(Text::NetworkUnavailable);
    const int state = state_();
    std::array<unsigned char, 32> error{};
    const int size = last_error_(error.data(), static_cast<int>(error.size()));
    const int port = bound_port_(), peers = peer_count_();
    // The ABI getters are separate reads; reject an observed state transition
    // rather than presenting details from a different state as authoritative.
    if (size < 0 || size >= static_cast<int>(error.size()) || error[size] != 0 ||
        state != state_() || state < 0 || state > 4) return phrase(Text::NetworkUnavailable);
    const std::string code(reinterpret_cast<const char*>(error.data()), static_cast<std::size_t>(size));
    if (state == 1) {
        if (port < 1 || port > 65535 || peers < 0 || peers > 3) return phrase(Text::NetworkUnavailable);
        return i18n::format(lan ? Text::HostingLan : Text::HostingLocal, language,
                            {{"port", std::to_string(port)}, {"peers", std::to_string(peers)}});
    }
    if (state == 2) return phrase(Text::Connecting);
    if (state == 3) return phrase(Text::Connected);
    if (code.empty() && state == 0) return phrase(Text::NotConnected);
    const std::pair<const char*, Text> failures[] = {
        {"hostlost", Text::HostLost}, {"refused", Text::Refused}, {"timeout", Text::Timeout},
        {"rejected", Text::Rejected}, {"hostfull", Text::HostFull}, {"unreachable", Text::Unreachable},
        {"portinuse", Text::PortInUse}, {"busy", Text::Busy}, {"invalid", Text::Invalid}, {"internal", Text::Internal}
    };
    for (const auto& failure : failures) if (code == failure.first) return phrase(failure.second);
    return phrase(Text::NetworkUnavailable);
}
std::string InGameRuntime::status() const {
    if (!ready()) return "Netzwerkmodul ist nicht geladen.";
    std::array<unsigned char, 4097> buffer{};
    const int count = status_(buffer.data(), static_cast<int>(buffer.size()));
    if (count < 0 || count > 4096 || buffer[static_cast<std::size_t>(count)] != 0)
        return "Netzwerkstatus ist voruebergehend nicht verfuegbar.";
    return std::string(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(count));
}
}
