#pragma once
#include "third_party/json/json.hpp"
#include "core/component_registry.h"
#include "core/field_context.h"

#include <string>

using json = nlohmann::json;


// ��������� ��� ������ (��������, my_func, 1 -> my_func1)
#define CSYREN_PASTE_IMPL(a, b) a##b
#define CSYREN_PASTE(a, b) CSYREN_PASTE_IMPL(a, b)

// --- ������-���� FOR_EACH ��� ��������� ����������� ����� ���������� ---
// ��� �������, �� ����������� ������� ��� ������ � __VA_ARGS__
#define CSYREN_FE_0(action, ...)
#define CSYREN_FE_1(action, j, root, rm, x) action(j, root, rm, x)
#define CSYREN_FE_2(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_1(action, j, root, rm, __VA_ARGS__)
#define CSYREN_FE_3(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_2(action, j, root, rm, __VA_ARGS__)
#define CSYREN_FE_4(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_3(action, j, root, rm, __VA_ARGS__)
#define CSYREN_FE_5(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_4(action, j, root, rm, __VA_ARGS__)
// ... ����� ���������� ��� �������� ���������� ����� ...

#define CSYREN_GET_FE_MACRO(a,b,c,d,e,f,NAME,...) NAME
#define FOR_EACH(action, j, root, rm, ...) \
    CSYREN_GET_FE_MACRO(__VA_ARGS__, CSYREN_FE_5, CSYREN_FE_4, CSYREN_FE_3, CSYREN_FE_2, CSYREN_FE_1, CSYREN_FE_0) \
    (action, j, root, rm, __VA_ARGS__)


/* ================================================================== */
/*          ������� ��� ����������� ������������� �����              */
/* ================================================================== */

// ����������� ���� ����, �������� FieldContext
#define SERIALIZE_FIELD(j, root, rm, field) \
    j[#field] = csyren::core::reflection::FieldContext<decltype(field)>{field, rm, root};

// ������������� ���� ����, �������� FieldContext
#define DESERIALIZE_FIELD(j, root, rm, field) \
    if (j.contains(#field)) { \
        auto ctx = csyren::core::reflection::FieldContext<decltype(field)>{field, rm, root}; \
        j.at(#field).get_to(ctx); \
    } /* <-- ������ ���� ��������� ����� */


/* ================================================================== */
/*              ������� ������ ��� ������������� � �����������         */
/* ================================================================== */

#define SERIALIZABLE(Type, ...) \
    /* 1. �������������� ����������� ���������� � ComponentRegistry */ \
    inline static const csyren::core::reflection::ComponentRegistrar<Type> CSYREN_PASTE(registrar_, __COUNTER__){#Type}; \
    \
    /* 2. ��������� ������ serialize, ������� ����� ������ �� SceneSerializer */ \
    void serialize(json& j, json& root, csyren::render::ResourceManager& rm) const { \
        /* ��������� SERIALIZE_FIELD � ������� ��������� �� __VA_ARGS__ */ \
        FOR_EACH(SERIALIZE_FIELD, j, root, rm, __VA_ARGS__) \
    } \
    \
    /* 3. ��������� ������ deserialize, ������� ����� ������ �� SceneSerializer */ \
    void deserialize(const json& j, json& root, csyren::render::ResourceManager& rm) { \
        /* ��������� DESERIALIZE_FIELD � ������� ��������� �� __VA_ARGS__ */ \
        FOR_EACH(DESERIALIZE_FIELD, j, root, rm, __VA_ARGS__) \
    }



namespace csyren::core
{

    class Scene;

	class Serializer
	{
	public:
        Serializer(Scene& scene, render::ResourceManager& rm);

        bool loadScene(const std::string& filepath);
        bool saveScene(const std::string& filepath);

        bool loadComponent(const json& componentData);
        bool saveComponent(json& componentData);

        bool loadEntity(const json& entityData);
        bool saveEntity(json& entityData);
	private:
        Scene& _scene;
        render::ResourceManager& _resourceManager;
	};
}
