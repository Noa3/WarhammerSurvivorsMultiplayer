#include "wsm/rewired_input_profile.h"
#include "wsm/main_thread_guard.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace wsm {
namespace {
bool eq(const char* a, const char* b) { return a && b && std::strcmp(a,b)==0; }
struct Discovery {
    Il2CppApi a; Il2CppDomain* domain; std::string& error;
    Il2CppClass* (*parent)(Il2CppClass*) = nullptr;
    Il2CppClass* (*from_type)(const Il2CppType*) = nullptr;
    const Il2CppImage* (*image)(Il2CppClass*) = nullptr;
    Il2CppClass* (*element)(Il2CppClass*) = nullptr;
    Il2CppClass* (*declaring)(Il2CppClass*) = nullptr;
    Il2CppClass* (*method_class)(const MethodInfo*) = nullptr;
    bool (*assignable)(Il2CppClass*,Il2CppClass*) = nullptr;
    void* (*handle_new)(Il2CppObject*,bool) = nullptr;
    Il2CppObject* (*handle_target)(void*) = nullptr;
    void (*handle_free)(void*) = nullptr;
    Il2CppObject* (*string_new)(const char*) = nullptr;
    struct Root {
        Discovery& d; void* h;
        Root(Discovery& d_,Il2CppObject* o):d(d_),h(o?d.handle_new(o,true):nullptr) {}
        ~Root(){if(h)d.handle_free(h);}
        Root(const Root&)=delete; Root& operator=(const Root&)=delete;
        Il2CppObject* get() const {return h?d.handle_target(h):nullptr;}
    };
    bool load(HMODULE module) {
#define LOAD(member, name) member=reinterpret_cast<decltype(member)>(GetProcAddress(module,name)); if(!member){error=std::string("Missing export ")+name;return false;}
        LOAD(parent,"il2cpp_class_get_parent"); LOAD(from_type,"il2cpp_class_from_type");
        LOAD(image,"il2cpp_class_get_image"); LOAD(element,"il2cpp_class_get_element_class");
        LOAD(declaring,"il2cpp_class_get_declaring_type"); LOAD(method_class,"il2cpp_method_get_class");
        LOAD(assignable,"il2cpp_class_is_assignable_from"); LOAD(handle_new,"il2cpp_gchandle_new");
        LOAD(handle_target,"il2cpp_gchandle_get_target"); LOAD(handle_free,"il2cpp_gchandle_free");
        LOAD(string_new,"il2cpp_string_new");
#undef LOAD
        return true;
    }
    bool in_image(Il2CppClass* c,const char* name) const {return c && eq(a.image_get_name(image(c)),name);}
    Il2CppClass* klass(const char* img,const char* ns,const char* name) const {
        std::size_t n=0; auto assemblies=a.domain_get_assemblies(domain,&n); if(!assemblies)return nullptr;
        Il2CppClass* found=nullptr;
        for(std::size_t i=0;i<n;++i){auto im=a.assembly_get_image(assemblies[i]);if(!im||!eq(a.image_get_name(im),img))continue;
            for(std::size_t j=0;j<a.image_get_class_count(im);++j){auto c=const_cast<Il2CppClass*>(a.image_get_class(im,j));
                if(c&&eq(a.class_get_namespace(c),ns)&&eq(a.class_get_name(c),name)){if(found)return nullptr;found=c;}}}
        return found && in_image(found,img)?found:nullptr;
    }
    bool type_name(const Il2CppType* t,const char* expected) const {
        if(!t)return false; auto s=a.type_get_name(t);bool ok=eq(s,expected);if(s)a.free(s);return ok;
    }
    bool is_a(Il2CppObject* o,Il2CppClass* expected) const {
        auto c=o?a.object_get_class(o):nullptr; for(int i=0;c&&i<128;++i,c=parent(c))if(c==expected)return true;return false;
    }
    const MethodInfo* method(Il2CppClass* owner,const char* name,Il2CppClass* ret,const char* ret_name,
                             Il2CppClass* arg=nullptr,const char* arg_name=nullptr,bool stat=false) const {
        if(!owner||!ret)return nullptr;const MethodInfo* found=nullptr;void* it=nullptr;
        while(auto m=a.class_get_methods(owner,&it)) {
            if(!eq(a.method_get_name(m),name)||method_class(m)!=owner||a.method_get_param_count(m)!=(arg?1u:0u))continue;
            std::uint32_t impl=0;auto flags=a.method_get_flags(m,&impl);
            if((flags&7u)!=6u||((flags&16u)!=0)!=stat)continue;
            auto rt=a.method_get_return_type(m);
            if(!rt||from_type(rt)!=ret||(ret_name&&!type_name(rt,ret_name)))continue;
            if(arg){auto at=a.method_get_param(m,0);if(!at||from_type(at)!=arg||!type_name(at,arg_name))continue;}
            if(found)return nullptr;found=m;
        } return found;
    }
    Il2CppObject* invoke(const MethodInfo* m,Il2CppObject* obj,void** args) {
        auto r=a.invoke(m,obj,args);if(r.exception){error=std::string("Managed exception in ")+a.method_get_name(m);return nullptr;}return r.value;
    }
    template<class T> bool boxed(Il2CppObject* raw,Il2CppClass* cls,T& out) {
        if(!error.empty())return false;Root r(*this,raw);auto o=r.get();
        if(!o||a.object_get_class(o)!=cls){error="Invalid or unrootable boxed value";return false;}
        auto p=a.object_unbox(o);if(!p){error="Boxed value could not be unboxed";return false;}
        std::memcpy(&out,p,sizeof(T));return true;
    }
};
}
PlayerReadStatus read_rewired_input_profile(HMODULE module,Il2CppApi api,Il2CppDomain* domain,DWORD native_thread,
    std::int32_t managed_thread,RewiredInputProfileSnapshot& output,std::string& error) {
    error.clear();const auto fail=[&](const char* s){error=s;return PlayerReadStatus::Error;};
    if(!module||!domain||!native_thread||!api.thread_current||!api.thread_current()||!api.domain_get_assemblies||
       !api.assembly_get_image||!api.image_get_name||!api.image_get_class_count||!api.image_get_class||
       !api.class_get_name||!api.class_get_namespace||!api.class_get_methods||!api.method_get_name||
       !api.method_get_param_count||!api.method_get_param||!api.method_get_return_type||!api.method_get_flags||
       !api.type_get_name||!api.free||!api.class_get_type||!api.type_get_object||!api.array_length||
       !api.runtime_invoke||!api.object_get_class||!api.object_unbox)
        return fail("Incomplete runtime API or thread not attached");
    MainThreadGuard guard;MainThreadIdentity identity;
    if(!MainThreadGuard::create(api,domain,guard,error)||!guard.verify(native_thread,managed_thread,identity,error)) {
        if(error.empty())error="Main-thread verification failed";return PlayerReadStatus::Error;
    }
    Discovery d{api,domain,error};if(!d.load(module))return PlayerReadStatus::Error;
    auto obj=d.klass("UnityEngine.CoreModule.dll","UnityEngine","Object");
    auto arr=d.klass("mscorlib.dll","System","Array");auto sysobj=d.klass("mscorlib.dll","System","Object");
    auto systype=d.klass("mscorlib.dll","System","Type");auto integer=d.klass("mscorlib.dll","System","Int32");
    auto boolean=d.klass("mscorlib.dll","System","Boolean");auto str=d.klass("mscorlib.dll","System","String");
    auto reinput=d.klass("Rewired_Core.dll","Rewired","ReInput");
    if(!reinput||!boolean)return fail("Missing canonical ReInput readiness metadata");
    auto ready=d.method(reinput,"get_isReady",boolean,"System.Boolean",nullptr,nullptr,true);
    if(!ready)return fail("Missing or ambiguous ReInput readiness getter");
    std::uint8_t is_ready=0;if(!d.boxed(d.invoke(ready,nullptr,nullptr),boolean,is_ready))return PlayerReadStatus::Error;
    if(is_ready>1)return fail("Invalid ReInput readiness Boolean");if(!is_ready)return PlayerReadStatus::NotReady;
    auto manager=d.klass("Rewired_Core.dll","Rewired","InputManager_Base");
    auto data=d.klass("Rewired_Core.dll","Rewired.Data","UserData");
    auto definition=d.klass("Rewired_Core.dll","Rewired.Data","CustomController_Editor");
    if(!obj||!arr||!sysobj||!systype||!integer||!boolean||!str||!reinput||!manager||!data||!definition)
        return fail("Missing or ambiguous canonical input metadata");
    // Resolve closed array classes from their declared method return types, not from guessed memory layout.
    const auto return_class=[&](Il2CppClass* owner,const char* name,const char* type)->Il2CppClass* {
        Il2CppClass* found=nullptr;void* it=nullptr;
        while(auto m=api.class_get_methods(owner,&it)) {
            if(!eq(api.method_get_name(m),name)||d.method_class(m)!=owner||api.method_get_param_count(m)!=0||
               !d.type_name(api.method_get_return_type(m),type))continue;
            std::uint32_t impl=0;auto f=api.method_get_flags(m,&impl);if((f&7u)!=6u||(f&16u))continue;
            auto c=d.from_type(api.method_get_return_type(m));if(!c||found)return nullptr;found=c;
        } return found;
    };
    auto ints=return_class(data,"GetCustomControllerIds","System.Int32[]");
    if(!ints||!d.in_image(ints,"mscorlib.dll")||d.element(ints)!=integer||
       !d.type_name(api.class_get_type(ints),"System.Int32[]"))return fail("Invalid canonical Int32 array type");
    // Nested types can have an empty metadata namespace; the canonical declaring ReInput pins ownership.
    const MethodInfo* mapping_get=nullptr;Il2CppClass* mapping=nullptr;void* it=nullptr;
    while(auto m=api.class_get_methods(reinput,&it)) {
        if(!eq(api.method_get_name(m),"get_mapping")||d.method_class(m)!=reinput||api.method_get_param_count(m)!=0)continue;
        std::uint32_t impl=0;auto flags=api.method_get_flags(m,&impl);if((flags&7u)!=6u||!(flags&16u))continue;
        auto t=api.method_get_return_type(m);auto c=t?d.from_type(t):nullptr;
        if(!c||d.declaring(c)!=reinput||!d.in_image(c,"Rewired_Core.dll")||
           !eq(api.class_get_name(c),"MappingHelper"))continue;
        if(mapping_get)return fail("Ambiguous mapping getter");mapping_get=m;mapping=c;
    }
    auto get_data=d.method(manager,"get_userData",data,"Rewired.Data.UserData");
    auto ids=d.method(data,"GetCustomControllerIds",ints,"System.Int32[]");
    auto by_id=d.method(data,"GetCustomControllerById",definition,"Rewired.Data.CustomController_Editor",integer,"System.Int32");
    auto axes=d.method(definition,"GetAxisElementIdentifierIds",ints,"System.Int32[]");
    auto action=d.method(mapping,"GetActionId",integer,"System.Int32",str,"System.String");
    auto item=d.method(arr,"GetValue",sysobj,"System.Object",integer,"System.Int32");
    // FindObjectsOfType's declared UnityEngine.Object[] type can differ from its runtime array type.
    const MethodInfo* find=nullptr;Il2CppClass* objects=nullptr;it=nullptr;
    while(auto m=api.class_get_methods(obj,&it)) {
        if(!eq(api.method_get_name(m),"FindObjectsOfType")||d.method_class(m)!=obj||api.method_get_param_count(m)!=1)continue;
        std::uint32_t impl=0;auto f=api.method_get_flags(m,&impl);auto rt=api.method_get_return_type(m);auto at=api.method_get_param(m,0);
        if((f&7u)!=6u||!(f&16u)||!d.type_name(rt,"UnityEngine.Object[]")||!d.type_name(at,"System.Type")||d.from_type(at)!=systype)continue;
        auto c=d.from_type(rt);if(!c||d.element(c)!=obj||!d.in_image(c,"UnityEngine.CoreModule.dll"))continue;
        if(find)return fail("Ambiguous Unity object lookup");find=m;objects=c;
    }
    if(!get_data)return fail("Missing or ambiguous InputManager_Base.get_userData metadata");
    if(!ids)return fail("Missing or ambiguous UserData.GetCustomControllerIds metadata");
    if(!by_id)return fail("Missing or ambiguous UserData.GetCustomControllerById metadata");
    if(!axes)return fail("Missing or ambiguous CustomController_Editor.GetAxisElementIdentifierIds metadata");
    if(!mapping_get)return fail("Missing or ambiguous ReInput.get_mapping / nested MappingHelper metadata");
    if(!action)return fail("Missing or ambiguous MappingHelper.GetActionId metadata");
    if(!item)return fail("Missing or ambiguous System.Array.GetValue(Int32) metadata");
    if(!find||!objects)return fail("Missing or ambiguous UnityEngine.Object.FindObjectsOfType(Type) metadata");
    Discovery::Root type(d,api.type_get_object(api.class_get_type(manager)));
    if(!d.is_a(type.get(),systype))return fail("Unable to root manager reflection type");
    void* lookup_args[]={type.get()};Discovery::Root managers(d,d.invoke(find,nullptr,lookup_args));
    if(!error.empty())return PlayerReadStatus::Error;if(!managers.get())return fail("Null or unrootable manager array");
    auto ac=api.object_get_class(managers.get());auto elem=ac?d.element(ac):nullptr;
    if(!elem||(!d.assignable(manager,elem)&&!d.assignable(elem,manager)))return fail("Unexpected manager lookup array type");
    auto count=api.array_length(reinterpret_cast<Il2CppArray*>(managers.get()));
    if(count==0)return PlayerReadStatus::NotReady;if(count!=1)return fail("Ambiguous active input managers");
    std::int32_t zero=0;void* index[]={&zero};Discovery::Root mgr(d,d.invoke(item,managers.get(),index));
    if(!error.empty())return PlayerReadStatus::Error;if(!d.is_a(mgr.get(),manager))return fail("Invalid input manager reference");
    auto raw_data=d.invoke(get_data,mgr.get(),nullptr);if(!error.empty())return PlayerReadStatus::Error;
    // A null UserData can occur before manager initialization; it is not an empty profile.
    if(!raw_data)return PlayerReadStatus::NotReady;
    Discovery::Root user(d,raw_data);if(!d.is_a(user.get(),data))return fail("Invalid or unrootable UserData");
    Discovery::Root helper(d,d.invoke(mapping_get,nullptr,nullptr));if(!error.empty())return PlayerReadStatus::Error;
    if(!d.is_a(helper.get(),mapping))return fail("Invalid or unrootable mapping helper");
    RewiredInputProfileSnapshot result;result.manager_count=1;
    const auto get_action=[&](const char* text,std::int32_t& value)->bool {
        Discovery::Root name(d,d.string_new(text));if(!name.get()||api.object_get_class(name.get())!=str){error="Unable to root canonical action name";return false;}
        void* args[]={name.get()};if(!d.boxed(d.invoke(action,helper.get(),args),integer,value))return false;
        if(value < -1){error="Invalid action identifier";return false;}return true;
    };
    if(!get_action("Move Horizontal",result.horizontal_action_id)||!get_action("Move Vertical",result.vertical_action_id))return PlayerReadStatus::Error;
    Discovery::Root id_array(d,d.invoke(ids,user.get(),nullptr));if(!error.empty())return PlayerReadStatus::Error;
    if(!id_array.get()||api.object_get_class(id_array.get())!=ints)return fail("Invalid custom controller ID array");
    auto size=api.array_length(reinterpret_cast<Il2CppArray*>(id_array.get()));if(size>16)return fail("Custom controller count exceeds 16");
    for(std::uintptr_t i=0;i<size;++i) {
        if(api.object_get_class(id_array.get())!=ints||api.array_length(reinterpret_cast<Il2CppArray*>(id_array.get()))!=size)return fail("Controller ID array changed during observation");
        std::int32_t ix=static_cast<std::int32_t>(i),id=0;void* ia[]={&ix};
        if(!d.boxed(d.invoke(item,id_array.get(),ia),integer,id))return PlayerReadStatus::Error;
        if(id<0||std::any_of(result.definitions.begin(),result.definitions.end(),[&](const RewiredControllerDefinitionSnapshot& x){return x.source_id==id;}))return fail("Negative or duplicate custom controller ID");
        void* arg[]={&id};Discovery::Root def(d,d.invoke(by_id,user.get(),arg));if(!error.empty())return PlayerReadStatus::Error;
        if(!d.is_a(def.get(),definition))return fail("Invalid or null custom controller definition");
        Discovery::Root axis_array(d,d.invoke(axes,def.get(),nullptr));if(!error.empty())return PlayerReadStatus::Error;
        if(!axis_array.get()||api.object_get_class(axis_array.get())!=ints)return fail("Invalid axis identifier array");
        auto n=api.array_length(reinterpret_cast<Il2CppArray*>(axis_array.get()));if(n>32)return fail("Axis identifier count exceeds 32");
        RewiredControllerDefinitionSnapshot row;row.source_id=id;
        for(std::uintptr_t j=0;j<n;++j) {
            if(api.object_get_class(axis_array.get())!=ints||api.array_length(reinterpret_cast<Il2CppArray*>(axis_array.get()))!=n)return fail("Axis identifier array changed during observation");
            std::int32_t pos=static_cast<std::int32_t>(j),axis=0;void* a[]={&pos};
            if(!d.boxed(d.invoke(item,axis_array.get(),a),integer,axis))return PlayerReadStatus::Error;
            if(axis<0||std::find(row.axis_ids.begin(),row.axis_ids.end(),axis)!=row.axis_ids.end())return fail("Negative or duplicate axis identifier");
            row.axis_ids.push_back(axis);
        }
        result.definitions.push_back(std::move(row));
    }
    output=std::move(result);return PlayerReadStatus::Captured;
}
}
