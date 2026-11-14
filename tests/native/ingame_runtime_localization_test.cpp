// Tests presentation only through a clearly labelled ABI-only fixture, not real networking.
#include "wsm/ingame_runtime.h"
#include "wsm/ingame_localization.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    using wsm::i18n::Text;
    const char* languages[] = {"en","de","fr","it","es","ja","ko","pl","pt-BR","ru","zh-CN","tr","uk"};
    try {
        wsm::InGameRuntime runtime; std::string error;
        check(runtime.localized_status("en") == wsm::i18n::text(Text::NetworkMissing,"en"), "missing backend is localized");
        check(runtime.load(std::wstring(argv[1], argv[1] + std::strlen(argv[1])), error), error.c_str());
        auto fixture = GetModuleHandleA(argv[1]);
        auto set = reinterpret_cast<void(__cdecl*)(int,int,int,const char*)>(GetProcAddress(fixture,"wsm_fixture_status"));
        check(set != nullptr,"fixture status setter missing");
        const std::pair<const char*, Text> failures[] = {{"refused",Text::Refused},{"timeout",Text::Timeout},
            {"rejected",Text::Rejected},{"hostfull",Text::HostFull},{"unreachable",Text::Unreachable},
            {"hostlost",Text::HostLost},{"portinuse",Text::PortInUse},{"busy",Text::Busy},{"invalid",Text::Invalid},{"internal",Text::Internal}};
        for (const auto language : languages) {
            if (std::string(argv[2]) == "legacy") {
                check(runtime.localized_status(language) == wsm::i18n::text(Text::NetworkUnavailable,language),"legacy ABI must not leak untranslated human text");
                continue;
            }
            set(0,0,0,""); check(runtime.localized_status(language) == wsm::i18n::text(Text::NotConnected,language),"idle translation");
            set(1,2,7780,""); check(runtime.localized_status(language) == wsm::i18n::format(Text::HostingLan,language,{{"port","7780"},{"peers","2"}}),"host translation and real fields");
            set(2,0,0,""); check(runtime.localized_status(language) == wsm::i18n::text(Text::Connecting,language),"connecting translation");
            set(3,1,0,""); check(runtime.localized_status(language) == wsm::i18n::text(Text::Connected,language),"connected translation");
            for (const auto& failure : failures) {
                set(4,0,0,failure.first); check(runtime.localized_status(language) == wsm::i18n::text(failure.second,language),"failure category translation");
            }
            set(0,0,0,"hostlost"); check(runtime.localized_status(language) == wsm::i18n::text(Text::HostLost,language),"disconnected host loss translation");
            set(9,0,0,""); check(runtime.localized_status(language) == wsm::i18n::text(Text::NetworkUnavailable,language),"invalid backend state fallback");
        }
        std::puts("PASS: localized native status categories and legacy ABI fallback in all 13 game languages; ABI-only, not TCP.");
        return 0;
    } catch (const std::exception& e) { std::printf("FAIL: %s\n",e.what()); return 1; }
}
