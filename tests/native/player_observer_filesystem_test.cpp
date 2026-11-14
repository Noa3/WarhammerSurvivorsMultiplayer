// Exercise the actual private filesystem helpers in their production translation unit.
#include "../../native/src/player_observer.cpp"
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::cout << "OBSERVER_IO " << label << '=' << (ok ? "PASS" : "FAIL") << '\n';
    if (!ok) ++failures;
}
template<class F> void refuses(F operation, const char* label) {
    try { operation(); check(false, label); }
    catch (const std::exception&) { check(true, label); }
}
std::string content(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::absolute(argv[1]) / ("wsm-player-observer-io-" + std::to_string(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(root)) return 2;
    try {
        SetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", L"relative-output");
        refuses([] { wsm::output_directory(); }, "relative_directory_refused");
        const auto empty = root / "output";
        std::filesystem::create_directory(empty);
        SetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", empty.c_str());
        check(wsm::output_directory() == empty, "empty_caller_directory_accepted");
        const auto file = root / "not-a-directory";
        { std::ofstream out(file); out << "preserve"; }
        SetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", file.c_str());
        refuses([] { wsm::output_directory(); }, "regular_file_as_directory_refused");
        const auto nonempty = root / "nonempty";
        std::filesystem::create_directory(nonempty);
        { std::ofstream out(nonempty / "sentinel"); out << "preserve"; }
        SetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", nonempty.c_str());
        refuses([] { wsm::output_directory(); }, "nonempty_directory_refused");
        check(content(nonempty / "sentinel") == "preserve", "existing_directory_content_preserved");
        const auto result = empty / "state-000001.json";
        wsm::publish(result, "first");
        check(content(result) == "first" && !std::filesystem::exists(result.wstring() + L".tmp"), "exclusive_write_and_rename");
        refuses([&] { wsm::publish(result, "second"); }, "existing_result_refused");
        check(content(result) == "first" && content(result.wstring() + L".tmp") == "second", "result_preserved_partial_retained");
        const auto request = empty / "request-000002";
        check(!wsm::request_exists(request), "absent_request_waits");
        std::filesystem::create_directory(request);
        refuses([&] { wsm::request_exists(request); }, "directory_as_request_refused");
        std::filesystem::remove(request);
        { std::ofstream out(request); }
        check(wsm::request_exists(request), "regular_request_accepted");
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "OBSERVER_IO unexpected: " << error.what() << '\n';
    }
    SetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", nullptr);
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
