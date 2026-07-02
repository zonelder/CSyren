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
		std::string name;
		bool has(uint64_t comp) const noexcept
		{
			return _components.test(comp);
		}

		void add(uint64_t comp)
		{
			if (_components.test(comp)) return;
			_components[comp] = true;
			componentList.push_back(comp);
		}

		void remove(uint64_t comp)
		{
			_components[comp] = false;
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
		std::bitset<reflection::MAX_COMPONENT_TYPES> _components;
		std::vector<uint64_t> componentList;
	};
}
