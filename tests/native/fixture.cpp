#include "wsm/il2cpp_api.h"
#define EXPORT extern "C" __declspec(dllexport)
EXPORT Il2CppDomain* il2cpp_domain_get() { return nullptr; }
EXPORT Il2CppThread* il2cpp_thread_attach(Il2CppDomain*) { return nullptr; }
EXPORT void il2cpp_thread_detach(Il2CppThread*) {}
EXPORT const Il2CppAssembly** il2cpp_domain_get_assemblies(const Il2CppDomain*, std::size_t*) { return nullptr; }
EXPORT const Il2CppImage* il2cpp_assembly_get_image(const Il2CppAssembly*) { return nullptr; }
EXPORT const char* il2cpp_image_get_name(const Il2CppImage*) { return "fixture"; }
EXPORT std::size_t il2cpp_image_get_class_count(const Il2CppImage*) { return 0; }
EXPORT const Il2CppClass* il2cpp_image_get_class(const Il2CppImage*, std::size_t) { return nullptr; }
EXPORT const char* il2cpp_class_get_name(Il2CppClass*) { return "fixture"; }
EXPORT const char* il2cpp_class_get_namespace(Il2CppClass*) { return "fixture"; }
EXPORT const MethodInfo* il2cpp_class_get_methods(Il2CppClass*, void**) { return nullptr; }
EXPORT const char* il2cpp_method_get_name(const MethodInfo*) { return "fixture"; }
EXPORT std::uint32_t il2cpp_method_get_param_count(const MethodInfo*) { return 0; }
EXPORT FieldInfo* il2cpp_class_get_fields(Il2CppClass*, void**) { return nullptr; }
EXPORT const char* il2cpp_field_get_name(FieldInfo*) { return "fixture"; }
EXPORT Il2CppObject* il2cpp_runtime_invoke(const MethodInfo*, void*, void**, Il2CppObject** exception) {
    *exception = reinterpret_cast<Il2CppObject*>(0x1234);
    return nullptr;
}
EXPORT const Il2CppType* il2cpp_class_get_type(Il2CppClass*) { return nullptr; }
EXPORT Il2CppObject* il2cpp_type_get_object(const Il2CppType*) { return nullptr; }
EXPORT char* il2cpp_type_get_name(const Il2CppType*) { return nullptr; }
EXPORT void il2cpp_free(void*) {}
EXPORT const Il2CppType* il2cpp_method_get_param(const MethodInfo*, std::uint32_t) { return nullptr; }
EXPORT const Il2CppType* il2cpp_method_get_return_type(const MethodInfo*) { return nullptr; }
EXPORT std::uint32_t il2cpp_method_get_flags(const MethodInfo*, std::uint32_t*) { return 0; }
EXPORT std::uintptr_t il2cpp_array_length(Il2CppArray*) { return 0; }
EXPORT Il2CppThread* il2cpp_thread_current() { return nullptr; }
EXPORT Il2CppClass* il2cpp_object_get_class(Il2CppObject*) { return nullptr; }
EXPORT void* il2cpp_object_unbox(Il2CppObject*) { return nullptr; }
