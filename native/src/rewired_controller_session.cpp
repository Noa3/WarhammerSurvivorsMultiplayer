#include "wsm/rewired_controller_session.h"
#include "wsm/rewired_input_profile.h"
#include "wsm/main_thread_guard.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wsm {
namespace {
bool same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
struct Runtime {
    HMODULE module{}; Il2CppApi a; Il2CppDomain* domain{};
    Il2CppClass* (*parent)(Il2CppClass*){};
    Il2CppClass* (*declaring)(Il2CppClass*){};
    Il2CppClass* (*from_type)(const Il2CppType*){};
    const Il2CppImage* (*image)(Il2CppClass*){};
    Il2CppClass* (*element)(Il2CppClass*){};
    Il2CppClass* (*interfaces)(Il2CppClass*, void**){};
    Il2CppClass* (*method_class)(const MethodInfo*){};
    const MethodInfo* (*virtual_method)(Il2CppObject*, const MethodInfo*){};
    bool (*assignable)(Il2CppClass*, Il2CppClass*){};
    bool (*value_type)(const Il2CppClass*){};
    const Il2CppType* (*enum_base)(Il2CppClass*){};
    void* (*pin)(Il2CppObject*, bool){};
    Il2CppObject* (*target)(void*){};
    void (*unpin)(void*){};
    Il2CppObject* (*new_string)(const char*){};
    int (*string_length)(Il2CppObject*){};
    const wchar_t* (*string_chars)(Il2CppObject*){};
    FieldInfo* (*field_named)(Il2CppClass*, const char*){};
    Il2CppClass* (*field_owner)(FieldInfo*){};
    const Il2CppType* (*field_type)(FieldInfo*){};
    int (*field_flags)(FieldInfo*){};
    void (*field_value)(Il2CppObject*, FieldInfo*, void*){};
    struct Root {
        Runtime* d{}; void* handle{};
        Root() = default;
        Root(Runtime& runtime, Il2CppObject* value) : d(&runtime), handle(value ? runtime.pin(value, true) : nullptr) {
            if (value && (!handle || !get())) { reset(); throw std::runtime_error("Unable to root control object"); }
        }
        Root(Root&& o) noexcept : d(o.d), handle(o.handle) { o.handle = nullptr; }
        Root& operator=(Root&& o) noexcept { if (this != &o) { reset(); d = o.d; handle = o.handle; o.handle = nullptr; } return *this; }
        Root(const Root&) = delete; Root& operator=(const Root&) = delete;
        ~Root() { reset(); }
        void reset() { if (handle) { d->unpin(handle); handle = nullptr; } }
        Il2CppObject* get() const { return handle ? d->target(handle) : nullptr; }
    };
    void load() {
#define API(member, name) member = reinterpret_cast<decltype(member)>(GetProcAddress(module, name)); if (!member) throw std::runtime_error("Missing export " name)
        API(parent,"il2cpp_class_get_parent"); API(declaring,"il2cpp_class_get_declaring_type");
        API(from_type,"il2cpp_class_from_type"); API(image,"il2cpp_class_get_image");
        API(element,"il2cpp_class_get_element_class"); API(interfaces,"il2cpp_class_get_interfaces");
        API(method_class,"il2cpp_method_get_class"); API(virtual_method,"il2cpp_object_get_virtual_method");
        API(assignable,"il2cpp_class_is_assignable_from"); API(enum_base,"il2cpp_class_enum_basetype");
        API(value_type,"il2cpp_class_is_valuetype");
        API(pin,"il2cpp_gchandle_new"); API(target,"il2cpp_gchandle_get_target"); API(unpin,"il2cpp_gchandle_free");
        API(new_string,"il2cpp_string_new"); API(string_length,"il2cpp_string_length"); API(string_chars,"il2cpp_string_chars");
        API(field_named,"il2cpp_class_get_field_from_name"); API(field_owner,"il2cpp_field_get_parent");
        API(field_type,"il2cpp_field_get_type"); API(field_flags,"il2cpp_field_get_flags"); API(field_value,"il2cpp_field_get_value");
#undef API
    }
    std::string type_name(const Il2CppType* t) const {
        if (!t) return {}; auto p = a.type_get_name(t); std::string result = p ? p : ""; if (p) a.free(p); return result;
    }
    std::string name(Il2CppClass* c) const { return c ? type_name(a.class_get_type(c)) : std::string(); }
    bool in_image(Il2CppClass* c, const char* expected) const { return c && same(a.image_get_name(image(c)), expected); }
    Il2CppClass* klass(const char* img, const char* ns, const char* label) const {
        std::size_t count = 0; auto list = a.domain_get_assemblies(domain, &count); Il2CppClass* found = nullptr;
        if (!list) throw std::runtime_error("Assembly list unavailable");
        for (std::size_t i = 0; i < count; ++i) {
            auto im = a.assembly_get_image(list[i]); if (!im || !same(a.image_get_name(im), img)) continue;
            for (std::size_t j = 0; j < a.image_get_class_count(im); ++j) {
                auto c = const_cast<Il2CppClass*>(a.image_get_class(im, j));
                if (c && !declaring(c) && same(a.class_get_namespace(c), ns) && same(a.class_get_name(c), label)) {
                    if (found) throw std::runtime_error(std::string("Ambiguous class ") + label); found = c;
                }
            }
        }
        if (!found) throw std::runtime_error(std::string("Missing canonical class ") + img + ":" + ns + "." + label);
        return found;
    }
    Il2CppClass* rewired(const char* label) const { return klass("Rewired_Core.dll", "Rewired", label); }
    bool matches(const Il2CppType* t, const std::string& expected) const {
        if (type_name(t) != expected) return false;
        auto c = from_type(t); if (!c) return false;
        if (expected.find('<') != std::string::npos) return in_image(c, "mscorlib.dll");
        if (expected.size() > 2 && expected.substr(expected.size()-2) == "[]") {
            auto e = element(c); return e && matches(a.class_get_type(e), expected.substr(0, expected.size()-2));
        }
        const auto split = expected.rfind('.'); if (split == std::string::npos) return false;
        const auto ns = expected.substr(0, split), label = expected.substr(split+1);
        const char* img = ns.rfind("Rewired", 0) == 0 ? "Rewired_Core.dll" :
            ns.rfind("UnityEngine", 0) == 0 ? "UnityEngine.CoreModule.dll" :
            ns.rfind("VampireSurvivors", 0) == 0 ? "VampireSurvivors.Runtime.dll" : "mscorlib.dll";
        return c == klass(img, ns.c_str(), label.c_str());
    }
    const MethodInfo* method(Il2CppClass* owner, const char* label, const std::string& result,
                             std::initializer_list<std::string> parameters = {}, bool stat = false) const {
        const MethodInfo* found = nullptr; void* it = nullptr;
        while (auto m = a.class_get_methods(owner, &it)) {
            if (method_class(m) != owner || !same(a.method_get_name(m), label) || a.method_get_param_count(m) != parameters.size()) continue;
            std::uint32_t impl = 0; auto flags = a.method_get_flags(m, &impl);
            if ((flags & 7u) != 6u || ((flags & 16u) != 0) != stat || !matches(a.method_get_return_type(m), result)) continue;
            unsigned index = 0; bool valid = true;
            for (const auto& p : parameters) if (!matches(a.method_get_param(m, index++), p)) { valid = false; break; }
            if (!valid) continue;
            if (found) throw std::runtime_error(std::string("Ambiguous method ") + label); found = m;
        }
        if (!found) throw std::runtime_error("Missing canonical method " + name(owner) + "." + label + " -> " + result);
        return found;
    }
    const MethodInfo* nested_getter(Il2CppClass* owner, const char* getter, const char* nested) const {
        const MethodInfo* found = nullptr; void* it = nullptr;
        while (auto m = a.class_get_methods(owner, &it)) {
            if (method_class(m) != owner || !same(a.method_get_name(m), getter) || a.method_get_param_count(m)) continue;
            std::uint32_t impl = 0; auto f = a.method_get_flags(m, &impl); auto c = from_type(a.method_get_return_type(m));
            if ((f & 7u) != 6u || !(f & 16u) || !c || !in_image(c,"Rewired_Core.dll") || declaring(c) != owner || !same(a.class_get_name(c),nested)) continue;
            if (found) throw std::runtime_error("Ambiguous nested helper getter"); found = m;
        }
        if (!found) throw std::runtime_error(std::string("Missing nested helper getter ") + getter);
        return found;
    }
    Il2CppClass* nested_field_type(Il2CppClass* owner, const char* label, const char* nested) const {
        auto f = field_named(owner, label); auto c = f ? from_type(field_type(f)) : nullptr;
        if (!f || field_owner(f) != owner || (field_flags(f)&7) != 6 || (field_flags(f)&16) ||
            !c || declaring(c) != owner || !in_image(c,"Rewired_Core.dll") || !same(a.class_get_name(c),nested))
            throw std::runtime_error(std::string("Invalid canonical helper field ") + label);
        return c;
    }
    Root field(Il2CppObject* object, Il2CppClass* owner, const char* label, Il2CppClass* expected) {
        auto f = field_named(owner,label);
        if (!object || !assignable(owner,a.object_get_class(object)) || !f || field_owner(f)!=owner ||
            (field_flags(f)&7)!=6 || (field_flags(f)&16) || from_type(field_type(f))!=expected)
            throw std::runtime_error(std::string("Invalid helper field access ")+label);
        Il2CppObject* raw = nullptr; field_value(object,f,&raw); Root r(*this,raw);
        require(r.get(),expected); return r;
    }
    FieldInfo* readonly_integer_field(Il2CppClass* owner,const char* label) const {
        auto f=field_named(owner,label);
        if(!f||field_owner(f)!=owner||(field_flags(f)&7)!=6||(field_flags(f)&16)||!(field_flags(f)&32)||!matches(field_type(f),"System.Int32"))
            throw std::runtime_error(std::string("Invalid canonical readonly Int32 field ")+label);
        return f;
    }
    int field_integer(FieldInfo* f,Il2CppObject* object) {
        require(object,field_owner(f));std::int32_t value{};field_value(object,f,&value);return value;
    }
    void require(Il2CppObject* o, Il2CppClass* expected) const {
        if (!o || !expected || !assignable(expected,a.object_get_class(o))) throw std::runtime_error("Invalid canonical runtime object");
    }
    Il2CppObject* invoke(const MethodInfo* m, Il2CppObject* object = nullptr, void** args = nullptr) const {
        std::uint32_t impl = 0; auto flags = a.method_get_flags(m,&impl);
        if (object && (flags&64u)) { m = virtual_method(object,m); if(!m) throw std::runtime_error("Virtual method resolution failed"); }
        // Virtual interface resolution can select a struct enumerator method.
        // Runtime::Invoke forwards the receiver unchanged to its invoker; such
        // instance methods need the unboxed value, not the boxed object header.
        void* receiver = object;
        if(object && value_type(method_class(m))) {
            receiver=a.object_unbox(object);
            if(!receiver)throw std::runtime_error("Invalid boxed value-type receiver");
        }
        auto r = a.invoke(m,receiver,args);
        if (r.exception) throw std::runtime_error(std::string("Managed call failed: ")+a.method_get_name(m)); return r.value;
    }
    template<class T> T scalar(Il2CppObject* object, const char* kind) {
        Root r(*this,object); auto cls = klass("mscorlib.dll","System",kind);
        if (!r.get() || a.object_get_class(r.get()) != cls || !a.object_unbox(r.get())) throw std::runtime_error("Invalid boxed primitive");
        T value{}; std::memcpy(&value,a.object_unbox(r.get()),sizeof value); return value;
    }
    int integer(const MethodInfo* m, Il2CppObject* o) { return scalar<std::int32_t>(invoke(m,o),"Int32"); }
    bool boolean(const MethodInfo* m, Il2CppObject* o = nullptr, void** args = nullptr) {
        auto b = scalar<std::uint8_t>(invoke(m,o,args),"Boolean"); if(b>1)throw std::runtime_error("Invalid Boolean");return b!=0;
    }
    std::string text(Il2CppObject* object) {
        Root s(*this,object); require(s.get(),klass("mscorlib.dll","System","String"));
        const auto n = string_length(s.get()); if(n<0||n>256)throw std::runtime_error("Input label exceeds 256 UTF16 units");
        if(!n)return {}; auto p=string_chars(s.get());if(!p)throw std::runtime_error("Invalid managed string");
        auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,p,n,nullptr,0,nullptr,nullptr);
        if(length<=0)throw std::runtime_error("Invalid input label UTF16"); std::string out(length,'\0');
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,p,n,out.data(),length,nullptr,nullptr)!=length)throw std::runtime_error("Input label conversion failed"); return out;
    }
    Il2CppClass* interface_for(Il2CppClass* start,const std::string& expected) const {
        std::vector<Il2CppClass*> pending{start};std::set<Il2CppClass*> seen;
        for(std::size_t i=0;i<pending.size();++i) {
            auto c=pending[i];if(!c||!seen.insert(c).second)continue;
            if(seen.size()>256)throw std::runtime_error("Interface traversal exceeds 256");
            if(name(c)==expected&&in_image(c,"mscorlib.dll"))return c;
            void* it=nullptr;while(auto iface=interfaces(c,&it))pending.push_back(iface);
            if(auto base=parent(c))pending.push_back(base);
        }
        throw std::runtime_error("Missing closed collection interface "+expected);
    }
    std::vector<Root> collect(Il2CppObject* source,Il2CppClass* expected,std::size_t limit) {
        if(!source)throw std::runtime_error("Null control collection");
        Root keep(*this,source);std::vector<Root> out;auto cls=a.object_get_class(keep.get());auto type=name(cls);
        if(type.size()>2&&type.substr(type.size()-2)=="[]") {
            auto e=element(cls);if(!e||!assignable(expected,e))throw std::runtime_error("Wrong control array element type");
            auto length=a.array_length(reinterpret_cast<Il2CppArray*>(keep.get()));if(length>limit)throw std::runtime_error("Control array exceeds bound");
            auto get=method(klass("mscorlib.dll","System","Array"),"GetValue","System.Object",{"System.Int32"});
            for(std::int32_t i=0;i<static_cast<std::int32_t>(length);++i){void* args[]={&i};Root row(*this,invoke(get,keep.get(),args));require(row.get(),expected);out.push_back(std::move(row));}
            return out;
        }
        auto iface=interface_for(cls,"System.Collections.Generic.IEnumerable<"+name(expected)+">");
        auto get=method(iface,"GetEnumerator","System.Collections.Generic.IEnumerator<"+name(expected)+">");
        Root iterator(*this,invoke(get,keep.get()));if(!iterator.get())throw std::runtime_error("Null collection iterator");
        auto typed=from_type(a.method_get_return_type(get));require(iterator.get(),typed);
        auto current=method(typed,"get_Current",name(expected));
        auto move=method(klass("mscorlib.dll","System.Collections","IEnumerator"),"MoveNext","System.Boolean");
        auto dispose=method(klass("mscorlib.dll","System","IDisposable"),"Dispose","System.Void");
        try {
            while(boolean(move,iterator.get())) {
                if(out.size()>=limit)throw std::runtime_error("Control collection exceeds bound");
                Root row(*this,invoke(current,iterator.get()));require(row.get(),expected);out.push_back(std::move(row));
            }
        } catch(...) { try{invoke(dispose,iterator.get());}catch(...){} throw; }
        invoke(dispose,iterator.get());return out;
    }
};
}
struct RewiredControllerSession::Impl {
    Runtime d; MainThreadGuard guard; DWORD os{}; std::int32_t managed{};
    RewiredOwnedControlState owned; RewiredControlOptions options;
    Runtime::Root controller, map, input_player, player_helper, controller_helper, mapping;
    std::vector<Runtime::Root> primary_roots, actor_roots, actor_inputs;
    Il2CppClass *reinput{},*player{},*controller_class{},*with_axes{},*custom{},*controller_map{},*custom_map{},*assignment{},*binding{},*identifier{},*category{},*category_base{},*layout{};
    Il2CppClass *players_helper_class{},*controllers_helper_class{},*mapping_class{},*player_controllers{},*player_maps{};
    FieldInfo* controller_id_field{};
    const MethodInfo *ready{},*players_get{},*controllers_get{},*mapping_get{},*get_player{},*get_customs{},*create_controller{},*destroy_controller{},*assigned{},*get_type{},*get_source{},*axis_ids{},*set_axis{},*read_axis{},*player_axis{};
    const MethodInfo *get_categories{},*get_layouts{},*category_id{},*category_name{},*layout_id{},*layout_name{},*get_action{},*all_controllers{},*get_maps{},*add_controller{},*remove_controller{},*add_empty_map{},*remove_map{},*create_assignment{},*create_element{},*enable_map{};
    const MethodInfo *map_id{},*map_controller{},*map_controller_id{},*map_type{},*map_category{},*map_layout{},*map_enabled{},*map_bindings{},*action_id{},*element_id{},*element_type{},*axis_range{},*inverted{},*identifier_id{};
    std::string primary_fingerprint;
    void clear_unowned_roots() {
        if(owned.cleanup_required)return;
        primary_roots.clear();actor_roots.clear();actor_inputs.clear();
        input_player.reset();player_helper.reset();controller_helper.reset();mapping.reset();
    }
    void verify() { MainThreadIdentity id; std::string e;if(!guard.verify(os,managed,id,e))throw std::runtime_error(e); }
    void bind() {
        d.load();std::string e;if(!MainThreadGuard::create(d.a,d.domain,guard,e))throw std::runtime_error(e);
        reinput=d.rewired("ReInput");player=d.rewired("Player");controller_class=d.rewired("Controller");with_axes=d.rewired("ControllerWithAxes");custom=d.rewired("CustomController");controller_map=d.rewired("ControllerMap");custom_map=d.rewired("CustomControllerMap");assignment=d.rewired("ElementAssignment");binding=d.rewired("ActionElementMap");identifier=d.rewired("ControllerElementIdentifier");category=d.rewired("InputMapCategory");category_base=d.rewired("InputCategory");layout=d.rewired("InputLayout");
        for(const char* label:{"ControllerType","ControllerElementType","AxisRange"})if(d.type_name(d.enum_base(d.rewired(label)))!="System.Int32")throw std::runtime_error("Control enum must be Int32");
        ready=d.method(reinput,"get_isReady","System.Boolean",{},true);
        players_get=d.nested_getter(reinput,"get_players","PlayerHelper");players_helper_class=d.from_type(d.a.method_get_return_type(players_get));
        controllers_get=d.nested_getter(reinput,"get_controllers","ControllerHelper");controllers_helper_class=d.from_type(d.a.method_get_return_type(controllers_get));
        mapping_get=d.nested_getter(reinput,"get_mapping","MappingHelper");mapping_class=d.from_type(d.a.method_get_return_type(mapping_get));
        player_controllers=d.nested_field_type(player,"controllers","ControllerHelper");player_maps=d.nested_field_type(player_controllers,"maps","MapHelper");
        get_player=d.method(players_helper_class,"GetPlayer","Rewired.Player",{"System.Int32"});
        get_customs=d.method(controllers_helper_class,"GetCustomControllers","Rewired.CustomController[]");
        create_controller=d.method(controllers_helper_class,"CreateCustomController","Rewired.CustomController",{"System.Int32","System.String"});
        destroy_controller=d.method(controllers_helper_class,"DestroyCustomController","System.Boolean",{"Rewired.CustomController"});
        assigned=d.method(controllers_helper_class,"IsCustomControllerAssignedToPlayer","System.Boolean",{"System.Int32","System.Int32"});
        controller_id_field=d.readonly_integer_field(controller_class,"id");get_type=d.method(controller_class,"get_type","Rewired.ControllerType");get_source=d.method(custom,"get_sourceControllerId","System.Int32");
        axis_ids=d.method(with_axes,"get_AxisElementIdentifiers","System.Collections.Generic.IList<Rewired.ControllerElementIdentifier>");identifier_id=d.method(identifier,"get_id","System.Int32");
        set_axis=d.method(custom,"SetAxisValueById","System.Void",{"System.Int32","System.Single"});read_axis=d.method(with_axes,"GetAxisById","System.Single",{"System.Int32"});
        player_axis=d.method(player,"GetAxis","System.Single",{"System.String"});
        get_categories=d.method(mapping_class,"get_MapCategories","System.Collections.Generic.IList<Rewired.InputMapCategory>");get_layouts=d.method(mapping_class,"get_CustomControllerLayouts","System.Collections.Generic.IList<Rewired.InputLayout>");
        category_id=d.method(category_base,"get_id","System.Int32");category_name=d.method(category_base,"get_name","System.String");layout_id=d.method(layout,"get_id","System.Int32");layout_name=d.method(layout,"get_name","System.String");get_action=d.method(mapping_class,"GetActionId","System.Int32",{"System.String"});
        all_controllers=d.method(player_controllers,"get_Controllers","System.Collections.Generic.IEnumerable<Rewired.Controller>");
        get_maps=d.method(player_maps,"GetMaps","System.Collections.Generic.IList<Rewired.ControllerMap>",{"Rewired.Controller"});
        add_controller=d.method(player_controllers,"AddController","System.Void",{"Rewired.Controller","System.Boolean"});remove_controller=d.method(player_controllers,"RemoveController","System.Void",{"Rewired.Controller"});
        add_empty_map=d.method(player_maps,"AddEmptyMap","System.Void",{"Rewired.ControllerType","System.Int32","System.Int32","System.Int32"});remove_map=d.method(player_maps,"RemoveMap","System.Void",{"Rewired.ControllerType","System.Int32","System.Int32"});
        enable_map=d.method(controller_map,"set_enabled","System.Void",{"System.Boolean"});
        create_assignment=d.method(assignment,"FullAxisAssignment","Rewired.ElementAssignment",{"System.Int32","System.Int32","System.Boolean"},true);create_element=d.method(controller_map,"CreateElementMap","System.Boolean",{"Rewired.ElementAssignment"});
        map_id=d.method(controller_map,"get_id","System.Int32");map_controller=d.method(controller_map,"get_controller","Rewired.Controller");map_controller_id=d.method(controller_map,"get_controllerId","System.Int32");map_type=d.method(controller_map,"get_controllerType","Rewired.ControllerType");map_category=d.method(controller_map,"get_categoryId","System.Int32");map_layout=d.method(controller_map,"get_layoutId","System.Int32");map_enabled=d.method(controller_map,"get_enabled","System.Boolean");map_bindings=d.method(controller_map,"get_AllMaps","System.Collections.Generic.IList<Rewired.ActionElementMap>");
        action_id=d.method(binding,"get_actionId","System.Int32");element_id=d.method(binding,"get_elementIdentifierId","System.Int32");element_type=d.method(binding,"get_elementType","Rewired.ControllerElementType");axis_range=d.method(binding,"get_axisRange","Rewired.AxisRange");inverted=d.method(binding,"get_invert","System.Boolean");
    }
    void helpers() {
        if(!d.boolean(ready))throw std::runtime_error("ReInput not ready");
        Runtime::Root players(d,d.invoke(players_get));d.require(players.get(),players_helper_class);
        Runtime::Root controls(d,d.invoke(controllers_get));d.require(controls.get(),controllers_helper_class);
        Runtime::Root maps(d,d.invoke(mapping_get));d.require(maps.get(),mapping_class);
        if(owned.cleanup_required&&(players.get()!=player_helper.get()||controls.get()!=controller_helper.get()||maps.get()!=mapping.get()))
            throw std::runtime_error("Input singleton identity changed; retain original ownership for cleanup");
        player_helper=std::move(players);controller_helper=std::move(controls);mapping=std::move(maps);
    }
    Runtime::Root get_player_ref(int id,bool optional=false) {
        void* args[]={&id};Runtime::Root r(d,d.invoke(get_player,player_helper.get(),args));
        if(r.get()||!optional)d.require(r.get(),player);return r;
    }
    Runtime::Root controllers_of(Il2CppObject* p) { return d.field(p,player,"controllers",player_controllers); }
    Runtime::Root maps_of(Il2CppObject* p) { auto c=controllers_of(p);return d.field(c.get(),player_controllers,"maps",player_maps); }
    std::vector<Runtime::Root> controllers_for(Il2CppObject* p) {auto c=controllers_of(p);return d.collect(d.invoke(all_controllers,c.get()),controller_class,16);}
    std::vector<Runtime::Root> maps_for(Il2CppObject* p,Il2CppObject* c) {auto maps=maps_of(p);void* args[]={c};return d.collect(d.invoke(get_maps,maps.get(),args),controller_map,32);}
    int enumeration(const MethodInfo* method,Il2CppObject* o,const char* type) {
        Runtime::Root box(d,d.invoke(method,o));if(!box.get()||d.a.object_get_class(box.get())!=d.rewired(type)||!d.a.object_unbox(box.get()))throw std::runtime_error("Wrong boxed control enum");
        std::int32_t value{};std::memcpy(&value,d.a.object_unbox(box.get()),4);return value;
    }
    RewiredMapState map_state(Il2CppObject* m) {
        RewiredMapState s;s.id=d.integer(map_id,m);s.controller_id=d.integer(map_controller_id,m);s.controller_type=enumeration(map_type,m,"ControllerType");s.category_id=d.integer(map_category,m);s.layout_id=d.integer(map_layout,m);s.enabled=d.boolean(map_enabled,m);
        auto list=d.collect(d.invoke(map_bindings,m),binding,64);
        for(auto& item:list){RewiredActionBinding b;b.action_id=d.integer(action_id,item.get());b.element_id=d.integer(element_id,item.get());b.element_type=enumeration(element_type,item.get(),"ControllerElementType");b.axis_range=enumeration(axis_range,item.get(),"AxisRange");b.inverted=d.boolean(inverted,item.get());s.bindings.push_back(b);}
        if(s.id<0||s.controller_id<0||s.category_id<0||s.layout_id<0)
            throw std::runtime_error("Negative map identifier: map="+std::to_string(s.id)+", controller="+std::to_string(s.controller_id)+", category="+std::to_string(s.category_id)+", layout="+std::to_string(s.layout_id));
        return s;
    }
    int action(const char* label) {Runtime::Root text(d,d.new_string(label));void* args[]={text.get()};auto value=d.scalar<std::int32_t>(d.invoke(get_action,mapping.get(),args),"Int32");if(value<0)throw std::runtime_error("Missing movement action");return value;}
    float axis(Il2CppObject* p,const char* label) {Runtime::Root text(d,d.new_string(label));void* args[]={text.get()};auto value=d.scalar<float>(d.invoke(player_axis,p,args),"Single");if(!std::isfinite(value))throw std::runtime_error("Nonfinite player axis");return value;}
    RewiredControlEnvironment environment() {
        helpers();RewiredControlEnvironment result;result.horizontal_action_id=action("Move Horizontal");result.vertical_action_id=action("Move Vertical");
        auto categories=d.collect(d.invoke(get_categories,mapping.get()),category,32);auto layouts=d.collect(d.invoke(get_layouts,mapping.get()),layout,32);
        std::set<int> ids;for(auto& row:categories){auto id=d.integer(category_id,row.get());if(id<0||!ids.insert(id).second)throw std::runtime_error("Invalid or duplicate category");result.categories.push_back({id,d.text(d.invoke(category_name,row.get()))});}
        ids.clear();for(auto& row:layouts){auto id=d.integer(layout_id,row.get());if(id<0||!ids.insert(id).second)throw std::runtime_error("Invalid or duplicate layout");result.layouts.push_back({id,d.text(d.invoke(layout_name,row.get()))});}
        for(int id=0;id<2;++id){auto p=get_player_ref(id);RewiredPlayerInputState s;s.player_id=id;s.horizontal=axis(p.get(),"Move Horizontal");s.vertical=axis(p.get(),"Move Vertical");auto controls=controllers_for(p.get());
            for(auto& c:controls){auto maps=maps_for(p.get(),c.get());for(auto& m:maps){Runtime::Root owner(d,d.invoke(map_controller,m.get()));if(owner.get()!=c.get())throw std::runtime_error("Map/controller reference mismatch");s.maps.push_back(map_state(m.get()));if(s.maps.size()>32)throw std::runtime_error("Player map count exceeds 32");}}
            result.players.push_back(std::move(s));}
        return result;
    }
    std::string fingerprint_primary(bool remember=false) {
        auto p=get_player_ref(0);auto controls=controllers_for(p.get());std::ostringstream out;
        if(remember)primary_roots.clear();
        for(auto& c:controls){if(remember)primary_roots.emplace_back(d,c.get());out<<reinterpret_cast<std::uintptr_t>(c.get())<<':'<<d.field_integer(controller_id_field,c.get())<<':'<<enumeration(get_type,c.get(),"ControllerType")<<';';auto maps=maps_for(p.get(),c.get());for(auto& m:maps){if(remember)primary_roots.emplace_back(d,m.get());auto s=map_state(m.get());out<<reinterpret_cast<std::uintptr_t>(m.get())<<':'<<s.id<<':'<<s.category_id<<':'<<s.layout_id<<':'<<s.enabled;for(auto& b:s.bindings)out<<'/'<<b.action_id<<':'<<b.element_id<<':'<<b.element_type<<':'<<b.axis_range<<':'<<b.inverted;}}
        return out.str();
    }
    void actor_identity(bool remember=false) {
        PlayerStateReader reader;std::string error;SceneStateSnapshot scene;
        if(!reader.initialize(d.module,d.a,d.domain,os,managed,error)||reader.read(scene,error)!=PlayerReadStatus::Captured||scene.players.size()!=2||scene.players[0].input_id!=0||scene.players[1].input_id!=1||scene.players[0].dead||scene.players[1].dead)throw std::runtime_error("Control requires two living run actors: "+error);
        auto manager=d.klass("VampireSurvivors.Runtime.dll","VampireSurvivors.Framework","GameManager");auto character=d.klass("VampireSurvivors.Runtime.dll","VampireSurvivors.Objects.Characters","CharacterController");auto unity=d.klass("UnityEngine.CoreModule.dll","UnityEngine","Object");
        auto find=d.method(unity,"FindObjectsOfType","UnityEngine.Object[]",{"System.Type"},true);Runtime::Root type(d,d.a.type_get_object(d.a.class_get_type(manager)));void* args[]={type.get()};auto managers=d.collect(d.invoke(find,nullptr,args),manager,2);if(managers.size()!=1)throw std::runtime_error("Ambiguous actual actor manager");
        auto get=d.method(manager,"get_MainPlayers","System.Collections.Generic.List<VampireSurvivors.Objects.Characters.CharacterController>");auto actors=d.collect(d.invoke(get,managers[0].get()),character,4);auto input=d.method(character,"get_PlayerInput","Rewired.Player");
        if(actors.size()!=2)throw std::runtime_error("Actual actor count changed");
        if(remember){actor_roots.clear();actor_inputs.clear();}
        for(int i=0;i<2;++i){Runtime::Root actual(d,d.invoke(input,actors[i].get()));auto expected=get_player_ref(i);if(actual.get()!=expected.get())throw std::runtime_error("Actor/ReInput player identity mismatch");
            if(remember){actor_roots.emplace_back(d,actors[i].get());actor_inputs.emplace_back(d,actual.get());}
            else if(actor_roots.size()!=2||actor_inputs.size()!=2||actors[i].get()!=actor_roots[i].get()||actual.get()!=actor_inputs[i].get())throw std::runtime_error("Run actor/input identity changed during owned control");}
    }
    void write(float x,float y) {
        auto hx=options.horizontal_axis_id,vy=options.vertical_axis_id;void* h[]={&hx,&x};void* v[]={&vy,&y};d.invoke(set_axis,controller.get(),h);d.invoke(set_axis,controller.get(),v);
        void* hi[]={&hx};void* vi[]={&vy};owned.horizontal=d.scalar<float>(d.invoke(read_axis,controller.get(),hi),"Single");owned.vertical=d.scalar<float>(d.invoke(read_axis,controller.get(),vi),"Single");
        if(!std::isfinite(owned.horizontal)||!std::isfinite(owned.vertical))throw std::runtime_error("Nonfinite owned axis readback");
    }
    bool close(std::string& error) {
        try {
            verify();if(!owned.cleanup_required){clear_unowned_roots();return true;}
            if(!controller.get())throw std::runtime_error("Owned controller root lost");
            // Removal of this map/controller is an independent neutralization path
            // if the value setter is failing or the run has already stopped.
            std::string zero_error;try{write(0,0);}catch(const std::exception& e){zero_error=e.what();}
            std::vector<Runtime::Root> targets;
            const auto remember=[&](Il2CppObject* p){
                if(p&&std::none_of(targets.begin(),targets.end(),[&](auto& r){return r.get()==p;}))targets.emplace_back(d,p);
            };
            // A replacement ReInput dictionary must not hide the old player
            // still referenced by an actor and by this owned session.
            remember(input_player.get());
            for(auto& p:actor_inputs)remember(p.get());
            for(int id=0;id<4;++id){auto p=get_player_ref(id,true);remember(p.get());}
            // RemoveMap accepts a numeric key, not a reference. Refuse any
            // foreign dependency before deletion, including key collisions.
            for(auto& p:targets){
                auto maps=maps_for(p.get(),controller.get());
                if(maps.size()>1)throw std::runtime_error("Ambiguous or foreign maps on owned controller; retain ownership");
                for(auto& m:maps){
                    if(!map.get()||m.get()!=map.get())throw std::runtime_error("Foreign map on owned controller; retain ownership");
                    auto s=map_state(m.get());Runtime::Root owner(d,d.invoke(map_controller,m.get()));
                    if(owner.get()!=controller.get()||s.controller_id!=owned.controller_id||s.controller_type!=20)
                        throw std::runtime_error("Owned map key/reference changed");
                }
            }
            for(auto& p:targets){
                auto maps=maps_for(p.get(),controller.get());
                if(!maps.empty()){
                    auto s=map_state(maps[0].get());auto helper=maps_of(p.get());int custom_type=20;
                    void* args[]={&custom_type,&s.controller_id,&s.id};d.invoke(remove_map,helper.get(),args);
                }
                if(!maps_for(p.get(),controller.get()).empty())throw std::runtime_error("Owned map removal not observed");
                auto controls=controllers_for(p.get());
                if(std::any_of(controls.begin(),controls.end(),[&](auto& c){return c.get()==controller.get();})){
                    auto helper=controllers_of(p.get());void* args[]={controller.get()};d.invoke(remove_controller,helper.get(),args);
                }
                auto after=controllers_for(p.get());
                if(std::any_of(after.begin(),after.end(),[&](auto& c){return c.get()==controller.get();}))throw std::runtime_error("Owned controller removal not observed");
                for(auto& other:controls)if(other.get()!=controller.get()&&std::none_of(after.begin(),after.end(),[&](auto& c){return c.get()==other.get();}))
                    throw std::runtime_error("Foreign controller changed during owned cleanup");
            }
            auto global=d.collect(d.invoke(get_customs,controller_helper.get()),custom,32);
            if(std::any_of(global.begin(),global.end(),[&](auto& c){return c.get()==controller.get();})){void* args[]={controller.get()};if(!d.boolean(destroy_controller,controller_helper.get(),args))throw std::runtime_error("Owned controller destruction refused");}
            global=d.collect(d.invoke(get_customs,controller_helper.get()),custom,32);if(std::any_of(global.begin(),global.end(),[&](auto& c){return c.get()==controller.get();}))throw std::runtime_error("Owned controller still registered after destruction");
            controller.reset();map.reset();owned={};clear_unowned_roots();error.clear();return true;
        }catch(const std::exception& e){error=e.what();return false;}
    }
};
RewiredControllerSession::RewiredControllerSession()=default;
RewiredControllerSession::~RewiredControllerSession(){if(impl_&&impl_->owned.cleanup_required)impl_.release();}
bool RewiredControllerSession::initialize(HMODULE module,Il2CppApi api,Il2CppDomain* domain,DWORD os,std::int32_t managed,std::string& error){
    error.clear();if(impl_&&impl_->owned.cleanup_required){error="Cannot replace a live owned controller session";return false;}
    try{if(!module||!domain||!os||!api.thread_current||!api.thread_current()||!api.runtime_invoke||!api.object_get_class||!api.object_unbox||!api.domain_get_assemblies||!api.class_get_methods)throw std::runtime_error("Incomplete attached runtime API");auto p=std::make_unique<Impl>();p->d.module=module;p->d.a=api;p->d.domain=domain;p->os=os;p->managed=managed;p->bind();impl_=std::move(p);return true;}catch(const std::exception& e){error=e.what();return false;}}
