#pragma once
#include "system_base.h"
#include "core/scene.h"
#include "frame_graph.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace csyren::core
{
    // Binding policy belongs to the host configuration, never to System.
    enum class EditorSystemScene { Editor, Active };
    struct EditorSystemConfiguration
    {
        std::string name;
        EditorSystemScene scene = EditorSystemScene::Editor;
    };
    struct SystemConfiguration
    {
        std::vector<std::string> app;
        std::vector<EditorSystemConfiguration> editor;
    };

    class SystemManager
    {
        struct SystemEntry
        {
            std::shared_ptr<System> system;
            std::string name;
            SceneInstanceId instance{};
        };
        static constexpr size_t BACK_POS = (std::numeric_limits<size_t>::max)();
    public:
        // User systems enter the application schedule by default.
        void add(const std::string& name, size_t pos = BACK_POS)
        {
            if (std::find(_configuration.app.begin(), _configuration.app.end(), name) != _configuration.app.end()) return;
            if (!ensureSystem(name)) return;
            _configuration.app.insert(_configuration.app.begin() + (std::min)(pos, _configuration.app.size()), name);
            refresh();
        }
        void addEditor(const std::string& name, EditorSystemScene scene = EditorSystemScene::Editor)
        {
            if (editorConfiguration(name)) return;
            if (!ensureSystem(name)) return;
            _configuration.editor.push_back({ name, scene });
            refresh();
        }
        void remove(const std::string& name)
        {
            auto it = std::find_if(_systems.begin(), _systems.end(), [&](const auto& entry) { return entry.name == name; });
            if (it == _systems.end()) return;
            it->system->detach();
            if (_initialized) it->system->shutdown();
            _systems.erase(it);
            std::erase(_configuration.app, name);
            std::erase_if(_configuration.editor, [&](const auto& entry) { return entry.name == name; });
            refresh();
        }
        void reserve(size_t capacity) { _systems.reserve(capacity); rebuildSchedule(); }
        const auto& list() const noexcept { return _schedule; }
        const SystemConfiguration& configuration() const noexcept { return _configuration; }
        void move(size_t src, size_t dst) { _deferredMove = { src, dst }; }

        void bind(SceneContext editor, SceneContext active, bool playing)
        {
            _editor.emplace(editor); _active.emplace(active); _playing = playing;
            refresh();
        }
        void unbind() noexcept
        {
            for (auto it = _systems.rbegin(); it != _systems.rend(); ++it)
            { it->system->detach(); it->instance = 0; }
            _editor.reset(); _active.reset(); _dirty = true;
        }
        void init()
        {
            if (_initialized) return;
            if (!_jobs.getNumThreads()) {
                const auto available = (std::max)(1u, std::thread::hardware_concurrency());
                _jobs.init((std::min)(8u, available > 4 ? available - 4 : 1u));
            }
            for (auto& entry : _systems) initializeSystem(*entry.system, entry.name);
            _initialized = true;
            refresh();
        }
        void shutdown()
        {
            for (auto it = _systems.rbegin(); it != _systems.rend(); ++it)
            {
                it->system->detach();
                if (_initialized) it->system->shutdown();
            }
            _graph.clear(); _transactions.clear(); _initialStates.clear(); _backend.reset(); _phases.clear();
            if (_jobs.getNumThreads()) _jobs.fini();
            _dirty = true;
            _schedule.clear(); _systems.clear(); _configuration = {};
            _deferredMove = { BACK_POS, BACK_POS }; _initialized = false;
        }
        void processMove()
        {
            auto [src, dst] = _deferredMove;
            _deferredMove = { BACK_POS, BACK_POS };
            if (src < _schedule.size() && dst < _schedule.size() && src != dst)
            {
                const auto source = _schedule[src]->name, destination = _schedule[dst]->name;
                if (!_playing)
                    moveName(_configuration.editor, source, destination, [](const auto& entry) -> const auto& { return entry.name; });
                else
                {
                    // Application and pinned editor schedules remain separate.
                    const bool appSource = std::find(_configuration.app.begin(), _configuration.app.end(), source) != _configuration.app.end();
                    const bool appDestination = std::find(_configuration.app.begin(), _configuration.app.end(), destination) != _configuration.app.end();
                    if (appSource && appDestination)
                        moveName(_configuration.app, source, destination, [](const auto& name) -> const auto& { return name; });
                    else if (!appSource && !appDestination)
                        moveName(_configuration.editor, source, destination, [](const auto& entry) -> const auto& { return entry.name; });
                }
                rebuildSchedule();
            }
        }
        template<class T> void resourceState(std::string state) { _initialStates.insert_or_assign(std::type_index(typeid(T)), std::move(state)); _dirty = true; }
        void taskBackend(std::shared_ptr<TaskBackend> backend) { _backend = std::move(backend); _dirty = true; }
        void phase(PhaseDescription phase) { _phases.push_back(std::move(phase)); _dirty = true; }
        const auto& phases() const { return _phases; }
        void execute() {
            processMove();
            if (_dirty) rebuild();
            try { _graph.execute(_jobs); }
            catch (...) { for (auto& transaction : _transactions) transaction->discard(); throw; }
        }
        void update() { processMove(); for (auto* entry : _schedule) if (entry->system->attached()) entry->system->update(); }
        void onFrame()
        {
            for (auto* entry : _schedule) if (entry->system->attached()) entry->system->onFrame();
        }
    private:
        bool ensureSystem(const std::string& name)
        {
            if (findSystem(name)) return true;
            auto system = details::SystemRegistry::instance().create(name);
            if (!system) return false;
            if (_initialized) initializeSystem(*system, name);
            _systems.push_back({ std::move(system), name });
            return true;
        }
        SystemEntry* findSystem(const std::string& name)
        {
            auto it = std::find_if(_systems.begin(), _systems.end(), [&](const auto& entry) { return entry.name == name; });
            return it == _systems.end() ? nullptr : &*it;
        }
        const EditorSystemConfiguration* editorConfiguration(const std::string& name) const
        {
            auto it = std::find_if(_configuration.editor.begin(), _configuration.editor.end(), [&](const auto& entry) { return entry.name == name; });
            return it == _configuration.editor.end() ? nullptr : &*it;
        }
        void rebuildSchedule()
        {
            _dirty = true;
            _schedule.clear();
            if (!_editor && !_active && Services::has<Scene>()) {
                for (const auto& name : _configuration.app) _schedule.push_back(findSystem(name));
                return;
            }
            if (!_playing)
            {
                for (const auto& entry : _configuration.editor) _schedule.push_back(findSystem(entry.name));
            }
            else
            {
                for (const auto& name : _configuration.app) _schedule.push_back(findSystem(name));
                for (const auto& entry : _configuration.editor)
                    if (entry.scene == EditorSystemScene::Editor &&
                        std::find(_configuration.app.begin(), _configuration.app.end(), entry.name) == _configuration.app.end())
                        _schedule.push_back(findSystem(entry.name));
            }
        }
        void refresh()
        {
            _deferredMove = { BACK_POS, BACK_POS };
            rebuildSchedule();
            if (!_initialized) return;
            if (!_editor && !_active && Services::has<Scene>()) {
                auto* scene = Services::get<Scene>();
                for (auto* entry : _schedule) if (!entry->system->attached()) entry->system->attach({*scene, scene->eventBus(), 0});
                return;
            }
            // Every outgoing consumer is detached before any incoming attach.
            for (auto it = _systems.rbegin(); it != _systems.rend(); ++it)
            {
                const auto* context = desiredContext(*it);
                if (!context || it->instance != context->instance)
                { it->system->detach(); it->instance = 0; }
            }
            for (auto* entry : _schedule)
            {
                const auto* context = desiredContext(*entry);
                if (context && !entry->system->attached())
                {
                    try { entry->system->attach(*context); entry->instance = context->instance; }
                    catch (const std::exception& error)
                    { log::exception(std::format("system '{}' attach to scene {}", entry->name, context->instance), error); throw; }
                    catch (...) { log::error("Non-standard exception attaching system '{}'", entry->name); throw; }
                }
            }
        }
        static void initializeSystem(System& system, const std::string& name)
        {
            try { system.init(); }
            catch (const std::exception& error)
            { log::exception(std::format("system '{}' init", name), error); throw; }
            catch (...) { log::error("Non-standard exception initializing system '{}'", name); throw; }
        }
        const SceneContext* desiredContext(const SystemEntry& entry) const
        {
            const auto* editor = editorConfiguration(entry.name);
            if (editor && editor->scene == EditorSystemScene::Editor) return _editor ? &*_editor : nullptr;
            const bool app = std::find(_configuration.app.begin(), _configuration.app.end(), entry.name) != _configuration.app.end();
            if ((_playing && app) || (!_playing && editor)) return _active ? &*_active : nullptr;
            return nullptr;
        }
        template<class Entries, class Name>
        static void moveName(Entries& entries, const std::string& source, const std::string& destination, Name name)
        {
            auto src = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return name(entry) == source; });
            auto dst = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return name(entry) == destination; });
            if (src == entries.end() || dst == entries.end() || src == dst) return;
            if (src < dst) std::rotate(src, src + 1, dst + 1);
            else std::rotate(dst, src, src + 1);
        }
        void rebuild() {
            _graph.clear();
            _transactions.clear();
            for (const auto& phase : _phases) _graph.phase(phase);
            for (const auto& [type, state] : _initialStates) _graph.resourceState(type, state);
            std::unordered_map<std::string, bool> structural;
            for (const auto* pointer : _schedule) structural[pointer->name] = pointer->system->describe().structuralChanges;
            for (const auto* pointer : _schedule) {
                const auto& entry = *pointer;
                auto description = entry.system->describe();
                if (_backend) _backend->describe(description);
                for (auto& name : description.after)
                    if (structural.contains(name) && structural[name]) name += "/apply";
                auto* scene = entry.system->boundScene();
                if (!scene && Services::has<Scene>()) scene = Services::get<Scene>();
                if (!scene) continue;
                auto transaction = std::make_shared<Scene::Transaction>();
                _transactions.push_back(transaction);
                const bool changes = description.structuralChanges;
                auto system = entry.system;
                _graph.task(entry.name, description, [this, system, scene, transaction, changes, description, name = entry.name] {
                    transaction->discard();
                    Scene::TaskScope scope(*scene, changes ? transaction.get() : nullptr, &description);
                    auto invoke = [system, scene, &description] {
                        Services::SceneScope binding(scene, &scene->eventBus());
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

        SystemConfiguration _configuration;
        std::optional<SceneContext> _editor, _active;
        bool _playing{}, _initialized{};
        std::pair<size_t, size_t> _deferredMove{ BACK_POS, BACK_POS };
        std::vector<SystemEntry> _systems;
        std::vector<SystemEntry*> _schedule;
    };
}
