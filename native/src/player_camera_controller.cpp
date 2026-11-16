#include "wsm/player_camera_controller.h"
#include "wsm/main_thread_guard.h"
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace wsm {
namespace {
bool same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
}
struct PlayerCameraController::Impl {
    Il2CppApi api{};
    HMODULE module{};
    Il2CppDomain* domain{};
    DWORD os{};
    std::int32_t managed{};
    MainThreadGuard guard;
    PlayerStateReader reader;
    PlayerCameraState view;
    bool exposed = false, pending = false, inside = false;
    // Store the highest refused generation. A late old worker must neither
    // taint a new lease nor erase a newer lease's refusal diagnostic.
    std::atomic<unsigned> foreign_generation{0};
    std::atomic<bool> recovery_pending{false}, diagnostic_lost{false};
    static std::atomic<Impl*> owner;
    // Never recycle a callback address. A managed Action retained by an old
    // scene must remain a tombstone, not dispatch into a later camera owner.
    static constexpr unsigned callback_capacity = 256;
    static std::array<std::atomic<Impl*>, callback_capacity> callback_owners;
    static std::atomic<unsigned> next_callback;
    unsigned callback_slot = callback_capacity;
    Il2CppClass* (*from_type)(const Il2CppType*){};
    Il2CppClass* (*method_owner)(const MethodInfo*){};
    Il2CppClass* (*element)(Il2CppClass*){};
    bool (*assignable)(Il2CppClass*, Il2CppClass*){};
    void* (*pin)(Il2CppObject*, bool){};
    Il2CppObject* (*target)(void*){};
    void (*unpin)(void*){};
    FieldInfo* (*field_named)(Il2CppClass*, const char*){};
    Il2CppClass* (*field_owner)(FieldInfo*){};
    const Il2CppType* (*field_type)(FieldInfo*){};
    int (*field_flags)(FieldInfo*){};
    void (*field_value)(Il2CppObject*, FieldInfo*, void*){};
    void (*field_set_value)(Il2CppObject*, FieldInfo*, void*){};
    int (*value_size)(Il2CppClass*, std::uint32_t*){};
    struct Root {
        Impl* p{}; void* h{};
        Root() = default;
        Root(Impl& value, Il2CppObject* object) : p(&value), h(object ? value.pin(object, true) : nullptr) {
            if (object && (!h || !get())) { reset(); throw std::runtime_error("Unable to retain camera object"); }
        }
        Root(Root&& other) noexcept : p(other.p), h(other.h) { other.h = nullptr; }
        Root& operator=(Root&& other) noexcept {
            if (this != &other) { reset(); p = other.p; h = other.h; other.h = nullptr; }
            return *this;
        }
        Root(const Root&) = delete;
        Root& operator=(const Root&) = delete;
        ~Root() { reset(); }
        void reset() { if (h) { p->unpin(h); h = nullptr; } }
        Il2CppObject* get() const { return h ? p->target(h) : nullptr; }
    };
    Root manager_root, stage_root, transform_root, action_root;
    Il2CppClass *manager{}, *object{}, *system_type{}, *action{}, *boolean{}, *integer{}, *vector3{};
    const MethodInfo *find{}, *array_get{}, *running{}, *stage_get{}, *camera_get{}, *manual_get{}, *manual_set{}, *normal{}, *live{}, *position_get{}, *position_set{}, *convert{};
    FieldInfo *x_field{}, *y_field{}, *z_field{}, *manual_field{};
    struct Position { float x{}, y{}, z{}; };
    void verify() {
        MainThreadIdentity id; std::string error;
        if (!guard.verify(os, managed, id, error)) throw std::runtime_error(error);
    }
    Il2CppClass* klass(const char* image, const char* ns, const char* name) {
        std::size_t count = 0; auto assemblies = api.domain_get_assemblies(domain, &count);
        if (!assemblies) throw std::runtime_error("Assembly list unavailable");
        Il2CppClass* result = nullptr;
        for (std::size_t i = 0; i < count; ++i) {
            auto im = api.assembly_get_image(assemblies[i]);
            if (!im || !same(api.image_get_name(im), image)) continue;
            for (std::size_t j = 0; j < api.image_get_class_count(im); ++j) {
                auto c = const_cast<Il2CppClass*>(api.image_get_class(im, j));
                if (c && same(api.class_get_namespace(c), ns) && same(api.class_get_name(c), name)) {
                    if (result) throw std::runtime_error("Ambiguous camera class");
                    result = c;
                }
            }
        }
        if (!result) throw std::runtime_error(std::string("Missing camera class ") + name);
        return result;
    }
    std::string type_name(const Il2CppType* type) {
        char* raw = type ? api.type_get_name(type) : nullptr;
        std::string name = raw ? raw : ""; if (raw) api.free(raw); return name;
    }
    const MethodInfo* method(Il2CppClass* cls, const char* name, Il2CppClass* result,
                             std::initializer_list<Il2CppClass*> args = {}, bool stat = false) {
        const MethodInfo* found = nullptr; void* iterator = nullptr;
        while (auto m = api.class_get_methods(cls, &iterator)) {
            if (method_owner(m) != cls || !same(api.method_get_name(m), name) ||
                api.method_get_param_count(m) != args.size() || from_type(api.method_get_return_type(m)) != result) continue;
            std::uint32_t impl = 0; auto flags = api.method_get_flags(m, &impl);
            if ((flags & 7) != 6 || ((flags & 16) != 0) != stat) continue;
            unsigned index = 0; bool match = true;
            for (auto arg : args) if (from_type(api.method_get_param(m, index++)) != arg) match = false;
            if (!match) continue;
            if (found) throw std::runtime_error(std::string("Ambiguous camera method ") + name);
            found = m;
        }
        if (!found) throw std::runtime_error(std::string("Missing exact camera method ") + name);
        return found;
    }
    void require(Il2CppObject* object_value, Il2CppClass* cls) {
        if (!object_value || !assignable(cls, api.object_get_class(object_value))) throw std::runtime_error("Invalid camera object type");
    }
    Il2CppObject* invoke(const MethodInfo* method_value, Il2CppObject* receiver = nullptr, void** args = nullptr) {
        auto result = api.invoke(method_value, receiver, args);
        if (result.exception) throw std::runtime_error(std::string("Managed camera exception in ") + api.method_get_name(method_value));
        return result.value;
    }
    bool flag(const MethodInfo* method_value, Il2CppObject* receiver = nullptr, void** args = nullptr) {
        Root boxed(*this, invoke(method_value, receiver, args));
        if (!boxed.get() || api.object_get_class(boxed.get()) != boolean) throw std::runtime_error("Invalid camera Boolean");
        auto raw = api.object_unbox(boxed.get()); if (!raw) throw std::runtime_error("Missing camera Boolean value");
        std::uint8_t value = 0; std::memcpy(&value, raw, sizeof value);
        if (value > 1) throw std::runtime_error("Invalid camera Boolean value");
        return value != 0;
    }
    bool alive(Il2CppObject* value) { if (!value) return false; void* args[]{value}; return flag(live, nullptr, args); }
    Root find_manager() {
        Root type(*this, api.type_get_object(api.class_get_type(manager))); require(type.get(), system_type);
        void* args[]{type.get()}; Root array(*this, invoke(find, nullptr, args));
        if (!array.get()) throw std::runtime_error("Null camera manager lookup");
        auto el = element(api.object_get_class(array.get()));
        if (!el || (!assignable(manager, el) && !assignable(el, manager))) throw std::runtime_error("Invalid camera manager lookup array");
        auto count = api.array_length(reinterpret_cast<Il2CppArray*>(array.get()));
        if (!count) return {};
        if (count != 1) throw std::runtime_error("Ambiguous camera manager");
        std::int32_t zero = 0; void* index[]{&zero}; Root result(*this, invoke(array_get, array.get(), index));
        require(result.get(), manager); return result;
    }
    void restore_override() {
        if (!pending) return;
        // Unity native destruction does not destroy the rooted managed
        // wrapper. This exact Action backing field is managed data: cleanup
        // never invokes a Unity getter or a method on the destroyed component.
        require(manager_root.get(), manager);
        Il2CppObject* value = nullptr;
        field_value(manager_root.get(), manual_field, &value);
        Root current(*this, value);
        if (value) require(current.get(), action);
        // Never overwrite a replacement owned by the game or another component.
        if (current.get() == action_root.get()) {
            // Field::SetValue uses deref_pointer=false for reference fields:
            // pass the reference itself, not the address of a local pointer.
            field_set_value(manager_root.get(), manual_field, nullptr);
            Il2CppObject* readback = nullptr;
            field_value(manager_root.get(), manual_field, &readback);
            if (readback == action_root.get()) throw std::runtime_error("Camera callback removal not confirmed");
        }
        pending = false; view.attached = false; view.has_target = false;
    }
    void update() {
        verify();
        if (inside) throw std::runtime_error("Reentrant camera callback");
        struct Scope { bool& value; explicit Scope(bool& v) : value(v) { value = true; } ~Scope() { value = false; } } scope(inside);
        if (!pending) return;
        auto current_manager = find_manager();
        if (current_manager.get() != manager_root.get() || !alive(manager_root.get())) {
            view.has_target = false; throw std::runtime_error("Camera manager identity changed");
        }
        Root current_action(*this, invoke(manual_get, manager_root.get()));
        if (current_action.get() != action_root.get()) { pending = false; view.attached = false; view.has_target = false; return; }
        if (!flag(running, manager_root.get())) { restore_override(); return; }
        Root stage(*this, invoke(stage_get, manager_root.get()));
        Root transform(*this, invoke(camera_get, manager_root.get()));
        if (stage.get() != stage_root.get() || transform.get() != transform_root.get() || !alive(transform.get())) {
            restore_override(); throw std::runtime_error("Camera stage or aggregate target identity changed");
        }
        // Return the aggregate to stock control during cutscenes/additional
        // targets, rather than suppress its calculation with our idle Action.
        if (!flag(normal, manager_root.get())) { restore_override(); return; }
        SceneStateSnapshot snapshot; std::string error;
        auto status = reader.read(snapshot, error);
        if (status != PlayerReadStatus::Captured) { view.has_target = false; throw std::runtime_error(error.empty() ? "Camera scene is not ready" : error); }
        std::vector<CameraSubject> actors; actors.reserve(snapshot.players.size());
        for (const auto& actor : snapshot.players) actors.push_back({actor.input_id, actor.x, actor.y, actor.dead});
        CameraFocus focus;
        if (!choose_camera_focus(actors, view.mode, view.local_input_id, view.target_input_id, focus, error)) throw std::runtime_error(error);
        view.has_target = focus.has_target; view.spectating = focus.spectating;
        if (!focus.has_target) { view.error.clear(); return; }
        Root position(*this, invoke(position_get, transform_root.get()));
        if (!position.get() || api.object_get_class(position.get()) != vector3) throw std::runtime_error("Invalid camera position value");
        Position next{focus.x, focus.y, 0}; field_value(position.get(), z_field, &next.z);
        if (!std::isfinite(next.z)) throw std::runtime_error("Non-finite camera depth");
        void* args[]{&next}; invoke(position_set, transform_root.get(), args);
        view.x = next.x; view.y = next.y; view.target_input_id = focus.input_id; ++view.updates; view.error.clear();
    }
    void failed_callback(const char* message) noexcept {
        view.has_target = false;
        try { view.error = message; }
        catch (...) { diagnostic_lost.store(true, std::memory_order_release); }
        // Once a main-thread update fails, restore stock aggregation rather
        // than leave our non-null override freezing the camera indefinitely.
        // A failed guard/removal retains all ownership for explicit close.
        try { verify(); restore_override(); }
        catch (...) { recovery_pending.store(true, std::memory_order_release); }
    }
    static void dispatch(unsigned slot) noexcept {
        auto* p = callback_owners[slot].load(std::memory_order_acquire);
        if (!p) return;
        if (GetCurrentThreadId() != p->os) {
            const auto generation = slot + 1;
            auto previous = p->foreign_generation.load(std::memory_order_relaxed);
            while (previous < generation && !p->foreign_generation.compare_exchange_weak(
                previous, generation, std::memory_order_release, std::memory_order_relaxed)) {}
            return;
        }
        try { p->update(); }
        catch (const std::exception& error) { p->failed_callback(error.what()); }
        catch (...) { p->failed_callback("Unknown camera callback failure"); }
    }
    template<std::size_t Slot> static void __cdecl callback() noexcept { dispatch(static_cast<unsigned>(Slot)); }
    using Callback = void (__cdecl*)();
    template<std::size_t... Slots>
    static constexpr std::array<Callback, sizeof...(Slots)> callback_entries(std::index_sequence<Slots...>) {
        return {{&callback<Slots>...}};
    }
    static unsigned reserve_callback() {
        auto slot = next_callback.load(std::memory_order_relaxed);
        do {
            if (slot >= callback_capacity) throw std::runtime_error("Camera callback generation capacity exhausted; restart the process");
        } while (!next_callback.compare_exchange_weak(slot, slot + 1, std::memory_order_relaxed));
        return slot;
    }
    void bind() {
#define LOAD(member, name) member = reinterpret_cast<decltype(member)>(GetProcAddress(module, name)); if (!member) throw std::runtime_error("Missing export " name)
        LOAD(from_type, "il2cpp_class_from_type"); LOAD(method_owner, "il2cpp_method_get_class");
        LOAD(element, "il2cpp_class_get_element_class"); LOAD(assignable, "il2cpp_class_is_assignable_from");
        LOAD(pin, "il2cpp_gchandle_new"); LOAD(target, "il2cpp_gchandle_get_target"); LOAD(unpin, "il2cpp_gchandle_free");
        LOAD(field_named, "il2cpp_class_get_field_from_name"); LOAD(field_owner, "il2cpp_field_get_parent");
        LOAD(field_type, "il2cpp_field_get_type"); LOAD(field_flags, "il2cpp_field_get_flags"); LOAD(field_value, "il2cpp_field_get_value");
        LOAD(field_set_value, "il2cpp_field_set_value");
        LOAD(value_size, "il2cpp_class_value_size");
#undef LOAD
        std::string error;
        if (!MainThreadGuard::create(api, domain, guard, error)) throw std::runtime_error(error);
        verify();
        if (!reader.initialize(module, api, domain, os, managed, error)) throw std::runtime_error(error);
        const char* core = "UnityEngine.CoreModule.dll";
        manager = klass("VampireSurvivors.Runtime.dll", "VampireSurvivors.Framework", "GameManager");
        object = klass(core, "UnityEngine", "Object"); auto transform = klass(core, "UnityEngine", "Transform");
        vector3 = klass(core, "UnityEngine", "Vector3");
        system_type = klass("mscorlib.dll", "System", "Type"); action = klass("mscorlib.dll", "System", "Action");
        boolean = klass("mscorlib.dll", "System", "Boolean"); integer = klass("mscorlib.dll", "System", "Int32");
        auto void_type = klass("mscorlib.dll", "System", "Void"); auto single = klass("mscorlib.dll", "System", "Single");
        auto array = klass("mscorlib.dll", "System", "Array");
        void* iterator = nullptr;
        while (auto m = api.class_get_methods(object, &iterator)) {
            if (method_owner(m) != object || !same(api.method_get_name(m), "FindObjectsOfType") || api.method_get_param_count(m) != 1 || from_type(api.method_get_param(m, 0)) != system_type || type_name(api.method_get_return_type(m)) != "UnityEngine.Object[]") continue;
            std::uint32_t impl = 0; auto flags = api.method_get_flags(m, &impl);
            if ((flags & 7) != 6 || !(flags & 16)) continue;
            if (find) throw std::runtime_error("Ambiguous camera object finder"); find = m;
        }
        if (!find) throw std::runtime_error("Missing camera object finder");
        array_get = method(array, "GetValue", klass("mscorlib.dll", "System", "Object"), {integer});
        running = method(manager, "get_IsGameRunning", boolean);
        stage_get = method(manager, "get_Stage", klass("VampireSurvivors.Runtime.dll", "VampireSurvivors.Objects", "Stage"));
        camera_get = method(manager, "get_CoopCameraTarget", transform);
        manual_get = method(manager, "get_ManualCameraTargetControl", action);
        manual_set = method(manager, "set_ManualCameraTargetControl", void_type, {action});
        manual_field = field_named(manager, "<ManualCameraTargetControl>k__BackingField");
        if (!manual_field || field_owner(manual_field) != manager ||
            (field_flags(manual_field) & (16 | 32 | 64)) || (field_flags(manual_field) & 7) != 1 ||
            from_type(field_type(manual_field)) != action) throw std::runtime_error("Invalid managed camera callback backing field");
        normal = method(manager, "IsNormalCameraTarget", boolean);
        live = method(object, "op_Implicit", boolean, {object}, true);
        position_get = method(transform, "get_position", vector3);
        position_set = method(transform, "set_position", void_type, {vector3});
        auto marshal = klass("mscorlib.dll", "System.Runtime.InteropServices", "Marshal");
        convert = method(marshal, "GetDelegateForFunctionPointer", klass("mscorlib.dll", "System", "Delegate"), {klass("mscorlib.dll", "System", "IntPtr"), system_type}, true);
        for (auto entry : {std::pair{"x", &x_field}, std::pair{"y", &y_field}, std::pair{"z", &z_field}}) {
            auto f = field_named(vector3, entry.first);
            if (!f || field_owner(f) != vector3 || (field_flags(f) & (16 | 64)) || from_type(field_type(f)) != single) throw std::runtime_error("Invalid Vector3 field metadata");
            *entry.second = f;
        }
        std::uint32_t alignment = 0;
        if (value_size(vector3, &alignment) != sizeof(Position)) throw std::runtime_error("Unsupported Vector3 value ABI");
    }
};
std::atomic<PlayerCameraController::Impl*> PlayerCameraController::Impl::owner{nullptr};
std::array<std::atomic<PlayerCameraController::Impl*>, PlayerCameraController::Impl::callback_capacity> PlayerCameraController::Impl::callback_owners{};
std::atomic<unsigned> PlayerCameraController::Impl::next_callback{0};
PlayerCameraController::PlayerCameraController() = default;
PlayerCameraController::~PlayerCameraController() {
    // Callback code is pinned. Keep its context stable even after an incorrect
    // destructor thread or a failed removal; explicit close owns root release.
    if (impl_ && impl_->exposed) impl_.release();
}
bool PlayerCameraController::initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain, DWORD os, std::int32_t managed, std::string& error) {
    error.clear();
    try {
        if (impl_) throw std::runtime_error("Camera controller already initialized; reuse it after close");
        if (!module || !domain || !os || !api.thread_current || !api.thread_current() || !api.runtime_invoke || !api.object_unbox || !api.object_get_class || !api.array_length || !api.type_get_object || !api.class_get_type || !api.domain_get_assemblies || !api.assembly_get_image || !api.image_get_name || !api.image_get_class_count || !api.image_get_class || !api.class_get_name || !api.class_get_namespace || !api.class_get_methods || !api.method_get_name || !api.method_get_param_count || !api.method_get_param || !api.method_get_return_type || !api.method_get_flags || !api.type_get_name || !api.free) throw std::runtime_error("Incomplete camera runtime API");
        auto p = std::make_unique<Impl>(); p->module = module; p->api = api; p->domain = domain; p->os = os; p->managed = managed; p->bind(); impl_ = std::move(p); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
PlayerReadStatus PlayerCameraController::open(std::int32_t local, CameraFollowMode mode, std::string& error) {
    error.clear();
    try {
        if (!impl_) throw std::runtime_error("Camera controller not initialized");
        auto& p = *impl_; p.verify();
        if (p.pending || p.manager_root.get()) throw std::runtime_error("Close previous camera ownership before opening");
        SceneStateSnapshot snapshot; auto read = p.reader.read(snapshot, error);
        if (read != PlayerReadStatus::Captured) return read;
        std::vector<CameraSubject> subjects;
        for (const auto& actor : snapshot.players) subjects.push_back({actor.input_id, actor.x, actor.y, actor.dead});
        CameraFocus focus;
        if (!choose_camera_focus(subjects, mode, local, -1, focus, error)) return PlayerReadStatus::Error;
        auto manager = p.find_manager(); if (!manager.get() || !p.alive(manager.get()) || !p.flag(p.normal, manager.get())) return PlayerReadStatus::NotReady;
        Impl::Root previous(p, p.invoke(p.manual_get, manager.get()));
        if (previous.get()) throw std::runtime_error("Foreign manual camera callback is already installed");
        Impl::Root stage(p, p.invoke(p.stage_get, manager.get()));
        Impl::Root transform(p, p.invoke(p.camera_get, manager.get()));
        if (!stage.get() || !p.alive(transform.get())) return PlayerReadStatus::NotReady;
        const auto slot = Impl::reserve_callback();
        const auto entries = Impl::callback_entries(std::make_index_sequence<Impl::callback_capacity>{});
        const auto callback = entries[slot];
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<const wchar_t*>(callback), &pinned)) throw std::runtime_error("Camera callback module pin failed");
        Impl::Root type(p, p.api.type_get_object(p.api.class_get_type(p.action))); p.require(type.get(), p.system_type);
        std::intptr_t pointer = reinterpret_cast<std::intptr_t>(callback); void* args[]{&pointer, type.get()};
        Impl::Root action(p, p.invoke(p.convert, nullptr, args)); p.require(action.get(), p.action);
        Impl* empty = nullptr;
        if (!Impl::owner.compare_exchange_strong(empty, &p)) throw std::runtime_error("Another camera controller owns the process callback");
        p.manager_root = std::move(manager); p.stage_root = std::move(stage); p.transform_root = std::move(transform); p.action_root = std::move(action);
        p.view = {}; p.view.local_input_id = local; p.view.mode = mode; p.exposed = true; p.pending = true;
        p.callback_slot = slot;
        Impl::callback_owners[slot].store(&p, std::memory_order_release);
        void* install[]{p.action_root.get()}; p.invoke(p.manual_set, p.manager_root.get(), install);
        Impl::Root readback(p, p.invoke(p.manual_get, p.manager_root.get()));
        if (readback.get() != p.action_root.get()) throw std::runtime_error("Camera callback registration not confirmed");
        p.view.attached = true; return PlayerReadStatus::Captured;
    } catch (const std::exception& e) { error = e.what(); return PlayerReadStatus::Error; }
}
bool PlayerCameraController::set_mode(CameraFollowMode mode, std::string& error) {
    error.clear();
    try {
        if (!impl_) throw std::runtime_error("Camera controller not initialized");
        impl_->verify();
        if (mode != CameraFollowMode::OwnPlayer && mode != CameraFollowMode::SharedAlive) throw std::runtime_error("Unknown camera mode");
        impl_->view.mode = mode; return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
PlayerCameraState PlayerCameraController::state() const {
    if (!impl_) return {};
    auto& p = *impl_; p.verify(); auto result = p.view;
    // Active is an observation of the current scene, not just the last Action
    // invocation. A destroyed/replaced manager may never call that Action again.
    // This query does not release the retained owner: the consumer must still
    // close it, including its managed wrapper, before opening the new scene.
    if (p.pending && result.attached) {
        bool current = false;
        try {
            auto manager = p.find_manager();
            if (manager.get() == p.manager_root.get() && p.alive(manager.get()) &&
                p.flag(p.running, manager.get()) && p.flag(p.normal, manager.get())) {
                Impl::Root stage(p, p.invoke(p.stage_get, manager.get()));
                Impl::Root transform(p, p.invoke(p.camera_get, manager.get()));
                Impl::Root action(p, p.invoke(p.manual_get, manager.get()));
                current = stage.get() == p.stage_root.get() && transform.get() == p.transform_root.get() &&
                    p.alive(transform.get()) && action.get() == p.action_root.get();
            }
        } catch (const std::exception& error) { result.error = error.what(); }
        if (!current) {
            result.attached = false; result.has_target = false; result.spectating = false;
            result.target_input_id = -1; result.x = 0; result.y = 0;
        }
    }
    if (p.callback_slot < Impl::callback_capacity &&
        p.foreign_generation.load(std::memory_order_acquire) == p.callback_slot + 1)
        result.error = "Camera callback refused a foreign thread";
    if (impl_->diagnostic_lost.load(std::memory_order_acquire)) result.error = "Camera callback failed while recording its diagnostic";
    if (impl_->recovery_pending.load(std::memory_order_acquire)) result.error += "; camera callback recovery remains pending";
    return result;
}
bool PlayerCameraController::close(std::string& error) {
    error.clear();
    try {
        if (!impl_) return true;
        auto& p = *impl_; p.verify();
        if (p.inside) throw std::runtime_error("Camera callback is active");
        p.restore_override();
        if (p.callback_slot < Impl::callback_capacity) {
            auto* expected_slot = &p;
            Impl::callback_owners[p.callback_slot].compare_exchange_strong(expected_slot, nullptr);
            p.callback_slot = Impl::callback_capacity;
        }
        auto* expected = &p; Impl::owner.compare_exchange_strong(expected, nullptr);
        p.action_root.reset(); p.transform_root.reset(); p.stage_root.reset(); p.manager_root.reset();
        p.recovery_pending.store(false, std::memory_order_release);
        p.view.attached = false; p.view.has_target = false; return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
}
