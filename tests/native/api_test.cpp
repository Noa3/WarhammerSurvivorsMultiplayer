#include "wsm/il2cpp_api.h"
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    HMODULE fixture = LoadLibraryA(argv[1]);
    if (!fixture) { std::puts("FAIL: fixture load"); return 1; }
    wsm::Il2CppApi api; std::string missing;
    if (!wsm::Il2CppApi::resolve(fixture, api, missing)) { std::printf("FAIL: fixture export %s\n", missing.c_str()); return 1; }
    if (!api.resolve_probe(fixture, missing) || !api.array_length || !api.thread_current) {
        std::printf("FAIL: probe fixture export %s\n", missing.c_str()); return 1;
    }
    wsm::Il2CppApi optional;
    if (optional.resolve_probe(GetModuleHandleW(L"kernel32.dll"), missing) || missing != "il2cpp_class_get_type") {
        std::puts("FAIL: missing probe export not diagnosed"); return 1;
    }
    auto invocation = api.invoke(nullptr, nullptr, nullptr);
    if (invocation.value != nullptr || invocation.exception != reinterpret_cast<Il2CppObject*>(0x1234)) {
        std::puts("FAIL: runtime exception not surfaced"); return 1;
    }
    HMODULE incomplete = GetModuleHandleW(L"kernel32.dll");
    if (wsm::Il2CppApi::resolve(incomplete, api, missing) || missing != "il2cpp_domain_get" || api.runtime_invoke || api.domain_get) {
        std::puts("FAIL: incomplete API was not rejected atomically"); return 1;
    }
    if (wsm::Il2CppApi::resolve(nullptr, api, missing) || missing.empty()) {
        std::puts("FAIL: null module not rejected"); return 1;
    }
    FreeLibrary(fixture);
    std::puts("PASS: export resolution, failure isolation, exception propagation");
    return 0;
}
