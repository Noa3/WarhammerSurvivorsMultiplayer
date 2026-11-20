#include "wsm/ingame_ui.h"
#include "wsm/endpoint_editor.h"
#include "wsm/portrait_layout.h"
#include "wsm/ingame_localization.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct Il2CppString;
namespace {
struct Vec2 { float x, y; };
struct Color { float r, g, b, a; };
bool same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
}

namespace wsm {
using i18n::Text;
struct InGameMenu::Impl {
    Il2CppApi api{};
    HWND owner = nullptr;
    DWORD thread = 0;
    Il2CppObject* (*object_new)(const Il2CppClass*) = nullptr;
    Il2CppArray* (*array_new)(Il2CppClass*, std::uintptr_t) = nullptr;
    Il2CppString* (*string_new)(const char*) = nullptr;
    std::int32_t (*string_length)(Il2CppString*) = nullptr;
    const wchar_t* (*string_chars)(Il2CppString*) = nullptr;
    void* (*gchandle_new)(Il2CppObject*, bool) = nullptr;
    void (*gchandle_free)(void*) = nullptr;
    std::vector<void*> handles;
    std::unordered_map<std::string, Il2CppClass*> classes;
    std::unordered_map<std::string, const MethodInfo*> methods;
    Il2CppArray* type_array = nullptr;
    Il2CppClass* (*class_from_type)(const Il2CppType*) = nullptr;
    Il2CppObject *raycast_results = nullptr, *pointer_event = nullptr, *pointer_owner = nullptr;
    Il2CppObject *fallback_events = nullptr, *fallback_event_object = nullptr;
    void* pointer_handle = nullptr;
    Il2CppObject *root = nullptr, *panel = nullptr, *tab = nullptr, *font = nullptr, *canvas = nullptr;
    float last_scale = 0;
    bool stock_portrait_layout = false;
    ULONGLONG next_portrait_check = 0;
    Il2CppObject *address = nullptr, *port = nullptr, *status_text = nullptr;
    Il2CppObject *address_rect = nullptr, *port_rect = nullptr;
    Il2CppObject *host_rect = nullptr, *join_rect = nullptr, *leave_rect = nullptr, *tab_rect = nullptr;
    Il2CppObject *camera_own_rect = nullptr, *camera_shared_rect = nullptr, *camera_status_text = nullptr;
    bool camera_buttons_visible = false;
    HostRules draft_rules;
    Il2CppObject* rules_group = nullptr;
    std::array<Il2CppObject*, 3> rule_rects{}, rule_labels{};
    bool rules_visible = false;
    std::string drawn_rules_language;
    bool camera_status_initialized = false;
    Il2CppObject *local_name_text = nullptr, *names_hud = nullptr, *names_title = nullptr;
    std::array<Il2CppObject*, 9> name_rows{};
    bool names_initialized = false;
    unsigned last_name_capabilities = 0;
    std::string last_local_name, last_roster, last_names_language;
    bool visible = true, foreground = false, mouse_down = false, f6_down = false;
    Text status_override = Text::Count;
    std::string last_status, last_camera_status, language = "en";
    std::vector<std::pair<Il2CppObject*, Text>> localized_labels;
    std::vector<Il2CppObject*> text_components;
    std::unordered_map<std::string, Il2CppObject*> owned_fonts;
    Il2CppObject* default_font = nullptr;
#ifdef WSM_INGAME_UI_TESTING
    bool fail_language_label = false;
#endif
    struct EditorField {
        EndpointEditor editor;
        Vec2 reference_position{};
        Il2CppObject *input = nullptr, *background = nullptr, *caret = nullptr, *selection = nullptr;
        explicit EditorField(EndpointKind kind) : editor(kind) {}
    };
    EditorField address_editor{EndpointKind::Address}, port_editor{EndpointKind::Port};
    EditorField* editing = nullptr;
    Vec2 editing_point{};
    Il2CppObject* endpoint_measure = nullptr;
    struct InputEvent { UINT message; POINT point; WPARAM key; bool control; bool shift; bool repeat; };
    std::array<InputEvent, 128> pending_clicks{};
    std::size_t pending_count = 0;
    Il2CppObject* pressed_rect = nullptr;
    bool subclass_installed = false;

