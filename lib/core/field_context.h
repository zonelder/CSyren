#pragma once
#include "third_party/json/json.hpp"

namespace csyren::render { class ResourceManager; }

namespace csyren::core::reflection
{
	/**
	 * @brief Обертка, которая несет в себе значение поля и контекст,
	 * необходимый для его сериализации/десериализации.
	 * @tparam T Тип поля.
	 */
	template<class T>
	struct FieldContext
	{
		T& value;
		render::ResourceManager& resourceManager;
		nlohmann::json& rootJson;
	};
}

namespace nlohmann
{
	//@brief main implementation for field serialization\deserialization;
	template<typename T>
	struct adl_serializer<csyren::core::reflection::FieldContext<T>>
	{
		// to_json  call base to_json implementation for value
		static void to_json(json& j, const csyren::core::reflection::FieldContext<T>& ctx)
		{
			j = ctx.value;
		}

		//from_json  call base from_json implementation for value
		static void from_json(const json& j, csyren::core::reflection::FieldContext<T>& ctx)
		{
			j.get_to(ctx.value);
		}
	};
}


