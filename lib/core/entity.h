#pragma once

#include <algorithm>
#include <limits>
#include <vector>
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
		std::string name;
		bool has(uint64_t comp) const noexcept
		{
			return std::find(componentList.begin(), componentList.end(), comp) != componentList.end();
		}

		void add(uint64_t comp)
		{
			if (has(comp)) return;
			componentList.push_back(comp);
		}

		void remove(uint64_t comp)
		{
			auto it = std::find(componentList.begin(), componentList.end(), comp);
			if (it != componentList.end())
			{
				componentList.erase(it);			}
		}

		const auto& componentView() const noexcept
		{
			return componentList;
		}
	private:
		// Families of the components. An entity has a handful of them, so a linear
		// search is enough and the number of component types is not limited.
		std::vector<uint64_t> componentList;
	};
}