PlayerReadStatus RewiredControllerSession::inspect(RewiredControlEnvironment& output,std::string& error){error.clear();bool verified=false;try{if(!impl_)throw std::runtime_error("Controller session not initialized");impl_->verify();verified=true;if(!impl_->d.boolean(impl_->ready)){impl_->clear_unowned_roots();return PlayerReadStatus::NotReady;}auto result=impl_->environment();impl_->clear_unowned_roots();output=std::move(result);return PlayerReadStatus::Captured;}catch(const std::exception& e){if(verified)impl_->clear_unowned_roots();error=e.what();return PlayerReadStatus::Error;}}
bool RewiredControllerSession::open(const RewiredControlOptions& options,std::string& error){
    error.clear();if(!impl_){error="Controller session not initialized";return false;}auto& p=*impl_;
    bool created_here=false,verified=false;
    try {
        p.verify();verified=true;if(p.owned.cleanup_required)throw std::runtime_error("Owned controller already active");
        if(options.player_id!=1||options.source_id<0||options.category_id<0||options.layout_id<0||options.horizontal_axis_id<0||options.vertical_axis_id<0||options.horizontal_axis_id==options.vertical_axis_id)throw std::runtime_error("Invalid second-player control options");
        auto env=p.environment();p.actor_identity(true);RewiredInputProfileSnapshot profile;
        if(read_rewired_input_profile(p.d.module,p.d.a,p.d.domain,p.os,p.managed,profile,error)!=PlayerReadStatus::Captured)throw std::runtime_error("Source definition query: "+error);
        auto definition=std::find_if(profile.definitions.begin(),profile.definitions.end(),[&](const auto& d){return d.source_id==options.source_id;});
        if(definition==profile.definitions.end()||std::find(definition->axis_ids.begin(),definition->axis_ids.end(),options.horizontal_axis_id)==definition->axis_ids.end()||std::find(definition->axis_ids.begin(),definition->axis_ids.end(),options.vertical_axis_id)==definition->axis_ids.end())throw std::runtime_error("Unknown source definition or axes");
        if(std::none_of(env.categories.begin(),env.categories.end(),[&](auto& c){return c.id==options.category_id;})||std::none_of(env.layouts.begin(),env.layouts.end(),[&](auto& l){return l.id==options.layout_id;}))throw std::runtime_error("Unknown live category or layout");
        bool h=false,v=false;for(auto& m:env.players[0].maps)if(m.enabled&&m.category_id==options.category_id)for(auto& b:m.bindings){h|=b.action_id==env.horizontal_action_id;v|=b.action_id==env.vertical_action_id;}if(!h||!v)throw std::runtime_error("Category is not an enabled existing movement category");
        p.options=options;p.primary_fingerprint=p.fingerprint_primary(true);auto before=p.d.collect(p.d.invoke(p.get_customs,p.controller_helper.get()),p.custom,32);
        Runtime::Root tag(p.d,p.d.new_string("WSM.OwnedRemote.v1"));auto source=options.source_id;void* create_args[]={&source,tag.get()};Runtime::Root fresh(p.d,p.d.invoke(p.create_controller,p.controller_helper.get(),create_args));p.d.require(fresh.get(),p.custom);
        if(std::any_of(before.begin(),before.end(),[&](auto& c){return c.get()==fresh.get();}))throw std::runtime_error("Factory returned a preexisting controller");
        p.controller=std::move(fresh);created_here=true;p.owned.cleanup_required=p.owned.controller_created=true;p.owned.player_id=1;p.owned.controller_id=p.d.field_integer(p.controller_id_field,p.controller.get());
        if(p.owned.controller_id<0||p.d.integer(p.get_source,p.controller.get())!=source)throw std::runtime_error("Created controller identity/source mismatch");
        if(std::any_of(before.begin(),before.end(),[&](auto& c){return p.d.field_integer(p.controller_id_field,c.get())==p.owned.controller_id;}))throw std::runtime_error("Factory reused a registered custom controller ID");
        auto identifiers=p.d.collect(p.d.invoke(p.axis_ids,p.controller.get()),p.identifier,32);std::set<int> axes;for(auto& i:identifiers){auto id=p.d.integer(p.identifier_id,i.get());if(id<0||!axes.insert(id).second)throw std::runtime_error("Invalid actual axis identifiers");}if(!axes.count(options.horizontal_axis_id)||!axes.count(options.vertical_axis_id))throw std::runtime_error("Created controller lacks requested axes");
        for(int id=0;id<4;++id){void* args[]={&p.owned.controller_id,&id};if(p.d.boolean(p.assigned,p.controller_helper.get(),args))throw std::runtime_error("New controller was unexpectedly assigned");}
        p.input_player=p.get_player_ref(1);auto existing=p.maps_for(p.input_player.get(),p.controller.get());if(!existing.empty())throw std::runtime_error("New controller has unexpected existing maps");
        p.write(0,0);if(p.owned.horizontal!=0||p.owned.vertical!=0)throw std::runtime_error("New controller is not neutral");std::uint8_t remove_from_other_players=0;auto helper=p.controllers_of(p.input_player.get());void* add_args[]={p.controller.get(),&remove_from_other_players};p.d.invoke(p.add_controller,helper.get(),add_args);
        p.owned.controller_attached=true;auto maps=p.maps_of(p.input_player.get());
        auto cat=options.category_id,layout=options.layout_id;int custom_type=20;
        void* register_args[]={&custom_type,&p.owned.controller_id,&cat,&layout};
        // AddEmptyMap registers directly; a detached Create() template has no
        // controller ID yet. Never weaken the registered-map validation for it.
        std::exception_ptr registration_failure;
        try{p.d.invoke(p.add_empty_map,maps.get(),register_args);}catch(...){registration_failure=std::current_exception();}
        auto registered=p.maps_for(p.input_player.get(),p.controller.get());
        if(registered.size()==1){p.d.require(registered[0].get(),p.custom_map);p.map=Runtime::Root(p.d,registered[0].get());}
        // Retain the operation-produced reference for rollback even if the API
        // threw after adding it. Unexpected multiple references remain refused.
        if(registration_failure)std::rethrow_exception(registration_failure);
        if(registered.size()!=1||!p.map.get())throw std::runtime_error("Owned empty-map registration is not unique");
        Runtime::Root map_owner(p.d,p.d.invoke(p.map_controller,p.map.get()));auto ms=p.map_state(p.map.get());
        if(map_owner.get()!=p.controller.get()||ms.controller_id!=p.owned.controller_id||ms.controller_type!=20||ms.category_id!=cat||ms.layout_id!=layout||!ms.bindings.empty())throw std::runtime_error("Registered map is not an empty owned custom map");
        p.owned.map_id=ms.id;
        for(int axis=0;axis<2;++axis){auto element=axis?options.vertical_axis_id:options.horizontal_axis_id;auto action=axis?env.vertical_action_id:env.horizontal_action_id;std::uint8_t invert=0;void* args[]={&element,&action,&invert};Runtime::Root value(p.d,p.d.invoke(p.create_assignment,nullptr,args));if(!value.get()||p.d.a.object_get_class(value.get())!=p.assignment||!p.d.a.object_unbox(value.get()))throw std::runtime_error("Invalid boxed full-axis assignment");void* binding_args[]={p.d.a.object_unbox(value.get())};if(!p.d.boolean(p.create_element,p.map.get(),binding_args))throw std::runtime_error("Full-axis map creation refused");}
        std::uint8_t enabled=1;void* enable_args[]={&enabled};p.d.invoke(p.enable_map,p.map.get(),enable_args);
        ms=p.map_state(p.map.get());
        if(!ms.enabled||ms.bindings.size()!=2)throw std::runtime_error("Owned map is disabled or has wrong bindings");
        for(int axis=0;axis<2;++axis){auto action=axis?env.vertical_action_id:env.horizontal_action_id;auto element=axis?options.vertical_axis_id:options.horizontal_axis_id;if(std::count_if(ms.bindings.begin(),ms.bindings.end(),[&](auto& b){return b.action_id==action&&b.element_id==element&&b.element_type==0&&b.axis_range==0&&!b.inverted;})!=1)throw std::runtime_error("Registered action/axis binding mismatch");}
        void* assigned_args[]={&p.owned.controller_id,&p.owned.player_id};if(!p.d.boolean(p.assigned,p.controller_helper.get(),assigned_args)||p.fingerprint_primary()!=p.primary_fingerprint)throw std::runtime_error("Target assignment or primary isolation failed");p.owned.map_registered=true;return true;
    }catch(const std::exception& e){error=e.what();if(created_here&&p.owned.cleanup_required){std::string cleanup;if(!p.close(cleanup))error+="; retained cleanup failure: "+cleanup;}if(verified)p.clear_unowned_roots();return false;}}
bool RewiredControllerSession::set_axes(float x,float y,std::string& error){error.clear();if(!impl_){error="Controller session not initialized";return false;}auto& p=*impl_;try{p.verify();if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1||std::abs(y)>1)throw std::runtime_error("Invalid control vector");if(!p.owned.map_registered||!p.owned.controller_attached)throw std::runtime_error("No registered owned control");if(x!=0||y!=0){p.actor_identity();if(p.fingerprint_primary()!=p.primary_fingerprint)throw std::runtime_error("Primary resources changed during owned control");auto maps=p.maps_for(p.input_player.get(),p.controller.get());if(maps.size()!=1||maps[0].get()!=p.map.get()||!p.d.boolean(p.map_enabled,p.map.get()))throw std::runtime_error("Owned map no longer enabled/registered");}p.write(x,y);if(x==0&&y==0&&(p.owned.horizontal!=0||p.owned.vertical!=0))throw std::runtime_error("Owned neutral readback not yet zero");return true;}catch(const std::exception& e){error=e.what();return false;}}
bool RewiredControllerSession::close(std::string& error){error.clear();return !impl_||impl_->close(error);}
RewiredOwnedControlState RewiredControllerSession::state() const{return impl_?impl_->owned:RewiredOwnedControlState{};}
}
