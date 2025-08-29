#pragma once
#include "third_party/json/json.hpp"
#include "core/component_registry.h"
#include "core/field_context.h"

#include <string>

using json = nlohmann::json;


// Соединяет два токена (например, my_func, 1 -> my_func1)
#define CSYREN_PASTE_IMPL(a, b) a##b
#define CSYREN_PASTE(a, b) CSYREN_PASTE_IMPL(a, b)

// --- Макрос-цикл FOR_EACH для обработки переменного числа аргументов ---
// Это сложная, но стандартная техника для работы с __VA_ARGS__
#define CSYREN_FE_0(action, ...)
#define CSYREN_FE_1(action, j, root, rm, x) action(j, root, rm, x)
#define CSYREN_FE_2(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_1(action, j, root, rm, __VA_ARGS__)
#define CSYREN_FE_3(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_2(action, j, root, rm, __VA_ARGS__)
#define CSYREN_FE_4(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_3(action, j, root, rm, __VA_ARGS__)
#define CSYREN_FE_5(action, j, root, rm, x, ...) action(j, root, rm, x) CSYREN_FE_4(action, j, root, rm, __VA_ARGS__)
// ... можно продолжить для большего количества полей ...

#define CSYREN_GET_FE_MACRO(a,b,c,d,e,f,NAME,...) NAME
#define FOR_EACH(action, j, root, rm, ...) \
    CSYREN_GET_FE_MACRO(__VA_ARGS__, CSYREN_FE_5, CSYREN_FE_4, CSYREN_FE_3, CSYREN_FE_2, CSYREN_FE_1, CSYREN_FE_0) \
    (action, j, root, rm, __VA_ARGS__)


/* ================================================================== */
/*          Макросы для определения сериализуемых полей              */
/* ================================================================== */

// Сериализует ОДНО поле, создавая FieldContext
#define SERIALIZE_FIELD(j, root, rm, field) \
    j[#field] = csyren::core::reflection::FieldContext<decltype(field)>{field, rm, root};

// Десериализует ОДНО поле, создавая FieldContext
#define DESERIALIZE_FIELD(j, root, rm, field) \
    if (j.contains(#field)) { \
        auto ctx = csyren::core::reflection::FieldContext<decltype(field)>{field, rm, root}; \
        j.at(#field).get_to(ctx); \
    } /* <-- Скобка была пропущена здесь */


/* ================================================================== */
/*              Главный макрос для использования в компонентах         */
/* ================================================================== */

#define SERIALIZABLE(Type, ...) \
    /* 1. Автоматическая регистрация компонента в ComponentRegistry */ \
    inline static const csyren::core::reflection::ComponentRegistrar<Type> CSYREN_PASTE(registrar_, __COUNTER__){#Type}; \
    \
    /* 2. Генерация метода serialize, который будет вызван из SceneSerializer */ \
    void serialize(json& j, json& root, csyren::render::ResourceManager& rm) const { \
        /* Применяем SERIALIZE_FIELD к каждому аргументу из __VA_ARGS__ */ \
        FOR_EACH(SERIALIZE_FIELD, j, root, rm, __VA_ARGS__) \
    } \
    \
    /* 3. Генерация метода deserialize, который будет вызван из SceneSerializer */ \
    void deserialize(const json& j, json& root, csyren::render::ResourceManager& rm) { \
        /* Применяем DESERIALIZE_FIELD к каждому аргументу из __VA_ARGS__ */ \
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
