#include "wsm/main_probe.h"
#include "wsm/window_hook.h"
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>

namespace {
struct Context {
    wsm::Il2CppApi api;
    Il2CppClass *manager = nullptr, *character = nullptr, *button = nullptr;
    const MethodInfo *find = nullptr, *get_value = nullptr, *active = nullptr, *enter = nullptr;
    bool coop = false;
    void (*log)(const std::string&) = nullptr;
};
Context ctx;
bool same(const char* a, const char* b) { return a && std::strcmp(a, b) == 0; }
Il2CppClass* find_class(Context& c, Il2CppDomain* d, const char* image_name, const char* ns, const char* name) {
    std::size_t n = 0; auto assemblies = c.api.domain_get_assemblies(d, &n);
    if (!assemblies) return nullptr;
    Il2CppClass* found = nullptr;
    for (std::size_t i = 0; i < n; ++i) {
        auto image = c.api.assembly_get_image(assemblies[i]);
        if (!image || !same(c.api.image_get_name(image), image_name)) continue;
        for (std::size_t j = 0; j < c.api.image_get_class_count(image); ++j) {
            auto klass = const_cast<Il2CppClass*>(c.api.image_get_class(image, j));
            if (klass && same(c.api.class_get_name(klass), name) && same(c.api.class_get_namespace(klass), ns)) {
                if (found) return nullptr;
                found = klass;
            }
        }
    }
    return found;
}
bool type_is(Context& c, const Il2CppType* t, const char* name) {
    if (!t) return false;
    char* text = c.api.type_get_name(t);
    bool match = same(text, name);
    if (text) c.api.free(text);
    return match;
}
const MethodInfo* find_method(Context& c, Il2CppClass* klass, const char* name,
                              const char* result, const char* param, bool is_static) {
    if (!klass) return nullptr;
    const MethodInfo* found = nullptr; void* iter = nullptr;
    while (auto method = c.api.class_get_methods(klass, &iter)) {
        if (!same(c.api.method_get_name(method), name) ||
            c.api.method_get_param_count(method) != (param ? 1u : 0u) ||
            !type_is(c, c.api.method_get_return_type(method), result)) continue;
        std::uint32_t impl = 0;
        if (((c.api.method_get_flags(method, &impl) & 0x10u) != 0) != is_static) continue;
        if (param && !type_is(c, c.api.method_get_param(method, 0), param)) continue;
        if (found) return nullptr;
        found = method;
    }
    return found;
}
void error_exception(Context& c, const char* operation, Il2CppObject* exception) {
    auto klass = c.api.object_get_class(exception);
    const char* ns = klass ? c.api.class_get_namespace(klass) : nullptr;
    const char* name = klass ? c.api.class_get_name(klass) : nullptr;
    c.log(std::string("ERROR: managed exception during ") + operation + ": " +
          (ns ? ns : "?") + "." + (name ? name : "?"));
}
bool enumerate(Context& c, Il2CppClass* klass, std::uintptr_t& count, Il2CppObject** array) {
    const auto type = c.api.class_get_type(klass);
    auto type_object = type ? c.api.type_get_object(type) : nullptr;
    if (!type_object) { c.log("ERROR: class type object unavailable"); return false; }
    void* args[] = {type_object};
    auto result = c.api.invoke(c.find, nullptr, args);
    if (result.exception) { error_exception(c, "FindObjectsOfType", result.exception); return false; }
    if (!result.value) { c.log("ERROR: FindObjectsOfType returned null"); return false; }
    count = c.api.array_length(reinterpret_cast<Il2CppArray*>(result.value));
    *array = result.value;
    return true;
}
void callback(void* raw) {
    auto& c = *static_cast<Context*>(raw);
    c.log("WSM_WINDOW_CALLBACK thread=" + std::to_string(GetCurrentThreadId()));
    if (!c.api.thread_current()) { c.log("ERROR: window thread is not attached to IL2CPP; enumeration refused"); return; }
    std::uintptr_t managers = 0, characters = 0, buttons = 0;
    Il2CppObject *array = nullptr;
    if (!enumerate(c, c.manager, managers, &array) ||
        !enumerate(c, c.character, characters, &array) ||
        !enumerate(c, c.button, buttons, &array)) return;
    if (buttons > static_cast<std::uintptr_t>((std::numeric_limits<std::int32_t>::max)())) {
        c.log("ERROR: coop button array exceeds Int32 indexing"); return;
    }
    std::uintptr_t active_buttons = 0;
    std::int32_t active_index = -1;
    for (std::int32_t i = 0; static_cast<std::uintptr_t>(i) < buttons; ++i) {
        void* args[] = {&i};
        auto item = c.api.invoke(c.get_value, array, args);
        if (item.exception) { error_exception(c, "Array.GetValue", item.exception); return; }
        if (!item.value) { c.log("ERROR: null button during enumeration"); return; }
        auto enabled = c.api.invoke(c.active, item.value, nullptr);
        if (enabled.exception) { error_exception(c, "Behaviour.isActiveAndEnabled", enabled.exception); return; }
        void* value = enabled.value ? c.api.object_unbox(enabled.value) : nullptr;
        if (!value) { c.log("ERROR: cannot unbox Behaviour.isActiveAndEnabled"); return; }
        if (*static_cast<std::uint8_t*>(value)) { ++active_buttons; active_index = i; }
    }
    c.log("WSM_MAIN_THREAD_COMPLETE managers=" + std::to_string(managers) +
          " characters=" + std::to_string(characters) + " coop_buttons=" + std::to_string(active_buttons));
    if (!c.coop) return;
    if (active_buttons != 1 || !c.enter) {
        c.log("WSM_COOP_UNAVAILABLE active_buttons=" + std::to_string(active_buttons) +
              (c.enter ? "" : " exact_method_missing")); return;
    }
    void* args[] = {&active_index};
    auto item = c.api.invoke(c.get_value, array, args);
    if (item.exception) { error_exception(c, "Array.GetValue", item.exception); return; }
    if (!item.value) { c.log("WSM_COOP_UNAVAILABLE null_button"); return; }
    auto result = c.api.invoke(c.enter, item.value, nullptr);
    if (result.exception) { error_exception(c, "EnterCoop", result.exception); return; }
    c.log("WSM_COOP_REQUESTED EnterCoop returned without managed exception; gameplay not verified");
}
struct Window { HWND hwnd = nullptr; DWORD thread = 0; };
BOOL CALLBACK find_window(HWND hwnd, LPARAM param) {
    DWORD pid = 0; DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t name[128] = {};
    if (!GetClassNameW(hwnd, name, 128) || std::wcscmp(name, L"UnityWndClass") != 0) return TRUE;
    auto& result = *reinterpret_cast<Window*>(param);
    result.hwnd = hwnd; result.thread = tid; return FALSE;
}
}
namespace wsm {
void run_main_probe(HMODULE bridge, HMODULE game, Il2CppApi api, Il2CppDomain* domain,
                    bool coop, void (*log)(const std::string&)) {
    ctx.api = api; ctx.coop = coop; ctx.log = log;
    std::string missing;
    if (!ctx.api.resolve_probe(game, missing)) { log("ERROR: main probe missing export " + missing); return; }
    auto& c = ctx;
    auto unity = find_class(c, domain, "UnityEngine.CoreModule.dll", "UnityEngine", "Object");
    auto behaviour = find_class(c, domain, "UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour");
    auto array = find_class(c, domain, "mscorlib.dll", "System", "Array");
    c.manager = find_class(c, domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Framework", "GameManager");
    c.character = find_class(c, domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Objects.Characters", "CharacterController");
    c.button = find_class(c, domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.App.Scripts.UI", "EnterCoopButton");
    c.find = find_method(c, unity, "FindObjectsOfType", "UnityEngine.Object[]", "System.Type", true);
    c.get_value = find_method(c, array, "GetValue", "System.Object", "System.Int32", false);
    c.active = find_method(c, behaviour, "get_isActiveAndEnabled", "System.Boolean", nullptr, false);
    c.enter = find_method(c, c.button, "EnterCoop", "System.Void", nullptr, false);
    if (!c.manager || !c.character || !c.button || !c.find || !c.get_value || !c.active) {
        log("ERROR: main probe missing/ambiguous metadata manager=" + std::to_string(c.manager != nullptr) +
            " character=" + std::to_string(c.character != nullptr) + " button=" + std::to_string(c.button != nullptr) +
            " find=" + std::to_string(c.find != nullptr) + " get_value=" + std::to_string(c.get_value != nullptr) +
            " isActiveAndEnabled=" + std::to_string(c.active != nullptr)); return;
    }
    log("WSM_MAIN_THREAD_ARMING: exact metadata resolved; waiting for owned UnityWndClass window");
    ULONGLONG deadline = GetTickCount64() + 45000;
    Window window;
    while (GetTickCount64() < deadline && !window.hwnd) { EnumWindows(find_window, reinterpret_cast<LPARAM>(&window)); if (!window.hwnd) Sleep(50); }
    if (!window.hwnd) { log("ERROR: main probe timeout waiting for owned UnityWndClass window"); return; }
    log("WSM_WINDOW_FOUND thread=" + std::to_string(window.thread));
    auto remaining = deadline > GetTickCount64() ? static_cast<DWORD>(deadline - GetTickCount64()) : 0;
    auto result = run_window_hook(bridge, window.hwnd, window.thread, callback, &ctx, remaining);
    if (result == HookResult::timeout) log("ERROR: main probe timeout waiting for window message callback");
    if (result == HookResult::failed) log("ERROR: failed to install or trigger window message hook win32=" + std::to_string(GetLastError()));
}
}
