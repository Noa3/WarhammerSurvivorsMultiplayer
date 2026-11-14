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
    bool wrong_equipment_collection=false;
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
        Root(Root&& other) noexcept:p(other.p),h(other.h){other.h=nullptr;}
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
void PlayerStateReader::test_use_wrong_equipment_collection() {
    if(impl_)impl_->wrong_equipment_collection=true;
}
#endif
bool PlayerStateReader::initialize(HMODULE module,Il2CppApi api,Il2CppDomain* domain,DWORD native_thread,std::int32_t managed_thread,std::string& error){
    impl_.reset(); error.clear();
    if(!module||!domain||!native_thread||!api.thread_current||!api.thread_current()||!api.domain_get_assemblies||!api.assembly_get_image||!api.image_get_name||!api.image_get_class_count||!api.image_get_class||!api.class_get_name||!api.class_get_namespace||!api.class_get_methods||!api.method_get_name||!api.method_get_param_count||!api.method_get_param||!api.method_get_return_type||!api.method_get_flags||!api.type_get_name||!api.free||!api.class_get_type||!api.type_get_object||!api.array_length||!api.runtime_invoke||!api.object_get_class||!api.object_unbox){error="Incomplete runtime API or thread not attached";return false;}
    auto p=std::make_unique<Impl>(); p->a=api;p->os=native_thread;p->managed=managed_thread;p->domain=domain;
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

PlayerReadStatus PlayerStateReader::read_upgrade_context(UpgradeContextSnapshot& output,std::string& error) const {
    return read_upgrade_context_impl(output,error,false,false);
}
PlayerReadStatus PlayerStateReader::read_upgrade_selection(UpgradeContextSnapshot& output,std::string& error) const {
    return read_upgrade_context_impl(output,error,true,false);
}
PlayerReadStatus PlayerStateReader::read_upgrade_items(UpgradeContextSnapshot& output,std::string& error) const {
    return read_upgrade_context_impl(output,error,true,true);
}
PlayerReadStatus PlayerStateReader::read_upgrade_context_impl(UpgradeContextSnapshot& output,std::string& error,bool observe_selection,bool observe_items) const {
    error.clear();
    if(!impl_){error="Reader not initialized";return PlayerReadStatus::Error;}
    const auto& p=*impl_;
    MainThreadIdentity identity;
    if(!p.guard.verify(p.os,p.managed,identity,error))return PlayerReadStatus::Error;
    // Resolve nothing upgrade-specific until the caller explicitly requests this diagnostic.
    const auto fail=[&](const char* message){error=message;return PlayerReadStatus::Error;};
    const auto find_one=[&](Il2CppClass* cls,std::unique_ptr<Impl::Root>& found,std::int32_t& count)->PlayerReadStatus {
        if(!cls)return fail("Missing or ambiguous diagnostic class");
        Impl::Root type(p,p.a.type_get_object(p.a.class_get_type(cls)));
        if(!type.get())return fail("Unable to root diagnostic type");
        void* args[]={type.get()};auto raw_array=p.invoke(p.find,nullptr,args,error);
        if(!error.empty())return PlayerReadStatus::Error;
        Impl::Root array(p,raw_array);
        if(!array.get())return fail("Unable to root diagnostic lookup array");
        auto array_class=p.a.object_get_class(array.get());auto element=p.element_class(array_class);
        if(!element || (!p.assignable(cls,element)&&!p.assignable(element,cls)))return fail("Unexpected diagnostic lookup array type");
        auto length=p.a.array_length(reinterpret_cast<Il2CppArray*>(array.get()));
        if(length==0){count=0;return PlayerReadStatus::NotReady;}
        if(length!=1)return fail("Ambiguous diagnostic scene instances");
        count=1;std::int32_t zero=0;void* index[]={&zero};auto raw=p.invoke(p.array_item,array.get(),index,error);
        if(!error.empty())return PlayerReadStatus::Error;
        found=std::make_unique<Impl::Root>(p,raw);
        if(!p.is_a(found->get(),cls))return fail("Invalid diagnostic scene instance");
        return PlayerReadStatus::Captured;
    };
    std::unique_ptr<Impl::Root> manager;std::int32_t manager_count=0;
    auto status=find_one(p.manager,manager,manager_count);if(status!=PlayerReadStatus::Captured)return status;
    std::uint8_t running=0;
    if(!p.value(p.invoke(p.running,manager->get(),nullptr,error),p.boolclass,running,error))return PlayerReadStatus::Error;
    if(running>1)return fail("Invalid diagnostic run-state Boolean");
    if(!running)return PlayerReadStatus::NotReady;
    auto raw_stage=p.invoke(p.stage_get,manager->get(),nullptr,error);
    if(!error.empty())return PlayerReadStatus::Error;
    if(!raw_stage)return PlayerReadStatus::NotReady;
    Impl::Root stage(p,raw_stage);
    if(!p.is_a(stage.get(),p.stage))return fail("Unable to root valid diagnostic Stage");

    const auto game_class=[&](const char* ns,const char* name){return p.klass(p.domain,"VampireSurvivors.Runtime.dll",ns,name);};
    auto page_class=game_class("Wilson.UI","WSLevelUpPage");
    auto weapons_class=game_class("VampireSurvivors.Objects","CharacterWeaponsManager");
    auto accessories_class=game_class("VampireSurvivors.Objects","CharacterAccessoriesManager");
    auto equipment_class=game_class("VampireSurvivors.Objects","EquipmentManager");
    auto equipment_element=game_class("VampireSurvivors.Objects","Equipment");
    auto session_class=game_class("VampireSurvivors.Framework","GameSessionData");
    auto factory_class=game_class("VampireSurvivors.Framework","LevelUpFactory");
    if(!page_class||!weapons_class||!accessories_class||!equipment_class||!equipment_element||!session_class||!factory_class)
        return fail("Missing or ambiguous upgrade-context metadata");
    if(p.parent(accessories_class)!=equipment_class)return fail("Unexpected accessories manager inheritance");
    const auto reference_field=[&](Il2CppClass* owner,const char* name,Il2CppClass* expected)->FieldInfo* {
        auto field=p.field_by_name(owner,name);
        if(!field || p.field_parent(field)!=owner || (p.field_flags(field)&(16|64)) ||
           !expected || p.class_from_type(p.field_type(field))!=expected)return nullptr;
        return field;
    };
    auto weapons_field=reference_field(p.character,"_weaponsManager",weapons_class);
    auto accessories_field=reference_field(p.character,"_accessoriesManager",accessories_class);
    auto session_field=reference_field(page_class,"_gameSession",session_class);
    auto factory_field=reference_field(page_class,"_levelUpFactory",factory_class);
    auto active_field=reference_field(session_class,"<ActiveCharacter>k__BackingField",p.character);
    const char* collection_name="<ActiveEquipment>k__BackingField";
#ifdef WSM_PLAYER_STATE_TESTING
    // The original fixture provides a real List<Int32> field. Redirect only the
    // test build to model incompatible field metadata without patching a runtime.
    if(p.wrong_equipment_collection)collection_name="<WrongEquipment>k__BackingField";
#endif
    auto collection_field=p.field_by_name(equipment_class,collection_name);
    auto collection_class=collection_field?p.class_from_type(p.field_type(collection_field)):nullptr;
    constexpr const char* equipment_type="VampireSurvivors.Objects.Equipment";
    constexpr const char* equipment_list="System.Collections.Generic.List<VampireSurvivors.Objects.Equipment>";
    auto collection_item=p.method(collection_class,"get_Item",equipment_type,"System.Int32");
    // Inspect metadata only: do not invoke get_Item or read a collection element.
    // Both the closed type and its canonical element image/identity must match.
    if(!collection_field || !p.type(p.field_type(collection_field),equipment_list) ||
       !p.result_image(collection_item,equipment_type,"VampireSurvivors.Runtime.dll","VampireSurvivors.Objects","Equipment") ||
       p.class_from_type(p.a.method_get_return_type(collection_item))!=equipment_element)
        return fail("ActiveEquipment must be List of canonical Equipment");
    if(!collection_class || !p.image_for_class(collection_class,"mscorlib.dll") ||
       !eq(p.a.class_get_namespace(collection_class),"System.Collections.Generic") ||
       !eq(p.a.class_get_name(collection_class),"List`1") ||
       reference_field(equipment_class,collection_name,collection_class)!=collection_field ||
       !weapons_field||!accessories_field||!session_field||!factory_field||!active_field)
        return fail("Invalid upgrade-context reference field metadata");
    auto transform_class=p.klass(p.domain,"UnityEngine.CoreModule.dll","UnityEngine","Transform");
    auto group_field=p.field_by_name(page_class,"_ChoiceContainerGroup");
    auto group_class=group_field?p.class_from_type(p.field_type(group_field)):nullptr;
    if(!transform_class || !group_class || !p.type(p.field_type(group_field),"Wilson.UI.UIGroup<UnityEngine.Transform>") ||
       !p.image_for_class(group_class,"VampireSurvivors.Runtime.dll") || !eq(p.a.class_get_namespace(group_class),"Wilson.UI") ||
       !eq(p.a.class_get_name(group_class),"UIGroup`1") || reference_field(page_class,"_ChoiceContainerGroup",group_class)!=group_field)
        return fail("Invalid choice container group metadata");
    auto group_items_field=p.field_by_name(group_class,"items");
    auto group_items_class=group_items_field?p.class_from_type(p.field_type(group_items_field)):nullptr;
    auto group_count=p.method(group_items_class,"get_Count","System.Int32");
    auto group_item=p.method(group_items_class,"get_Item","UnityEngine.Transform","System.Int32");
    if(!group_items_class || !p.type(p.field_type(group_items_field),"System.Collections.Generic.List<UnityEngine.Transform>") ||
       !p.image_for_class(group_items_class,"mscorlib.dll") || !eq(p.a.class_get_namespace(group_items_class),"System.Collections.Generic") ||
       !eq(p.a.class_get_name(group_items_class),"List`1") || reference_field(group_class,"items",group_items_class)!=group_items_field ||
       !group_count || !p.result_image(group_item,"UnityEngine.Transform","UnityEngine.CoreModule.dll","UnityEngine","Transform") ||
       p.class_from_type(p.a.method_get_return_type(group_item))!=transform_class)
        return fail("Invalid choice container items metadata");
    Il2CppClass *game_object_class=nullptr,*unity_object=nullptr,*component=nullptr;
    const MethodInfo *object_alive=nullptr,*component_game_object=nullptr,*active_in_hierarchy=nullptr;
    if(observe_selection) {
        unity_object=p.klass(p.domain,"UnityEngine.CoreModule.dll","UnityEngine","Object");
        component=p.klass(p.domain,"UnityEngine.CoreModule.dll","UnityEngine","Component");
        game_object_class=p.klass(p.domain,"UnityEngine.CoreModule.dll","UnityEngine","GameObject");
        object_alive=p.method(unity_object,"op_Implicit","System.Boolean","UnityEngine.Object",true);
        component_game_object=p.method(component,"get_gameObject","UnityEngine.GameObject");
        active_in_hierarchy=p.method(game_object_class,"get_activeInHierarchy","System.Boolean");
        if(!unity_object || !component || !game_object_class)return fail("Missing canonical Unity selection classes");
        if(!p.result_image(object_alive,"System.Boolean","mscorlib.dll","System","Boolean") ||
           p.class_from_type(p.a.method_get_param(object_alive,0))!=unity_object ||
           p.class_from_type(p.a.method_get_return_type(object_alive))!=p.boolclass)
            return fail("Missing or invalid UnityEngine.Object.op_Implicit metadata");
        if(!p.result_image(component_game_object,"UnityEngine.GameObject","UnityEngine.CoreModule.dll","UnityEngine","GameObject") ||
           p.class_from_type(p.a.method_get_return_type(component_game_object))!=game_object_class)
            return fail("Missing or invalid UnityEngine.Component.get_gameObject metadata");
        if(!p.result_image(active_in_hierarchy,"System.Boolean","mscorlib.dll","System","Boolean") ||
           p.class_from_type(p.a.method_get_return_type(active_in_hierarchy))!=p.boolclass)
            return fail("Missing or invalid UnityEngine.GameObject.get_activeInHierarchy metadata");
    }
    Il2CppClass *item_class=nullptr,*data_class=nullptr,*weapon_enum=nullptr;
    const MethodInfo *child_count=nullptr,*get_child=nullptr,*get_component=nullptr;
    FieldInfo *type_field=nullptr,*data_field=nullptr,*new_field=nullptr;
    std::unique_ptr<Impl::Root> item_type;
    if(observe_items) {
        item_class=game_class("Wilson.UI","WSLevelUpItemUI");
        data_class=game_class("VampireSurvivors.Data.Weapons","WeaponData");
        weapon_enum=game_class("VampireSurvivors.Data","WeaponType");
        auto system_type=p.klass(p.domain,"mscorlib.dll","System","Type");
        auto value_type=p.klass(p.domain,"mscorlib.dll","System","ValueType");
        auto enum_class=p.klass(p.domain,"mscorlib.dll","System","Enum");
        if(!item_class||!data_class||!weapon_enum||!system_type||!value_type||!enum_class ||
           !p.image_for_class(item_class,"VampireSurvivors.Runtime.dll") ||
           !p.image_for_class(data_class,"VampireSurvivors.Runtime.dll") ||
           !p.image_for_class(weapon_enum,"VampireSurvivors.Runtime.dll") ||
           !p.image_for_class(transform_class,"UnityEngine.CoreModule.dll") ||
           !p.assignable(component,transform_class) || !p.assignable(component,item_class) ||
           p.assignable(unity_object,data_class) || p.assignable(value_type,data_class) ||
           p.parent(weapon_enum)!=enum_class)
            return fail("Invalid canonical upgrade item class or inheritance metadata");
        auto enum_base=p.enum_base(weapon_enum);
        if(!p.type(enum_base,"System.Int32") || p.class_from_type(enum_base)!=p.intclass)
            return fail("WeaponType enum underlying type is not canonical Int32");
        child_count=p.method(transform_class,"get_childCount","System.Int32");
        get_child=p.method(transform_class,"GetChild","UnityEngine.Transform","System.Int32");
        get_component=p.method(component,"GetComponent","UnityEngine.Component","System.Type");
        if(!p.result_image(child_count,"System.Int32","mscorlib.dll","System","Int32") ||
           p.class_from_type(p.a.method_get_return_type(child_count))!=p.intclass ||
           !p.result_image(get_child,"UnityEngine.Transform","UnityEngine.CoreModule.dll","UnityEngine","Transform") ||
           p.class_from_type(p.a.method_get_return_type(get_child))!=transform_class ||
           p.class_from_type(p.a.method_get_param(get_child,0))!=p.intclass ||
           !p.result_image(get_component,"UnityEngine.Component","UnityEngine.CoreModule.dll","UnityEngine","Component") ||
           p.class_from_type(p.a.method_get_return_type(get_component))!=component ||
           p.class_from_type(p.a.method_get_param(get_component,0))!=system_type)
            return fail("Invalid canonical child or GetComponent method metadata");
        const auto item_field=[&](const char* name,Il2CppClass* expected,const char* type_name)->FieldInfo* {
            auto f=p.field_by_name(item_class,name);
            if(!f || p.field_parent(f)!=item_class || (p.field_flags(f)&7)!=1 ||
               (p.field_flags(f)&(16|64)) || !p.type(p.field_type(f),type_name) ||
               p.class_from_type(p.field_type(f))!=expected)return nullptr;
            return f;
        };
        type_field=item_field("_type",weapon_enum,"VampireSurvivors.Data.WeaponType");
        data_field=item_field("_data",data_class,"VampireSurvivors.Data.Weapons.WeaponData");
        new_field=item_field("_isNew",p.boolclass,"System.Boolean");
        if(!type_field||!data_field||!new_field || type_field==data_field || type_field==new_field || data_field==new_field)
            return fail("Invalid canonical upgrade item field metadata");
        auto raw_type=p.a.type_get_object(p.a.class_get_type(item_class));
        item_type=std::make_unique<Impl::Root>(p,raw_type);
        if(!p.is_a(item_type->get(),system_type))return fail("Unable to root canonical upgrade item reflection type");
    }
    const auto read_ref=[&](Il2CppObject* owner,FieldInfo* field,Il2CppClass* expected,std::unique_ptr<Impl::Root>& root)->bool {
        if(!p.is_a(owner,p.field_parent(field))){error="Unexpected diagnostic field owner";return false;}
        Il2CppObject* raw=nullptr;p.field_value(owner,field,&raw);
        root=std::make_unique<Impl::Root>(p,raw);
        if(raw && (!root->get() || !p.is_a(root->get(),expected))){error="Unable to root expected diagnostic reference";return false;}
        return true;
    };
    UpgradeContextSnapshot result;result.choice_selection_observed=observe_selection;std::unique_ptr<Impl::Root> page;
    status=find_one(page_class,page,result.level_up_page_count);if(status!=PlayerReadStatus::Captured)return status;
    std::unique_ptr<Impl::Root> group,group_items,selected_container;
    if(!read_ref(page->get(),group_field,group_class,group))return PlayerReadStatus::Error;
    result.choice_group_present=group->get()!=nullptr;
    if(result.choice_group_present) {
        if(!read_ref(group->get(),group_items_field,group_items_class,group_items))return PlayerReadStatus::Error;
        result.choice_group_items_present=group_items->get()!=nullptr;
        if(result.choice_group_items_present) {
            std::int32_t size=0;
            if(!p.integer32(p.invoke(group_count,group_items->get(),nullptr,error),size,error))return PlayerReadStatus::Error;
            if(size<0||size>8)return fail("Choice container item count outside supported bound");
            for(std::int32_t i=0;i<size;++i) {
                void* index[]={&i};auto raw=p.invoke(group_item,group_items->get(),index,error);
                if(!error.empty())return PlayerReadStatus::Error;
                Impl::Root item(p,raw);
                if(raw && !p.is_a(item.get(),transform_class))return fail("Unable to root expected choice container reference");
                result.choice_group_item_references.push_back(item.get()!=nullptr);
                if(observe_selection) {
                    std::uint8_t alive=0,active=0;
                    if(item.get()) {
                        void* args[]={item.get()};auto boxed_alive=p.invoke(object_alive,nullptr,args,error);
                        if(!error.empty()){error="Unity object truthiness observation: "+error;return PlayerReadStatus::Error;}
                        if(!p.value(boxed_alive,p.boolclass,alive,error))return PlayerReadStatus::Error;
                        if(alive>1)return fail("Invalid Unity object truthiness Boolean");
                        if(alive) {
                            auto raw_game_object=p.invoke(component_game_object,item.get(),nullptr,error);
                            if(!error.empty()){error="Choice container GameObject observation: "+error;return PlayerReadStatus::Error;}
                            Impl::Root game_object(p,raw_game_object);
                            if(!p.is_a(game_object.get(),game_object_class))return fail("Unable to root live choice container GameObject");
                            auto boxed_active=p.invoke(active_in_hierarchy,game_object.get(),nullptr,error);
                            if(!error.empty()){error="Choice container active-state observation: "+error;return PlayerReadStatus::Error;}
                            if(!p.value(boxed_active,p.boolclass,active,error))return PlayerReadStatus::Error;
                            if(active>1)return fail("Invalid choice container active-state Boolean");
                        }
                    }
                    result.choice_group_item_unity_alive.push_back(alive!=0);
                    result.choice_group_item_active.push_back(active!=0);
                    if(active && result.selected_choice_container_slot<0) {
                        result.selected_choice_container_slot=i;
                        if(observe_items) {
                            selected_container=std::make_unique<Impl::Root>(std::move(item));
                            if(!p.is_a(selected_container->get(),transform_class))return fail("Unable to retain selected choice container root");
                        }
                    }
                }
            }
        }
    }
    if(observe_items && selected_container) {
        const auto check_count=[&](std::int32_t& value)->bool {
            if(!p.is_a(selected_container->get(),transform_class)){error="Lost selected choice container root";return false;}
            auto boxed=p.invoke(child_count,selected_container->get(),nullptr,error);
            if(!error.empty()){error="Choice child count observation: "+error;return false;}
            if(!p.integer32(boxed,value,error)){error="Choice child count observation: "+error;return false;}
            if(value<0||value>32){error="Choice child count outside supported bound";return false;}
            return true;
        };
        std::int32_t size=0;
        if(!check_count(size))return PlayerReadStatus::Error;
        result.choice_items_observed=true;result.choice_child_count=size;
        for(std::int32_t i=0;i<size;++i) {
            std::int32_t current=0;
            if(!check_count(current))return PlayerReadStatus::Error;
            if(current!=size)return fail("Choice child count changed during observation");
            void* index[]={&i};auto raw=p.invoke(get_child,selected_container->get(),index,error);
            if(!error.empty()){error="Choice GetChild observation: "+error;return PlayerReadStatus::Error;}
            Impl::Root child(p,raw);UpgradeChoiceItemSnapshot row;row.child_present=raw!=nullptr;
            if(raw && !p.is_a(child.get(),transform_class))return fail("Unable to root expected choice child Transform");
            if(child.get()) {
                std::uint8_t alive=0;void* args[]={child.get()};
                auto boxed=p.invoke(object_alive,nullptr,args,error);
                if(!error.empty()){error="Choice child liveness observation: "+error;return PlayerReadStatus::Error;}
                if(!p.value(boxed,p.boolclass,alive,error)){error="Choice child liveness observation: "+error;return PlayerReadStatus::Error;}
                if(alive>1)return fail("Invalid choice child liveness Boolean");
                row.child_unity_alive=alive!=0;
                if(alive) {
                    if(!item_type->get())return fail("Lost upgrade item reflection type root");
                    void* type_arg[]={item_type->get()};
                    auto raw_component=p.invoke(get_component,child.get(),type_arg,error);
                    if(!error.empty()){error="Choice child GetComponent observation: "+error;return PlayerReadStatus::Error;}
                    Impl::Root item(p,raw_component);row.component_present=raw_component!=nullptr;
                    if(raw_component && !p.is_a(item.get(),item_class))return fail("Unable to root expected upgrade item component");
                    if(item.get()) {
                        void* item_args[]={item.get()};auto boxed_item=p.invoke(object_alive,nullptr,item_args,error);
                        if(!error.empty()){error="Upgrade item liveness observation: "+error;return PlayerReadStatus::Error;}
                        std::uint8_t item_alive=0;
                        if(!p.value(boxed_item,p.boolclass,item_alive,error)){error="Upgrade item liveness observation: "+error;return PlayerReadStatus::Error;}
                        if(item_alive>1)return fail("Invalid upgrade item liveness Boolean");
                        row.component_unity_alive=item_alive!=0;
                        if(item_alive) {
                            p.field_value(item.get(),type_field,&row.weapon_type);
                            std::uint8_t is_new=0;p.field_value(item.get(),new_field,&is_new);
                            if(is_new>1)return fail("Invalid upgrade item isNew Boolean");
                            std::unique_ptr<Impl::Root> data;
                            if(!read_ref(item.get(),data_field,data_class,data))return PlayerReadStatus::Error;
                            row.data_present=data->get()!=nullptr;row.is_new=is_new!=0;row.fields_observed=true;
                        }
                    }
                }
            }
            result.choice_items.push_back(row);
        }
        std::int32_t final_count=0;
        if(!check_count(final_count))return PlayerReadStatus::Error;
        if(final_count!=size)return fail("Choice child count changed during observation");
    }
    auto raw_list=p.invoke(p.players,manager->get(),nullptr,error);
    if(!error.empty())return PlayerReadStatus::Error;
    if(!raw_list)return PlayerReadStatus::NotReady;
    Impl::Root list(p,raw_list);
    if(!list.get())return fail("Unable to root diagnostic MainPlayers");
    auto list_class=p.a.object_get_class(list.get());
    if(!p.image_for_class(list_class,"mscorlib.dll")||!p.type(p.a.class_get_type(list_class),kList))return fail("Unexpected diagnostic MainPlayers type");
    auto count_get=p.method(list_class,"get_Count","System.Int32");auto item_get=p.method(list_class,"get_Item",kCharacter,"System.Int32");
    if(!count_get||!item_get)return fail("Missing diagnostic MainPlayers methods");
    std::int32_t count=0;if(!p.integer32(p.invoke(count_get,list.get(),nullptr,error),count,error))return PlayerReadStatus::Error;
    if(count==0)return PlayerReadStatus::NotReady;
    if(count<0||count>kMaximumPlayers)return fail("Diagnostic MainPlayers count outside supported bound");
    std::vector<std::unique_ptr<Impl::Root>> actors;
    for(std::int32_t i=0;i<count;++i) {
        void* index[]={&i};auto raw=p.invoke(item_get,list.get(),index,error);
        if(!error.empty())return PlayerReadStatus::Error;
        auto actor=std::make_unique<Impl::Root>(p,raw);
        if(!p.is_a(actor->get(),p.character))return fail("Invalid diagnostic actor");
        for(const auto& prior:actors)if(prior->get()==actor->get())return fail("Ambiguous duplicate diagnostic actor identity");
        UpgradeActorContext row;row.slot=i;
        if(!p.value(p.invoke(p.kind,actor->get(),nullptr,error),p.character_enum,row.character_type,error))return PlayerReadStatus::Error;
        Impl::Root input(p,p.invoke(p.player_input,actor->get(),nullptr,error));
        if(!error.empty())return PlayerReadStatus::Error;
        if(!p.is_a(input.get(),p.input))return fail("Invalid diagnostic PlayerInput");
        if(!p.integer32(p.invoke(p.input_id,input.get(),nullptr,error),row.input_id,error))return PlayerReadStatus::Error;
        std::unique_ptr<Impl::Root> weapons,accessories,collection;
        if(!read_ref(actor->get(),weapons_field,weapons_class,weapons)||!read_ref(actor->get(),accessories_field,accessories_class,accessories))return PlayerReadStatus::Error;
        row.weapons_manager_present=weapons->get()!=nullptr;row.accessories_manager_present=accessories->get()!=nullptr;
        if(row.accessories_manager_present) {
            if(!read_ref(accessories->get(),collection_field,collection_class,collection))return PlayerReadStatus::Error;
            row.accessories_collection_present=collection->get()!=nullptr;
        }
        actors.push_back(std::move(actor));result.actors.push_back(row);
    }
    std::unique_ptr<Impl::Root> session,factory,active;
    if(!read_ref(page->get(),session_field,session_class,session)||!read_ref(page->get(),factory_field,factory_class,factory))return PlayerReadStatus::Error;
    result.page_game_session_present=session->get()!=nullptr;result.level_up_factory_present=factory->get()!=nullptr;
    if(result.page_game_session_present) {
        if(!read_ref(session->get(),active_field,p.character,active))return PlayerReadStatus::Error;
        result.active_character_present=active->get()!=nullptr;
        if(result.active_character_present) {
            result.active_character_slot=-2;
            for(std::int32_t i=0;i<count;++i)if(actors[i]->get()==active->get())result.active_character_slot=i;
        }
    }
    output=std::move(result);return PlayerReadStatus::Captured;
}
}
