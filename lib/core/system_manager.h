#pragma once

#include "core/system_base.h"
#include "core/scene.h"
#include "frame_graph.h"

#include <algorithm>
#include <limits>
#include <string>
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
            _dirty = true;
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
            _dirty = true;
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



        template<class T> void resourceState(std::string state) {
            _initialStates.insert_or_assign(std::type_index(typeid(T)), std::move(state)); _dirty = true;
        }
        void taskBackend(std::shared_ptr<TaskBackend> backend) { _backend = std::move(backend); _dirty = true; }
        void phase(PhaseDescription phase) { _phases.push_back(std::move(phase)); _dirty = true; }
        const auto& phases() const { return _phases; }

        void init()
        {
            if (!_jobs.getNumThreads()) {
                // Jolt has four internal workers. Keep a bounded shared CPU budget.
                const auto available = std::max(1u, std::thread::hardware_concurrency());
                _jobs.init(std::min(8u, available > 4 ? available - 4 : 1u));
            }
            _dirty = true;
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

            _graph.clear();
            _transactions.clear();
            _initialStates.clear();
            _backend.reset();
            _systems.clear();
            _phases.clear();
            if (_jobs.getNumThreads()) _jobs.fini();
            _dirty = true;
        }

        void execute() {
            processMove();
            if (_dirty) rebuild();
            try { _graph.execute(_jobs); }
            catch (...) {
                for (auto& transaction : _transactions) transaction->discard();
                throw;
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
            _dirty = true;
        }
        void rebuild() {
            _graph.clear();
            _transactions.clear();
            for (const auto& phase : _phases) _graph.phase(phase);
            for (const auto& [type, state] : _initialStates) _graph.resourceState(type, state);
            std::unordered_map<std::string, bool> structural;
            for (const auto& entry : _systems) structural[entry.name] = entry.system->describe().structuralChanges;
            for (const auto& entry : _systems) {
                auto description = entry.system->describe();
                if (_backend) _backend->describe(description);
                for (auto& name : description.after)
                    if (structural.contains(name) && structural[name]) name += "/apply";
                auto* scene = Services::get<Scene>();
                auto transaction = std::make_shared<Scene::Transaction>();
                _transactions.push_back(transaction);
                const bool changes = description.structuralChanges;
                auto system = entry.system;
                _graph.task(entry.name, description, [this, system, scene, transaction, changes, description, name = entry.name] {
                    transaction->discard();
                    Scene::TaskScope scope(*scene, changes ? transaction.get() : nullptr, &description);
                    auto invoke = [system, &description] {
                        Services::TaskScope resources(description);
                        system->execute();
                    };
                    if (!description.gpuAccesses.empty()) {
                        if (!_backend) throw std::logic_error("GPU task has no execution backend");
                        _backend->record(name, invoke);
                    } else invoke();
                });
                if (changes) {
                    TaskDescription apply;
                    apply.phase = description.phase;
                    apply.after = {entry.name};
                    apply.before = description.before;
                    apply.exclusiveScene = true;
                    apply.structuralCommitFor = entry.name;
                    // Event registration and destruction callbacks have main-thread affinity.
                    apply.affinity = TaskAffinity::MainThread;
                    _graph.task(entry.name + "/apply", std::move(apply), [scene, transaction] { transaction->apply(*scene); });
                }
            }
            _graph.compile();
            if (_backend) _backend->compile(_graph.orderedTasks());
            _dirty = false;
        }
        FrameGraph _graph;
        std::vector<std::shared_ptr<Scene::Transaction>> _transactions;
        std::shared_ptr<TaskBackend> _backend;
        cstdmf::multicore::JobSystem _jobs;
        std::vector<PhaseDescription> _phases;
        std::unordered_map<std::type_index, std::string> _initialStates;
        bool _dirty = true;

		movePair _deferredMove{ SIZE_MAX, SIZE_MAX };

		std::vector<SystemEntry> _systems;
	};
}

