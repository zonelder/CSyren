#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
#include <string>

#include "component_base.h"
#include "cstdmf/sparse_set.h"

namespace csyren::core
{
	struct Entity
	{
		// An ID is a slot (low 32 bits) and the slot's generation (high 32 bits).
		// The generation grows when the slot is freed, so the ID of a destroyed
		// entity never names another one: every lookup compares the whole ID.
		using ID = uint64_t;
		static constexpr ID invalidID = std::numeric_limits<ID>::max();
		static constexpr uint32_t slot(ID id) noexcept { return static_cast<uint32_t>(id); }
		static constexpr uint32_t generation(ID id) noexcept { return static_cast<uint32_t>(id >> 32); }
		static constexpr ID makeID(uint32_t slot, uint32_t generation) noexcept { return (ID(generation) << 32) | slot; }

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

	// Sparse slot of an entity ID, for sets keyed by entity IDs.
	struct EntitySlot
	{
		constexpr uint64_t operator()(Entity::ID id) const noexcept { return Entity::slot(id); }
	};
	template<class T> using EntitySet = cstdmf::SparseSet<T, Entity::ID, EntitySlot>;
}
