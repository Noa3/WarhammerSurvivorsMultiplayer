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
            set(1,2,7780,"");
            const auto host = wsm::i18n::format(Text::HostingLan,language,{{"port","7780"},{"peers","2"}});
            const bool has_hint = std::string(argv[2]) != "no-hint";
            check(runtime.localized_status(language) == host + (has_hint ? "\n192.0.2.7:7780, 192.0.2.8:7780" : ""),"localized LAN endpoints");
            const int repeat = runtime.host(7780, true, wsm::HostRules{});
            check(repeat < 0, "second Host request must not create a new host");
            check(runtime.localized_status(language, true, repeat < 0) == host + (has_hint ? "\n192.0.2.7:7780, 192.0.2.8:7780" : ""),
                  "rejected second Host must leave the live peer count and invitation visible");
            check(runtime.localized_status(language, false) == wsm::i18n::format(Text::HostingLocal,language,{{"port","7780"},{"peers","2"}}),"local host must not suggest LAN endpoints");
            set(2,0,0,""); check(runtime.localized_status(language) == wsm::i18n::text(Text::Connecting,language),"connecting translation");
            set(3,1,0,""); check(runtime.localized_status(language) == wsm::i18n::text(Text::Connected,language),"connected translation");
            check(runtime.localized_status(language, true, true) == wsm::i18n::text(Text::Connected,language),"rejected command must not hide a live guest session");
            for (const auto& failure : failures) {
                set(4,0,0,failure.first); check(runtime.localized_status(language) == wsm::i18n::text(failure.second,language),"failure category translation");
            }
            set(0,0,0,"hostlost"); check(runtime.localized_status(language) == wsm::i18n::text(Text::HostLost,language),"disconnected host loss translation");
            check(runtime.localized_status(language, true, true) == wsm::i18n::text(Text::HostLost,language),"a prior click must not hide a subsequent host loss");
            set(0,0,0,""); check(runtime.localized_status(language, true, true) == wsm::i18n::text(Text::ActionRejected,language),"idle rejected action still gets feedback");
            set(9,0,0,""); check(runtime.localized_status(language) == wsm::i18n::text(Text::NetworkUnavailable,language),"invalid backend state fallback");
        }
        std::puts("PASS: localized native status categories and legacy ABI fallback in all 13 game languages; ABI-only, not TCP.");
        return 0;
    } catch (const std::exception& e) { std::printf("FAIL: %s\n",e.what()); return 1; }
}
