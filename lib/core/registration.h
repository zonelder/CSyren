#pragma once
// Registration markers. They do not register anything by themselves: before
// every build tools/csyren_registry.py scans the engine sources and the
// project's source/ folder and writes one registry file
// (build/generated/registry.gen.cpp) that Application runs at startup.
//
// Write a marker in a header, at namespace scope, after the type:
//
//     struct Health { float value = 100.0f; static void describe(); };
//     CS_COMPONENT(Health);
//
//   CS_REFLECT(T)       calls T::describe() (reflected type that is not a component)
//   CS_COMPONENT(T)     T::describe() + component registration under the name "T"
//   CS_SYSTEM(T)        system factory under the name "T" (referenced by .systems files)
//   CS_EDITOR(T, E)     inspector editor E for fields of type T (use an alias for
//                       template types with commas)
//   CS_SERVICE(T)       core::Services entry created after the engine services
//
// Registered names are unqualified type names and must be unique: scene and
// .systems files refer to them. See docs/project_structure.md.
#include <type_traits>

#define CS_REFLECT(Type) \
    static_assert(sizeof(Type) > 0, "CS_REFLECT: " #Type " must be a complete type")
#define CS_COMPONENT(Type) \
    static_assert(sizeof(Type) > 0, "CS_COMPONENT: " #Type " must be a complete type")
#define CS_SYSTEM(Type) \
    static_assert(std::is_base_of_v<::csyren::core::System, Type>, "CS_SYSTEM: " #Type " must derive from core::System")
#define CS_EDITOR(Type, Editor) \
    static_assert(sizeof(Type) > 0 && std::is_base_of_v<::csyren::editor::IEditor, Editor>, \
        "CS_EDITOR: " #Editor " must derive from editor::IEditor")
#define CS_SERVICE(Type) \
    static_assert(std::is_default_constructible_v<Type>, "CS_SERVICE: " #Type " must be default constructible")

namespace csyren::generated
{
    // Defined in the generated registry of the executable.
    void registerTypes();   // CS_REFLECT, CS_COMPONENT, CS_SYSTEM, CS_EDITOR
    void createServices();  // CS_SERVICE
}
