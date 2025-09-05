#pragma once

#include "core/component_registry.h"
#include "core/field_context.h"
#include "core/serialize_common.h"

#include <string>

/* ================================================================== */
/*              Вспомогательные макросы-утилиты                      */
/* ================================================================== */

#define CSYREN_PASTE_IMPL(a, b) a##b
#define CSYREN_PASTE(a, b) CSYREN_PASTE_IMPL(a, b)

#define EXPAND(x) x


/* ================================================================== */
/*          FOR_EACH (16 args max)      */
/* ================================================================== */

#define GET_16TH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, N, ...) N


#define COUNT_ARGS(...) \
    EXPAND(GET_16TH_ARG(__VA_ARGS__, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#define FOR_EACH_0(action, ...)
#define FOR_EACH_1(action, x) action(x)
#define FOR_EACH_2(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_1(action, __VA_ARGS__))
#define FOR_EACH_3(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_2(action, __VA_ARGS__))
#define FOR_EACH_4(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_3(action, __VA_ARGS__))
#define FOR_EACH_5(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_4(action, __VA_ARGS__))
#define FOR_EACH_6(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_5(action, __VA_ARGS__))
#define FOR_EACH_7(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_6(action, __VA_ARGS__))
#define FOR_EACH_8(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_7(action, __VA_ARGS__))
#define FOR_EACH_9(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_8(action, __VA_ARGS__))
#define FOR_EACH_10(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_9(action, __VA_ARGS__))
#define FOR_EACH_11(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_10(action, __VA_ARGS__))
#define FOR_EACH_12(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_11(action, __VA_ARGS__))
#define FOR_EACH_13(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_12(action, __VA_ARGS__))
#define FOR_EACH_14(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_13(action, __VA_ARGS__))
#define FOR_EACH_15(action, x, ...) EXPAND(action(x)) EXPAND(FOR_EACH_14(action, __VA_ARGS__))

#define FOR_EACH(action, ...) \
    EXPAND(CSYREN_PASTE(FOR_EACH_, COUNT_ARGS(__VA_ARGS__))(action, ##__VA_ARGS__))



/* ================================================================== */
/*          Макросы для определения сериализуемых полей              */
/* ================================================================== */

// Нам нужен макрос-обертка, чтобы передать все аргументы в action.
#define SERIALIZE_FIELD_ACTION(field) \
    SERIALIZE_FIELD_IMPL(field)

#define DESERIALIZE_FIELD_ACTION(field) \
    DESERIALIZE_FIELD_IMPL(field)

// Внутренняя реализация, которая получает j, root, rm из "окружения"
#define SERIALIZE_FIELD_IMPL(field) \
    j[#field] = csyren::core::reflection::FieldContext<decltype(field)>{const_cast<decltype(field)&>(field), rm, root};

#define DESERIALIZE_FIELD_IMPL(field) \
    if (j.contains(#field)) { \
        auto ctx = csyren::core::reflection::FieldContext<decltype(field)>{field, rm, root}; \
        j.at(#field).get_to(ctx); \
    }

/* ================================================================== */
/*                  Главный макрос SERIALIZABLE                       */
/* ================================================================== */

#define SERIALIZABLE(Type, ...) \
    friend class csyren::core::reflection::ComponentRegistrar<Type>; \
    \
    inline static const csyren::core::reflection::ComponentRegistrar<Type> CSYREN_PASTE(registrar_, __COUNTER__){#Type}; \
    \
    void serialize(json& j, json& root, csyren::render::ResourceManager& rm) const \
    { \
        FOR_EACH(SERIALIZE_FIELD_ACTION, __VA_ARGS__) \
    } \
    \
    void deserialize(const json& j, json& root, csyren::render::ResourceManager& rm) \
    { \
        FOR_EACH(DESERIALIZE_FIELD_ACTION, __VA_ARGS__) \
    }

namespace csyren::core::events
{
    struct LoadSceneRequest 
    {
        std::string scenePath;
    };


    struct SaveSceneRequest
    {
        std::string filepath;
    };
    struct ReloadSceneRequest{};
}

namespace csyren::core
{

    class Scene;

    struct SceneLoaderRequest
    {
        enum RequestType
        {
            LOAD,
            RELOAD,
            SAVE,
        };
        RequestType type;
        std::string path;
    };

	class Serializer
	{
	public:
        Serializer(Scene& scene, render::ResourceManager& rm);

        bool loadScene(const std::string& filepath);
        bool saveScene(const std::string& filepath);

	private:
        Scene& _scene;
        render::ResourceManager& _resourceManager;
	};
}
