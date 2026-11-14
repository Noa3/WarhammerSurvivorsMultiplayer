#include "wsm/coop_roster.h"
#include "wsm/main_thread_guard.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace {
bool equal(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
constexpr const char* kPlayer = "Rewired.Player";
constexpr const char* kPlayerList = "System.Collections.Generic.List<Rewired.Player>";
constexpr const char* kPlayerInterface = "System.Collections.Generic.IList<Rewired.Player>";
constexpr std::int32_t kMaximumPlayers = 16;
}
namespace wsm {
struct CoopRoster::Impl {
    Il2CppApi api;
    HMODULE module = nullptr;
    Il2CppDomain* domain = nullptr;
    MainThreadGuard guard;
    DWORD os = 0;
    std::int32_t managed = 0;
    Il2CppClass *manager_class = nullptr, *player_class = nullptr, *int_class = nullptr;
    Il2CppClass *input_class = nullptr, *helper_class = nullptr;
    Il2CppClass *configured_interface = nullptr, *roster_list = nullptr;
    const MethodInfo *instance = nullptr, *players = nullptr, *configured = nullptr;
    const MethodInfo *enrolled = nullptr, *player_count = nullptr, *id = nullptr;
    const MethodInfo *add = nullptr, *remove = nullptr, *selected_player = nullptr, *selected_index = nullptr;
    Il2CppClass* (*parent)(Il2CppClass*) = nullptr;
    Il2CppClass* (*declaring)(Il2CppClass*) = nullptr;
    Il2CppClass* (*from_type)(const Il2CppType*) = nullptr;
    const Il2CppImage* (*image)(Il2CppClass*) = nullptr;
    Il2CppClass* (*method_class)(const MethodInfo*) = nullptr;
    bool (*assignable)(Il2CppClass*, Il2CppClass*) = nullptr;
    void* (*handle_new)(Il2CppObject*, bool) = nullptr;
    Il2CppObject* (*handle_target)(void*) = nullptr;
    void (*handle_free)(void*) = nullptr;

    struct Root {
        const Impl* owner = nullptr;
        void* handle = nullptr;
        Root() = default;
        Root(const Impl& p, Il2CppObject* value) : owner(&p), handle(value ? p.handle_new(value, true) : nullptr) {}
        Root(const Root&) = delete;
        Root& operator=(const Root&) = delete;
        Root(Root&& other) noexcept : owner(other.owner), handle(std::exchange(other.handle, nullptr)) {}
        Root& operator=(Root&& other) noexcept {
            if (this != &other) { reset(); owner = other.owner; handle = std::exchange(other.handle, nullptr); }
            return *this;
        }
        ~Root() { reset(); }
        void reset() { if (handle) { owner->handle_free(handle); handle = nullptr; } }
        Il2CppObject* get() const { return handle ? owner->handle_target(handle) : nullptr; }
    };
    struct Capture {
        Root manager, helper;
        std::vector<Root> configured_players, enrolled_players;
        CoopRosterSnapshot values;
    };
    bool pending = false;
    Root owned_manager, original_player, added_player;
    std::int32_t original_id = -1, added_id = -1;

