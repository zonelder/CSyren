#pragma once
#include "third_party/json/json.hpp"

namespace csyren::render { class ResourceManager; }


namespace csyren::core::reflection
{
	/**
	 * @brief �������, ������� ����� � ���� �������� ���� � ��������,
	 * ����������� ��� ��� ������������/��������������.
	 * @tparam T ��� ����.
	 */
	template<class T>
	struct FieldContext
	{
		T& value;// ������ �� ���� ���� (��������, �� SpriteRenderer::texture)
		render::ResourceManager& resourceManager;
		nlohmann::json& rootJson; // ��� "�������" ������ ��������.
	};
}
