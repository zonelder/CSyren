#pragma once

#include <limits>
#include <vector>
#include <unordered_map>

#include "component_base.h"

namespace csyren::core
{
	struct Entity
	{
		using ID = uint32_t;
		static constexpr ID invalidID = std::numeric_limits<ID>::max();

		ID id{ 0 };
		ID parent{ invalidID };
		std::vector<ID> childrens;
		std::unordered_map<size_t, Component::ID> components;//component index in pool;

	};
}
