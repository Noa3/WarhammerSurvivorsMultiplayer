#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

struct Il2CppDomain; struct Il2CppThread; struct Il2CppAssembly; struct Il2CppImage;
struct Il2CppClass; struct MethodInfo; struct FieldInfo; struct Il2CppObject;
struct Il2CppType; struct Il2CppArray;

namespace wsm {
// Pointers are borrowed from the loaded runtime. Call only on an IL2CPP-attached
// thread; gameplay methods must additionally be invoked on Unity's main thread.
struct Il2CppApi {
    Il2CppDomain* (*domain_get)() = nullptr;
    Il2CppThread* (*thread_attach)(Il2CppDomain*) = nullptr;
    void (*thread_detach)(Il2CppThread*) = nullptr;
    const Il2CppAssembly** (*domain_get_assemblies)(const Il2CppDomain*, std::size_t*) = nullptr;
    const Il2CppImage* (*assembly_get_image)(const Il2CppAssembly*) = nullptr;
    const char* (*image_get_name)(const Il2CppImage*) = nullptr;
    std::size_t (*image_get_class_count)(const Il2CppImage*) = nullptr;
    const Il2CppClass* (*image_get_class)(const Il2CppImage*, std::size_t) = nullptr;
    const char* (*class_get_name)(Il2CppClass*) = nullptr;
    const char* (*class_get_namespace)(Il2CppClass*) = nullptr;
    const MethodInfo* (*class_get_methods)(Il2CppClass*, void**) = nullptr;
    const char* (*method_get_name)(const MethodInfo*) = nullptr;
    std::uint32_t (*method_get_param_count)(const MethodInfo*) = nullptr;
    FieldInfo* (*class_get_fields)(Il2CppClass*, void**) = nullptr;
    const char* (*field_get_name)(FieldInfo*) = nullptr;
    Il2CppObject* (*runtime_invoke)(const MethodInfo*, void*, void**, Il2CppObject**) = nullptr;
    const Il2CppType* (*class_get_type)(Il2CppClass*) = nullptr;
    Il2CppObject* (*type_get_object)(const Il2CppType*) = nullptr;
    char* (*type_get_name)(const Il2CppType*) = nullptr;
    void (*free)(void*) = nullptr;
    const Il2CppType* (*method_get_param)(const MethodInfo*, std::uint32_t) = nullptr;
    const Il2CppType* (*method_get_return_type)(const MethodInfo*) = nullptr;
    std::uint32_t (*method_get_flags)(const MethodInfo*, std::uint32_t*) = nullptr;
    std::uintptr_t (*array_length)(Il2CppArray*) = nullptr;
    Il2CppThread* (*thread_current)() = nullptr;
    Il2CppClass* (*object_get_class)(Il2CppObject*) = nullptr;
    void* (*object_unbox)(Il2CppObject*) = nullptr;

    // Atomic fail-closed resolution: no partially usable API is published.
    static bool resolve(HMODULE module, Il2CppApi& result, std::string& missing);
    bool resolve_probe(HMODULE module, std::string& missing);
    struct Invocation { Il2CppObject* value; Il2CppObject* exception; };
    [[nodiscard]] Invocation invoke(const MethodInfo* method, void* object, void** args) const;
};
}
