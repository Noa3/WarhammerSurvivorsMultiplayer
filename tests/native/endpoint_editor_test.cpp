#include "wsm/endpoint_editor.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

using namespace wsm;

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void state(const EndpointEditor& e, std::string_view s, std::size_t c, std::size_t a) {
    require(e.text() == s && e.cursor() == c && e.anchor() == a, "state mismatch");
}
static void empty() {
    EndpointEditor e(EndpointKind::Port); state(e, "", 0, 0); require(e.begin(""), "empty begin");
    for (auto key : {EditKey::Left, EditKey::Right, EditKey::Home, EditKey::End, EditKey::Backspace, EditKey::Delete, EditKey::SelectAll}) require(!e.key(key), "empty key");
    require(!e.paste(""), "empty paste"); state(e, "", 0, 0);
}
static void typing_and_reset() {
    EndpointEditor e(EndpointKind::Address);
    require(e.character(U'1') && e.character(U'2') && e.character(U'.') && e.character(U'3'), "typing"); state(e, "12.3", 4, 4);
    require(e.begin("127.0.0.1"), "begin"); state(e, "127.0.0.1", 9, 0);
    require(e.begin("127.0.0.1"), "unchanged begin");
    require(!e.begin("hello") && !e.begin("1234567890123456"), "invalid begin"); state(e, "127.0.0.1", 9, 0);
    require(e.begin(""), "reset empty"); state(e, "", 0, 0);
}
static void selection_replacement() {
    EndpointEditor e(EndpointKind::Address); require(e.begin("127.0.0.1") && e.character(U'9'), "replace all"); state(e, "9", 1, 1);
    require(e.paste(".123"), "paste"); require(e.key(EditKey::Left, true) && e.key(EditKey::Left, true), "shift left"); state(e, "9.123", 3, 5);
    require(e.paste("45"), "replace backwards selection"); state(e, "9.145", 5, 5);
    require(e.key(EditKey::SelectAll), "select all"); state(e, "9.145", 5, 0);
    require(e.paste(""), "empty paste deletes selection"); state(e, "", 0, 0);
}
static void invalid_atomic() {
    EndpointEditor e(EndpointKind::Port); require(e.begin("12345"), "begin port");
    require(!e.character(U'.') && !e.character(U'\n') && !e.character(U'\u0661') && !e.character(U'\0'), "invalid chars");
    require(!e.paste("12x") && !e.paste("12\n") && !e.paste("\xc3\xa9") && !e.paste(std::string_view("1\0", 2)), "invalid paste");
    require(!e.begin("1.2") && !e.begin("123456"), "invalid begin"); state(e, "12345", 5, 0);
}
static void capacity() {
    EndpointEditor p(EndpointKind::Port); require(p.begin("12345") && p.key(EditKey::End), "port cap init");
    require(!p.character(U'6') && !p.paste("6"), "port cap"); state(p, "12345", 5, 5);
    require(p.key(EditKey::Left, true) && !p.paste("66"), "selection over cap"); state(p, "12345", 4, 5);
    require(p.character(U'6'), "replace at cap"); state(p, "12346", 5, 5);
    EndpointEditor a(EndpointKind::Address); require(a.begin("255.255.255.255") && a.key(EditKey::End), "address cap init");
    require(!a.character(U'.') && !a.paste("1"), "address cap"); state(a, "255.255.255.255", 15, 15);
}
static void navigation() {
    EndpointEditor e(EndpointKind::Address); require(e.begin("12.34.56") && e.key(EditKey::Left), "collapse left"); state(e, "12.34.56", 0, 0);
    require(!e.key(EditKey::Left), "left boundary"); require(e.key(EditKey::Right, true) && e.key(EditKey::Right, true), "select forward");
    state(e, "12.34.56", 2, 0); require(e.key(EditKey::Right), "collapse right"); state(e, "12.34.56", 2, 2);
    require(e.key(EditKey::End, true), "shift end"); state(e, "12.34.56", 8, 2);
    require(e.key(EditKey::Left), "collapse left"); state(e, "12.34.56", 2, 2);
    require(e.key(EditKey::Home, true, true), "shift ctrl home"); state(e, "12.34.56", 0, 2);
    require(e.key(EditKey::End, false, true) && !e.key(EditKey::End), "end boundary"); state(e, "12.34.56", 8, 8);
}
static void deletion() {
    EndpointEditor e(EndpointKind::Port); require(e.begin("12345") && e.key(EditKey::Home), "init");
    require(!e.key(EditKey::Backspace) && e.key(EditKey::Delete), "boundary and delete"); state(e, "2345", 0, 0);
    require(e.key(EditKey::Right) && e.key(EditKey::Right) && e.key(EditKey::Backspace), "backspace"); state(e, "245", 1, 1);
    require(e.key(EditKey::Right, true) && e.key(EditKey::Backspace), "backspace selection"); state(e, "25", 1, 1);
    require(e.key(EditKey::Left, true) && e.key(EditKey::Delete), "delete selection"); state(e, "5", 0, 0);
    require(e.key(EditKey::End) && !e.key(EditKey::Delete), "delete boundary");
}
static void control_words() {
    EndpointEditor e(EndpointKind::Address); require(e.begin("12.34.56") && e.key(EditKey::End), "init");
    require(e.key(EditKey::Left, false, true), "ctrl left"); state(e, "12.34.56", 6, 6);
    require(e.key(EditKey::Left, false, true), "ctrl left again"); state(e, "12.34.56", 3, 3);
    require(e.key(EditKey::Right, false, true), "ctrl right"); state(e, "12.34.56", 5, 5);
    require(e.key(EditKey::Backspace, false, true), "ctrl backspace"); state(e, "12..56", 3, 3);
    require(e.key(EditKey::Delete, false, true), "ctrl delete"); state(e, "12.", 3, 3);
    EndpointEditor p(EndpointKind::Port); require(p.begin("12345") && p.key(EditKey::End) && p.key(EditKey::Backspace, false, true), "ctrl port delete"); state(p, "", 0, 0);
}
static void aliases() {
    EndpointEditor e(EndpointKind::Address); require(e.begin("12.3") && !e.key(EditKey::SelectAll), "already selected");
    require(e.paste(std::string_view(e.text()).substr(1, 2)), "alias paste"); state(e, "2.", 2, 2);
    require(e.begin(std::string_view(e.text()).substr(1)), "alias begin"); state(e, ".", 1, 0);
    require(e.character(U'.'), "same replacement changes selection"); state(e, ".", 1, 1);
}
static void invite_paste() {
    EndpointEditor a(EndpointKind::Address);
    std::string port = "7780";
    require(a.begin("127.0.0.1"), "select old IP");
    require(a.paste_invite("192.168.1.22:9000", port), "paste host invite into address field");
    state(a, "192.168.1.22", 12, 12);
    require(port == "9000", "invite replaces port");
    for (auto bad : {"10.0.0:1", "999.1.2.3:1", "10.1.2.3:0", "1.2.3.4:65536", "1.2.3.4:23x", "1.2.3.4::5", "01.2.3.4:9", "1.2.3.4:", ":9"}) {
        require(!a.paste_invite(bad, port), "reject invalid host invite");
        state(a, "192.168.1.22", 12, 12); require(port == "9000", "bad invite cannot change port");
    }
    require(a.paste_invite("10.0.0.7:65535", port), "invite replaces both fields regardless of caret");
    require(port == "65535" && a.text() == "10.0.0.7", "max port");
    require(a.key(EditKey::SelectAll) && a.paste_invite("127.0.0.1", port), "plain IP remains normal paste");
    require(a.text() == "127.0.0.1" && port == "65535", "plain IP preserves port");
}
int main() {
    using Test = void (*)(); const Test tests[] = {empty, typing_and_reset, selection_replacement, invalid_atomic, capacity, navigation, deletion, control_words, aliases, invite_paste};
    for (auto test : tests) test();
    std::printf("PASS: %zu endpoint editor test cases\n", sizeof(tests) / sizeof(tests[0]));
}
