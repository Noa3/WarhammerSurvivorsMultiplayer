#include "wsm/upgrade_choice_controller.h"
#include "wsm/main_thread_guard.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace wsm {
namespace {
bool equal(const char* a,const char* b){return a&&b&&std::strcmp(a,b)==0;}
std::atomic<std::uint64_t> next_token{0};
std::uint64_t token() {
    auto old=next_token.load(std::memory_order_relaxed);
    do { if(old==(std::numeric_limits<std::uint64_t>::max)()) throw std::runtime_error("Offer token space exhausted"); }
    while(!next_token.compare_exchange_weak(old,old+1,std::memory_order_relaxed));
    return old+1;
}
}
struct UpgradeChoiceController::Impl {
    Il2CppApi a{}; HMODULE module{}; Il2CppDomain* domain{}; DWORD os{}; std::int32_t managed{}; MainThreadGuard guard;
    Il2CppClass* (*parent)(Il2CppClass*){}; Il2CppClass* (*from_type)(const Il2CppType*){};
    const Il2CppImage* (*image)(Il2CppClass*){}; Il2CppClass* (*element)(Il2CppClass*){};
    Il2CppClass* (*method_owner)(const MethodInfo*){}; const MethodInfo* (*virtual_method)(Il2CppObject*,const MethodInfo*){}; bool (*assignable)(Il2CppClass*,Il2CppClass*){};
    const Il2CppType* (*enum_base)(Il2CppClass*){};
    FieldInfo* (*field_named)(Il2CppClass*,const char*){}; Il2CppClass* (*field_owner)(FieldInfo*){};
    const Il2CppType* (*field_type)(FieldInfo*){}; int (*field_flags)(FieldInfo*){}; void (*field_value)(Il2CppObject*,FieldInfo*,void*){};
    void* (*pin)(Il2CppObject*,bool){}; Il2CppObject* (*target)(void*){}; void (*unpin)(void*){};
    struct Root {
        Impl* p{}; void* h{};
        Root()=default;
        Root(Impl& x,Il2CppObject* o):p(&x),h(o?x.pin(o,true):nullptr){if(o&&(!h||!get())){reset();throw std::runtime_error("Unable to retain managed object");}}
        Root(Root&& x) noexcept:p(x.p),h(x.h){x.h=nullptr;}
        Root& operator=(Root&& x) noexcept {if(this!=&x){reset();p=x.p;h=x.h;x.h=nullptr;}return *this;}
        Root(const Root&)=delete; Root& operator=(const Root&)=delete;
        ~Root(){reset();} void reset(){if(h){p->unpin(h);h=nullptr;}} Il2CppObject* get() const{return h?p->target(h):nullptr;}
    };
    struct Card { UpgradeChoiceCard public_card; Root child,card,data; };
    struct Offer { std::uint64_t token{}; int id{},level{}; Root manager,stage,page,session,actor,group,items,container; std::vector<Card> cards; };
    std::unique_ptr<Offer> offer; bool invoking=false;
    Il2CppClass *object{},*component{},*gameobject{},*transform{},*manager{},*stage{},*character{},*input{},*page{},*session{},*item{},*data{},*weapon{},*selectable{},*boolean{},*integer{},*systemtype{},*array{},*voidclass{},*playerlist{},*groupclass{},*transformlist{};
    const MethodInfo *find{},*arrayget{},*running{},*stageget{},*players{},*listcount{},*listitem{},*dead{},*level{},*inputget{},*inputid{},*alive{},*gameobjectget{},*active{},*enabled{},*groupcount{},*groupitem{},*childcount{},*childget{},*getcomponent{},*animating{},*interactable{},*onclick{};
    FieldInfo *sessionfield{},*actorfield{},*groupfield{},*itemsfield{},*pagefield{},*typefield{},*datafield{},*levelfield{},*newfield{},*limitlessfield{};
    void verify(){MainThreadIdentity id;std::string e;if(!guard.verify(os,managed,id,e))throw std::runtime_error(e);}
    std::string typenameof(const Il2CppType* t){if(!t)return {};char* n=a.type_get_name(t);std::string s=n?n:"";if(n)a.free(n);return s;}
    Il2CppClass* klass(const char* img,const char* ns,const char* name){
        std::size_t n=0;auto as=a.domain_get_assemblies(domain,&n);if(!as)throw std::runtime_error("Assembly list unavailable");Il2CppClass* found=nullptr;
        for(std::size_t i=0;i<n;++i){auto im=a.assembly_get_image(as[i]);if(!im||!equal(a.image_get_name(im),img))continue;for(std::size_t j=0;j<a.image_get_class_count(im);++j){auto c=const_cast<Il2CppClass*>(a.image_get_class(im,j));if(c&&equal(a.class_get_namespace(c),ns)&&equal(a.class_get_name(c),name)){if(found)throw std::runtime_error("Ambiguous class");found=c;}}}
        if(!found)throw std::runtime_error(std::string("Missing class ")+img+":"+ns+"."+name);return found;
    }
    bool imageis(Il2CppClass* c,const char* name){return c&&equal(a.image_get_name(image(c)),name);}
    const MethodInfo* method(Il2CppClass* owner,const char* name,Il2CppClass* ret,Il2CppClass* arg=nullptr,bool stat=false){
        const MethodInfo* found=nullptr;void* it=nullptr;
        while(auto m=a.class_get_methods(owner,&it)){if(method_owner(m)!=owner||!equal(a.method_get_name(m),name)||a.method_get_param_count(m)!=(arg?1u:0u)||from_type(a.method_get_return_type(m))!=ret)continue;std::uint32_t impl=0;auto f=a.method_get_flags(m,&impl);if((f&7)!=6||((f&16)!=0)!=stat||(arg&&from_type(a.method_get_param(m,0))!=arg))continue;if(found)throw std::runtime_error(std::string("Ambiguous method ")+name);found=m;}
        if(!found)throw std::runtime_error(std::string("Missing exact method ")+name);return found;
    }
    FieldInfo* field(Il2CppClass* owner,const char* name,Il2CppClass* type,int visibility=-1){auto f=field_named(owner,name);if(!f||field_owner(f)!=owner||(field_flags(f)&(16|64))||(visibility>=0&&(field_flags(f)&7)!=visibility)||from_type(field_type(f))!=type)throw std::runtime_error(std::string("Invalid field ")+name);return f;}
    void require(Il2CppObject* o,Il2CppClass* c){if(!o||!c||!assignable(c,a.object_get_class(o)))throw std::runtime_error("Invalid managed object identity/type");}
    Il2CppObject* invoke(const MethodInfo* m,Il2CppObject* obj=nullptr,void** args=nullptr){std::uint32_t impl=0;if(obj&&(a.method_get_flags(m,&impl)&64u)){m=virtual_method(obj,m);if(!m)throw std::runtime_error("Virtual method resolution failed");}auto r=a.invoke(m,obj,args);if(r.exception)throw std::runtime_error(std::string("Managed exception in ")+a.method_get_name(m));return r.value;}
    template<class T> T scalar(Il2CppObject* o,Il2CppClass* cls){Root r(*this,o);if(!r.get()||a.object_get_class(r.get())!=cls)throw std::runtime_error("Invalid boxed scalar");auto v=a.object_unbox(r.get());if(!v)throw std::runtime_error("Unable to unbox scalar");T out{};std::memcpy(&out,v,sizeof out);return out;}
    bool flag(const MethodInfo* m,Il2CppObject* o=nullptr,void** args=nullptr){auto b=scalar<std::uint8_t>(invoke(m,o,args),boolean);if(b>1)throw std::runtime_error("Invalid Boolean");return b!=0;}
    int number(const MethodInfo* m,Il2CppObject* o,void** args=nullptr){return scalar<std::int32_t>(invoke(m,o,args),integer);}
    Root ref(Il2CppObject* o,FieldInfo* f,Il2CppClass* cls){require(o,field_owner(f));Il2CppObject* raw=nullptr;field_value(o,f,&raw);Root r(*this,raw);if(raw)require(r.get(),cls);return r;}
    bool live(Il2CppObject* o){if(!o)return false;void* args[]={o};return flag(alive,nullptr,args);}
    bool active_component(Il2CppObject* o){if(!live(o))return false;require(o,component);Root go(*this,invoke(gameobjectget,o));require(go.get(),gameobject);return live(go.get())&&flag(active,go.get());}
    Root findone(Il2CppClass* cls){Root t(*this,a.type_get_object(a.class_get_type(cls)));require(t.get(),systemtype);void* args[]={t.get()};Root arr(*this,invoke(find,nullptr,args));if(!arr.get())throw std::runtime_error("Object lookup returned null");auto el=element(a.object_get_class(arr.get()));if(!el||(!assignable(cls,el)&&!assignable(el,cls)))throw std::runtime_error("Unexpected lookup array");auto n=a.array_length(reinterpret_cast<Il2CppArray*>(arr.get()));if(n==0)return {};if(n!=1)throw std::runtime_error("Ambiguous scene object");int zero=0;void* index[]={&zero};Root r(*this,invoke(arrayget,arr.get(),index));require(r.get(),cls);return r;}
    void bind(){
#define LOAD(v,n) v=reinterpret_cast<decltype(v)>(GetProcAddress(module,n));if(!v)throw std::runtime_error("Missing export " n)
        LOAD(parent,"il2cpp_class_get_parent");LOAD(from_type,"il2cpp_class_from_type");LOAD(image,"il2cpp_class_get_image");LOAD(element,"il2cpp_class_get_element_class");LOAD(method_owner,"il2cpp_method_get_class");LOAD(virtual_method,"il2cpp_object_get_virtual_method");LOAD(assignable,"il2cpp_class_is_assignable_from");LOAD(enum_base,"il2cpp_class_enum_basetype");LOAD(field_named,"il2cpp_class_get_field_from_name");LOAD(field_owner,"il2cpp_field_get_parent");LOAD(field_type,"il2cpp_field_get_type");LOAD(field_flags,"il2cpp_field_get_flags");LOAD(field_value,"il2cpp_field_get_value");LOAD(pin,"il2cpp_gchandle_new");LOAD(target,"il2cpp_gchandle_get_target");LOAD(unpin,"il2cpp_gchandle_free");
#undef LOAD
        std::string e;if(!MainThreadGuard::create(a,domain,guard,e))throw std::runtime_error(e);verify();
        const char* core="UnityEngine.CoreModule.dll";const char* game="VampireSurvivors.Runtime.dll";const char* sys="mscorlib.dll";
        object=klass(core,"UnityEngine","Object");component=klass(core,"UnityEngine","Component");gameobject=klass(core,"UnityEngine","GameObject");transform=klass(core,"UnityEngine","Transform");
        manager=klass(game,"VampireSurvivors.Framework","GameManager");stage=klass(game,"VampireSurvivors.Objects","Stage");character=klass(game,"VampireSurvivors.Objects.Characters","CharacterController");input=klass("Rewired_Core.dll","Rewired","Player");page=klass(game,"Wilson.UI","WSLevelUpPage");session=klass(game,"VampireSurvivors.Framework","GameSessionData");item=klass(game,"Wilson.UI","WSLevelUpItemUI");data=klass(game,"VampireSurvivors.Data.Weapons","WeaponData");weapon=klass(game,"VampireSurvivors.Data","WeaponType");selectable=klass("UnityEngine.UI.dll","UnityEngine.UI","Selectable");
        boolean=klass(sys,"System","Boolean");integer=klass(sys,"System","Int32");systemtype=klass(sys,"System","Type");array=klass(sys,"System","Array");voidclass=klass(sys,"System","Void");auto enumclass=klass(sys,"System","Enum");
        if(parent(item)!=selectable||!assignable(component,page)||!assignable(component,transform)||!assignable(component,item)||parent(weapon)!=enumclass||from_type(enum_base(weapon))!=integer||typenameof(enum_base(weapon))!="System.Int32")throw std::runtime_error("Invalid canonical inheritance or enum");
        auto objarray=from_type(a.class_get_type(object)); // Obtain array class from the exact FindObjectsOfType return below.
        (void)objarray;
        const MethodInfo* finder=nullptr;void* it=nullptr;
        while(auto m=a.class_get_methods(object,&it)){if(method_owner(m)!=object||!equal(a.method_get_name(m),"FindObjectsOfType")||a.method_get_param_count(m)!=1||from_type(a.method_get_param(m,0))!=systemtype||typenameof(a.method_get_return_type(m))!="UnityEngine.Object[]")continue;std::uint32_t impl=0;auto flags=a.method_get_flags(m,&impl);auto arr=from_type(a.method_get_return_type(m));if((flags&7)!=6||!(flags&16)||!arr||element(arr)!=object)continue;if(finder)throw std::runtime_error("Ambiguous object finder");finder=m;}
        if(!finder)throw std::runtime_error("Missing exact object finder");find=finder;arrayget=method(array,"GetValue",klass(sys,"System","Object"),integer);
        running=method(manager,"get_IsGameRunning",boolean);stageget=method(manager,"get_Stage",stage);
        it=nullptr;while(auto m=a.class_get_methods(manager,&it)){if(method_owner(m)!=manager||!equal(a.method_get_name(m),"get_MainPlayers")||a.method_get_param_count(m)||typenameof(a.method_get_return_type(m))!="System.Collections.Generic.List<VampireSurvivors.Objects.Characters.CharacterController>")continue;std::uint32_t x=0;auto f=a.method_get_flags(m,&x);if((f&7)!=6||(f&16))continue;if(players)throw std::runtime_error("Ambiguous player getter");players=m;}
        if(!players)throw std::runtime_error("Missing MainPlayers getter");playerlist=from_type(a.method_get_return_type(players));if(!imageis(playerlist,sys)||!equal(a.class_get_namespace(playerlist),"System.Collections.Generic")||!equal(a.class_get_name(playerlist),"List`1"))throw std::runtime_error("Invalid player list");listcount=method(playerlist,"get_Count",integer);listitem=method(playerlist,"get_Item",character,integer);
        dead=method(character,"get_IsDead",boolean);level=method(character,"get_Level",integer);inputget=method(character,"get_PlayerInput",input);inputid=method(input,"get_id",integer);
        alive=method(object,"op_Implicit",boolean,object,true);gameobjectget=method(component,"get_gameObject",gameobject);active=method(gameobject,"get_activeInHierarchy",boolean);auto behaviour=klass(core,"UnityEngine","Behaviour");if(!assignable(behaviour,item)||!assignable(behaviour,page))throw std::runtime_error("Page or card is not a Behaviour");enabled=method(behaviour,"get_isActiveAndEnabled",boolean);
        sessionfield=field(page,"_gameSession",session,1);actorfield=field(session,"<ActiveCharacter>k__BackingField",character,1);
        auto gf=field_named(page,"_ChoiceContainerGroup");groupclass=gf?from_type(field_type(gf)):nullptr;if(!groupclass||!imageis(groupclass,game)||!equal(a.class_get_namespace(groupclass),"Wilson.UI")||!equal(a.class_get_name(groupclass),"UIGroup`1")||typenameof(field_type(gf))!="Wilson.UI.UIGroup<UnityEngine.Transform>")throw std::runtime_error("Invalid group type");groupfield=field(page,"_ChoiceContainerGroup",groupclass);
        auto fi=field_named(groupclass,"items");transformlist=fi?from_type(field_type(fi)):nullptr;if(!transformlist||!imageis(transformlist,sys)||!equal(a.class_get_namespace(transformlist),"System.Collections.Generic")||!equal(a.class_get_name(transformlist),"List`1")||typenameof(field_type(fi))!="System.Collections.Generic.List<UnityEngine.Transform>")throw std::runtime_error("Invalid group items type");itemsfield=field(groupclass,"items",transformlist);
        groupcount=method(transformlist,"get_Count",integer);groupitem=method(transformlist,"get_Item",transform,integer);childcount=method(transform,"get_childCount",integer);childget=method(transform,"GetChild",transform,integer);getcomponent=method(component,"GetComponent",component,systemtype);
        pagefield=field(item,"_page",page,1);typefield=field(item,"_type",weapon,1);datafield=field(item,"_data",data,1);levelfield=field(item,"_currentLevel",integer,1);newfield=field(item,"_isNew",boolean,1);
        limitlessfield=field(item,"_isLimitless",boolean,1);
        animating=method(page,"get_AreChoicesAnimating",boolean);interactable=method(selectable,"IsInteractable",boolean);onclick=method(item,"OnClick",voidclass);
    }
    std::unique_ptr<Offer> observe(int id){
        if(id<0)throw std::runtime_error("Invalid input ID");auto out=std::make_unique<Offer>();out->id=id;
        out->manager=findone(manager);if(!out->manager.get())return {};if(!live(out->manager.get())||!flag(running,out->manager.get()))return {};
        out->stage=Root(*this,invoke(stageget,out->manager.get()));if(!out->stage.get())return {};require(out->stage.get(),stage);
        out->page=findone(page);if(!out->page.get()||!active_component(out->page.get())||!flag(enabled,out->page.get()))return {};if(flag(animating,out->page.get()))return {};
        out->session=ref(out->page.get(),sessionfield,session);if(!out->session.get())return {};out->actor=ref(out->session.get(),actorfield,character);if(!out->actor.get()||flag(dead,out->actor.get()))return {};
        Root list(*this,invoke(players,out->manager.get()));if(!list.get())return {};if(a.object_get_class(list.get())!=playerlist)throw std::runtime_error("Invalid MainPlayers collection");int count=number(listcount,list.get());if(count<=0)return {};if(count>4)throw std::runtime_error("MainPlayers exceeds bound");int matches=0;std::vector<Root> actors;std::vector<int> ids;
        for(int i=0;i<count;++i){void* idx[]={&i};Root actor(*this,invoke(listitem,list.get(),idx));require(actor.get(),character);for(auto& previous:actors)if(previous.get()==actor.get())throw std::runtime_error("Duplicate actor");Root inp(*this,invoke(inputget,actor.get()));require(inp.get(),input);int actual=number(inputid,inp.get());if(actual<0)throw std::runtime_error("Invalid actor input ID");for(int prior:ids)if(prior==actual)throw std::runtime_error("Duplicate actor input ID");ids.push_back(actual);if(actor.get()==out->actor.get()&&actual==id)++matches;actors.push_back(std::move(actor));}
        if(matches!=1)return {};out->level=number(level,out->actor.get());
        out->group=ref(out->page.get(),groupfield,groupclass);if(!out->group.get())return {};out->items=ref(out->group.get(),itemsfield,transformlist);if(!out->items.get())return {};if(a.object_get_class(out->items.get())!=transformlist)throw std::runtime_error("Invalid container list");int groups=number(groupcount,out->items.get());if(groups<0||groups>8)throw std::runtime_error("Container count outside bound");
        for(int i=0;i<groups;++i){void* idx[]={&i};Root c(*this,invoke(groupitem,out->items.get(),idx));if(c.get()){require(c.get(),transform);if(active_component(c.get())){out->container=std::move(c);break;}}}if(!out->container.get())return {};
        int n=number(childcount,out->container.get());if(n<0||n>32)throw std::runtime_error("Child count outside bound");Root type(*this,a.type_get_object(a.class_get_type(item)));require(type.get(),systemtype);
        for(int i=0;i<n;++i){void* idx[]={&i};Root child(*this,invoke(childget,out->container.get(),idx));require(child.get(),transform);if(!active_component(child.get()))continue;void* args[]={type.get()};Root card(*this,invoke(getcomponent,child.get(),args));if(!card.get())continue;require(card.get(),item);if(!active_component(card.get())||!flag(enabled,card.get()))continue;Root owner=ref(card.get(),pagefield,page);if(owner.get()!=out->page.get())throw std::runtime_error("Card page identity mismatch");Card c;c.public_card.child_index=i;field_value(card.get(),typefield,&c.public_card.weapon_type);field_value(card.get(),levelfield,&c.public_card.current_level);std::uint8_t isnew=0,limitless=0;field_value(card.get(),newfield,&isnew);field_value(card.get(),limitlessfield,&limitless);if(isnew>1||limitless>1)throw std::runtime_error("Invalid item mode Boolean");if(limitless||c.public_card.weapon_type<=0)continue;c.public_card.is_new=isnew!=0;c.public_card.interactable=flag(interactable,card.get());c.data=ref(card.get(),datafield,data);if(!c.data.get())continue;c.child=std::move(child);c.card=std::move(card);out->cards.push_back(std::move(c));}
        return out;
    }
};
UpgradeChoiceController::UpgradeChoiceController()=default;
UpgradeChoiceController::~UpgradeChoiceController(){if(impl_&&impl_->offer)impl_.release();}
bool UpgradeChoiceController::initialize(HMODULE module,Il2CppApi api,Il2CppDomain* domain,DWORD os,std::int32_t managed,std::string& error){error.clear();try{if(impl_&&impl_->offer)throw std::runtime_error("Close outstanding offer before initialization");if(!module||!domain||!os||!api.thread_current||!api.thread_current()||!api.domain_get_assemblies||!api.assembly_get_image||!api.image_get_name||!api.image_get_class_count||!api.image_get_class||!api.class_get_name||!api.class_get_namespace||!api.class_get_methods||!api.method_get_name||!api.method_get_param_count||!api.method_get_param||!api.method_get_return_type||!api.method_get_flags||!api.type_get_name||!api.free||!api.class_get_type||!api.type_get_object||!api.array_length||!api.runtime_invoke||!api.object_get_class||!api.object_unbox)throw std::runtime_error("Incomplete runtime API or thread not attached");auto p=std::make_unique<Impl>();p->a=api;p->module=module;p->domain=domain;p->os=os;p->managed=managed;p->bind();impl_=std::move(p);return true;}catch(const std::exception& e){error=e.what();return false;}catch(...){error="Unknown initialization failure";return false;}}
PlayerReadStatus UpgradeChoiceController::capture(std::int32_t id,UpgradeChoiceOffer& output,std::string& error){error.clear();try{if(!impl_)throw std::runtime_error("Controller not initialized");impl_->verify();if(impl_->invoking)throw std::runtime_error("Selection callback is active");auto captured=impl_->observe(id);if(!captured){if(impl_->offer)impl_->offer->token=0;return PlayerReadStatus::NotReady;}UpgradeChoiceOffer result;result.input_id=id;result.actor_level=captured->level;for(auto& c:captured->cards)result.cards.push_back(c.public_card);captured->token=token();result.token=captured->token;impl_->offer=std::move(captured);output=std::move(result);return PlayerReadStatus::Captured;}catch(const std::exception& e){error=e.what();return PlayerReadStatus::Error;}catch(...){error="Unknown capture failure";return PlayerReadStatus::Error;}}
bool UpgradeChoiceController::select(std::uint64_t t,std::int32_t id,std::int32_t index,std::int32_t type,UpgradeChoiceResult& result,std::string& error){result={};error.clear();bool validated_token=false;try{if(!impl_)throw std::runtime_error("Controller not initialized");auto& p=*impl_;p.verify();if(p.invoking)throw std::runtime_error("Selection callback is active");if(!t||!p.offer||p.offer->token!=t||p.offer->id!=id)throw std::runtime_error("Stale or invalid offer token");validated_token=true;auto current=p.observe(id);if(!current)throw std::runtime_error("Offer is no longer ready");auto& old=*p.offer;if(current->manager.get()!=old.manager.get()||current->stage.get()!=old.stage.get()||current->page.get()!=old.page.get()||current->session.get()!=old.session.get()||current->actor.get()!=old.actor.get()||current->group.get()!=old.group.get()||current->items.get()!=old.items.get()||current->container.get()!=old.container.get()||current->level!=old.level)throw std::runtime_error("Offer scene identity changed");Impl::Card* chosen=nullptr;for(auto& c:old.cards)if(c.public_card.child_index==index)chosen=&c;if(type<=0||!chosen||chosen->public_card.weapon_type!=type||!chosen->public_card.interactable)throw std::runtime_error("Invalid or disabled choice");Impl::Card* now=nullptr;for(auto& c:current->cards)if(c.public_card.child_index==index)now=&c;if(!now||now->child.get()!=chosen->child.get()||now->card.get()!=chosen->card.get()||now->data.get()!=chosen->data.get()||now->public_card.weapon_type!=type||now->public_card.current_level!=chosen->public_card.current_level||now->public_card.is_new!=chosen->public_card.is_new||!now->public_card.interactable)throw std::runtime_error("Choice identity or state changed");old.token=0;{struct CallbackScope { bool& active; explicit CallbackScope(bool& v):active(v){active=true;} ~CallbackScope(){active=false;} } scope(p.invoking);result.invocation_attempted=true;p.invoke(p.onclick,chosen->card.get());result.invocation_returned=true;}p.offer.reset();return true;}catch(const std::exception& e){if(validated_token&&impl_&&impl_->offer)impl_->offer->token=0;error=e.what();return false;}catch(...){if(validated_token&&impl_&&impl_->offer)impl_->offer->token=0;error="Unknown selection failure";return false;}}
bool UpgradeChoiceController::close(std::string& error){error.clear();try{if(!impl_)return true;impl_->verify();if(impl_->invoking)throw std::runtime_error("Selection callback is active");impl_->offer.reset();impl_.reset();return true;}catch(const std::exception& e){error=e.what();return false;}catch(...){error="Unknown close failure";return false;}}
}