    bool type(const Il2CppType* t, const char* expected) const {
        if (!t) return false;
        char* name = api.type_get_name(t);
        const bool result = equal(name, expected);
        if (name) api.free(name);
        return result;
    }
    bool class_image(Il2CppClass* c, const char* expected) const {
        const auto im = c ? image(c) : nullptr;
        return im && equal(api.image_get_name(im), expected);
    }
    Il2CppClass* klass(Il2CppDomain* runtime_domain, const char* assembly, const char* ns, const char* name) const {
        std::size_t count = 0;
        auto assemblies = api.domain_get_assemblies(runtime_domain, &count);
        if (!assemblies) return nullptr;
        Il2CppClass* found = nullptr;
        for (std::size_t i = 0; i < count; ++i) {
            auto im = api.assembly_get_image(assemblies[i]);
            if (!im || !equal(api.image_get_name(im), assembly)) continue;
            for (std::size_t j = 0; j < api.image_get_class_count(im); ++j) {
                auto c = const_cast<Il2CppClass*>(api.image_get_class(im, j));
                if (!c || !equal(api.class_get_namespace(c), ns) || !equal(api.class_get_name(c), name)) continue;
                if (found) return nullptr;
                found = c;
            }
        }
        return found;
    }
    const MethodInfo* method(Il2CppClass* c, const char* name, const char* result,
                             const char* argument = nullptr, bool is_static = false,
                             std::uint32_t access = 6) const {
        const MethodInfo* found = nullptr;
        void* iterator = nullptr;
        while (c) {
            auto m = api.class_get_methods(c, &iterator);
            if (!m) break;
            if (!equal(api.method_get_name(m), name) || api.method_get_param_count(m) != (argument ? 1u : 0u)) continue;
            std::uint32_t implementation = 0;
            auto flags = api.method_get_flags(m, &implementation);
            if ((flags & 7u) != access || ((flags & 16u) != 0) != is_static || method_class(m) != c) continue;
            if ((result && !type(api.method_get_return_type(m), result)) ||
                (argument && !type(api.method_get_param(m, 0), argument))) continue;
            if (found) return nullptr;
            found = m;
        }
        return found;
    }
    const MethodInfo* inherited_method(Il2CppClass* c, const char* name, const char* result, const char* argument = nullptr) const {
        for (int depth = 0; c && depth < 128; ++depth, c = parent(c))
            if (auto m = method(c, name, result, argument)) return m;
        return nullptr;
    }
    bool check(std::string& error) const {
        MainThreadIdentity identity;
        return guard.verify(os, managed, identity, error);
    }
    Il2CppObject* invoke(const MethodInfo* m, Il2CppObject* object, void** args, std::string& error) const {
        auto result = api.invoke(m, object, args);
        if (result.exception) { error = "Managed exception in roster operation: " + std::string(api.method_get_name(m)); return nullptr; }
        return result.value;
    }
    bool root(Root& output, Il2CppObject* value, std::string& error) const {
        if (!value) { error = "Unexpected null roster object"; return false; }
        Root candidate(*this, value);
        if (!candidate.get()) { error = "Failed to root non-null roster object"; return false; }
        output = std::move(candidate);
        return true;
    }
    bool integer(const MethodInfo* m, Il2CppObject* object, std::int32_t& value, std::string& error) const {
        auto raw = invoke(m, object, nullptr, error);
        if (!error.empty()) return false;
        Root result;
        if (!root(result, raw, error) || api.object_get_class(result.get()) != int_class) {
            if (error.empty()) error = "Roster getter did not return canonical Int32";
            return false;
        }
        const void* data = api.object_unbox(result.get());
        if (!data) { error = "Unable to unbox roster Int32"; return false; }
        std::memcpy(&value, data, sizeof(value));
        return true;
    }
    bool list(Il2CppObject* raw, Il2CppClass* expected, std::vector<Root>& objects,
              std::vector<std::int32_t>& ids, std::string& error) const {
        Root values;
        if (!root(values, raw, error)) return false;
        auto c = api.object_get_class(values.get());
        if (!c || !class_image(c, "mscorlib.dll") || !assignable(expected, c)) {
            error = "Roster list class/image mismatch"; return false;
        }
        auto count_method = inherited_method(c, "get_Count", "System.Int32");
        auto item_method = inherited_method(c, "get_Item", kPlayer, "System.Int32");
        if (!count_method || !item_method || from_type(api.method_get_return_type(item_method)) != player_class) {
            error = "Missing or ambiguous closed roster list accessors"; return false;
        }
        std::int32_t count = 0;
        if (!integer(count_method, values.get(), count, error)) return false;
        if (count < 0 || count > kMaximumPlayers) { error = "Roster count outside supported bound"; return false; }
        for (std::int32_t i = 0; i < count; ++i) {
            void* args[] = { &i };
            auto player = invoke(item_method, values.get(), args, error);
            if (!error.empty()) return false;
            Root reference;
            if (!root(reference, player, error)) return false;
            if (api.object_get_class(reference.get()) != player_class) { error = "Roster member is not the canonical Player"; return false; }
            std::int32_t player_id = -1;
            if (!integer(id, reference.get(), player_id, error)) return false;
            if (player_id < 0 || std::find(ids.begin(), ids.end(), player_id) != ids.end()) {
                error = "Negative or duplicate roster player ID"; return false;
            }
            ids.push_back(player_id);
            objects.push_back(std::move(reference));
        }
        return true;
    }
    CoopRosterStatus capture(Capture& output, std::string& error) const {
        Capture result;
        auto manager = invoke(instance, nullptr, nullptr, error);
        if (!error.empty()) return CoopRosterStatus::Error;
        if (!manager) return CoopRosterStatus::NotReady;
        if (!root(result.manager, manager, error)) return CoopRosterStatus::Error;
        if (api.object_get_class(result.manager.get()) != manager_class) { error = "Unexpected manager instance class"; return CoopRosterStatus::Error; }
        auto helper = invoke(players, nullptr, nullptr, error);
        if (!error.empty()) return CoopRosterStatus::Error;
        if (!helper) return CoopRosterStatus::NotReady;
        if (!root(result.helper, helper, error)) return CoopRosterStatus::Error;
        if (api.object_get_class(result.helper.get()) != helper_class) { error = "Unexpected configured player helper class"; return CoopRosterStatus::Error; }
        bool include_system = false;
        void* args[] = { &include_system };
        auto configured_list = invoke(configured, result.helper.get(), args, error);
        if (!error.empty()) return CoopRosterStatus::Error;
        if (!configured_list) return CoopRosterStatus::NotReady;
        if (!list(configured_list, configured_interface, result.configured_players, result.values.configured_ids, error)) return CoopRosterStatus::Error;
        auto enrolled_list = invoke(enrolled, result.manager.get(), nullptr, error);
        if (!error.empty()) return CoopRosterStatus::Error;
        if (!enrolled_list) return CoopRosterStatus::NotReady;
        if (!list(enrolled_list, roster_list, result.enrolled_players, result.values.enrolled_ids, error)) return CoopRosterStatus::Error;
        std::int32_t reported = -1;
        if (!integer(player_count, result.manager.get(), reported, error)) return CoopRosterStatus::Error;
        if (reported != static_cast<std::int32_t>(result.enrolled_players.size())) { error = "Manager count differs from captured roster"; return CoopRosterStatus::Error; }
        for (std::size_t i = 0; i < result.values.enrolled_ids.size(); ++i) {
            auto it = std::find(result.values.configured_ids.begin(), result.values.configured_ids.end(), result.values.enrolled_ids[i]);
            if (it == result.values.configured_ids.end() || result.configured_players[static_cast<std::size_t>(it - result.values.configured_ids.begin())].get() != result.enrolled_players[i].get()) {
                error = "Enrolled player identity differs from configured player"; return CoopRosterStatus::Error;
            }
        }
        output = std::move(result);
        return CoopRosterStatus::Captured;
    }
    bool baseline(const Capture& c) const {
        return c.manager.get() == owned_manager.get() && c.values.enrolled_ids.size() == 1 &&
            c.values.enrolled_ids[0] == original_id && c.enrolled_players[0].get() == original_player.get();
    }
    bool addition(const Capture& c) const {
        return c.manager.get() == owned_manager.get() && c.values.enrolled_ids.size() == 2 &&
            c.values.enrolled_ids[0] == original_id && c.enrolled_players[0].get() == original_player.get() &&
            c.values.enrolled_ids[1] == added_id && c.enrolled_players[1].get() == added_player.get();
    }
    void clear_ownership() {
        added_player.reset(); original_player.reset(); owned_manager.reset();
        pending = false; original_id = added_id = -1;
    }
};

CoopRoster::CoopRoster() = default;
CoopRoster::~CoopRoster() { if (impl_ && impl_->pending) (void)impl_.release(); }
bool CoopRoster::has_pending_enrollment() const noexcept { return impl_ && impl_->pending; }

bool CoopRoster::initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain,
                            DWORD os, std::int32_t managed, std::string& error) {
    error.clear();
    if (impl_) { error = "Roster module is already initialized"; return false; }
    if (!module || !domain || !os || !api.thread_current || !api.thread_current() ||
        !api.domain_get_assemblies || !api.assembly_get_image || !api.image_get_name ||
        !api.image_get_class_count || !api.image_get_class || !api.class_get_name || !api.class_get_namespace ||
        !api.class_get_methods || !api.method_get_name || !api.method_get_param_count || !api.method_get_param ||
        !api.method_get_return_type || !api.method_get_flags || !api.class_get_type || !api.type_get_name ||
        !api.free || !api.runtime_invoke || !api.object_get_class || !api.object_unbox) {
        error = "Incomplete roster API or unattached runtime thread"; return false;
    }
    auto p = std::make_unique<Impl>();
    p->api = api; p->module = module; p->domain = domain; p->os = os; p->managed = managed;
#define LOAD(member, name) p->member = reinterpret_cast<decltype(p->member)>(GetProcAddress(module, name)); if (!p->member) { error = std::string("Missing roster export ") + name; return false; }
    LOAD(parent, "il2cpp_class_get_parent"); LOAD(declaring, "il2cpp_class_get_declaring_type");
    LOAD(from_type, "il2cpp_class_from_type"); LOAD(image, "il2cpp_class_get_image");
    LOAD(method_class, "il2cpp_method_get_class"); LOAD(assignable, "il2cpp_class_is_assignable_from");
    LOAD(handle_new, "il2cpp_gchandle_new"); LOAD(handle_target, "il2cpp_gchandle_get_target"); LOAD(handle_free, "il2cpp_gchandle_free");
#undef LOAD
    if (!MainThreadGuard::create(api, domain, p->guard, error)) return false;
    p->manager_class = p->klass(domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Framework", "MultiplayerManager");
    p->player_class = p->klass(domain, "Rewired_Core.dll", "Rewired", "Player");
    p->input_class = p->klass(domain, "Rewired_Core.dll", "Rewired", "ReInput");
    p->int_class = p->klass(domain, "mscorlib.dll", "System", "Int32");
    if (!p->manager_class || !p->player_class || !p->input_class || !p->int_class) { error = "Missing or ambiguous roster class/image"; return false; }
    p->instance = p->method(p->manager_class, "get_Instance", "VampireSurvivors.Framework.MultiplayerManager", nullptr, true);
    p->players = p->method(p->input_class, "get_players", nullptr, nullptr, true);
    if (!p->instance || !p->players || p->from_type(api.method_get_return_type(p->instance)) != p->manager_class) {
        error = "Missing or ambiguous roster singleton/helper getter"; return false;
    }
    p->helper_class = p->from_type(api.method_get_return_type(p->players));
    if (!p->helper_class || p->declaring(p->helper_class) != p->input_class ||
        !equal(api.class_get_name(p->helper_class), "PlayerHelper") || !p->class_image(p->helper_class, "Rewired_Core.dll")) {
        error = "PlayerHelper is not the expected nested ReInput type"; return false;
    }
    p->configured = p->method(p->helper_class, "GetPlayers", kPlayerInterface, "System.Boolean");
    p->enrolled = p->method(p->manager_class, "GetPlayers", kPlayerList);
    p->player_count = p->method(p->manager_class, "GetPlayerCount", "System.Int32");
    p->id = p->method(p->player_class, "get_id", "System.Int32");
    p->add = p->method(p->manager_class, "AddPlayer", "System.Void", kPlayer);
    p->remove = p->method(p->manager_class, "RemovePlayer", "System.Void", kPlayer, false, 1);
    p->selected_player = p->method(p->manager_class, "GetSelectedPlayer", kPlayer);
    p->selected_index = p->method(p->manager_class, "GetSelectedPlayerIndex", "System.Int32");
    if (!p->configured || !p->enrolled || !p->player_count || !p->id || !p->add || !p->remove || !p->selected_player || !p->selected_index ||
        p->from_type(api.method_get_return_type(p->selected_player)) != p->player_class ||
        p->from_type(api.method_get_return_type(p->selected_index)) != p->int_class ||
        p->from_type(api.method_get_param(p->add, 0)) != p->player_class ||
        p->from_type(api.method_get_param(p->remove, 0)) != p->player_class ||
        p->from_type(api.method_get_return_type(p->id)) != p->int_class ||
        p->from_type(api.method_get_return_type(p->player_count)) != p->int_class) {
        error = "Missing or ambiguous exact roster methods (AddPlayer public; RemovePlayer private)"; return false;
    }
    p->configured_interface = p->from_type(api.method_get_return_type(p->configured));
    p->roster_list = p->from_type(api.method_get_return_type(p->enrolled));
    if (!p->class_image(p->configured_interface, "mscorlib.dll") || !p->class_image(p->roster_list, "mscorlib.dll")) {
        error = "Roster collection return image mismatch"; return false;
    }
    impl_ = std::move(p);
    return true;
}
CoopRosterStatus CoopRoster::inspect(CoopRosterSnapshot& output, std::string& error) const {
    error.clear();
    if (!impl_) { error = "Roster module not initialized"; return CoopRosterStatus::Error; }
    const auto& p = *impl_;
    if (!p.check(error)) return CoopRosterStatus::Error;
    Impl::Capture c;
    auto status = p.capture(c, error);
    if (status == CoopRosterStatus::Captured) output = std::move(c.values);
    return status;
}
bool CoopRoster::inspect_character_configuration(CoopCharacterConfiguration& output, std::string& error) const {
    error.clear();
    if (!impl_) { error = "Roster module not initialized"; return false; }
    const auto& p = *impl_;
    if (!p.check(error)) return false;
    auto character = p.klass(p.domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Data", "CharacterType");
    auto config_class = p.klass(p.domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Framework", "CoopConfig");
    auto selections = p.method(p.manager_class, "GetCharacterSelections", "System.Collections.Generic.List<VampireSurvivors.Data.CharacterType>");
    using EnumBase = const Il2CppType* (*)(Il2CppClass*);
    using FieldByName = FieldInfo* (*)(Il2CppClass*, const char*);
    using FieldType = const Il2CppType* (*)(FieldInfo*);
    using FieldParent = Il2CppClass* (*)(FieldInfo*);
    using FieldFlags = int (*)(FieldInfo*);
    using FieldValue = void (*)(Il2CppObject*, FieldInfo*, void*);
    const auto enum_base = reinterpret_cast<EnumBase>(GetProcAddress(p.module, "il2cpp_class_enum_basetype"));
    const auto field_by_name = reinterpret_cast<FieldByName>(GetProcAddress(p.module, "il2cpp_class_get_field_from_name"));
    const auto field_type = reinterpret_cast<FieldType>(GetProcAddress(p.module, "il2cpp_field_get_type"));
    const auto field_parent = reinterpret_cast<FieldParent>(GetProcAddress(p.module, "il2cpp_field_get_parent"));
    const auto field_flags = reinterpret_cast<FieldFlags>(GetProcAddress(p.module, "il2cpp_field_get_flags"));
    const auto field_value = reinterpret_cast<FieldValue>(GetProcAddress(p.module, "il2cpp_field_get_value"));
    if (!character || !config_class || !selections || !enum_base || !field_by_name || !field_type ||
        !field_parent || !field_flags || !field_value || !p.type(enum_base(character), "System.Int32")) {
        error = "Optional character/configuration metadata is unavailable"; return false;
    }
    auto config_field = field_by_name(p.manager_class, "_coopConfig");
    if (!config_field || field_parent(config_field) != p.manager_class || (field_flags(config_field) & 16) ||
        p.from_type(field_type(config_field)) != config_class) {
        error = "MultiplayerManager CoopConfig field contract mismatch"; return false;
    }
    Impl::Capture roster;
    if (p.capture(roster, error) != CoopRosterStatus::Captured) { if (error.empty()) error = "Roster not ready for configuration capture"; return false; }
    auto raw = p.invoke(selections, roster.manager.get(), nullptr, error);
    if (!error.empty()) return false;
    Impl::Root list;
    if (!p.root(list, raw, error)) return false;
    auto c = p.api.object_get_class(list.get());
    auto declared = p.from_type(p.api.method_get_return_type(selections));
    if (!c || !declared || !p.class_image(c, "mscorlib.dll") || !p.assignable(declared, c)) { error = "Character selection list type mismatch"; return false; }
    auto get_count = p.inherited_method(c, "get_Count", "System.Int32");
    auto get_item = p.inherited_method(c, "get_Item", "VampireSurvivors.Data.CharacterType", "System.Int32");
    if (!get_count || !get_item || p.from_type(p.api.method_get_return_type(get_item)) != character) { error = "Character selection accessor mismatch"; return false; }
    std::int32_t count = 0;
    if (!p.integer(get_count, list.get(), count, error)) return false;
    if (count < 0 || count > kMaximumPlayers) { error = "Character selection count outside bound"; return false; }
    CoopCharacterConfiguration result;
    auto is_multiplayer = p.method(p.manager_class, "get_IsMultiplayer", "System.Boolean");
    auto selected_player = p.method(p.manager_class, "GetSelectedPlayer", kPlayer);
    auto selected_index = p.method(p.manager_class, "GetSelectedPlayerIndex", "System.Int32");
    auto boolean = p.klass(p.domain, "mscorlib.dll", "System", "Boolean");
    if (!is_multiplayer || !selected_player || !selected_index || !boolean ||
        p.from_type(p.api.method_get_return_type(selected_player)) != p.player_class) { error = "Selected menu player metadata unavailable"; return false; }
    auto raw_mode = p.invoke(is_multiplayer, roster.manager.get(), nullptr, error);
    if (!error.empty()) return false;
    Impl::Root mode;
    if (!p.root(mode, raw_mode, error) || p.api.object_get_class(mode.get()) != boolean) { if (error.empty()) error = "Multiplayer mode box mismatch"; return false; }
    const auto mode_value = p.api.object_unbox(mode.get());
    if (!mode_value) { error = "Multiplayer mode cannot be unboxed"; return false; }
    std::uint8_t mode_byte = 0;
    std::memcpy(&mode_byte, mode_value, sizeof(mode_byte));
    if (mode_byte > 1) { error = "Invalid multiplayer mode Boolean"; return false; }
    result.is_multiplayer = mode_byte != 0;
    if (!p.integer(selected_index, roster.manager.get(), result.selected_player_index, error)) return false;
    auto raw_selected = p.invoke(selected_player, roster.manager.get(), nullptr, error);
    if (!error.empty()) return false;
    if (raw_selected) {
        Impl::Root selected;
        if (!p.root(selected, raw_selected, error) || p.api.object_get_class(selected.get()) != p.player_class ||
            !p.integer(p.id, selected.get(), result.selected_player_id, error)) { if (error.empty()) error = "Selected player identity mismatch"; return false; }
    }
    for (std::int32_t i = 0; i < count; ++i) {
        void* args[] = {&i};
        auto value = p.invoke(get_item, list.get(), args, error);
        if (!error.empty()) return false;
        Impl::Root item;
        if (!p.root(item, value, error) || p.api.object_get_class(item.get()) != character) { if (error.empty()) error = "Character selection enum box mismatch"; return false; }
        const auto data = p.api.object_unbox(item.get());
        if (!data) { error = "Character selection enum cannot be unboxed"; return false; }
        std::int32_t selection = 0;
        std::memcpy(&selection, data, sizeof(selection));
        result.selected_types.push_back(selection);
    }
    Il2CppObject* config = nullptr;
    field_value(roster.manager.get(), config_field, &config);
    if (config) {
        Impl::Root reference;
        if (!p.root(reference, config, error) || p.api.object_get_class(reference.get()) != config_class) { if (error.empty()) error = "CoopConfig instance class mismatch"; return false; }
        result.coop_config_present = true;
    }
    output = std::move(result);
    return true;
}
bool CoopRoster::enroll(std::int32_t configured_player_id, std::string& error) {
    error.clear();
    if (!impl_) { error = "Roster module not initialized"; return false; }
    auto& p = *impl_;
    if (!p.check(error)) return false;
    if (p.pending) { error = "An owned roster addition still requires rollback"; return false; }
    Impl::Capture before;
    if (p.capture(before, error) != CoopRosterStatus::Captured) { if (error.empty()) error = "Roster not ready"; return false; }
    const auto& ids = before.values.configured_ids;
    auto it = std::find(ids.begin(), ids.end(), configured_player_id);
    if (before.values.enrolled_ids.size() != 1 || it == ids.end() ||
        before.values.enrolled_ids[0] == configured_player_id) {
        error = "Enrollment requires exactly one existing player and a distinct configured target"; return false;
    }
    p.original_id = before.values.enrolled_ids[0]; p.added_id = configured_player_id;
    p.owned_manager = std::move(before.manager);
    p.original_player = std::move(before.enrolled_players[0]);
    p.added_player = std::move(before.configured_players[static_cast<std::size_t>(it - ids.begin())]);
    p.pending = true;
    void* args[] = { p.added_player.get() };
    const auto result = p.api.invoke(p.add, p.owned_manager.get(), args);
    Impl::Capture after;
    const auto status = p.capture(after, error);
    if (status == CoopRosterStatus::Captured && p.baseline(after)) {
        p.clear_ownership();
        error = result.exception ? "AddPlayer raised before a verified roster addition" : "AddPlayer did not add the exact target";
        return false;
    }
    if (status == CoopRosterStatus::Captured && p.addition(after) && !result.exception) return true;
    if (error.empty()) error = result.exception ? "AddPlayer raised; owned roster state requires reconciliation/rollback" : "AddPlayer postcondition failed; ownership retained for rollback";
    return false;
}
bool CoopRoster::select_menu_player(std::int32_t enrolled_player_id, std::string& error) {
    error.clear();
    if (!impl_) { error = "Roster module not initialized"; return false; }
    auto& p = *impl_;
    if (!p.check(error)) return false;
    Impl::Capture before;
    if (!p.pending || p.capture(before, error) != CoopRosterStatus::Captured || !p.addition(before)) {
        if (error.empty()) error = "Menu player selection requires the exact owned two-player roster";
        return false;
    }
    auto it = std::find(before.values.enrolled_ids.begin(), before.values.enrolled_ids.end(), enrolled_player_id);
    if (it == before.values.enrolled_ids.end()) { error = "Requested menu player is not enrolled"; return false; }
    const auto index = static_cast<std::size_t>(it - before.values.enrolled_ids.begin());
    const MethodInfo* select = nullptr;
    void* iterator = nullptr;
    while (auto method = p.api.class_get_methods(p.manager_class, &iterator)) {
        if (!equal(p.api.method_get_name(method), "SelectPlayer") || p.api.method_get_param_count(method) != 4) continue;
        std::uint32_t implementation = 0;
        const auto flags = p.api.method_get_flags(method, &implementation);
        if ((flags & 7u) != 6u || (flags & 16u) || p.method_class(method) != p.manager_class ||
            !p.type(p.api.method_get_return_type(method), "System.Void") ||
            p.from_type(p.api.method_get_param(method, 0)) != p.player_class ||
            !p.type(p.api.method_get_param(method, 1), "System.Boolean") ||
            !p.type(p.api.method_get_param(method, 2), "System.Boolean") ||
            !p.type(p.api.method_get_param(method, 3), "System.Single")) continue;
        if (select) { error = "Ambiguous menu player selection method"; return false; }
        select = method;
    }
    auto selected = p.method(p.manager_class, "GetSelectedPlayer", kPlayer);
    auto selected_index = p.method(p.manager_class, "GetSelectedPlayerIndex", "System.Int32");
    if (!select || !selected || !selected_index || p.from_type(p.api.method_get_return_type(selected)) != p.player_class) {
        error = "Menu player selection metadata is missing"; return false;
    }
    bool exclusive = false, vibrate = false;
    float duration = 0;
    void* args[] = {before.enrolled_players[index].get(), &exclusive, &vibrate, &duration};
    const auto invocation = p.api.invoke(select, before.manager.get(), args);
    if (invocation.exception) { error = "Managed exception selecting menu player"; return false; }
    auto raw_selected = p.invoke(selected, before.manager.get(), nullptr, error);
    if (!error.empty()) return false;
    Impl::Root after_selected;
    if (!p.root(after_selected, raw_selected, error) || after_selected.get() != before.enrolled_players[index].get()) {
        if (error.empty()) error = "Menu player pointer did not match the requested roster member";
        return false;
    }
    std::int32_t after_index = -1;
    if (!p.integer(selected_index, before.manager.get(), after_index, error)) return false;
    Impl::Capture after;
    if (p.capture(after, error) != CoopRosterStatus::Captured || !p.addition(after) ||
        after_index < 0 || after_index >= static_cast<std::int32_t>(after.enrolled_players.size()) ||
        after.enrolled_players[static_cast<std::size_t>(after_index)].get() != after_selected.get()) {
        if (error.empty()) error = "Selected menu index or roster identity changed";
        return false;
    }
    return true;
}
bool CoopRoster::copy_menu_character(std::int32_t enrolled_player_id, std::string& error) {
    error.clear();
    if (!impl_) { error = "Roster module not initialized"; return false; }
    const auto& p = *impl_;
    if (!p.check(error)) return false;
    Impl::Capture before;
    if (!p.pending || p.capture(before, error) != CoopRosterStatus::Captured || !p.addition(before)) {
        if (error.empty()) error = "Character assignment requires the exact owned two-player roster";
        return false;
    }
    const auto it = std::find(before.values.enrolled_ids.begin(), before.values.enrolled_ids.end(), enrolled_player_id);
    if (it == before.values.enrolled_ids.end()) { error = "Character target is not enrolled"; return false; }
    const auto target = before.enrolled_players[static_cast<std::size_t>(it - before.values.enrolled_ids.begin())].get();
    auto options_class = p.klass(p.domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Objects", "PlayerOptions");
    auto config_class = p.klass(p.domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Data", "PlayerOptionsData");
    auto selection_class = p.klass(p.domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Framework", "CoopSelections");
    auto character_class = p.klass(p.domain, "VampireSurvivors.Runtime.dll", "VampireSurvivors.Data", "CharacterType");
    auto boolean_class = p.klass(p.domain, "mscorlib.dll", "System", "Boolean");
    using FieldByName = FieldInfo* (*)(Il2CppClass*, const char*);
    using FieldType = const Il2CppType* (*)(FieldInfo*);
    using FieldParent = Il2CppClass* (*)(FieldInfo*);
    using FieldFlags = int (*)(FieldInfo*);
    using FieldValue = void (*)(Il2CppObject*, FieldInfo*, void*);
    using EnumBase = const Il2CppType* (*)(Il2CppClass*);
    const auto field_by_name = reinterpret_cast<FieldByName>(GetProcAddress(p.module, "il2cpp_class_get_field_from_name"));
    const auto field_type = reinterpret_cast<FieldType>(GetProcAddress(p.module, "il2cpp_field_get_type"));
    const auto field_parent = reinterpret_cast<FieldParent>(GetProcAddress(p.module, "il2cpp_field_get_parent"));
    const auto field_flags = reinterpret_cast<FieldFlags>(GetProcAddress(p.module, "il2cpp_field_get_flags"));
    const auto field_get = reinterpret_cast<FieldValue>(GetProcAddress(p.module, "il2cpp_field_get_value"));
    const auto field_set = reinterpret_cast<FieldValue>(GetProcAddress(p.module, "il2cpp_field_set_value"));
    const auto enum_base = reinterpret_cast<EnumBase>(GetProcAddress(p.module, "il2cpp_class_enum_basetype"));
    if (!options_class || !config_class || !selection_class || !character_class || !boolean_class ||
        !field_by_name || !field_type || !field_parent || !field_flags || !field_get || !field_set || !enum_base ||
        !p.type(enum_base(character_class), "System.Int32")) { error = "Character assignment metadata is unavailable"; return false; }
    auto reference_field = [&](Il2CppObject* owner, Il2CppClass* declaring_class, const char* name,
                               Il2CppClass* expected, Impl::Root& result) {
        auto field = field_by_name(declaring_class, name);
        if (!field || field_parent(field) != declaring_class || (field_flags(field) & 16) ||
            (expected && p.from_type(field_type(field)) != expected)) { error = std::string("Reference field contract mismatch: ") + name; return false; }
        Il2CppObject* value = nullptr;
        field_get(owner, field, &value);
        if (!p.root(result, value, error)) return false;
        if (expected && p.api.object_get_class(result.get()) != expected) { error = std::string("Reference field instance mismatch: ") + name; return false; }
        return true;
    };
    Impl::Root options;
    if (!reference_field(before.manager.get(), p.manager_class, "_playerOptions", options_class, options)) return false;
    auto config_get = p.method(options_class, "get_Config", "VampireSurvivors.Data.PlayerOptionsData");
    auto source_get = p.method(config_class, "get_SelectedCharacter", "VampireSurvivors.Data.CharacterType");
    if (!config_get || !source_get || p.from_type(p.api.method_get_return_type(config_get)) != config_class ||
        p.from_type(p.api.method_get_return_type(source_get)) != character_class) { error = "Stock character selection getters are unavailable"; return false; }
    auto raw_config = p.invoke(config_get, options.get(), nullptr, error);
    if (!error.empty()) return false;
    Impl::Root config;
    if (!p.root(config, raw_config, error) || p.api.object_get_class(config.get()) != config_class) { if (error.empty()) error = "Stock configuration class mismatch"; return false; }
    auto raw_source = p.invoke(source_get, config.get(), nullptr, error);
    if (!error.empty()) return false;
    Impl::Root source;
    if (!p.root(source, raw_source, error) || p.api.object_get_class(source.get()) != character_class) { if (error.empty()) error = "Stock character enum box mismatch"; return false; }
    const auto source_data = p.api.object_unbox(source.get());
    if (!source_data) { error = "Stock character enum cannot be unboxed"; return false; }
    std::int32_t character = 0;
    std::memcpy(&character, source_data, sizeof(character));
    const MethodInfo* bought = nullptr;
    void* iterator = nullptr;
    while (auto method = p.api.class_get_methods(options_class, &iterator)) {
        if (!equal(p.api.method_get_name(method), "IsBought") || p.api.method_get_param_count(method) != 2) continue;
        std::uint32_t implementation = 0;
        const auto flags = p.api.method_get_flags(method, &implementation);
        if ((flags & 7u) != 6u || (flags & 16u) || p.method_class(method) != options_class ||
            p.from_type(p.api.method_get_return_type(method)) != boolean_class ||
            p.from_type(p.api.method_get_param(method, 0)) != character_class ||
            p.from_type(p.api.method_get_param(method, 1)) != boolean_class) continue;
        if (bought) { error = "Ambiguous IsBought character validation"; return false; }
        bought = method;
    }
    if (!bought) { error = "Character ownership validation is unavailable"; return false; }
    bool ignore_skins = false;
    void* bought_args[] = {&character, &ignore_skins};
    auto raw_bought = p.invoke(bought, options.get(), bought_args, error);
    if (!error.empty()) return false;
    Impl::Root ownership;
    if (!p.root(ownership, raw_bought, error) || p.api.object_get_class(ownership.get()) != boolean_class) { if (error.empty()) error = "Character ownership result type mismatch"; return false; }
    const auto ownership_data = p.api.object_unbox(ownership.get());
    std::uint8_t owned = 0;
    if (!ownership_data) { error = "Character ownership cannot be unboxed"; return false; }
    std::memcpy(&owned, ownership_data, sizeof(owned));
    if (owned != 1) { error = "The stock menu character is not bought/available for assignment"; return false; }
    Impl::Root dictionary;
    if (!reference_field(config.get(), config_class, "MultiplayerSelections", nullptr, dictionary)) return false;
    auto dictionary_class = p.api.object_get_class(dictionary.get());
    if (!p.class_image(dictionary_class, "mscorlib.dll") || !equal(p.api.class_get_name(dictionary_class), "Dictionary`2")) { error = "MultiplayerSelections dictionary type mismatch"; return false; }
    auto contains = p.method(dictionary_class, "ContainsKey", "System.Boolean", kPlayer);
    auto item = p.method(dictionary_class, "get_Item", "VampireSurvivors.Framework.CoopSelections", kPlayer);
    if (!contains || !item || p.from_type(p.api.method_get_param(contains, 0)) != p.player_class ||
        p.from_type(p.api.method_get_param(item, 0)) != p.player_class || p.from_type(p.api.method_get_return_type(item)) != selection_class) {
        error = "MultiplayerSelections accessors do not match canonical player/selection types"; return false;
    }
    void* key_args[] = {target};
    auto raw_contains = p.invoke(contains, dictionary.get(), key_args, error);
    if (!error.empty()) return false;
    Impl::Root present;
    if (!p.root(present, raw_contains, error) || p.api.object_get_class(present.get()) != boolean_class) { if (error.empty()) error = "Selection membership result type mismatch"; return false; }
    const auto present_data = p.api.object_unbox(present.get());
    std::uint8_t has_entry = 0;
    if (!present_data) { error = "Selection membership cannot be unboxed"; return false; }
    std::memcpy(&has_entry, present_data, sizeof(has_entry));
    if (has_entry != 1) { error = "No existing per-player CoopSelections entry; no entry was created"; return false; }
    auto raw_selection = p.invoke(item, dictionary.get(), key_args, error);
    if (!error.empty()) return false;
    Impl::Root selection;
    if (!p.root(selection, raw_selection, error) || p.api.object_get_class(selection.get()) != selection_class) { if (error.empty()) error = "Per-player CoopSelections instance mismatch"; return false; }
    auto selected_field = field_by_name(selection_class, "SelectedCharacter");
    if (!selected_field || field_parent(selected_field) != selection_class || (field_flags(selected_field) & 7) != 6 ||
        (field_flags(selected_field) & (16 | 32 | 64)) || p.from_type(field_type(selected_field)) != character_class) {
        error = "Per-player SelectedCharacter field is not a writable public instance enum"; return false;
    }
    Impl::Capture recheck;
    if (p.capture(recheck, error) != CoopRosterStatus::Captured || !p.addition(recheck)) { if (error.empty()) error = "Owned roster changed before character assignment"; return false; }
    field_set(selection.get(), selected_field, &character);
    std::int32_t assigned = 0;
    field_get(selection.get(), selected_field, &assigned);
    if (assigned != character) { error = "Per-player character write did not read back"; return false; }
    return true;
}
bool CoopRoster::rollback(std::string& error) {
    error.clear();
    if (!impl_) { error = "Roster module not initialized"; return false; }
    auto& p = *impl_;
    if (!p.check(error)) return false;
    if (!p.pending) return true;
    Impl::Capture before;
    if (p.capture(before, error) != CoopRosterStatus::Captured) { if (error.empty()) error = "Roster unavailable during rollback"; return false; }
    auto raw_selected = p.invoke(p.selected_player, before.manager.get(), nullptr, error);
    if (!error.empty()) return false;
    Impl::Root selected;
    std::int32_t selected_index = -1;
    if (!p.root(selected, raw_selected, error) || !p.integer(p.selected_index, before.manager.get(), selected_index, error)) return false;
    if (selected.get() != p.original_player.get() || selected_index != 0) {
        error = "Restore the original menu player before roster rollback"; return false;
    }
    if (p.baseline(before)) { p.clear_ownership(); return true; }
    if (!p.addition(before)) { error = "Manager or roster identity changed; refusing removal from an unexpected roster"; return false; }
    void* args[] = { p.added_player.get() };
    const auto result = p.api.invoke(p.remove, p.owned_manager.get(), args);
    Impl::Capture after;
    if (p.capture(after, error) == CoopRosterStatus::Captured && p.baseline(after)) {
        p.clear_ownership();
        if (!result.exception) return true;
        error = "RemovePlayer raised although original roster membership was restored";
        return false;
    }
    if (error.empty()) error = result.exception ? "RemovePlayer raised; owned addition retained for retry" : "RemovePlayer did not restore the original roster";
    return false;
}
}
