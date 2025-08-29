#pragma once
#include <string>
#include <functional>

#include "core/entity.h"
#include "core/scene.h"
#include "core/renderer.h"

namespace csyren::core::reflection
{

	
	/**
	 * @struct ComponentMeta
	 * @brief Contain reflection data of components that allow serializer work with component in polymorphic way.
	 */
	struct ComponentMeta
	{
		std::function<void* (Scene&, Entity::ID)> add;
		std::function<bool(Scene&, Entity::ID)> has;

		std::function<void* (Scene&, Entity::ID)> get;

		std::function<void(const void*, json&,json&,render::ResourceManager&)> serialize;

		std::function<void(void*, const json&,json&,render::ResourceManager&)> deserialize;
	};

	/**
	 * @class ComponentRegistry
	 */
	class ComponentRegistry
	{
		template<typename T>
		friend class ComponentRegistrar;

	public:
		using Registries = std::unordered_map<std::string, ComponentMeta>;
		/**
		 * @brief Getting information about component by its registered name;
		 * @param name - registered component name;
		 * @return pointer to ComponentMeta of component;
		 */
		static const ComponentMeta* get(const std::string& name)
		{
			auto it = getRegistry().find(name);
			return (it != getRegistry().end()) ? &it->second : nullptr;
		}

		static const Registries& getAll()
		{
			return getRegistry();
		}

	private:
		template<typename T>
		static void registerComponentImpl(const std::string& name)
		{
			auto& registry = getRegistry();
			if (registry.find(name) != registry.end())
			{
				log::error("ComponentRegistry: receive  attempt to register component of type {},but its already registered.", name);
				return;
			}

			getRegistry()[name] = {
				// add
				[](Scene& scene, Entity::ID entity) -> void* { return &scene.addComponent<T>(entity); },
				// has
				[](Scene& scene, Entity::ID entity) { return scene.hasComponent<T>(entity); },
				// get
				[](Scene& scene, Entity::ID entity) -> void* { return &scene.getComponent<T>(entity); },
				// serialize
				[](const void* comp, json& j) { static_cast<const T*>(comp)->serialize(j); },
				// deserialize
				[](void* comp, const json& j) { static_cast<T*>(comp)->deserialize(j); }
			};
			log::debug("ComponentRegistry: {} registered automatically.", name);
		}
		static Registries& getRegistry()
		{
			static Registries s_componentInfo;
			return s_componentInfo;
		}

	};


	template<class T>
	class ComponentRegistrar
	{
	public:
		ComponentRegistrar(const std::string& name)
		{
			ComponentRegistry::registerComponentImpl<T>(name);
		}
	};



}
