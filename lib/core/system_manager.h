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
		};
		static constexpr size_t BACK_POS = std::numeric_limits<size_t>::max();
		using movePair = std::pair<size_t, size_t>;

	public:
		void add(const std::string& name, size_t pos = BACK_POS )
		{
			auto system = details::SystemRegistry::instance().create(name);
			if (!system)
			{
				log::error("SystemManager::failed to create system with name {}. Skip.\n", name);
				return;
			}

			if (pos >= _systems.size())
			{
				_systems.emplace_back(SystemEntry{ std::move(system), name });
			}
			else
			{
				_systems.insert(_systems.begin() + pos, SystemEntry{ std::move(system), name });
			}
		}

		void remove(const std::string& name)
		{
			auto it = std::find_if(_systems.begin(), _systems.end(), [name](const SystemEntry& entry) {return name == entry.name;});
			if (it == _systems.end())
				return;

			_systems.erase(it);
		}

		void reserve(size_t capacity)
		{
			_systems.reserve(capacity);
		}

		const auto& list() const noexcept
		{
			return _systems;
		}

		void move(size_t src, size_t dst)
		{
			_deferredMove = { src, dst };
		}



		void init()
		{
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
			processMove();
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
		void processMove()
		{
			auto [src, dst] = _deferredMove;
			if (src >= _systems.size() || dst >= _systems.size() || src == dst)
				return;

			if (src < dst)
			{
				std::rotate(_systems.begin() + src, _systems.begin() + src + 1, _systems.begin() + dst + 1);
			}
			else
			{
				std::rotate(_systems.begin() + dst, _systems.begin() + src, _systems.begin() + src + 1);
			}

			_deferredMove = { SIZE_MAX, SIZE_MAX };
		}
		movePair _deferredMove;

		std::vector<SystemEntry> _systems;
	};
}

