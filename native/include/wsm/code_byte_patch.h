#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace wsm {
// Calls must be externally serialized on the owner thread; the mapping must stay
// allocated and its protection must not be modified concurrently. The caller is
// responsible for quiescing execution at the patch site during modifications.
// Keep this object alive while active() or protection_pending() is true. There is
// deliberately no destructor patching: destruction cannot choose a safe thread.
class CodeBytePatch {
public:
    CodeBytePatch() = default;
    CodeBytePatch(const CodeBytePatch&) = delete;
    CodeBytePatch& operator=(const CodeBytePatch&) = delete;
    bool install(void* address, unsigned char original, unsigned char replacement,
                 DWORD owner_thread, std::string& error);
    bool remove(std::string& error);
    bool active() const { return byte_owned_ || cache_pending_; }
    bool protection_pending() const { return protection_pending_; }

private:
    bool restore_protection(std::string& error);
    bool flush(std::string& error);
    void release_if_clean();
    void* address_ = nullptr;
    DWORD owner_thread_ = 0;
    DWORD original_protection_ = 0;
    unsigned char original_ = 0;
    unsigned char replacement_ = 0;
    bool byte_owned_ = false;
    bool cache_pending_ = false;
    bool protection_pending_ = false;
};
}
