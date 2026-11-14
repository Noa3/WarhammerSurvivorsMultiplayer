#include "wsm/player_state_reader.h"
#include "wsm/main_thread_guard.h"
#include <cmath>
#include <cstring>
#include <utility>

namespace {
bool eq(const char* a,const char* b) { return a && b && std::strcmp(a,b)==0; }
constexpr const char* kCharacter="VampireSurvivors.Objects.Characters.CharacterController";
constexpr const char* kList="System.Collections.Generic.List<VampireSurvivors.Objects.Characters.CharacterController>";
constexpr const char* kObjects="UnityEngine.Object[]";
constexpr std::int32_t kMaximumPlayers=4;
}
namespace wsm {
struct PlayerStateReader::Impl {
    Il2CppApi a; MainThreadGuard guard; DWORD os=0; std::int32_t managed=0;
    Il2CppClass *manager=nullptr,*character=nullptr,*sprite=nullptr,*stage=nullptr,*input=nullptr,*float2=nullptr,*character_enum=nullptr,*stage_enum=nullptr;
    Il2CppClass *intclass=nullptr,*singleclass=nullptr,*boolclass=nullptr;
    Il2CppDomain* domain=nullptr;
    const MethodInfo *find=nullptr,*array_item=nullptr,*players=nullptr,*stage_get=nullptr,*hp=nullptr,*dead=nullptr,*xp=nullptr,*level=nullptr,*kind=nullptr,*player_input=nullptr,*position=nullptr,*input_id=nullptr,*stage_type=nullptr,*running=nullptr;
    FieldInfo *fx=nullptr,*fy=nullptr;
    Il2CppClass* (*parent)(Il2CppClass*)=nullptr;
    const Il2CppType* (*enum_base)(Il2CppClass*)=nullptr;
    FieldInfo* (*field_by_name)(Il2CppClass*,const char*)=nullptr;
    int (*field_flags)(FieldInfo*)=nullptr;
    const Il2CppType* (*field_type)(FieldInfo*)=nullptr;
    Il2CppClass* (*field_parent)(FieldInfo*)=nullptr;
    void (*field_value)(Il2CppObject*,FieldInfo*,void*)=nullptr;
    Il2CppClass* (*class_from_type)(const Il2CppType*)=nullptr;
    const Il2CppImage* (*class_image)(Il2CppClass*)=nullptr;
    Il2CppClass* (*element_class)(Il2CppClass*)=nullptr;
    bool (*assignable)(Il2CppClass*,Il2CppClass*)=nullptr;
    void* (*handle_new)(Il2CppObject*,bool)=nullptr;
    Il2CppObject* (*handle_target)(void*)=nullptr;
    void (*handle_free)(void*)=nullptr;
#ifdef WSM_PLAYER_STATE_TESTING
    mutable Il2CppClass* fail_root_class=nullptr;
#endif
    struct Root {
        const Impl& p; void* h;
        Root(const Impl& p_, Il2CppObject* o):p(p_),h(nullptr) {
#ifdef WSM_PLAYER_STATE_TESTING
            if(o && p.fail_root_class && p.a.object_get_class(o)==p.fail_root_class){p.fail_root_class=nullptr;return;}
#endif
            h=o?p.handle_new(o,true):nullptr;
        }
        ~Root(){if(h)p.handle_free(h);}
        Root(const Root&)=delete; Root& operator=(const Root&)=delete;
        Il2CppObject* get() const {return h?p.handle_target(h):nullptr;}
    };
    bool type(const Il2CppType* t,const char* name) const {
        if(!t)return false; char* n=a.type_get_name(t); bool ok=eq(n,name); if(n)a.free(n); return ok;
    }
    bool image_for_class(Il2CppClass* c,const char* image) const {
        if(!c||!class_image||!a.image_get_name)return false;return eq(a.image_get_name(class_image(c)),image);
    }
    std::string name_of(Il2CppClass* c) const { auto n=c?a.class_get_name(c):nullptr; return n?std::string(n):std::string("?"); }
    bool result_image(const MethodInfo* method,const char* type_name,const char* image,const char* ns,const char* cls) const {
        if(!method||!class_from_type||!class_image||!a.image_get_name)return false;
        auto t=a.method_get_return_type(method);auto c=t?class_from_type(t):nullptr;
        return type(t,type_name)&&c&&eq(a.class_get_name(c),cls)&&eq(a.class_get_namespace(c),ns)&&image_for_class(c,image);
    }
    Il2CppClass* klass(Il2CppDomain* d,const char* image,const char* ns,const char* name) const {
        std::size_t n=0; auto as=a.domain_get_assemblies(d,&n); if(!as)return nullptr; Il2CppClass* result=nullptr;
        for(std::size_t i=0;i<n;++i){auto im=a.assembly_get_image(as[i]); if(!im||!eq(a.image_get_name(im),image))continue;
            for(std::size_t j=0;j<a.image_get_class_count(im);++j){auto c=const_cast<Il2CppClass*>(a.image_get_class(im,j));
                if(c&&eq(a.class_get_namespace(c),ns)&&eq(a.class_get_name(c),name)){if(result)return nullptr;result=c;}}
        } return result;
    }
    const MethodInfo* method(Il2CppClass* c,const char* name,const char* ret,const char* arg=nullptr,bool stat=false) const {
        if(!c)return nullptr; const MethodInfo* found=nullptr; void* it=nullptr;
        while(auto m=a.class_get_methods(c,&it)){if(!eq(a.method_get_name(m),name)||a.method_get_param_count(m)!=(arg?1u:0u)||!type(a.method_get_return_type(m),ret))continue;
            std::uint32_t impl=0; auto flags=a.method_get_flags(m,&impl); if((flags&7u)!=6u||((flags&16u)!=0)!=stat||(arg&&!type(a.method_get_param(m,0),arg)))continue;
            if(found)return nullptr;found=m;
        }return found;
    }
    bool is_a(Il2CppObject* object,Il2CppClass* expected) const {
        if(!object||!expected)return false; auto c=a.object_get_class(object);
        for(int depth=0;c&&depth<128;++depth,c=parent(c))if(c==expected)return true;
        return false;
    }
    Il2CppObject* invoke(const MethodInfo* m,Il2CppObject* o,void** args,std::string& error) const {
        if(!m){error="Missing method";return nullptr;} auto r=a.invoke(m,o,args);
        if(r.exception){error="Managed exception invoking getter";return nullptr;} return r.value;
    }
    template<class T> bool value(Il2CppObject* obj,Il2CppClass* exact,T& out,std::string& error) const {
        Root root(*this,obj); auto o=root.get(); if(!o||a.object_get_class(o)!=exact){error="Unexpected boxed result type";return false;}
        auto p=a.object_unbox(o);if(!p){error="Unbox failed";return false;}std::memcpy(&out,p,sizeof(T));return true;
    }
    bool integer32(Il2CppObject* obj,std::int32_t& out,std::string& error) const {
        return value(obj,intclass,out,error);
    }
};
PlayerStateReader::PlayerStateReader()=default;
PlayerStateReader::~PlayerStateReader()=default;
#ifdef WSM_PLAYER_STATE_TESTING
void PlayerStateReader::test_fail_next_scene_root(bool players) {
    if(impl_)impl_->fail_root_class=players?impl_->class_from_type(impl_->a.method_get_return_type(impl_->players)):impl_->stage;
}
#endif
bool PlayerStateReader::initialize(HMODULE module,Il2CppApi api,Il2CppDomain* domain,DWORD native_thread,std::int32_t managed_thread,std::string& error){
    impl_.reset(); error.clear();
    if(!module||!domain||!native_thread||!api.thread_current||!api.thread_current()||!api.domain_get_assemblies||!api.assembly_get_image||!api.image_get_name||!api.image_get_class_count||!api.image_get_class||!api.class_get_name||!api.class_get_namespace||!api.class_get_methods||!api.method_get_name||!api.method_get_param_count||!api.method_get_param||!api.method_get_return_type||!api.method_get_flags||!api.type_get_name||!api.free||!api.class_get_type||!api.type_get_object||!api.array_length||!api.runtime_invoke||!api.object_get_class||!api.object_unbox){error="Incomplete runtime API or thread not attached";return false;}
    auto p=std::make_unique<Impl>(); p->a=api;p->os=native_thread;p->managed=managed_thread;
#define LOAD(member,name) p->member=reinterpret_cast<decltype(p->member)>(GetProcAddress(module,name)); if(!p->member){error=std::string("Missing export ")+name;return false;}
    LOAD(parent,"il2cpp_class_get_parent"); LOAD(enum_base,"il2cpp_class_enum_basetype");
    LOAD(field_by_name,"il2cpp_class_get_field_from_name");LOAD(field_flags,"il2cpp_field_get_flags");LOAD(field_type,"il2cpp_field_get_type");LOAD(field_parent,"il2cpp_field_get_parent");LOAD(field_value,"il2cpp_field_get_value");
    LOAD(class_from_type,"il2cpp_class_from_type");LOAD(class_image,"il2cpp_class_get_image");
    LOAD(element_class,"il2cpp_class_get_element_class");LOAD(assignable,"il2cpp_class_is_assignable_from");
    LOAD(handle_new,"il2cpp_gchandle_new");LOAD(handle_target,"il2cpp_gchandle_get_target");LOAD(handle_free,"il2cpp_gchandle_free");
#undef LOAD
    if(!MainThreadGuard::create(api,domain,p->guard,error))return false;
    auto core=p->klass(domain,"UnityEngine.CoreModule.dll","UnityEngine","Object");
    auto array=p->klass(domain,"mscorlib.dll","System","Array");
    p->manager=p->klass(domain,"VampireSurvivors.Runtime.dll","VampireSurvivors.Framework","GameManager");
    p->character=p->klass(domain,"VampireSurvivors.Runtime.dll","VampireSurvivors.Objects.Characters","CharacterController");
    p->stage=p->klass(domain,"VampireSurvivors.Runtime.dll","VampireSurvivors.Objects","Stage");
    p->character_enum=p->klass(domain,"VampireSurvivors.Runtime.dll","VampireSurvivors.Data","CharacterType");
    p->stage_enum=p->klass(domain,"VampireSurvivors.Runtime.dll","VampireSurvivors.Data","StageType");
    p->sprite=p->klass(domain,"PhaserPort.dll","","ArcadeSprite");p->input=p->klass(domain,"Rewired_Core.dll","Rewired","Player");
    p->float2=p->klass(domain,"Unity.Mathematics.dll","Unity.Mathematics","float2");
    p->intclass=p->klass(domain,"mscorlib.dll","System","Int32");p->singleclass=p->klass(domain,"mscorlib.dll","System","Single");p->boolclass=p->klass(domain,"mscorlib.dll","System","Boolean");
    if(!core||!array||!p->manager||!p->character||!p->stage||!p->character_enum||!p->stage_enum||!p->sprite||!p->input||!p->float2){error="Missing or ambiguous required class/image";return false;}
    if(!p->image_for_class(core,"UnityEngine.CoreModule.dll")||!p->image_for_class(array,"mscorlib.dll")||!p->image_for_class(p->manager,"VampireSurvivors.Runtime.dll")||!p->image_for_class(p->character,"VampireSurvivors.Runtime.dll")||!p->image_for_class(p->stage,"VampireSurvivors.Runtime.dll")||!p->image_for_class(p->character_enum,"VampireSurvivors.Runtime.dll")||!p->image_for_class(p->stage_enum,"VampireSurvivors.Runtime.dll")||!p->image_for_class(p->sprite,"PhaserPort.dll")||!p->image_for_class(p->input,"Rewired_Core.dll")||!p->image_for_class(p->float2,"Unity.Mathematics.dll")||!p->image_for_class(p->intclass,"mscorlib.dll")||!p->image_for_class(p->singleclass,"mscorlib.dll")||!p->image_for_class(p->boolclass,"mscorlib.dll")){error="Class/image association mismatch";return false;}
    auto base=p->parent(p->character);if(!base||base!=p->sprite){error="CharacterController parent is not ArcadeSprite";return false;}
    if(!p->type(p->enum_base(p->character_enum),"System.Int32")||!p->type(p->enum_base(p->stage_enum),"System.Int32")){error="Enum underlying type is not Int32";return false;}
    p->find=p->method(core,"FindObjectsOfType",kObjects,"System.Type",true);p->array_item=p->method(array,"GetValue","System.Object","System.Int32");
    p->players=p->method(p->manager,"get_MainPlayers",kList);p->stage_get=p->method(p->manager,"get_Stage","VampireSurvivors.Objects.Stage");
    p->running=p->method(p->manager,"get_IsGameRunning","System.Boolean");
    p->hp=p->method(p->character,"get_CurrentHp","System.Single");p->dead=p->method(p->character,"get_IsDead","System.Boolean");p->xp=p->method(p->character,"get_Xp","System.Single");p->level=p->method(p->character,"get_Level","System.Int32");p->kind=p->method(p->character,"get_CharacterType","VampireSurvivors.Data.CharacterType");p->player_input=p->method(p->character,"get_PlayerInput","Rewired.Player");
    p->position=p->method(p->sprite,"get_position","Unity.Mathematics.float2");p->input_id=p->method(p->input,"get_id","System.Int32");p->stage_type=p->method(p->stage,"get_StageType","VampireSurvivors.Data.StageType");
    if(!p->find||!p->array_item||!p->players||!p->stage_get||!p->hp||!p->dead||!p->xp||!p->level||!p->kind||!p->player_input||!p->position||!p->input_id||!p->stage_type||!p->running){error="Missing or ambiguous exact method metadata";return false;}
    if(!p->result_image(p->find,kObjects,"UnityEngine.CoreModule.dll","UnityEngine","Object[]")||!p->result_image(p->array_item,"System.Object","mscorlib.dll","System","Object")||!p->result_image(p->players,kList,"mscorlib.dll","System.Collections.Generic","List`1")||!p->result_image(p->stage_get,"VampireSurvivors.Objects.Stage","VampireSurvivors.Runtime.dll","VampireSurvivors.Objects","Stage")||!p->result_image(p->kind,"VampireSurvivors.Data.CharacterType","VampireSurvivors.Runtime.dll","VampireSurvivors.Data","CharacterType")||!p->result_image(p->player_input,"Rewired.Player","Rewired_Core.dll","Rewired","Player")||!p->result_image(p->position,"Unity.Mathematics.float2","Unity.Mathematics.dll","Unity.Mathematics","float2")||!p->result_image(p->stage_type,"VampireSurvivors.Data.StageType","VampireSurvivors.Runtime.dll","VampireSurvivors.Data","StageType")){error="Return type does not resolve to expected declaring image";return false;}
    p->fx=p->field_by_name(p->float2,"x");p->fy=p->field_by_name(p->float2,"y");
    auto valid=[&](FieldInfo* f,const char* name){return f&&p->field_parent(f)==p->float2&&p->field_by_name(p->float2,name)==f&&!(p->field_flags(f)&(16|64))&&p->type(p->field_type(f),"System.Single");};
    if(!p->fx||!p->fy||p->fx==p->fy||!valid(p->fx,"x")||!valid(p->fy,"y")){error="Invalid float2 field metadata";return false;}
    impl_=std::move(p);return true;
}
PlayerReadStatus PlayerStateReader::read(SceneStateSnapshot& output,std::string& error) const {
    error.clear();if(!impl_){error="Reader not initialized";return PlayerReadStatus::Error;}const auto& p=*impl_;
    MainThreadIdentity identity;if(!p.guard.verify(p.os,p.managed,identity,error))return PlayerReadStatus::Error;
    auto typeobj=p.a.type_get_object(p.a.class_get_type(p.manager));Impl::Root tr(p,typeobj);if(!tr.get()){error="Manager type unavailable";return PlayerReadStatus::Error;}
    void* findargs[]={tr.get()};Impl::Root array(p,p.invoke(p.find,nullptr,findargs,error));if(!array.get()){if(error.empty())error="FindObjectsOfType returned null";return PlayerReadStatus::Error;}
    // The declared signature is Object[], but Unity allocates the requested
    // element type, so the runtime array can be GameManager[] as well. Accept
    // only arrays whose element type is comparable with GameManager; every
    // element is still validated with is_a() below.
    auto arrclass=p.a.object_get_class(array.get());auto element=p.element_class(arrclass);
    if(!arrclass||!element){error="Manager lookup did not return an array: "+p.name_of(arrclass);return PlayerReadStatus::Error;}
    if(!p.assignable(p.manager,element)&&!p.assignable(element,p.manager)){error="Manager lookup element type is not comparable with GameManager: "+p.name_of(arrclass);return PlayerReadStatus::Error;}
    auto n=p.a.array_length(reinterpret_cast<Il2CppArray*>(array.get()));if(n==0)return PlayerReadStatus::NotReady;if(n!=1){error="Ambiguous GameManager instances";return PlayerReadStatus::Error;}
    std::int32_t zero=0;void* arg[]={&zero};Impl::Root mgr(p,p.invoke(p.array_item,array.get(),arg,error));if(!p.is_a(mgr.get(),p.manager)){if(error.empty())error="Invalid manager array element";return PlayerReadStatus::Error;}
    std::uint8_t running=0;
    if(!p.value(p.invoke(p.running,mgr.get(),nullptr,error),p.boolclass,running,error))return PlayerReadStatus::Error;
    if(running>1){error="Invalid run-state Boolean";return PlayerReadStatus::Error;}
    if(!running)return PlayerReadStatus::NotReady;
    auto raw_stage=p.invoke(p.stage_get,mgr.get(),nullptr,error);
    if(!error.empty())return PlayerReadStatus::Error;
    if(!raw_stage)return PlayerReadStatus::NotReady;
    Impl::Root stage(p,raw_stage);
    if(!stage.get()){error="Unable to root non-null Stage";return PlayerReadStatus::Error;}
    if(!p.is_a(stage.get(),p.stage)){error="Invalid Stage class";return PlayerReadStatus::Error;}
    auto raw_list=p.invoke(p.players,mgr.get(),nullptr,error);
    if(!error.empty())return PlayerReadStatus::Error;
    if(!raw_list)return PlayerReadStatus::NotReady;
    Impl::Root list(p,raw_list);
    if(!list.get()){error="Unable to root non-null MainPlayers";return PlayerReadStatus::Error;}
    auto listclass=p.a.object_get_class(list.get());if(!listclass||!p.type(p.a.class_get_type(listclass),kList)){error="Unexpected closed MainPlayers list class";return PlayerReadStatus::Error;}
    if(!p.image_for_class(listclass,"mscorlib.dll")){error="MainPlayers list is not from mscorlib";return PlayerReadStatus::Error;}
    auto countget=p.method(listclass,"get_Count","System.Int32");auto itemget=p.method(listclass,"get_Item",kCharacter,"System.Int32");if(!countget||!itemget){error="Missing closed list method metadata";return PlayerReadStatus::Error;}
    std::int32_t count=0; if(!p.integer32(p.invoke(countget,list.get(),nullptr,error),count,error))return PlayerReadStatus::Error;
    if(count==0)return PlayerReadStatus::NotReady;if(count<0||count>kMaximumPlayers){error="MainPlayers count outside supported bound";return PlayerReadStatus::Error;}
    SceneStateSnapshot result;if(!p.value(p.invoke(p.stage_type,stage.get(),nullptr,error),p.stage_enum,result.stage_type,error))return PlayerReadStatus::Error;
    for(std::int32_t i=0;i<count;++i){void* args[]={&i};auto raw=p.invoke(itemget,list.get(),args,error);Impl::Root player(p,raw);if(!error.empty())return PlayerReadStatus::Error;if(!player.get()){error="Null MainPlayers member";return PlayerReadStatus::Error;}if(!p.is_a(player.get(),p.character)){error="Invalid character inheritance";return PlayerReadStatus::Error;}
        PlayerStateSnapshot s;s.slot=i;std::uint8_t b=0;
        if(!p.value(p.invoke(p.hp,player.get(),nullptr,error),p.singleclass,s.hp,error)||!p.value(p.invoke(p.xp,player.get(),nullptr,error),p.singleclass,s.xp,error)||!p.integer32(p.invoke(p.level,player.get(),nullptr,error),s.level,error)||!p.value(p.invoke(p.kind,player.get(),nullptr,error),p.character_enum,s.character_type,error)||!p.value(p.invoke(p.dead,player.get(),nullptr,error),p.boolclass,b,error))return PlayerReadStatus::Error;
        if(b>1){error="Invalid Boolean value";return PlayerReadStatus::Error;}s.dead=b!=0;
        Impl::Root input(p,p.invoke(p.player_input,player.get(),nullptr,error));if(!error.empty())return PlayerReadStatus::Error;if(!p.is_a(input.get(),p.input)){if(error.empty())error="Invalid PlayerInput";return PlayerReadStatus::Error;}
        if(!p.integer32(p.invoke(p.input_id,input.get(),nullptr,error),s.input_id,error))return PlayerReadStatus::Error;
        Impl::Root pos(p,p.invoke(p.position,player.get(),nullptr,error));if(!pos.get()||p.a.object_get_class(pos.get())!=p.float2){error="Invalid boxed float2";return PlayerReadStatus::Error;}
        p.field_value(pos.get(),p.fx,&s.x);p.field_value(pos.get(),p.fy,&s.y);
        if(!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.hp)||!std::isfinite(s.xp)){error="Non-finite player value";return PlayerReadStatus::Error;}
        result.players.push_back(s);
    }
    output=std::move(result);return PlayerReadStatus::Captured;
}
}
