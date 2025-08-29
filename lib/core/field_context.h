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
		T& value;// Ссылка на само поле (например, на SpriteRenderer::texture)
		render::ResourceManager& resourceManager;
		nlohmann::json& rootJson; // Для "ленивой" записи ресурсов.
	};
}
