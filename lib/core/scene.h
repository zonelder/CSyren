#ifndef __CSYREN_SCENE__
#define __CSYREN_SCENE__

#include "family_generator.h"

#include <stdint.h>
#include <limits>
#include <vector>
#include <unordered_map>

#include "cstdmf/page_view.h"


namespace csyren::core::reflection
{
	class ComponentFamilyID {};
	using ComponentFamily = Family<ComponentFamilyID>;
}


namespace csyren::core
{
	class Time
	{
		float deltaTime;
	};

	class Scene;

	class Component
	{
	public:
		using ID = uint32_t;
		virtual ~Component() = default;
		virtual void onCreate(Scene&) {}
		virtual void onDestroy(Scene&){}
		virtual void update(Scene&,Time&){}
	};

	struct Entity
	{
		using ID = uint32_t;
		static constexpr ID invalidID = std::numeric_limits<ID>::max();

		ID id{ 0 };
		ID parent{ invalidID };
		std::vector<ID> childrens;
		std::unordered_map<Component::ID, size_t> components;//component index in pool;

	};

	class Scene
	{
	public:
		explicit Scene(size_t reserveEntities = 0)
		{
			if (reserveEntities)
			{
				
			}
		}

		Entity::ID createEntity(Entity::ID parent = Entity::invalidID)
		{

		}

		void destroyEntity(Entity::ID id)
		{

		}

		template<typename T>
		T* addComponent(Entity::ID id)
		{

		}

		template<typename T>
		void removeComponent(Entity::ID id)
		{

		}

		template<typename T>
		T* getComponent(Entity::ID id)
		{

		}

		void update(Time& time)
		{

		}

	private:

	};
}

#endif;
