#include "wsm/ingame_localization.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <string_view>

using namespace wsm::i18n;

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        unsigned n; unsigned cp;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; cp = c & 31; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; cp = c & 15; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp = c & 7; }
        else return false;
        if (i + n > s.size()) return false;
        for (unsigned j = 0; j < n; ++j) {
            auto b = static_cast<unsigned char>(s[i++]);
            if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 63);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) ||
            (n == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}

static std::set<std::string> tokens(std::string_view s) {
    std::set<std::string> result;
    for (std::size_t pos = 0; pos < s.size();) {
        auto a = s.find('{', pos);
        auto stray = s.find('}', pos);
        require(stray == s.npos || (a != s.npos && a < stray), "stray closing brace");
        if (a == s.npos) break;
        auto b = s.find('}', a + 1);
        require(b != s.npos && b > a + 1 && s.find('{', a + 1) > b, "invalid token");
        result.emplace(s.substr(a + 1, b - a - 1));
        pos = b + 1;
    }
    return result;
}

int main() {
    constexpr std::array langs{"en", "de", "fr", "it", "es", "ja", "ko", "pl", "pt-BR", "ru", "zh-CN", "tr", "uk"};
    constexpr std::array names{"English", "German", "French", "Italian", "Spanish", "Japanese", "Korean", "Polish", "Portuguese", "Russian", "Chinese", "Turkish", "Ukrainian"};
    for (std::size_t language_index = 0; language_index < langs.size(); ++language_index) {
        auto language = langs[language_index];
        require(normalize_language(language) == language, "canonical language");
        require(normalize_language(names[language_index]) == language, "display name alias");
        for (int i = 0; i < static_cast<int>(Text::Count); ++i) {
            auto id = static_cast<Text>(i);
            auto value = text(id, language);
            require(!value.empty(), "empty translation");
            require(utf8(value), "invalid UTF-8");
            require(tokens(value) == tokens(text(id, "en")), "inconsistent placeholders");
            require(value.size() < 700, "unexpectedly long translation");
        }
    }
    require(text(Text::Tab, "de") == "Koop [F6]", "German tab");
    require(text(Text::Title, "de") == "Koop-Verbindung", "German title");
    require(text(Text::Address, "de") == "IP-Adresse", "German address");
    require(text(Text::Host, "de") == "Hosten", "German host");
    require(text(Text::Join, "de") == "Beitreten", "German join");
    require(text(Text::Leave, "de") == "Verlassen", "German leave");
    require(text(Text::Title, "en") == "Co-op connection", "English title");
    require(tokens(text(Text::HostingLan, "en")) == std::set<std::string>{"peers", "port"}, "host tokens");
    require(tokens(text(Text::HostingLocal, "en")) == std::set<std::string>{"peers", "port"}, "local tokens");
    require(tokens(text(Text::Connecting, "en")).empty(), "connecting has no guessed endpoint");
    require(tokens(text(Text::Connected, "en")).empty(), "connected has no guessed endpoint");
    for (auto alias : {"de-DE", "de_DE", "German"}) require(normalize_language(alias) == "de", "German alias");
    require(normalize_language("en-US") == "en", "English region");
    require(normalize_language("pt_BR") == "pt-BR", "Portuguese alias");
    for (auto alias : {"zh-Hans", "zh-CHS", "Chinese (Simplified)"}) require(normalize_language(alias) == "zh-CN", "Chinese alias");
    require(normalize_language("") == "en" && normalize_language("unknown") == "en", "unknown fallback");
    require(text(Text::Title, "unknown") == text(Text::Title, "en"), "unknown text fallback");
    require(text(static_cast<Text>(9999), "en") == text(Text::NetworkUnavailable, "en"), "invalid text fallback");
    auto value = format(Text::HostingLan, "en", {{"port", "7780"}, {"peers", "2"}});
    require(value.find("7780") != value.npos && value.find('2') != value.npos && tokens(value).empty(), "host formatting");
    value = format(Text::HostingLan, "en", {{"port", "{peers}"}, {"peers", "2"}});
    require(value.find("{peers}") != value.npos && value.find('2') != value.npos, "do not recursively replace values");
    require(format(Text::HostingLan, "en", {}).find("{port}") != std::string::npos, "missing value retained");
    std::puts("PASS: 13 locales, UTF-8, tokens, aliases, fallbacks and formatting");
}
