#pragma once

#include "core/system_base.h"

#include <vector>
#include <memory>

namespace csyren::core
{
	class SystemManager
	{
		struct SystemEntry
		{
			std::shared_ptr<System> system;
			int priority;
		};
	public:

		void addSystem(std::shared_ptr<System> system, int priority = 0) 
		{
			_systems.push_back({ std::move(system), priority });
			_sorted = false;
		}

		void removeSystem(std::shared_ptr<System> system)
		{
			auto it = std::find_if(_systems.begin(), _systems.end(), [system](const SystemEntry& entry) {return system == entry.system;});
			if (it == _systems.end())
				return;

			_systems.erase(it);
			_sorted = false;
		}

		void sort() {
			if (_sorted) return;
			std::sort(_systems.begin(), _systems.end(),
				[](const auto& a, const auto& b) {
					return a.priority > b.priority;
				});
			_sorted = true;
		}

		void init()
		{
			for (auto entry : _systems)
			{
				entry.system->init();
			}
			sort();
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

