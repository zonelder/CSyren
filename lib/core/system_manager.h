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

		void init(events::SystemEvent& event)
		{
			for (auto entry : _systems)
			{
				entry.system->init(event);
			}
			_updateSub = event.bus.subscribe<events::UpdateEvent>([&](events::UpdateEvent& event){ this->update(event);});
			_drawSub   = event.bus.subscribe<events::DrawEvent>([&](events::DrawEvent& event) { this->draw(event); });

			sort();
		}

		void shutdown(events::SystemEvent& event)
		{
			for (auto entry : _systems)
			{
				entry.system->shutdown(event);
			}

			_systems.clear();
			event.bus.unsubscribe(_updateSub);
			event.bus.unsubscribe(_drawSub);
		}

	private:
		void update(events::UpdateEvent& event)
		{
			for (auto entry : _systems)
			{
				entry.system->update(event);
			}
		}

		void draw(events::DrawEvent& event)
		{
			for (auto entry : _systems)
			{
				entry.system->draw(event);
			}
		}

		std::vector<SystemEntry> _systems;
		bool _sorted{ true };


		events::SubscriberToken _updateSub;
		events::SubscriberToken _drawSub;

	};
}

