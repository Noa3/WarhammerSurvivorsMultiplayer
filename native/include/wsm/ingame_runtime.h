#pragma once
#include "wsm/host_rules.h"
#include <windows.h>
#include <string>

namespace wsm {
// Own Native AOT networking DLL. It stays loaded for the process lifetime;
// session operations queue work and never wait for network I/O on Unity's thread.
class InGameRuntime {
public:
    bool load(const std::wstring& path, std::string& error);
    bool ready() const { return module_ != nullptr && version_ != nullptr; }
    int host(int port, bool lan);
    int host(int port, bool lan, HostRules rules);
    bool can_edit_host_rules() const;
    int join(const std::string& address, int port);
    int leave();
    bool names_ready() const;
    int set_player_name(const std::string& name);
    std::string local_player_name() const;
    std::string player_names() const;
    std::string status() const;
    // Translate stable machine categories, never the backend's human-readable text.
    std::string localized_status(const std::string& language, bool lan = true, bool command_failed = false) const;
private:
    HMODULE module_ = nullptr;
    int (__cdecl* version_)() = nullptr;
    int (__cdecl* host_)(int, int) = nullptr;
    int (__cdecl* host_rules_)(int, int, int, int, int) = nullptr;
    int (__cdecl* join_)(const unsigned char*, int, int) = nullptr;
    int (__cdecl* leave_)() = nullptr;
    int (__cdecl* status_)(unsigned char*, int) = nullptr;
    int (__cdecl* lan_hint_)(unsigned char*, int) = nullptr;
    int (__cdecl* state_)() = nullptr;
    int (__cdecl* peer_count_)() = nullptr;
    int (__cdecl* bound_port_)() = nullptr;
    int (__cdecl* last_error_)(unsigned char*, int) = nullptr;
    int (__cdecl* set_player_name_)(const unsigned char*, int) = nullptr;
    int (__cdecl* local_player_name_)(unsigned char*, int) = nullptr;
    int (__cdecl* player_names_)(unsigned char*, int) = nullptr;
};
}
