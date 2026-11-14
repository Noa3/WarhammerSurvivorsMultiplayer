#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
struct Il2CppDomain; struct Il2CppAssembly; struct Il2CppImage; struct Il2CppClass; struct MethodInfo; struct Il2CppObject;
#define DECL(ret,name,...) static ret (*name)(__VA_ARGS__)=nullptr
DECL(Il2CppDomain*,domain_get); DECL(const Il2CppAssembly**,domain_get_assemblies,const Il2CppDomain*,size_t*); DECL(const Il2CppImage*,assembly_get_image,const Il2CppAssembly*); DECL(Il2CppClass*,class_from_name,const Il2CppImage*,const char*,const char*); DECL(const MethodInfo*,class_get_methods,Il2CppClass*,void**); DECL(const char*,method_get_name,const MethodInfo*); DECL(uint32_t,method_get_param_count,const MethodInfo*); DECL(Il2CppObject*,runtime_invoke,const MethodInfo*,void*,void**,Il2CppObject**); DECL(Il2CppClass*,object_get_class,Il2CppObject*); DECL(void*,object_unbox,Il2CppObject*); DECL(const char*,class_get_name,Il2CppClass*); DECL(const char*,class_get_namespace,Il2CppClass*);
#define LOAD(x) x=reinterpret_cast<decltype(x)>(GetProcAddress(module,"il2cpp_" #x)); if(!x){printf("NATIVE missing export %s\n",#x);return 10;}
static Il2CppClass* find(const char* ns,const char* name){size_t n=0;auto a=domain_get_assemblies(domain_get(),&n);for(size_t i=0;i<n;i++){auto c=class_from_name(assembly_get_image(a[i]),ns,name);if(c)return c;}return nullptr;}
static const MethodInfo* method(Il2CppClass* c,const char* name,int arity,int expected=1){if(!c)return nullptr;void* it=nullptr;const MethodInfo* m;const MethodInfo* found=nullptr;int count=0;while((m=class_get_methods(c,&it)))if(!strcmp(method_get_name(m),name)&&(arity<0||(int)method_get_param_count(m)==arity)){found=m;count++;}printf("NATIVE resolve %s arity=%d matches=%d expected=%d\n",name,arity,count,expected);return count==expected?found:nullptr;}
static Il2CppObject* call(const MethodInfo* m,void* obj=nullptr,void** args=nullptr){if(!m)return nullptr;Il2CppObject* ex=nullptr;auto r=runtime_invoke(m,obj,args,&ex);if(ex){printf("NATIVE exception\n");return nullptr;}return r;}
static bool integer(Il2CppObject* o,int& out){if(!o)return false;auto c=object_get_class(o);if(strcmp(class_get_namespace(c),"System")||strcmp(class_get_name(c),"Int32"))return false;out=*static_cast<int*>(object_unbox(o));return true;}
static bool single(Il2CppObject* o,float& out){if(!o)return false;auto c=object_get_class(o);if(strcmp(class_get_namespace(c),"System")||strcmp(class_get_name(c),"Single"))return false;out=*static_cast<float*>(object_unbox(o));return true;}
extern "C" __declspec(dllexport) int __cdecl fixture_test(int managed,uint32_t os,int mode){
 auto module=GetModuleHandleW(L"GameAssembly.dll");if(!module)return 11;
 LOAD(domain_get); LOAD(domain_get_assemblies);LOAD(assembly_get_image);LOAD(class_from_name);LOAD(class_get_methods);LOAD(method_get_name);LOAD(method_get_param_count);LOAD(runtime_invoke);LOAD(object_get_class);LOAD(object_unbox);LOAD(class_get_name);LOAD(class_get_namespace);
 if(mode==1)return method(find("System.Threading","Thread"),"definitely_missing_method",0)?0:21;
 if(mode==2)return method(find("System.Threading","Thread"),"Sleep",-1)?0:22;
 if(GetCurrentThreadId()!=os){printf("NATIVE os mismatch actual=%lu expected=%u\n",GetCurrentThreadId(),os);return 23;}
 auto sc=find("System.Threading","SynchronizationContext");auto uc=find("UnityEngine","UnitySynchronizationContext");auto thread=find("System.Threading","Thread");
 auto ctx=call(method(sc,"get_Current",0));if(!ctx||object_get_class(ctx)!=uc){printf("NATIVE context null or wrong class\n");return 24;}
 int mainid=0,curid=0;auto mainGetter=method(uc,"get_MainThreadId",0);auto threadobj=call(method(thread,"get_CurrentThread",0));
 if(!integer(call(mainGetter,ctx),mainid)||!integer(call(method(thread,"get_ManagedThreadId",0),threadobj),curid)){printf("NATIVE main/current thread getter unavailable or non-int\n");return 25;}
 printf("NATIVE thread main=%d current=%d expected=%d os=%u\n",mainid,curid,managed,os);if(mainid!=managed||curid!=managed)return 26;
 auto fc=find("","Fixture");auto trans=call(method(fc,"GetFixtureTransform",0));auto list=call(method(fc,"GetFixtureItems",0));if(!trans||!list)return 27;
 auto tc=find("UnityEngine","Transform");auto vector=call(method(tc,"get_position",0),trans);if(!vector)return 28;
 auto vc=object_get_class(vector);if(strcmp(class_get_name(vc),"Vector3")||strcmp(class_get_namespace(vc),"UnityEngine"))return 29;
 auto index=method(vc,"get_Item",1);for(int i=0;i<3;i++){void* args[]={&i};float value=0; if(!single(call(index,object_unbox(vector),args),value)||value!=(i+1)*1.25f){printf("NATIVE vector index=%d value=%f\n",i,value);return 30;}}
 auto lc=object_get_class(list);int count=0;if(!integer(call(method(lc,"get_Count",0),list),count)||count!=23)return 31;auto item=method(lc,"get_Item",1);for(int i=0;i<count&&i<16;i++){void* args[]={&i};auto o=call(item,list,args);if(!o||strcmp(class_get_name(object_get_class(o)),"FixtureItem"))return 32; int value=-1; if(!integer(call(method(object_get_class(o),"get_Value",0),o),value)||value!=i)return 33;}
 printf("NATIVE PASS boxed Vector3=(1.25,2.5,3.75) list actual=%d sampled=16\n",count);fflush(stdout);return 0;
}
