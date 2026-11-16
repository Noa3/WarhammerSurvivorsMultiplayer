#include "wsm/steam_persona_reader.h"
#include "wsm/main_thread_guard.h"
#include <cstring>
#include <utility>
#include <vector>

namespace {
bool eq(const char* a, const char* b) { return a && b && std::strcmp(a,b)==0; }
constexpr int kMaxPersonaUtf16 = 1024;
}
namespace wsm {
struct SteamPersonaReader::Impl {
    Il2CppApi a{}; MainThreadGuard guard; DWORD os=0; std::int32_t managed=0;
    Il2CppClass *boolean=nullptr,*integer=nullptr,*str=nullptr,*user=nullptr;
    const MethodInfo *running=nullptr,*get_user=nullptr,*to_int=nullptr,*persona=nullptr;
    Il2CppClass* (*from_type)(const Il2CppType*)=nullptr;
    void* (*root_new)(Il2CppObject*,bool)=nullptr;
    Il2CppObject* (*root_target)(void*)=nullptr;
    void (*root_free)(void*)=nullptr;
    std::int32_t (*string_length)(Il2CppObject*)=nullptr;
    const wchar_t* (*string_chars)(Il2CppObject*)=nullptr;
    struct Root {
        const Impl& p; void* h;
        Root(const Impl& owner, Il2CppObject* object):p(owner),h(object?p.root_new(object,true):nullptr) {}
        ~Root(){if(h)p.root_free(h);}
        Root(const Root&)=delete; Root& operator=(const Root&)=delete;
        Il2CppObject* get() const {return h?p.root_target(h):nullptr;}
    };
    Il2CppClass* klass(Il2CppDomain* d,const char* image,const char* ns,const char* name) const {
        std::size_t n=0; auto assemblies=a.domain_get_assemblies(d,&n); if(!assemblies)return nullptr;
        Il2CppClass* found=nullptr;
        for(std::size_t i=0;i<n;++i) { auto im=a.assembly_get_image(assemblies[i]); if(!im||!eq(a.image_get_name(im),image))continue;
            for(std::size_t j=0;j<a.image_get_class_count(im);++j){auto c=const_cast<Il2CppClass*>(a.image_get_class(im,j));
                if(c&&eq(a.class_get_namespace(c),ns)&&eq(a.class_get_name(c),name)){if(found)return nullptr;found=c;}}
        } return found;
    }
    const MethodInfo* method(Il2CppClass* c,const char* name,Il2CppClass* result,Il2CppClass* arg=nullptr) const {
        if(!c||!result)return nullptr; const MethodInfo* found=nullptr; void* it=nullptr;
        while(auto m=a.class_get_methods(c,&it)) {
            if(!eq(a.method_get_name(m),name)||a.method_get_param_count(m)!=(arg?1u:0u))continue;
            std::uint32_t impl=0; auto flags=a.method_get_flags(m,&impl);
            if((flags&7u)!=6u || !(flags&16u))continue;
            auto ret=a.method_get_return_type(m); if(!ret||from_type(ret)!=result)continue;
            if(arg){auto param=a.method_get_param(m,0);if(!param||from_type(param)!=arg)continue;}
            if(found)return nullptr;found=m;
        } return found;
    }
    Il2CppObject* call(const MethodInfo* method,void** args,std::string& error) const {
        auto result=a.invoke(method,nullptr,args);
        if(result.exception){error="Steamworks managed getter raised an exception";return nullptr;}
        return result.value;
    }
    template<typename T> bool value(Il2CppObject* raw,Il2CppClass* expected,T& out,std::string& error) const {
        Root root(*this,raw); auto object=root.get();
        if(!object||a.object_get_class(object)!=expected){error="Invalid or unrootable boxed Steamworks value";return false;}
        auto data=a.object_unbox(object);if(!data){error="Unable to unbox Steamworks value";return false;}
        std::memcpy(&out,data,sizeof(out));return true;
    }
};
SteamPersonaReader::SteamPersonaReader()=default;
SteamPersonaReader::~SteamPersonaReader()=default;
bool SteamPersonaReader::initialize(HMODULE module,Il2CppApi api,Il2CppDomain* domain,DWORD os,std::int32_t managed,std::string& error) {
    impl_.reset();error.clear();
    if(!module||!domain||!os||managed<=0||!api.thread_current||!api.thread_current()||!api.domain_get_assemblies||!api.assembly_get_image||!api.image_get_name||!api.image_get_class_count||!api.image_get_class||!api.class_get_name||!api.class_get_namespace||!api.class_get_methods||!api.method_get_name||!api.method_get_param_count||!api.method_get_param||!api.method_get_return_type||!api.method_get_flags||!api.type_get_name||!api.free||!api.runtime_invoke||!api.object_get_class||!api.object_unbox){error="Incomplete IL2CPP API or unattached thread";return false;}
    auto p=std::make_unique<Impl>();p->a=api;p->os=os;p->managed=managed;
#define LOAD(member, name) do { p->member=reinterpret_cast<decltype(p->member)>(GetProcAddress(module,name)); if(!p->member){error=std::string("Missing IL2CPP export ")+name;return false;} } while(false)
    LOAD(from_type,"il2cpp_class_from_type");LOAD(root_new,"il2cpp_gchandle_new");LOAD(root_target,"il2cpp_gchandle_get_target");LOAD(root_free,"il2cpp_gchandle_free");
    LOAD(string_length,"il2cpp_string_length");LOAD(string_chars,"il2cpp_string_chars");
#undef LOAD
    if(!MainThreadGuard::create(api,domain,p->guard,error))return false;
    constexpr const char* steam="com.rlabrecque.steamworks.net.dll";
    auto steamapi=p->klass(domain,steam,"Steamworks","SteamAPI");auto friends=p->klass(domain,steam,"Steamworks","SteamFriends");
    p->user=p->klass(domain,steam,"Steamworks","HSteamUser");p->boolean=p->klass(domain,"mscorlib.dll","System","Boolean");
    p->integer=p->klass(domain,"mscorlib.dll","System","Int32");p->str=p->klass(domain,"mscorlib.dll","System","String");
    p->running=p->method(steamapi,"IsSteamRunning",p->boolean);p->get_user=p->method(steamapi,"GetHSteamUser",p->user);
    p->to_int=p->method(p->user,"op_Explicit",p->integer,p->user);p->persona=p->method(friends,"GetPersonaName",p->str);
    if(!p->running||!p->get_user||!p->to_int||!p->persona){error="Optional Steamworks metadata is missing or ambiguous";return false;}
    impl_=std::move(p);return true;
}
PlayerReadStatus SteamPersonaReader::read(std::string& output,std::string& error) const {
    error.clear();if(!impl_){error="Steam persona reader not initialized";return PlayerReadStatus::Error;}
    const auto& p=*impl_;MainThreadIdentity identity;
    if(!p.guard.verify(p.os,p.managed,identity,error))return PlayerReadStatus::Error;
    auto raw=p.call(p.running,nullptr,error);if(!error.empty())return PlayerReadStatus::Error;
    std::uint8_t running=0;if(!p.value(raw,p.boolean,running,error))return PlayerReadStatus::Error;
    if(running>1){error="Invalid Steam running Boolean";return PlayerReadStatus::Error;}if(!running)return PlayerReadStatus::NotReady;
    raw=p.call(p.get_user,nullptr,error);if(!error.empty())return PlayerReadStatus::Error;
    Impl::Root handle(p,raw);auto boxed=handle.get();
    if(!boxed||p.a.object_get_class(boxed)!=p.user){error="Invalid or unrootable HSteamUser";return PlayerReadStatus::Error;}
    auto data=p.a.object_unbox(boxed);if(!data){error="Unable to unbox HSteamUser";return PlayerReadStatus::Error;}
    void* args[]={data};raw=p.call(p.to_int,args,error);if(!error.empty())return PlayerReadStatus::Error;
    std::int32_t user=0;if(!p.value(raw,p.integer,user,error))return PlayerReadStatus::Error;if(!user)return PlayerReadStatus::NotReady;
    raw=p.call(p.persona,nullptr,error);if(!error.empty())return PlayerReadStatus::Error;if(!raw)return PlayerReadStatus::NotReady;
    Impl::Root name(p,raw);auto text=name.get();
    if(!text||p.a.object_get_class(text)!=p.str){error="Invalid or unrootable persona string";return PlayerReadStatus::Error;}
    const auto length=p.string_length(text);if(length<0||length>kMaxPersonaUtf16){error="Persona string exceeds supported length";return PlayerReadStatus::Error;}
    if(!length)return PlayerReadStatus::NotReady;
    const auto chars=p.string_chars(text);if(!chars){error="Persona string data unavailable";return PlayerReadStatus::Error;}
    // Validate the entire UTF-16 sequence before trimming, including embedded NULs.
    for(int i=0;i<length;++i)if(chars[i]==L'\0'){error="Persona contains a NUL character";return PlayerReadStatus::Error;}
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,chars,length,nullptr,0,nullptr,nullptr);
    if(!bytes){error="Persona contains invalid UTF-16";return PlayerReadStatus::Error;}
    std::vector<WORD> types(static_cast<std::size_t>(length));
    if(!GetStringTypeW(CT_CTYPE1,chars,length,types.data())){error="Unable to classify persona whitespace";return PlayerReadStatus::Error;}
    int first=0,last=length;while(first<last&&(types[first]&C1_SPACE))++first;while(last>first&&(types[last-1]&C1_SPACE))--last;
    if(first==last)return PlayerReadStatus::NotReady;
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,chars+first,last-first,nullptr,0,nullptr,nullptr);
    if(!count){error="Unable to convert persona to UTF-8";return PlayerReadStatus::Error;}
    std::string candidate(static_cast<std::size_t>(count),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,chars+first,last-first,candidate.data(),count,nullptr,nullptr)!=count){error="Unable to convert persona to UTF-8";return PlayerReadStatus::Error;}
    output=std::move(candidate);return PlayerReadStatus::Captured;
}
}
