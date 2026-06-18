#pragma once

#include "core/system_base.h"

#include <vector>
#include <memory>
#include <string_view>

namespace csyren::core
{
	class SystemManager
	{
		struct SystemEntry
		{
			std::shared_ptr<System> system;
			//TODO it may be string_view 
			std::string name;
			int priority;
		};
	public:

		void addSystem(const std::string& name, int priority = 0)
		{
			auto system = details::SystemRegistry::instance().create(name);
			if (!system)
			{
				log::error("SystemManager::failed to create system with name {}. Skip.\n", name);
				return;
			}
			_systems.emplace_back(SystemEntry{ std::move(system),name, priority });
			_sorted = false;
		}

		void removeSystem(const std::string& name)
		{
			auto it = std::find_if(_systems.begin(), _systems.end(), [name](const SystemEntry& entry) {return name == entry.name;});
			if (it == _systems.end())
				return;

			_systems.erase(it);
			_sorted = false;
		}

		const auto& list() const noexcept
		{
			return _systems;
		}

		void sort() 
		{
			if (_sorted) return;
			std::sort(_systems.begin(), _systems.end(),
				[](const auto& a, const auto& b) {
					return a.priority > b.priority;
				});
			_sorted = true;
		}


		void init()
		{
			sort();
			for (auto entry : _systems)
			{
				entry.system->init();
			}
		}

		void shutdown()
		{
			for (auto entry : _systems)
			{
				entry.system->shutdown();
			}

			_systems.clear();
		}

		void update()
		{
			for (auto entry : _systems)
			{
				entry.system->update();
			}
		}

		void onFrame()
		{
			for (auto entry : _systems)
			{
				entry.system->onFrame();
			}
		}
	private:

		std::vector<SystemEntry> _systems;
		bool _sorted{ true };
	};
}

