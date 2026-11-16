#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace wsm {
// Only the explicitly verified 2.1.6 Windows x64 image is admitted.
inline constexpr std::uint32_t kUpgradePassMethodRva = 0x49a7400;
inline constexpr std::uint32_t kUpgradePassMethodSize = 1365;
inline constexpr std::uint32_t kUpgradePassBranchRva = 0x49a773b;
inline constexpr std::uint32_t kUpgradePassPatchRva = 0x49a773d;
inline constexpr std::uint32_t kUpgradePassContinueRva = 0x49a7866;
inline constexpr unsigned char kUpgradePassOriginalByte = 0xed;
inline constexpr unsigned char kUpgradePassRedirectByte = 0x25;

// File checks run before dispatch to the Unity thread. No code is executed.
[[nodiscard]] bool verify_upgrade_pass_files(const std::filesystem::path& assembly,
                                           const std::filesystem::path& metadata,
                                           std::string& error);
// Requires the real loaded GameAssembly module, not a data/resource mapping.
// All checks are read-only, including the complete original/patched method hash.
[[nodiscard]] bool verify_upgrade_pass_image(HMODULE game, bool patched,
                                           std::string& error);
}
