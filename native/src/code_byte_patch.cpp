#include "wsm/code_byte_patch.h"
#include <intrin.h>

namespace wsm {
namespace {
void append(std::string& error, const std::string& message) {
    if (!error.empty()) error += "; ";
    error += message;
}
void win_error(std::string& error, const char* operation, DWORD code) {
    append(error, std::string(operation) + " failed (Win32 error " + std::to_string(code) + ")");
}
bool executable_readable(DWORD protection) {
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    switch (protection & 0xff) {
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY: return true;
    default: return false;
    }
}
bool validate_region(void* address, std::string& error) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info))) {
        win_error(error, "VirtualQuery", GetLastError());
        return false;
    }
    if (info.State != MEM_COMMIT || !executable_readable(info.Protect)) {
        append(error, "address is not committed, readable executable non-guard memory");
        return false;
    }
    return true;
}
unsigned char load_byte(void* address) {
    return *static_cast<volatile unsigned char*>(address);
}
unsigned char exchange_byte(void* address, unsigned char replacement, unsigned char expected) {
    return static_cast<unsigned char>(_InterlockedCompareExchange8(
        static_cast<volatile char*>(address), static_cast<char>(replacement), static_cast<char>(expected)));
}
}

bool CodeBytePatch::restore_protection(std::string& error) {
    if (!protection_pending_) return true;
    DWORD ignored = 0;
    if (!VirtualProtect(address_, 1, original_protection_, &ignored)) {
        win_error(error, "VirtualProtect restore", GetLastError());
        return false;
    }
    protection_pending_ = false;
    return true;
}

bool CodeBytePatch::flush(std::string& error) {
    if (!cache_pending_) return true;
    if (!FlushInstructionCache(GetCurrentProcess(), address_, 1)) {
        win_error(error, "FlushInstructionCache", GetLastError());
        return false;
    }
    cache_pending_ = false;
    return true;
}

void CodeBytePatch::release_if_clean() {
    if (!active() && !protection_pending_) {
        address_ = nullptr;
        owner_thread_ = 0;
    }
}

bool CodeBytePatch::install(void* address, unsigned char original, unsigned char replacement,
                            DWORD owner_thread, std::string& error) {
    error.clear();
    if (address_ || active() || protection_pending_) {
        append(error, "patch already owns a byte or pending recovery"); return false;
    }
    if (!owner_thread || GetCurrentThreadId() != owner_thread) {
        append(error, "not the specified owner thread"); return false;
    }
    if (!address || original == replacement) {
        append(error, "invalid address or identical bytes"); return false;
    }
    if (!validate_region(address, error)) return false;
    if (load_byte(address) != original) {
        append(error, "original byte mismatch"); return false;
    }
    DWORD previous = 0;
    if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &previous)) {
        win_error(error, "VirtualProtect writable", GetLastError()); return false;
    }
    address_ = address;
    owner_thread_ = owner_thread;
    original_ = original;
    replacement_ = replacement;
    original_protection_ = previous;
    protection_pending_ = true;
    bool ok = true;
    if (!executable_readable(previous)) {
        append(error, "page protection changed before patch"); ok = false;
    } else if (exchange_byte(address_, replacement_, original_) != original_) {
        append(error, "original byte changed before atomic exchange"); ok = false;
    } else {
        byte_owned_ = true;
        cache_pending_ = true;
        if (!flush(error)) ok = false;
    }
    if (!restore_protection(error)) ok = false;
    release_if_clean();
    return ok;
}

bool CodeBytePatch::remove(std::string& error) {
    error.clear();
    if (!address_) return true;
    if (GetCurrentThreadId() != owner_thread_) {
        append(error, "not the owner thread"); return false;
    }
    if (!restore_protection(error)) return false;
    if (!byte_owned_) {
        const bool ok = flush(error);
        release_if_clean();
        return ok;
    }
    if (!validate_region(address_, error)) return false;
    if (load_byte(address_) != replacement_) {
        append(error, "replacement byte mismatch; refusing to overwrite foreign byte"); return false;
    }
    DWORD previous = 0;
    if (!VirtualProtect(address_, 1, PAGE_EXECUTE_READWRITE, &previous)) {
        win_error(error, "VirtualProtect writable", GetLastError()); return false;
    }
    original_protection_ = previous;
    protection_pending_ = true;
    bool ok = true;
    if (!executable_readable(previous)) {
        append(error, "page protection changed before removal"); ok = false;
    } else if (exchange_byte(address_, original_, replacement_) != replacement_) {
        append(error, "replacement byte changed before atomic exchange"); ok = false;
    } else {
        byte_owned_ = false;
        cache_pending_ = true;
        if (!flush(error)) ok = false;
    }
    if (!restore_protection(error)) ok = false;
    release_if_clean();
    return ok;
}
}
