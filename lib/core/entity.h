#pragma once

#include <limits>
#include <vector>
#include <bitset>
#include <string>

#include "component_base.h"

namespace csyren::core
{
	struct Entity
	{
		using ID = uint32_t;
		static constexpr ID invalidID = std::numeric_limits<ID>::max();

		ID id{ 0 };
		Entity::ID parent = Entity::invalidID;
		std::vector<ID> children;
		std::bitset<reflection::MAX_COMPONENT_TYPES> components;
		std::string name;
	};
}