    ~Impl() = default;
    static LRESULT CALLBACK window_input(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<Impl*>(data);
        if (message == WM_KILLFOCUS) {
            self->pending_count = 0;
            self->pressed_rect = nullptr;
            self->editing = nullptr;
        } else if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_CANCELMODE || message == WM_CAPTURECHANGED ||
                    message == WM_CHAR || message == WM_KEYDOWN) &&
                   GetForegroundWindow() == hwnd) {
            // WndProc only records events. Unity calls remain on the tick path.
            if (self->pending_count < self->pending_clicks.size())
                self->pending_clicks[self->pending_count++] = {message, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, wp, (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (lp & (1LL << 30)) != 0};
            else {
                // An incomplete gesture must never become an accidental click.
                self->pending_count = 0;
                self->pressed_rect = nullptr;
                self->editing = nullptr;
            }
        }
        if ((message == WM_CHAR || message == WM_KEYDOWN) && self->editing && GetForegroundWindow() == hwnd &&
            !(message == WM_KEYDOWN && wp == VK_F6)) return 0;
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, window_input, id);
            self->subclass_installed = false;
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    void install_input() {
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&window_input), &pinned) ||
            !SetWindowSubclass(owner, window_input, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
            fail("cannot install owned-window input capture");
        subclass_installed = true;
    }
    [[noreturn]] static void fail(const std::string& message) { throw std::runtime_error(message); }
    void retain(Il2CppObject* obj) {
        if (!obj) fail("cannot retain a null managed object");
        auto h = gchandle_new(obj, false);
        if (!h) fail("il2cpp_gchandle_new failed");
        handles.push_back(h);
    }
    template<typename T> void load(HMODULE game, T& target, const char* name) {
        target = reinterpret_cast<T>(GetProcAddress(game, name));
        if (!target) fail(std::string("missing export ") + name);
    }
    Il2CppClass* klass(Il2CppDomain* domain, const char* image_name, const char* ns, const char* name) {
        std::size_t n = 0; auto assemblies = api.domain_get_assemblies(domain, &n);
        if (!assemblies) fail("no IL2CPP assemblies");
        Il2CppClass* found = nullptr;
        for (std::size_t i = 0; i < n; ++i) {
            auto image = api.assembly_get_image(assemblies[i]);
            if (!image || !same(api.image_get_name(image), image_name)) continue;
            for (std::size_t j = 0; j < api.image_get_class_count(image); ++j) {
                auto c = const_cast<Il2CppClass*>(api.image_get_class(image, j));
                if (c && same(api.class_get_namespace(c), ns) && same(api.class_get_name(c), name)) {
                    if (found) fail(std::string("ambiguous class ") + ns + "." + name);
                    found = c;
                }
            }
        }
        if (!found) fail(std::string("missing class ") + image_name + ":" + ns + "." + name);
        return found;
    }
    void add_class(Il2CppDomain* domain, const char* id, const char* image, const char* ns, const char* name) {
        classes.emplace(id, klass(domain, image, ns, name));
    }
    bool type_is(const Il2CppType* t, const char* expected) {
        if (!t) return false;
        char* name = api.type_get_name(t); bool result = same(name, expected);
        if (name) api.free(name); return result;
    }
    void method(const char* id, const char* class_id, const char* name, const char* result,
                std::initializer_list<const char*> params, bool is_static = false, bool required_method = true) {
        const MethodInfo* found = nullptr; void* iter = nullptr;
        while (auto m = api.class_get_methods(classes.at(class_id), &iter)) {
            if (!same(api.method_get_name(m), name) || api.method_get_param_count(m) != params.size() ||
                !type_is(api.method_get_return_type(m), result)) continue;
            std::uint32_t flags = 0;
            if (((api.method_get_flags(m, &flags) & 0x10u) != 0) != is_static) continue;
            std::size_t i = 0; bool match = true;
            for (const char* p : params) { if (!type_is(api.method_get_param(m, static_cast<std::uint32_t>(i++)), p)) { match = false; break; } }
            if (!match) continue;
            if (found) fail(std::string("ambiguous method ") + class_id + "." + name);
            found = m;
        }
        if (!found && required_method) fail(std::string("missing exact method ") + class_id + "." + name);
        if (found) methods.emplace(id, found);
    }
    Il2CppObject* call(const char* id, void* object = nullptr, void** args = nullptr) {
        auto result = api.invoke(methods.at(id), object, args);
        if (result.exception) {
            auto c = api.object_get_class(result.exception);
            fail(std::string("managed exception in ") + id + ": " + (c && api.class_get_name(c) ? api.class_get_name(c) : "unknown"));
        }
        return result.value;
    }
    Il2CppObject* required(Il2CppObject* object, const char* what) { if (!object) fail(std::string("null ") + what); return object; }
    Il2CppObject* type_object(const char* id) {
        auto t = api.class_get_type(classes.at(id));
        return required(t ? api.type_get_object(t) : nullptr, id);
    }
    void set_string(const char* id, Il2CppObject* object, const std::string& text) {
        auto str = reinterpret_cast<Il2CppObject*>(string_new(text.c_str()));
        if (!str) fail("cannot allocate string");
        auto handle = gchandle_new(str, false); if (!handle) fail("cannot retain string");
        try { void* args[] = {str}; call(id, object, args); } catch (...) { gchandle_free(handle); throw; }
        gchandle_free(handle);
    }
    std::string get_string(Il2CppObject* str) {
        if (!str) return {};
        auto s = reinterpret_cast<Il2CppString*>(str); auto len = string_length(s); auto chars = string_chars(s);
        if (len < 0 || len > 4096 || (!chars && len)) fail("invalid managed string");
        if (!len) return {};
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, len, nullptr, 0, nullptr, nullptr);
        if (!n) fail("invalid UTF-16 in field");
        std::string output(static_cast<std::size_t>(n), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, len, &output[0], n, nullptr, nullptr)) fail("UTF-8 conversion failed");
        return output;
    }
    void set_value(const char* method_id, Il2CppObject* object, void* value) { void* args[] = {value}; call(method_id, object, args); }
    void set_ref(const char* method_id, Il2CppObject* object, Il2CppObject* value) { void* args[] = {value}; call(method_id, object, args); }
    Il2CppObject* add_component(Il2CppObject* go, const char* class_id) {
        void* args[] = {type_object(class_id)};
        auto c = required(call("add_component", go, args), class_id); retain(c); return c;
    }
    Il2CppObject* make_go(const char* name, Il2CppObject* parent, Vec2 size, Vec2 pos, Vec2 anchor = {0.5f, 0.5f}) {
        auto go = required(object_new(classes.at("GameObject")), "GameObject allocation"); retain(go);
        auto name_string = reinterpret_cast<Il2CppObject*>(string_new(name));
        if (!name_string) fail("cannot allocate GameObject name");
        auto name_handle = gchandle_new(name_string, false); if (!name_handle) fail("cannot retain GameObject name");
        try {
            auto rect_type = type_object("RectTransform"); int index = 0;
            void* set_args[] = {rect_type, &index}; call("array_set", type_array, set_args);
            void* args[] = {name_string, type_array}; call("go_ctor", go, args);
        } catch (...) { gchandle_free(name_handle); throw; }
        gchandle_free(name_handle);
        if (!parent && !root) root = go; // Ensure later setup failures still destroy our root.
        auto rect = required(call("transform", go), "RectTransform"); retain(rect);
        if (parent) { auto pt = required(call("transform", parent), "parent transform"); bool stays = false; void* args[] = {pt, &stays}; call("parent", rect, args); }
        set_value("anchor_min", rect, &anchor); set_value("anchor_max", rect, &anchor);
        Vec2 pivot{0.5f, 0.5f}; set_value("pivot", rect, &pivot);
        set_value("size", rect, &size); set_value("position", rect, &pos);
        return go;
    }
    Il2CppObject* rectangle(Il2CppObject* go) { return required(call("transform", go), "button transform"); }
    Il2CppObject* image(Il2CppObject* go, Color color) {
        auto c = add_component(go, "Image"); set_value("color", c, &color); return c;
    }
    Il2CppObject* text(Il2CppObject* parent, const char* name, const std::string& value, Vec2 size, Vec2 pos, int font_size = 20) {
        auto go = make_go(name, parent, size, pos);
        auto c = add_component(go, "Text"); set_ref("font", c, font);
        set_value("font_size", c, &font_size); int alignment = 4; set_value("alignment", c, &alignment);
        Color white{0.96f, 0.96f, 0.98f, 1.0f}; set_value("color", c, &white);
        bool raycast = false; set_value("raycast", c, &raycast);
        set_string("text", c, value); text_components.push_back(c); return c;
    }
    Il2CppObject* label(Il2CppObject* parent, const char* name, Text id, Vec2 size, Vec2 pos, int font_size = 20) {
        auto result = text(parent, name, std::string(i18n::text(id, language)), size, pos, font_size);
        localized_labels.emplace_back(result, id); return result;
    }
    Il2CppObject* name_text(Il2CppObject* parent, const char* name, Vec2 size, Vec2 pos, int font_size) {
        auto component = text(parent, name, "", size, pos, font_size);
        bool rich_text = false; set_value("rich_text", component, &rich_text);
        return component;
    }
    static bool valid_name(const std::string& value) {
        if (value.empty() || value.size() > 192) return false;
        for (unsigned char c : value) if (c < 32 || c == 127) return false;
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0) > 0;
    }
    float text_width(Il2CppObject* component, const std::string& value) {
        set_string("text", component, value);
        auto boxed = required(call("preferred_width", component), "name width");
        auto type = api.object_get_class(boxed);
        if (!type || !same(api.class_get_namespace(type), "System") || !same(api.class_get_name(type), "Single")) fail("name width is not Single");
        auto raw = api.object_unbox(boxed); if (!raw) fail("cannot unbox name width");
        auto width = *static_cast<float*>(raw);
        if (!std::isfinite(width) || width < 0) fail("invalid name width");
        return width;
    }
    void fit_name(Il2CppObject* component, const std::string& value, float width) {
        if (text_width(component, value) <= width) return;
        std::vector<std::size_t> ends{0};
        for (std::size_t i = 1; i <= value.size(); ++i)
            if (i == value.size() || (static_cast<unsigned char>(value[i]) & 0xc0) != 0x80) ends.push_back(i);
        std::size_t low = 0, high = ends.size() - 1;
        while (low < high) {
            const auto middle = (low + high + 1) / 2;
            if (text_width(component, value.substr(0, ends[middle]) + "…") <= width) low = middle;
            else high = middle - 1;
        }
        set_string("text", component, value.substr(0, ends[low]) + "…");
    }
    void update_names(const InGameMenuActions& actions) {
        if (!names_hud) return;
        const auto local = actions.local_player_name ? actions.local_player_name(actions.context, language) : std::string{};
        const auto roster = actions.player_names ? actions.player_names(actions.context, language) : std::string{};
        const unsigned capabilities = (actions.local_player_name ? 1u : 0u) | (actions.player_names ? 2u : 0u);
        if (names_initialized && capabilities == last_name_capabilities && local == last_local_name &&
            roster == last_roster && language == last_names_language) return;
        bool show_local = actions.local_player_name != nullptr;
        bool show_hud = show_local || actions.player_names != nullptr;
        set_value("active", call("component_object", local_name_text), &show_local);
        set_value("active", names_hud, &show_hud);
        if (show_local) fit_name(local_name_text, i18n::format(Text::LocalPlayerName, language,
            {{"name", valid_name(local) ? local : std::string(i18n::text(Text::PlayerFallback, language))}}), 540);
        std::vector<std::string> names;
        if (roster.size() <= 4096) {
            std::size_t start = 0;
            while (start < roster.size()) {
                auto end = roster.find('\n', start);
                const auto name = roster.substr(start, end == std::string::npos ? end : end - start);
                if (!valid_name(name) || names.size() == name_rows.size()) { names.clear(); break; }
                names.push_back(name);
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        if (names.empty() && show_local) names.push_back(valid_name(local) ? local : std::string(i18n::text(Text::PlayerFallback, language)));
        const float height = static_cast<float>(names.size()) * 22 + 34;
        Vec2 size{320, height}, position{176, height / 2 + 12};
        set_value("size", rectangle(names_hud), &size); set_value("position", rectangle(names_hud), &position);
        position = {0, height / 2 - 15}; set_value("position", rectangle(call("component_object", names_title)), &position);
        for (std::size_t i = 0; i < name_rows.size(); ++i) {
            bool active = i < names.size();
            set_value("active", call("component_object", name_rows[i]), &active);
            if (!active) continue;
            position = {0, height / 2 - 39 - static_cast<float>(i) * 22};
            set_value("position", rectangle(call("component_object", name_rows[i])), &position);
            fit_name(name_rows[i], names[i], 296);
        }
        last_local_name = local; last_roster = roster; last_names_language = language;
        last_name_capabilities = capabilities; names_initialized = true;
    }
    std::string read_language() {
        if (!methods.count("current_language")) return "en";
        // Read only the game's getter. Never change game language or PlayerPrefs.
        const auto result = api.invoke(methods.at("current_language"), nullptr, nullptr);
        if (result.exception || !result.value) return "en";
        return i18n::normalize_language(get_string(result.value));
    }
    Il2CppObject* language_font(const std::string& requested) {
        if (!methods.count("os_font")) return default_font;
        const std::string family = requested == "ja" ? "Yu Gothic" : requested == "ko" ? "Malgun Gothic" :
            requested == "zh-CN" ? "Microsoft YaHei" : "Arial";
        if (auto found = owned_fonts.find(family); found != owned_fonts.end()) return found->second;
        auto name = reinterpret_cast<Il2CppObject*>(string_new(family.c_str()));
        if (!name) fail("cannot allocate font family");
        auto handle = gchandle_new(name, false); if (!handle) fail("cannot retain font family");
        int size = 20; void* args[] = {name, &size};
        const auto result = api.invoke(methods.at("os_font"), nullptr, args);
        gchandle_free(handle);
        if (result.exception || !result.value) return default_font;
        retain(result.value); owned_fonts.emplace(family, result.value); return result.value;
    }
    void refresh_language() {
        const auto current = read_language();
        if (current == language) return;
        auto next_font = language_font(current);
        for (auto component : text_components) set_ref("font", component, next_font);
        for (const auto& item : localized_labels) {
#ifdef WSM_INGAME_UI_TESTING
            if (fail_language_label && item.second == Text::Title) { fail_language_label = false; fail("fixture one-shot label setter failure"); }
#endif
            set_string("text", item.first, std::string(i18n::text(item.second, current)));
        }
        // Commit the applied locale only after every setter succeeds. A one-shot
        // failure leaves the old locale committed so the next tick retries.
        language = current; font = next_font;
        if (status_override == Text::UiError) status_override = Text::Count;
        last_status.clear();
        last_camera_status.clear();
        camera_status_initialized = false;
    }
    Il2CppObject* input(Il2CppObject* parent, const char* name, const char* initial, Vec2 size, Vec2 pos, EditorField& editor) {
        auto go = make_go(name, parent, size, pos);
        auto bg = image(go, {0.10f, 0.13f, 0.18f, 1.0f});
        auto field = add_component(go, "InputField");
        set_ref("target_graphic", field, bg);
        auto label = text(go, "WSM Input Text", "", {size.x - 16.0f, size.y - 4.0f}, {0, 0}, 19);
        set_ref("text_component", field, label); set_string("input_text_set", field, initial);
        // This field is a view of our editor, not an owner of vanilla selection.
        bool read_only = true; set_value("input_readonly", field, &read_only);
        editor.input = field; editor.background = bg; editor.reference_position = pos;
        editor.selection = make_go((std::string(name) + " Selection").c_str(), go, {0, 25}, {0, 0});
        auto selection = image(editor.selection, {0.15f, 0.45f, 0.85f, 0.35f});
        bool raycast = false; set_value("raycast", selection, &raycast);
        editor.caret = make_go((std::string(name) + " Caret").c_str(), go, {1.5f, 25}, {0, 0});
        auto caret = image(editor.caret, {0.94f, 0.96f, 1.0f, 1.0f}); set_value("raycast", caret, &raycast);
        bool hidden = false; set_value("active", editor.selection, &hidden); set_value("active", editor.caret, &hidden);
        return field;
    }
    Il2CppObject* button(Il2CppObject* parent, const char* name, Text caption, Vec2 size, Vec2 pos, int font_size = 20, Il2CppObject** dynamic_label = nullptr) {
        auto go = make_go(name, parent, size, pos);
        auto bg = image(go, {0.22f, 0.32f, 0.42f, 1.0f});
        auto b = add_component(go, "Button"); set_ref("target_graphic", b, bg);
        if (dynamic_label) *dynamic_label = text(go, "WSM Button Text", "", size, {0, 0}, font_size);
        else label(go, "WSM Button Text", caption, size, {0, 0}, font_size);
        return rectangle(go);
    }
    void resolve(HMODULE game, Il2CppDomain* domain) {
        if (!game || !domain || !api.domain_get_assemblies || !api.assembly_get_image || !api.image_get_name ||
            !api.image_get_class_count || !api.image_get_class || !api.class_get_namespace || !api.class_get_name ||
            !api.class_get_methods || !api.method_get_name || !api.method_get_param_count || !api.method_get_return_type ||
            !api.method_get_param || !api.method_get_flags || !api.type_get_name || !api.free || !api.runtime_invoke ||
            !api.class_get_type || !api.type_get_object || !api.array_length || !api.object_get_class || !api.object_unbox)
            fail("incomplete IL2CPP reflection API");
        load(game, class_from_type, "il2cpp_class_from_type");
        load(game, object_new, "il2cpp_object_new"); load(game, array_new, "il2cpp_array_new");
        load(game, string_new, "il2cpp_string_new"); load(game, string_length, "il2cpp_string_length");
        load(game, string_chars, "il2cpp_string_chars"); load(game, gchandle_new, "il2cpp_gchandle_new"); load(game, gchandle_free, "il2cpp_gchandle_free");
        add_class(domain, "GameObject", "UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
        add_class(domain, "Object", "UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        add_class(domain, "Transform", "UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
        add_class(domain, "RectTransform", "UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform");
        add_class(domain, "Resources", "UnityEngine.CoreModule.dll", "UnityEngine", "Resources");
        add_class(domain, "Screen", "UnityEngine.CoreModule.dll", "UnityEngine", "Screen");
        add_class(domain, "Font", "UnityEngine.TextRenderingModule.dll", "UnityEngine", "Font");
        add_class(domain, "Canvas", "UnityEngine.UIModule.dll", "UnityEngine", "Canvas");
        add_class(domain, "RectUtil", "UnityEngine.UIModule.dll", "UnityEngine", "RectTransformUtility");
        add_class(domain, "Graphic", "UnityEngine.UI.dll", "UnityEngine.UI", "Graphic");
        add_class(domain, "Image", "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
        add_class(domain, "Text", "UnityEngine.UI.dll", "UnityEngine.UI", "Text");
        add_class(domain, "InputField", "UnityEngine.UI.dll", "UnityEngine.UI", "InputField");
        add_class(domain, "Selectable", "UnityEngine.UI.dll", "UnityEngine.UI", "Selectable");
        add_class(domain, "Button", "UnityEngine.UI.dll", "UnityEngine.UI", "Button");
        add_class(domain, "Raycaster", "UnityEngine.UI.dll", "UnityEngine.UI", "GraphicRaycaster");
        // Localization is optional for SDK/unsupported builds: explicit English fallback.
        try { add_class(domain, "Language", "l2localization.dll", "I2.Loc", "LocalizationManager"); }
        catch (const std::runtime_error&) { }
        add_class(domain, "Component", "UnityEngine.CoreModule.dll", "UnityEngine", "Component");
        add_class(domain, "Behaviour", "UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour");
        add_class(domain, "EventSystem", "UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem");
        add_class(domain, "PointerEventData", "UnityEngine.UI.dll", "UnityEngine.EventSystems", "PointerEventData");
        add_class(domain, "RaycastResult", "UnityEngine.UI.dll", "UnityEngine.EventSystems", "RaycastResult");
        try { add_class(domain, "InputModule", "UnityEngine.UI.dll", "UnityEngine.EventSystems", "StandaloneInputModule"); }
        catch (const std::runtime_error&) { /* A compatible game-owned module can still provide input. */ }
        add_class(domain, "Array", "mscorlib.dll", "System", "Array");
        add_class(domain, "Type", "mscorlib.dll", "System", "Type");
        if (classes.count("Language")) method("current_language", "Language", "get_CurrentLanguageCode", "System.String", {}, true, false);
        method("os_font", "Font", "CreateDynamicFontFromOSFont", "UnityEngine.Font", {"System.String", "System.Int32"}, true, false);
        method("event_current", "EventSystem", "get_current", "UnityEngine.EventSystems.EventSystem", {}, true);
        method("event_current_set", "EventSystem", "set_current", "System.Void", {"UnityEngine.EventSystems.EventSystem"}, true);
        method("behaviour_active", "Behaviour", "get_isActiveAndEnabled", "System.Boolean", {});
        method("component_object", "Component", "get_gameObject", "UnityEngine.GameObject", {});
        method("pointer_ctor", "PointerEventData", ".ctor", "System.Void", {"UnityEngine.EventSystems.EventSystem"});
        method("pointer_position", "PointerEventData", "set_position", "System.Void", {"UnityEngine.Vector2"});
        method("raycast_object", "RaycastResult", "get_gameObject", "UnityEngine.GameObject", {});
        void* raycast_iter = nullptr;
        while (auto candidate = api.class_get_methods(classes.at("EventSystem"), &raycast_iter)) {
            if (!same(api.method_get_name(candidate), "RaycastAll") || api.method_get_param_count(candidate) != 2 ||
                !type_is(api.method_get_return_type(candidate), "System.Void") ||
                !type_is(api.method_get_param(candidate, 0), "UnityEngine.EventSystems.PointerEventData")) continue;
            auto list = class_from_type(api.method_get_param(candidate, 1));
            if (!list || !same(api.class_get_namespace(list), "System.Collections.Generic") || !same(api.class_get_name(list), "List`1")) continue;
            if (methods.count("raycast_all")) fail("ambiguous EventSystem.RaycastAll");
            methods.emplace("raycast_all", candidate); classes.emplace("RaycastResults", list);
        }
        if (!methods.count("raycast_all")) fail("missing supported EventSystem.RaycastAll");
        method("results_ctor", "RaycastResults", ".ctor", "System.Void", {});
        method("results_clear", "RaycastResults", "Clear", "System.Void", {});
        method("results_count", "RaycastResults", "get_Count", "System.Int32", {});
        method("results_item", "RaycastResults", "get_Item", "UnityEngine.EventSystems.RaycastResult", {"System.Int32"});
        method("go_ctor", "GameObject", ".ctor", "System.Void", {"System.String", "System.Type[]"});
        method("add_component", "GameObject", "AddComponent", "UnityEngine.Component", {"System.Type"});
        method("transform", "GameObject", "get_transform", "UnityEngine.Transform", {});
        method("active", "GameObject", "SetActive", "System.Void", {"System.Boolean"});
        method("destroy", "Object", "Destroy", "System.Void", {"UnityEngine.Object"}, true);
        method("persist", "Object", "DontDestroyOnLoad", "System.Void", {"UnityEngine.Object"}, true);
        method("parent", "Transform", "SetParent", "System.Void", {"UnityEngine.Transform", "System.Boolean"});
        method("anchor_min", "RectTransform", "set_anchorMin", "System.Void", {"UnityEngine.Vector2"});
        method("anchor_max", "RectTransform", "set_anchorMax", "System.Void", {"UnityEngine.Vector2"});
        method("pivot", "RectTransform", "set_pivot", "System.Void", {"UnityEngine.Vector2"});
        method("size", "RectTransform", "set_sizeDelta", "System.Void", {"UnityEngine.Vector2"});
        method("position", "RectTransform", "set_anchoredPosition", "System.Void", {"UnityEngine.Vector2"});
        method("find_fonts", "Resources", "FindObjectsOfTypeAll", "UnityEngine.Object[]", {"System.Type"}, true);
        method("builtin_font", "Resources", "GetBuiltinResource", "UnityEngine.Object", {"System.Type", "System.String"}, true, false);
        method("screen_width", "Screen", "get_width", "System.Int32", {}, true);
        method("screen_height", "Screen", "get_height", "System.Int32", {}, true);
        method("render_mode", "Canvas", "set_renderMode", "System.Void", {"UnityEngine.RenderMode"});
        method("scale_factor", "Canvas", "set_scaleFactor", "System.Void", {"System.Single"});
        method("sorting_order", "Canvas", "set_sortingOrder", "System.Void", {"System.Int32"});
        method("hit", "RectUtil", "RectangleContainsScreenPoint", "System.Boolean", {"UnityEngine.RectTransform", "UnityEngine.Vector2", "UnityEngine.Camera"}, true);
        method("color", "Graphic", "set_color", "System.Void", {"UnityEngine.Color"});
        method("raycast", "Graphic", "set_raycastTarget", "System.Void", {"System.Boolean"});
        method("font", "Text", "set_font", "System.Void", {"UnityEngine.Font"});
        method("font_size", "Text", "set_fontSize", "System.Void", {"System.Int32"});
        method("alignment", "Text", "set_alignment", "System.Void", {"UnityEngine.TextAnchor"});
        method("text", "Text", "set_text", "System.Void", {"System.String"});
        method("rich_text", "Text", "set_supportRichText", "System.Void", {"System.Boolean"}, false, false);
        method("input_readonly", "InputField", "set_readOnly", "System.Void", {"System.Boolean"});
        method("preferred_width", "Text", "get_preferredWidth", "System.Single", {});
        method("input_text_set", "InputField", "set_text", "System.Void", {"System.String"});
        method("input_text_get", "InputField", "get_text", "System.String", {});
        method("text_component", "InputField", "set_textComponent", "System.Void", {"UnityEngine.UI.Text"});
        method("target_graphic", "Selectable", "set_targetGraphic", "System.Void", {"UnityEngine.UI.Graphic"});
        method("array_get", "Array", "GetValue", "System.Object", {"System.Int32"});
        method("array_set", "Array", "SetValue", "System.Void", {"System.Object", "System.Int32"});
        // Optional game-specific UI; absence must not prevent using the menu.
        try {
            add_class(domain, "StockEquipment", "VampireSurvivors.Runtime.dll", "Wilson.UI", "WSGameEquipmentPanel");
            method("stock_find", "GameObject", "Find", "UnityEngine.GameObject", {"System.String"}, true);
            method("stock_component", "GameObject", "GetComponent", "UnityEngine.Component", {"System.Type"});
            method("stock_count", "Transform", "get_childCount", "System.Int32", {});
            method("stock_child", "Transform", "GetChild", "UnityEngine.Transform", {"System.Int32"});
            method("stock_descendant", "Transform", "Find", "UnityEngine.Transform", {"System.String"});
            method("stock_size", "RectTransform", "get_sizeDelta", "UnityEngine.Vector2", {});
            method("stock_active", "GameObject", "get_activeInHierarchy", "System.Boolean", {});
            stock_portrait_layout = true;
        } catch (const std::runtime_error&) { /* No supported stock HUD bindings. */ }
    }
    void update_stock_portraits() {
        const auto now = GetTickCount64();
        if (!stock_portrait_layout || now < next_portrait_check) return;
        next_portrait_check = now + 500; // Discover late spawns; never retain scene objects across ticks.
        void* path_args[] = {string_new("/GAME UI/Canvas - Game UI/Safe area/Aspect Restricted Safe Area - New UI/View - Main Game WS - NEW/EquipmentPanelContainer")};
        auto container = call("stock_find", nullptr, path_args);
        if (!container) return;
        auto group = call("transform", container);
        auto count = static_cast<int*>(api.object_unbox(required(call("stock_count", group), "childCount")));
        if (!count) return;
        struct Portrait { Il2CppObject *slot, *icon; };
        layout_stock_portraits(*count, [&](int i) -> std::optional<Portrait> {
            void* index[] = {&i}; auto slot = call("stock_child", group, index);
            if (!slot || api.object_get_class(slot) != classes.at("RectTransform")) return {};
            void* type[] = {type_object("StockEquipment")};
            auto panel_object = call("component_object", slot);
            if (!call("stock_component", panel_object, type) || !boolean(call("stock_active", panel_object))) return {};
            void* icon_path[] = {string_new("Horizontal Layout Group/PlayerIcon")};
            auto icon = call("stock_descendant", slot, icon_path);
            if (!icon || api.object_get_class(icon) != classes.at("RectTransform") ||
                !boolean(call("stock_active", call("component_object", icon)))) return {};
            return Portrait{slot, icon};
        }, [&](Portrait portrait) {
            auto size_of = [&](Il2CppObject* rect) {
                auto boxed = required(call("stock_size", rect), "stock RectTransform.sizeDelta");
                auto data = static_cast<Vec2*>(api.object_unbox(boxed));
                if (!data) fail("cannot unbox stock RectTransform.sizeDelta");
                return *data;
            };
            auto size = size_of(portrait.slot); const auto height = portrait_slot_height(size.y, size_of(portrait.icon).y);
            if (height > size.y) { size.y = height; set_value("size", portrait.slot, &size); }
        });
    }
    bool boolean(Il2CppObject* value) {
        auto object = required(value, "Boolean result"); auto c = api.object_get_class(object);
        if (!c || !same(api.class_get_namespace(c), "System") || !same(api.class_get_name(c), "Boolean")) fail("expected Boolean result");
        auto data = api.object_unbox(object); if (!data) fail("cannot unbox Boolean");
        return *static_cast<std::uint8_t*>(data) != 0;
    }
    void enable_owned_event_system() {
        if (!classes.count("InputModule")) fail("overlay requires the supported standalone input module");
        if (!fallback_event_object) {
            fallback_event_object = make_go("WSM Owned EventSystem", root, {0,0}, {0,0});
            fallback_events = add_component(fallback_event_object, "EventSystem");
            add_component(fallback_event_object, "InputModule");
        } else { bool enabled = true; set_value("active", fallback_event_object, &enabled); }
    }
    Il2CppObject* ensure_event_system() {
        auto current = call("event_current");
        if (visible) {
            // The game's input module can reselect its own menu every frame.
            // uGUI processes only its current EventSystem. Temporarily make ours
            // current; disabling it on close restores the game's registered system.
            enable_owned_event_system();
            if (current != fallback_events) set_ref("event_current_set", nullptr, fallback_events);
            return fallback_events;
        }
        if (current && current != fallback_events) {
            if (fallback_event_object) { bool disabled = false; set_value("active", fallback_event_object, &disabled); }
            return current;
        }
        if (fallback_events && current == fallback_events) {
            void* args[] = {type_object("EventSystem")};
            auto systems = required(call("find_fonts", nullptr, args), "event system inventory");
            auto held = gchandle_new(systems, false); if (!held) fail("cannot retain event system inventory");
            Il2CppObject* other = nullptr;
            try {
                const auto length = api.array_length(reinterpret_cast<Il2CppArray*>(systems));
                for (int index = 0; static_cast<std::uintptr_t>(index) < length; ++index) {
                    void* item[] = {&index}; auto candidate = call("array_get", systems, item);
                    if (candidate && candidate != fallback_events && boolean(call("behaviour_active", candidate))) { other = candidate; break; }
                }
            } catch (...) { gchandle_free(held); throw; }
            gchandle_free(held);
            if (other) { bool disabled = false; set_value("active", fallback_event_object, &disabled); return call("event_current"); }
            return current;
        }
        enable_owned_event_system();
        return required(call("event_current"), "active owned EventSystem");
    }
    bool topmost(Il2CppObject* rectangle, Vec2 point) {
        auto events = ensure_event_system();
        if (pointer_owner != events) {
            auto next = required(object_new(classes.at("PointerEventData")), "pointer event allocation");
            auto held = gchandle_new(next, false); if (!held) fail("cannot retain pointer event");
            try { void* args[] = {events}; call("pointer_ctor", next, args); }
            catch (...) { gchandle_free(held); throw; }
            if (pointer_handle) gchandle_free(pointer_handle);
            pointer_handle = held; pointer_event = next; pointer_owner = events;
        }
        if (!raycast_results) {
            raycast_results = required(object_new(classes.at("RaycastResults")), "raycast list allocation"); retain(raycast_results);
            call("results_ctor", raycast_results);
        }
        call("results_clear", raycast_results);
        set_value("pointer_position", pointer_event, &point);
        void* args[] = {pointer_event, raycast_results}; call("raycast_all", events, args);
        auto count = required(call("results_count", raycast_results), "raycast count");
        auto data = api.object_unbox(count); if (!data || *static_cast<int*>(data) < 1) return false;
        int zero = 0; void* index[] = {&zero};
        auto first = required(call("results_item", raycast_results, index), "first raycast");
        if (api.object_get_class(first) != classes.at("RaycastResult")) fail("unexpected raycast result type");
        auto unboxed = api.object_unbox(first); if (!unboxed) fail("cannot unbox raycast result");
        auto target = call("raycast_object", unboxed);
        return target && target == call("component_object", rectangle);
    }
    void create() {
        type_array = array_new(classes.at("Type"), 1); retain(reinterpret_cast<Il2CppObject*>(type_array));
        void* args[] = {type_object("Font")};
        auto fonts = required(call("find_fonts", nullptr, args), "loaded Font array"); retain(fonts);
        if (api.array_length(reinterpret_cast<Il2CppArray*>(fonts)) != 0) {
            int zero = 0; void* index[] = {&zero}; font = call("array_get", fonts, index);
        } else if (methods.count("builtin_font")) {
            // Unity 6000's own uGUI Text.AssignDefaultFont uses this built-in resource.
            auto name = reinterpret_cast<Il2CppObject*>(string_new("LegacyRuntime.ttf"));
            if (!name) fail("cannot allocate built-in font name");
            auto handle = gchandle_new(name, false); if (!handle) fail("cannot retain built-in font name");
            try { void* builtin_args[] = {type_object("Font"), name}; font = call("builtin_font", nullptr, builtin_args); }
            catch (...) { gchandle_free(handle); throw; }
            gchandle_free(handle);
        }
        font = required(font, "Unity-owned Font");
        auto font_class = api.object_get_class(font);
        if (!font_class || !same(api.class_get_namespace(font_class), "UnityEngine") || !same(api.class_get_name(font_class), "Font")) fail("unexpected built-in Font type");
        retain(font); default_font = font; language = read_language(); font = language_font(language);
        root = make_go("WSM Coop Canvas", nullptr, {0, 0}, {0, 0});
        canvas = add_component(root, "Canvas"); int mode = 0, order = 30000;
        set_value("render_mode", canvas, &mode); set_value("sorting_order", canvas, &order);
        add_component(root, "Raycaster"); void* persist_args[] = {root}; call("persist", nullptr, persist_args);
        tab = make_go("WSM Koop", root, {120, 40}, {80, -35}, {0, 1});
        auto tab_bg = image(tab, {0.18f, 0.27f, 0.36f, 1}); auto tab_button = add_component(tab, "Button");
        set_ref("target_graphic", tab_button, tab_bg); label(tab, "WSM Tab Text", Text::Tab, {120, 40}, {0, 0}, 16); tab_rect = rectangle(tab);
        panel = make_go("WSM Coop Panel", root, {600, 500}, {0, 0});
        image(panel, {0.05f, 0.07f, 0.10f, 0.97f});
        // Never render untrusted names unless literal-text rendering is supported.
        if (methods.count("rich_text"))
            local_name_text = name_text(panel, "WSM Local Player Name", {550, 28}, {0, 197}, 17);
        label(panel, "WSM Title", Text::Title, {550, 40}, {0, 145}, 25);
        label(panel, "WSM Address Label", Text::Address, {340, 30}, {-85, 99}, 18);
        label(panel, "WSM Port Label", Text::Port, {120, 30}, {190, 99}, 18);
        address = input(panel, "WSM Address", "127.0.0.1", {360, 42}, {-85, 59}, address_editor);
        port = input(panel, "WSM Port", "7780", {120, 42}, {190, 59}, port_editor);
        address_rect = rectangle(call("component_object", address));
        port_rect = rectangle(call("component_object", port));
        label(panel, "WSM Network Warning", Text::Notice, {550, 30}, {0, 12}, 13);
        host_rect = button(panel, "WSM Hosten", Text::Host, {170, 46}, {-190, -30});
        join_rect = button(panel, "WSM Beitreten", Text::Join, {170, 46}, {0, -30});
        leave_rect = button(panel, "WSM Verlassen", Text::Leave, {170, 46}, {190, -30});
        status_text = label(panel, "WSM Status", Text::Ready, {550, 65}, {0, -120}, 17);
        camera_own_rect = button(panel, "WSM Camera Own", Text::CameraOwn, {260, 36}, {-140, -183}, 16);
        camera_shared_rect = button(panel, "WSM Camera Shared", Text::CameraShared, {260, 36}, {140, -183}, 16);
        bool hidden = false;
        set_value("active", call("component_object", camera_own_rect), &hidden);
        set_value("active", call("component_object", camera_shared_rect), &hidden);
        camera_status_text = text(panel, "WSM Camera Status", std::string(i18n::text(Text::CameraUnavailable, language)), {550, 36}, {0, -226}, 16);
        rules_group = make_go("WSM Next Host Rules", panel, {600, 80}, {0, -285});
        image(rules_group, {0.05f, 0.07f, 0.10f, 0.97f});
        label(rules_group, "WSM Rules Notice", Text::RuleNotice, {580, 34}, {0, 21}, 14);
        rule_rects[0] = button(rules_group, "WSM Bot Slots", Text::RuleBots, {130, 34}, {-225, -16}, 16, &rule_labels[0]);
        rule_rects[1] = button(rules_group, "WSM Bot Difficulty", Text::RuleNormal, {180, 34}, {-60, -16}, 16, &rule_labels[1]);
        rule_rects[2] = button(rules_group, "WSM Death Rule", Text::RuleAllDead, {248, 34}, {164, -16}, 15, &rule_labels[2]);
        set_value("active", rules_group, &hidden);
        if (methods.count("rich_text")) {
            names_hud = make_go("WSM Player Names HUD", root, {320, 56}, {176, 40}, {0, 0});
            auto names_background = image(names_hud, {0.05f, 0.07f, 0.10f, 0.75f});
            bool raycast_names = false; set_value("raycast", names_background, &raycast_names);
            names_title = label(names_hud, "WSM Player Names Title", Text::PlayerNames, {300, 20}, {0, 13}, 14);
            for (std::size_t i = 0; i < name_rows.size(); ++i)
                name_rows[i] = name_text(names_hud, ("WSM Player Name " + std::to_string(i)).c_str(), {300, 20}, {0, -9 - static_cast<float>(i) * 22}, 16);
        }
        endpoint_measure = text(panel, "WSM Endpoint Measure", "", {0, 0}, {0, -10000}, 19);
        Color transparent{1, 1, 1, 0}; set_value("color", endpoint_measure, &transparent);
        ensure_event_system();
    }
    bool hit(Il2CppObject* rect, Vec2 point) {
        void* args[] = {rect, &point, nullptr}; auto value = required(call("hit", nullptr, args), "hit result");
        auto boxed = api.object_get_class(value);
        if (!boxed || !same(api.class_get_namespace(boxed), "System") || !same(api.class_get_name(boxed), "Boolean")) fail("hit returned non-Boolean");
        auto p = api.object_unbox(value); if (!p) fail("cannot unbox hit result"); return *static_cast<std::uint8_t*>(p) != 0;
    }
    void set_visible(bool value) { visible = value; if (!visible) editing = nullptr; set_value("active", panel, &visible); ensure_event_system(); }
    int get_port() {
        auto s = get_string(call("input_text_get", port));
        if (s.empty() || s.size() > 5) return 0;
        int n = 0; for (unsigned char c : s) { if (c < '0' || c > '9') return 0; n = n * 10 + (c - '0'); }
        return n >= 1 && n <= 65535 ? n : 0;
    }
    bool click(Vec2 p, const InGameMenuActions& a) {
        if (hit(tab_rect, p) && topmost(tab_rect, p)) { set_visible(!visible); return true; }
        if (!visible) return false;
        if (auto rect = rule_hit(p, a)) {
            if (rect == rule_rects[0]) draft_rules.bots = (draft_rules.bots + 1) % 9;
            else if (rect == rule_rects[1]) draft_rules.difficulty = (draft_rules.difficulty + 1) % 3;
            else draft_rules.death = 1 - draft_rules.death;
            drawn_rules_language.clear(); update_rules(a); return true;
        }
        if (a.camera_mode && camera_buttons_visible) {
            if (hit(camera_own_rect, p) && topmost(camera_own_rect, p)) { a.camera_mode(a.context, false); return true; }
            if (hit(camera_shared_rect, p) && topmost(camera_shared_rect, p)) { a.camera_mode(a.context, true); return true; }
        }
        bool h = hit(host_rect, p) && topmost(host_rect, p), j = hit(join_rect, p) && topmost(join_rect, p), l = hit(leave_rect, p) && topmost(leave_rect, p);
        if (!h && !j && !l) return false;
        status_override = Text::Count;
        if (l) { if (a.leave) a.leave(a.context); else status_override = Text::LeaveUnavailable; return true; }
        int n = get_port(); if (!n) { status_override = Text::InvalidPort; return true; }
        if (h) {
            if (a.host_with_rules) a.host_with_rules(a.context, n, draft_rules);
            else if (a.host && draft_rules.defaults()) a.host(a.context, n);
            else status_override = Text::HostUnavailable;
            return true;
        }
        auto ip = get_string(call("input_text_get", address));
        auto first = ip.find_first_not_of(" \t\r\n"), last = ip.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) { status_override = Text::MissingAddress; return true; }
        ip = ip.substr(first, last - first + 1);
        if (a.join) a.join(a.context, ip, n); else status_override = Text::JoinUnavailable;
        return true;
    }
    int integer(const char* id) {
        auto value = required(call(id), id); auto c = api.object_get_class(value);
        if (!c || !same(api.class_get_namespace(c), "System") || !same(api.class_get_name(c), "Int32")) fail("screen dimension not Int32");
        auto p = api.object_unbox(value); if (!p) fail("cannot unbox screen dimension"); return *static_cast<int*>(p);
    }
    void dispatch_input(bool focused, bool mouse, bool f6, bool valid_point, Vec2 point, const InGameMenuActions& a) {
        if (!focused) { foreground = false; mouse_down = false; f6_down = false; return; }
        if (!foreground) { foreground = true; mouse_down = mouse; f6_down = f6; return; }
        if (f6 && !f6_down) set_visible(!visible);
        if (mouse && !mouse_down && valid_point) click(point, a);
        mouse_down = mouse; f6_down = f6;
    }
    void begin_edit(EditorField& field) {
        if (field.editor.begin(get_string(call("input_text_get", field.input)))) {
            editing = &field;
            editing_point = {integer("screen_width") / 2.f + field.reference_position.x * last_scale,
                             integer("screen_height") / 2.f + field.reference_position.y * last_scale};
        } else editing = nullptr;
    }
    void paste_endpoint() {
        if (!editing || !OpenClipboard(owner)) return;
        struct Close { ~Close() { CloseClipboard(); } } close;
        auto data = GetClipboardData(CF_UNICODETEXT);
        if (!data) return;
        auto bytes = GlobalSize(data);
        if (bytes < sizeof(wchar_t) || bytes > 1024 * 1024) return;
        auto* text = static_cast<const wchar_t*>(GlobalLock(data));
        if (!text) return;
        std::string candidate;
        const auto limit = std::min<std::size_t>(bytes / sizeof(wchar_t), 64);
        bool terminated = false, valid = true;
        for (std::size_t i = 0; i < limit; ++i) {
            if (!text[i]) { terminated = true; break; }
            if (text[i] > 127 || text[i] < 32) { valid = false; break; }
            candidate.push_back(static_cast<char>(text[i]));
        }
        GlobalUnlock(data);
        if (!terminated || !valid) return;
        if (editing == &address_editor && candidate.find(':') != std::string::npos) {
            std::string invite_port;
            if (editing->editor.paste_invite(candidate, invite_port)) set_string("input_text_set", port, invite_port);
        } else editing->editor.paste(candidate);
    }
    bool copy_endpoint() {
        if (!editing) return false;
        auto& model = editing->editor;
        const auto first = std::min(model.cursor(), model.anchor()), last = std::max(model.cursor(), model.anchor());
        if (first == last || !OpenClipboard(owner)) return false;
        struct Close { ~Close() { CloseClipboard(); } } close;
        auto data = GlobalAlloc(GMEM_MOVEABLE, (last - first + 1) * sizeof(wchar_t));
        if (!data) return false;
        auto* text = static_cast<wchar_t*>(GlobalLock(data));
        if (!text) { GlobalFree(data); return false; }
        for (std::size_t i = first; i < last; ++i) text[i - first] = static_cast<unsigned char>(model.text()[i]);
        text[last - first] = 0; GlobalUnlock(data);
        if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, data)) { GlobalFree(data); return false; }
        return true; // The clipboard now owns the allocation.
    }
    void edit_key(const InputEvent& event) {
        if (!editing) return;
        if (event.message == WM_CHAR) { editing->editor.character(static_cast<char32_t>(event.key)); return; }
        if (event.key == VK_RETURN || event.key == VK_ESCAPE) { editing = nullptr; return; }
        if (event.key == VK_TAB) { begin_edit(editing == &address_editor ? port_editor : address_editor); return; }
        if (event.control && event.key == 'V') { paste_endpoint(); return; }
        if (event.control && (event.key == 'C' || event.key == 'X')) {
            if (copy_endpoint() && event.key == 'X') editing->editor.key(EditKey::Backspace);
            return;
        }
        EditKey key;
        if (event.control && event.key == 'A') key = EditKey::SelectAll;
        else switch (event.key) {
            case VK_LEFT: key = EditKey::Left; break;
            case VK_RIGHT: key = EditKey::Right; break;
            case VK_HOME: key = EditKey::Home; break;
            case VK_END: key = EditKey::End; break;
            case VK_BACK: key = EditKey::Backspace; break;
            case VK_DELETE: key = EditKey::Delete; break;
            default: return;
        }
        editing->editor.key(key, event.shift, event.control);
    }
    float endpoint_width(std::string_view value) {
        set_string("text", endpoint_measure, std::string(value));
        auto boxed = required(call("preferred_width", endpoint_measure), "endpoint text width");
        auto type = api.object_get_class(boxed);
        if (!type || !same(api.class_get_namespace(type), "System") || !same(api.class_get_name(type), "Single")) fail("endpoint width is not Single");
        auto raw = api.object_unbox(boxed); if (!raw) fail("cannot unbox endpoint width");
        auto width = *static_cast<float*>(raw);
        if (!std::isfinite(width) || width < 0 || width > 2048) fail("invalid endpoint width");
        return width;
    }
    void draw_endpoint_editor() {
        for (auto* field : {&address_editor, &port_editor}) {
            if (editing == field && !topmost(field == &address_editor ? address_rect : port_rect, editing_point)) editing = nullptr;
            bool active = visible && editing == field;
            Color background = active ? Color{0.12f, 0.22f, 0.34f, 1} : Color{0.10f, 0.13f, 0.18f, 1};
            set_value("color", field->background, &background);
            set_value("active", field->caret, &active);
            bool selected = active && field->editor.cursor() != field->editor.anchor();
            set_value("active", field->selection, &selected);
            if (!active) continue;
            auto& model = field->editor;
            if (get_string(call("input_text_get", field->input)) != model.text()) set_string("input_text_set", field->input, model.text());
            const auto full = endpoint_width(model.text());
            const auto cursor = endpoint_width(std::string_view(model.text()).substr(0, model.cursor()));
            const auto anchor = endpoint_width(std::string_view(model.text()).substr(0, model.anchor()));
            Vec2 position{cursor - full / 2, 0}; set_value("position", rectangle(field->caret), &position);
            if (selected) {
                Vec2 size{std::abs(cursor - anchor), 25}; set_value("size", rectangle(field->selection), &size);
                position = {(cursor + anchor - full) / 2, 0}; set_value("position", rectangle(field->selection), &position);
            }
        }
    }
    Il2CppObject* rule_hit(Vec2 p, const InGameMenuActions& a) {
        if (visible && rules_visible && a.rules_editable && a.host_with_rules)
            for (auto rect : rule_rects) if (hit(rect, p) && topmost(rect, p)) return rect;
        return nullptr;
    }
    void update_rules(const InGameMenuActions& a) {
        bool editable = a.rules_editable && a.host_with_rules;
        if (editable != rules_visible) { set_value("active", rules_group, &editable); rules_visible = editable; }
        if (!editable || language == drawn_rules_language) return;
        const Text difficulties[] = {Text::RuleEasy, Text::RuleNormal, Text::RuleHard};
        set_string("text", rule_labels[0], i18n::format(Text::RuleBots, language, {{"n", std::to_string(draft_rules.bots)}}));
        set_string("text", rule_labels[1], std::string(i18n::text(difficulties[draft_rules.difficulty], language)));
        set_string("text", rule_labels[2], std::string(i18n::text(draft_rules.death ? Text::RuleFirstDeath : Text::RuleAllDead, language)));
        drawn_rules_language = language;
    }
    void update(const InGameMenuActions& a) {
        if (GetCurrentThreadId() != thread) return;
        ensure_event_system();
        refresh_language();
        update_rules(a);
        // Reference-size scaling keeps the same menu readable on ultrawide/4K displays.
        float scale = std::max(0.5f, std::min(integer("screen_width") / 1280.f, integer("screen_height") / 720.f));
        if (scale != last_scale) { editing = nullptr; set_value("scale_factor", canvas, &scale); last_scale = scale; }
        update_names(a);
        optional_portrait_update([&] { update_stock_portraits(); }, [](const std::exception& e) {
            OutputDebugStringA("WSM stock portraits: "); OutputDebugStringA(e.what()); OutputDebugStringA("\n");
        });
        std::string current = status_override == Text::Count ? (a.status ? a.status(a.context, language) : std::string(i18n::text(Text::NotConnected, language))) : std::string(i18n::text(status_override, language));
        if (current != last_status) { set_string("text", status_text, current); last_status = current; }
        const bool camera_available = a.camera_mode != nullptr;
        if (camera_available != camera_buttons_visible) {
            bool enabled = camera_available;
            set_value("active", call("component_object", camera_own_rect), &enabled);
            set_value("active", call("component_object", camera_shared_rect), &enabled);
            camera_buttons_visible = enabled;
        }
        const auto camera_status = a.camera_status ? a.camera_status(a.context, language) : std::string(i18n::text(Text::CameraUnavailable, language));
        if (!camera_status_initialized || camera_status != last_camera_status) {
            set_string("text", camera_status_text, camera_status);
            last_camera_status = camera_status; camera_status_initialized = true;
        }
        bool focused = GetForegroundWindow() == owner;
        if (!focused) { pending_count = 0; pressed_rect = nullptr; editing = nullptr; draw_endpoint_editor(); dispatch_input(false, false, false, false, {}, a); return; }
        const auto clicks = pending_clicks;
        const auto count = pending_count;
        pending_count = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto event = clicks[i];
            if (event.message == WM_KEYDOWN && event.key == VK_F6) { if (!event.repeat) set_visible(!visible); continue; }
            if (event.message == WM_CHAR || event.message == WM_KEYDOWN) {
                edit_key(event);
                if (editing && get_string(call("input_text_get", editing->input)) != editing->editor.text())
                    set_string("input_text_set", editing->input, editing->editor.text());
                continue;
            }
            if (event.message == WM_CANCELMODE || event.message == WM_CAPTURECHANGED) { pressed_rect = nullptr; continue; }
            const auto p = event.point;
            RECT r{};
            if (GetClientRect(owner, &r) && r.right > 0 && r.bottom > 0 &&
                p.x >= 0 && p.y >= 0 && p.x < r.right && p.y < r.bottom) {
                Vec2 point{static_cast<float>(p.x) * integer("screen_width") / r.right,
                           static_cast<float>(r.bottom - p.y) * integer("screen_height") / r.bottom};
                if (event.message == WM_LBUTTONDOWN) {
                    auto field = visible && hit(address_rect, point) && topmost(address_rect, point) ? &address_editor :
                        visible && hit(port_rect, point) && topmost(port_rect, point) ? &port_editor : nullptr;
                    if (field) { begin_edit(*field); pressed_rect = nullptr; continue; }
                    editing = nullptr;
                    pressed_rect = hit(tab_rect, point) && topmost(tab_rect, point) ? tab_rect : visible && hit(host_rect, point) && topmost(host_rect, point) ? host_rect :
                        visible && hit(join_rect, point) && topmost(join_rect, point) ? join_rect : visible && hit(leave_rect, point) && topmost(leave_rect, point) ? leave_rect :
                        visible && camera_buttons_visible && a.camera_mode && hit(camera_own_rect, point) && topmost(camera_own_rect, point) ? camera_own_rect :
                        visible && camera_buttons_visible && a.camera_mode && hit(camera_shared_rect, point) && topmost(camera_shared_rect, point) ? camera_shared_rect : rule_hit(point, a);
                } else {
                    auto pressed = pressed_rect; pressed_rect = nullptr;
                    if (pressed && (pressed == tab_rect || visible) && hit(pressed, point)) click(point, a);
                }
            } else pressed_rect = nullptr;
        }
        draw_endpoint_editor();
    }
    void cleanup() noexcept {
        if (subclass_installed && GetCurrentThreadId() == thread) {
            RemoveWindowSubclass(owner, window_input, reinterpret_cast<UINT_PTR>(this));
            subclass_installed = false;
        }
        pending_count = 0;
        if (root && GetCurrentThreadId() == thread) { try { void* args[] = {root}; call("destroy", nullptr, args); } catch (...) {} }
        for (const auto& item : owned_fonts) { try { void* args[] = {item.second}; call("destroy", nullptr, args); } catch (...) { } }
        owned_fonts.clear(); localized_labels.clear(); text_components.clear();
        root = nullptr;
        if (gchandle_free) for (auto h : handles) gchandle_free(h);
        handles.clear();
        if (pointer_handle) { gchandle_free(pointer_handle); pointer_handle = nullptr; }
    }
};
InGameMenu::InGameMenu() = default;
InGameMenu::~InGameMenu() { shutdown(); }
bool InGameMenu::initialize(HMODULE game, const Il2CppApi& api, Il2CppDomain* domain, HWND owner, std::string& error) {
    error.clear();
    if (impl_) { error = "menu already initialized"; return false; }
    DWORD pid = 0; DWORD tid = owner ? GetWindowThreadProcessId(owner, &pid) : 0;
    if (!owner || pid != GetCurrentProcessId() || tid != GetCurrentThreadId()) { error = "owner must be this process's Unity main-thread window"; return false; }
    auto candidate = std::make_unique<Impl>(); candidate->api = api; candidate->owner = owner; candidate->thread = tid;
    try { candidate->resolve(game, domain); candidate->create(); candidate->install_input(); }
    catch (const std::exception& e) { error = e.what(); candidate->cleanup(); return false; }
    impl_ = std::move(candidate); return true;
}
void InGameMenu::tick(const InGameMenuActions& actions) {
    if (!impl_) return;
    try { impl_->update(actions); } catch (const std::exception& e) {
        OutputDebugStringA((std::string("WSM in-game UI: ") + e.what() + "\n").c_str());
        impl_->status_override = Text::UiError;
    }
}
std::string InGameMenu::language() const { return impl_ ? impl_->language : "en"; }
void InGameMenu::set_visible(bool value) {
    if (impl_ && GetCurrentThreadId() == impl_->thread) impl_->set_visible(value);
}
void InGameMenu::shutdown() {
    if (!impl_ || GetCurrentThreadId() != impl_->thread) return;
    impl_->cleanup(); impl_.reset();
}
#ifdef WSM_INGAME_UI_TESTING
void InGameMenu::test_fail_next_language_label() { if (impl_) impl_->fail_language_label = true; }
bool InGameMenu::test_click(float x, float y, const InGameMenuActions& actions) {
    if (!impl_ || GetCurrentThreadId() != impl_->thread) return false;
    try { return impl_->click({x, y}, actions); } catch (...) { return false; }
}
void InGameMenu::test_input(bool foreground, bool mouse_down, bool f6_down, float x, float y, const InGameMenuActions& actions) {
    if (!impl_ || GetCurrentThreadId() != impl_->thread) return;
    try { impl_->dispatch_input(foreground, mouse_down, f6_down, true, {x, y}, actions); } catch (...) {}
}
#endif
}
