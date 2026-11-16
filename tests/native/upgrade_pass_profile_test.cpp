#include "wsm/upgrade_pass_profile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) ++failures;
}
}
int wmain(int argc, wchar_t** argv) {
    std::string error;
    check("null module refused", !wsm::verify_upgrade_pass_image(nullptr, false, error) && !error.empty());
    error.clear();
    check("own unrelated executable refused", !wsm::verify_upgrade_pass_image(GetModuleHandleW(nullptr), false, error) && !error.empty());
    const auto root = std::filesystem::temp_directory_path() /
        (L"wsm-upgrade-profile-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directory(root);
    const auto assembly = root / L"GameAssembly.dll";
    const auto metadata = root / L"global-metadata.dat";
    error.clear();
    check("missing version files refused", !wsm::verify_upgrade_pass_files(assembly, metadata, error) && !error.empty());
    { std::ofstream file(assembly, std::ios::binary); file << "Not the game"; }
    { std::ofstream file(metadata, std::ios::binary); file << "Not metadata"; }
    error.clear();
    check("same named unsupported files refused", !wsm::verify_upgrade_pass_files(assembly, metadata, error) && !error.empty());
    // Correct lengths must not substitute for the pinned content digest.
    { std::ofstream file(assembly, std::ios::binary | std::ios::trunc); file.seekp(115045375); file.put('\0'); }
    { std::ofstream file(metadata, std::ios::binary | std::ios::trunc); file.seekp(27884159); file.put('\0'); }
    error.clear();
    check("correct lengths with wrong hashes refused",
          !wsm::verify_upgrade_pass_files(assembly, metadata, error) && error == "Unsupported version-file SHA256");
    check("redirect preserves instruction and changes only low displacement byte",
          wsm::kUpgradePassPatchRva == wsm::kUpgradePassBranchRva + 2 &&
          wsm::kUpgradePassContinueRva == wsm::kUpgradePassBranchRva + 6 + 0x125 &&
          wsm::kUpgradePassOriginalByte == 0xed && wsm::kUpgradePassRedirectByte == 0x25);
    std::filesystem::remove(metadata);
    std::filesystem::remove(assembly);
    std::filesystem::remove(root);
    if (argc == 3 && std::wstring(argv[1]) == L"--installed-game") {
        const std::filesystem::path game(argv[2]);
        error.clear();
        const bool ok = wsm::verify_upgrade_pass_files(game / L"GameAssembly.dll",
            game / L"WarhammerSurvivors_Data/il2cpp_data/Metadata/global-metadata.dat", error);
        check("exact installed files admitted read-only", ok);
        if (!ok) std::cerr << error << '\n';
    } else if (argc != 1) {
        std::cerr << "Unexpected arguments\n";
        ++failures;
    }
    return failures ? 1 : 0;
}
