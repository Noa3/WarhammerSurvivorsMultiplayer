#include "wsm/il2cpp_api.h"

namespace wsm {
bool Il2CppApi::resolve(HMODULE module, Il2CppApi& result, std::string& missing) {
    result = {}; missing.clear();
    Il2CppApi candidate;
    if (!module) { missing = "GameAssembly.dll module handle"; return false; }
#define WSM_RESOLVE(member) do { \
    candidate.member = reinterpret_cast<decltype(candidate.member)>(GetProcAddress(module, "il2cpp_" #member)); \
    if (!candidate.member) { missing = "il2cpp_" #member; return false; } \
} while (false)
    WSM_RESOLVE(domain_get); WSM_RESOLVE(thread_attach); WSM_RESOLVE(thread_detach);
    WSM_RESOLVE(domain_get_assemblies); WSM_RESOLVE(assembly_get_image);
    WSM_RESOLVE(image_get_name); WSM_RESOLVE(image_get_class_count); WSM_RESOLVE(image_get_class);
    WSM_RESOLVE(class_get_name); WSM_RESOLVE(class_get_namespace); WSM_RESOLVE(class_get_methods);
    WSM_RESOLVE(method_get_name); WSM_RESOLVE(method_get_param_count);
    WSM_RESOLVE(class_get_fields); WSM_RESOLVE(field_get_name); WSM_RESOLVE(runtime_invoke);
#undef WSM_RESOLVE
    result = candidate;
    return true;
}
Il2CppApi::Invocation Il2CppApi::invoke(const MethodInfo* method, void* object, void** args) const {
    Il2CppObject* exception = nullptr;
    auto* value = runtime_invoke(method, object, args, &exception);
    return {value, exception};
}
bool Il2CppApi::resolve_probe(HMODULE module, std::string& missing) {
    missing.clear();
#define WSM_EXTRA(member) do { \
    member = reinterpret_cast<decltype(member)>(GetProcAddress(module, "il2cpp_" #member)); \
    if (!member) { missing = "il2cpp_" #member; return false; } \
} while (false)
    WSM_EXTRA(class_get_type); WSM_EXTRA(type_get_object); WSM_EXTRA(type_get_name);
    WSM_EXTRA(free); WSM_EXTRA(method_get_param); WSM_EXTRA(method_get_return_type);
    WSM_EXTRA(method_get_flags); WSM_EXTRA(array_length);
    WSM_EXTRA(thread_current); WSM_EXTRA(object_get_class); WSM_EXTRA(object_unbox);
#undef WSM_EXTRA
    return true;
}
}
