#include "wsm/upgrade_pass_profile.h"
#include <bcrypt.h>
#include <psapi.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace wsm {
namespace {
constexpr std::uint64_t kAssemblyBytes = 115045376;
constexpr std::uint64_t kMetadataBytes = 27884160;
constexpr DWORD kImageBytes = 118001664;
constexpr char kAssemblyHash[] = "840e1f68d5e8110d05ab0aff9087c36da75ccb5998566dccc17bc96605f94791";
constexpr char kMetadataHash[] = "f7b632c69bd5ea2dcbb41e96e4f78689e41d23ba37e1edf8780db2e8909e7ace";
constexpr char kOriginalMethodHash[] = "029a955dc59cfc301a70fa4fa05643b77967d8172784621c9801e2e35e8bd7f2";
constexpr char kRedirectedMethodHash[] = "86c4a1e8ec549d1939355674bd045867dfc4b356b7589f8ad42a577db8d06767";

void checked(NTSTATUS status, const char* operation) {
    if (status < 0) throw std::runtime_error(std::string(operation) + " failed, NTSTATUS=" + std::to_string(status));
}
class Hash {
public:
    Hash() {
        try {
            checked(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "Open SHA256");
            ULONG length = 0, written = 0;
            checked(BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &written, 0), "SHA256 object length");
            if (written != sizeof(length) || !length || length > 65536) throw std::runtime_error("Unexpected SHA256 object length");
            object_.resize(length);
            checked(BCryptCreateHash(algorithm_, &hash_, object_.data(), length, nullptr, 0, 0), "Create SHA256");
        } catch (...) { release(); throw; }
    }
    ~Hash() { release(); }
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;
    void add(const unsigned char* bytes, ULONG size) {
        checked(BCryptHashData(hash_, const_cast<PUCHAR>(bytes), size, 0), "Hash SHA256 bytes");
    }
    std::string finish() {
        std::array<unsigned char, 32> digest{};
        checked(BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0), "Finish SHA256");
        constexpr char hex[] = "0123456789abcdef";
        std::string result; result.reserve(64);
        for (const auto value : digest) { result += hex[value >> 4]; result += hex[value & 15]; }
        return result;
    }
private:
    void release() noexcept {
        if (hash_) { BCryptDestroyHash(hash_); hash_ = nullptr; }
        if (algorithm_) { BCryptCloseAlgorithmProvider(algorithm_, 0); algorithm_ = nullptr; }
    }
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> object_;
};
class File {
public:
    explicit File(const std::filesystem::path& path) {
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open version file, Win32=" + std::to_string(GetLastError()));
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileType(handle_) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle_, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Version file is not a regular non-linked disk file");
        }
    }
    ~File() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    void verify(std::uint64_t expected_size, const char* expected_hash) {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle_, &size) || size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) != expected_size)
            throw std::runtime_error("Unsupported version-file size");
        Hash hash;
        std::vector<unsigned char> buffer(1024 * 1024);
        std::uint64_t total = 0;
        while (total < expected_size) {
            DWORD count = 0;
            const auto remaining = expected_size - total;
            const auto requested = static_cast<DWORD>(remaining < buffer.size() ? remaining : buffer.size());
            if (!ReadFile(handle_, buffer.data(), requested, &count, nullptr) || count != requested)
                throw std::runtime_error("Incomplete version-file read");
            hash.add(buffer.data(), count); total += count;
        }
        if (hash.finish() != expected_hash) throw std::runtime_error("Unsupported version-file SHA256");
    }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
bool readable_image_range(HMODULE module, const void* address, std::size_t bytes) {
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (!start || bytes > UINTPTR_MAX - start) return false;
    const auto end = start + bytes;
    auto cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info) ||
            info.AllocationBase != module || info.State != MEM_COMMIT || info.Type != MEM_IMAGE ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto protect = info.Protect & 0xff;
        if (protect != PAGE_READONLY && protect != PAGE_READWRITE && protect != PAGE_WRITECOPY &&
            protect != PAGE_EXECUTE_READ && protect != PAGE_EXECUTE_READWRITE && protect != PAGE_EXECUTE_WRITECOPY) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (info.RegionSize > UINTPTR_MAX - base || base + info.RegionSize <= cursor) return false;
        cursor = base + info.RegionSize;
    }
    return true;
}
}

bool verify_upgrade_pass_files(const std::filesystem::path& assembly,
                               const std::filesystem::path& metadata, std::string& error) {
    error.clear();
    try {
        // Keep both file handles open without write/delete sharing throughout verification.
        File assembly_file(assembly), metadata_file(metadata);
        assembly_file.verify(kAssemblyBytes, kAssemblyHash);
        metadata_file.verify(kMetadataBytes, kMetadataHash);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool verify_upgrade_pass_image(HMODULE game, bool patched, std::string& error) {
    error.clear();
    try {
        if (!game || GetModuleHandleW(L"GameAssembly.dll") != game) throw std::runtime_error("Not the loaded GameAssembly module");
        MODULEINFO module{};
        if (!GetModuleInformation(GetCurrentProcess(), game, &module, sizeof(module)) ||
            module.lpBaseOfDll != game || module.SizeOfImage != kImageBytes)
            throw std::runtime_error("Unsupported mapped GameAssembly size");
        const auto base = reinterpret_cast<const unsigned char*>(game);
        if (!readable_image_range(game, base, sizeof(IMAGE_DOS_HEADER))) throw std::runtime_error("Unreadable module header");
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096 ||
            !readable_image_range(game, base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64)))
            throw std::runtime_error("Invalid mapped PE header");
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->OptionalHeader.SizeOfImage != kImageBytes)
            throw std::runtime_error("Unsupported mapped PE identity");
        const auto method = base + kUpgradePassMethodRva;
        if (!readable_image_range(game, method, kUpgradePassMethodSize)) throw std::runtime_error("Unreadable mapped upgrade method");
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(method, &info, sizeof(info)) || !(info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            throw std::runtime_error("Upgrade method is not executable");
        Hash hash; hash.add(method, kUpgradePassMethodSize);
        if (hash.finish() != (patched ? kRedirectedMethodHash : kOriginalMethodHash))
            throw std::runtime_error("Unsupported or modified mapped upgrade method");
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
}
