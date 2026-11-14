#pragma once
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
    int join(const std::string& address, int port);
    int leave();
    std::string status() const;
    // Translate stable machine categories, never the backend's human-readable text.
    std::string localized_status(const std::string& language, bool lan = true) const;
private:
    HMODULE module_ = nullptr;
    int (__cdecl* version_)() = nullptr;
    int (__cdecl* host_)(int, int) = nullptr;
    int (__cdecl* join_)(const unsigned char*, int, int) = nullptr;
    int (__cdecl* leave_)() = nullptr;
    int (__cdecl* status_)(unsigned char*, int) = nullptr;
    int (__cdecl* state_)() = nullptr;
    int (__cdecl* peer_count_)() = nullptr;
    int (__cdecl* bound_port_)() = nullptr;
    int (__cdecl* last_error_)(unsigned char*, int) = nullptr;
};
}
