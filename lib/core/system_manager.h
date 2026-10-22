#pragma once
#include "system_base.h"
#include "core/scene.h"
#include "frame_graph.h"
#include "cstdmf/profiler.h"

#include <algorithm>
#include <atomic>
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
            requireIdle("add");
            if (std::find(_configuration.app.begin(), _configuration.app.end(), name) != _configuration.app.end()) return;
            if (!ensureSystem(name)) return;
            _configuration.app.insert(_configuration.app.begin() + (std::min)(pos, _configuration.app.size()), name);
            refresh();
        }
        void addEditor(const std::string& name, EditorSystemScene scene = EditorSystemScene::Editor)
        {
            requireIdle("addEditor");
            if (editorConfiguration(name)) return;
            if (!ensureSystem(name)) return;
            _configuration.editor.push_back({ name, scene });
            refresh();
        }
        void remove(const std::string& name)
        {
            requireIdle("remove");
            auto it = std::find_if(_systems.begin(), _systems.end(), [&](const auto& entry) { return entry.name == name; });
            if (it == _systems.end()) return;
            it->system->detach();
            if (_initialized) it->system->shutdown();
            _systems.erase(it);
            std::erase(_configuration.app, name);
            std::erase_if(_configuration.editor, [&](const auto& entry) { return entry.name == name; });
            refresh();
        }
        void reserve(size_t capacity) { requireIdle("reserve"); _systems.reserve(capacity); rebuildSchedule(); }
        const auto& list() const noexcept { return _schedule; }
        const SystemConfiguration& configuration() const noexcept { return _configuration; }
        void move(size_t src, size_t dst) { _deferredMove = { src, dst }; }

        void bind(SceneContext editor, SceneContext active, bool playing)
        {
            requireIdle("bind");
            _editor.emplace(editor); _active.emplace(active); _playing = playing;
            refresh();
        }
        void unbind()
        {
            requireIdle("unbind");
            for (auto it = _systems.rbegin(); it != _systems.rend(); ++it)
            { it->system->detach(); it->instance = 0; }
            _editor.reset(); _active.reset(); invalidateGraph();
        }
        void init()
        {
            requireIdle("init");
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
            requireIdle("shutdown");
            for (auto it = _systems.rbegin(); it != _systems.rend(); ++it)
            {
                it->system->detach();
                if (_initialized) it->system->shutdown();
            }
            _graph.clear(); _inspection = {}; _transactions.clear(); _initialStates.clear(); _backend.reset(); _phases.clear();
            if (_jobs.getNumThreads()) _jobs.fini();
            invalidateGraph();
            _schedule.clear(); _systems.clear(); _configuration = {};
            _deferredMove = { BACK_POS, BACK_POS }; _initialized = false;
        }
        void processMove()
        {
            requireIdle("processMove");
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
        template<class T> void resourceState(std::string state) { requireIdle("resourceState"); _initialStates.insert_or_assign(TypeKey::of<T>(), std::move(state)); invalidateGraph(); }
        void taskBackend(std::shared_ptr<TaskBackend> backend) { requireIdle("taskBackend"); _backend = std::move(backend); invalidateGraph(); }
        const std::shared_ptr<TaskBackend>& taskBackend() const noexcept { return _backend; }
        void phase(PhaseDescription phase) { requireIdle("phase"); _phases.push_back(std::move(phase)); invalidateGraph(); }
        const auto& phases() const { return _phases; }
        // Inspect the last successfully compiled schedule; never rebuild from editor callbacks.
        const FrameGraph::Inspection& executionGraph() const noexcept { return _inspection; }
        bool executionGraphPending() const noexcept { return _dirty; }
        struct GraphPreview {
            FrameGraph::Inspection graph;
            std::string error;
            size_t revision = (std::numeric_limits<size_t>::max)();
        };
        const GraphPreview& previewExecutionGraph(bool playing, bool standalone = false) const {
            auto& preview = standalone ? _standalonePreview : playing ? _gamePreview : _editorPreview;
            if (preview.revision == _graphRevision) return preview;
            preview.graph = {}; preview.error.clear();
            try {
                FrameGraph graph;
                initializeGraph(graph);
                std::vector<const SystemEntry*> schedule;
                const auto names = standalone ? _configuration.app : scheduleNames(playing);
                for (const auto& name : names) schedule.push_back(findSystem(name));
                for (const auto& entry : describeSchedule(schedule)) {
                    graph.task(entry.entry->name, entry.description, []{});
                    if (entry.description.structuralChanges)
                        graph.task(entry.entry->name + "/apply", commitDescription(entry.entry->name, entry.description), []{});
                }
                graph.compile();
                preview.graph = graph.inspection();
            }
            catch (const std::exception& error) { preview.error = error.what(); }
            preview.revision = _graphRevision;
            return preview;
        }
        bool playing() const noexcept { return _playing; }
        cstdmf::multicore::JobSystem& jobs() noexcept { return _jobs; }
        void execute() {
            requireIdle("execute");
            CS_PROFILE_SCOPE("Frame graph");
            processMove();
            if (_dirty) { CS_PROFILE_SCOPE("Frame graph rebuild"); rebuild(); }
            // System lifecycle (attach/detach/init/shutdown) and schedule edits run
            // only between frames; they are rejected while tasks may be running.
            struct Executing {
                std::atomic<bool>& flag;
                explicit Executing(std::atomic<bool>& f) : flag(f) { flag.store(true, std::memory_order_release); }
                ~Executing() { flag.store(false, std::memory_order_release); }
            } guard(_executing);
            try { _graph.execute(_jobs); }
            catch (...) { for (auto& transaction : _transactions) transaction->discard(); throw; }
        }
        bool executing() const noexcept { return _executing.load(std::memory_order_acquire); }
    private:
        void requireIdle(const char* operation) const
        {
            if (executing())
                throw std::logic_error(std::string("SystemManager::") + operation + " is not allowed while the frame graph executes");
        }
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
        const SystemEntry* findSystem(const std::string& name) const {
            const auto it = std::find_if(_systems.begin(), _systems.end(), [&](const auto& entry) { return entry.name == name; });
            return it == _systems.end() ? nullptr : &*it;
        }
        std::vector<std::string> scheduleNames(bool playing) const {
            std::vector<std::string> names;
            if (!playing) {
                for (const auto& entry : _configuration.editor) names.push_back(entry.name);
            } else {
                names = _configuration.app;
                for (const auto& entry : _configuration.editor)
                    if (entry.scene == EditorSystemScene::Editor &&
                        std::find(names.begin(), names.end(), entry.name) == names.end()) names.push_back(entry.name);
            }
            return names;
        }
        void invalidateGraph() { _dirty = true; ++_graphRevision; }
        void rebuildSchedule()
        {
            invalidateGraph();
            _schedule.clear();
            const bool standalone = !_editor && !_active && Services::has<Scene>();
            // Standalone hosts use only the application list, without editor systems.
            const auto names = standalone ? _configuration.app : scheduleNames(_playing);
            for (const auto& name : names) _schedule.push_back(findSystem(name));
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
                    catch (...) { log::exception(std::format("system '{}' attach to scene {}", entry->name, context->instance)); throw; }
                }
            }
        }
        static void initializeSystem(System& system, const std::string& name)
        {
            try { system.init(); }
            catch (...) { log::exception(std::format("system '{}' init", name)); throw; }
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
        void initializeGraph(FrameGraph& graph) const {
            for (const auto& phase : _phases) graph.phase(phase);
            for (const auto& [type, state] : _initialStates) graph.resourceState(type, state);
        }
        struct DescribedSystem { const SystemEntry* entry; TaskDescription description; };
        std::vector<DescribedSystem> describeSchedule(const std::vector<const SystemEntry*>& schedule) const {
            std::vector<DescribedSystem> result;
            std::unordered_map<std::string, bool> structural;
            for (const auto* entry : schedule) {
                if (!entry) throw std::logic_error("Configured system is missing");
                if (!entry->system->scheduled()) continue; // OnceSystem: lifecycle only
                auto description = entry->system->describe();
                structural[entry->name] = description.structuralChanges;
                if (_backend) _backend->describe(description);
                result.push_back({entry, std::move(description)});
            }
            for (auto& entry : result)
                for (auto& name : entry.description.after)
                    if (structural.contains(name) && structural[name]) name += "/apply";
            return result;
        }
        static TaskDescription commitDescription(const std::string& name, const TaskDescription& description) {
            TaskDescription apply;
            apply.phase = description.phase;
            apply.after = {name};
            apply.before = description.before;
            apply.exclusiveScene = true;
            apply.structuralCommitFor = name;
            // Event registration and destruction callbacks have main-thread affinity.
            apply.affinity = TaskAffinity::MainThread;
            return apply;
        }
        void rebuild() {
            _graph.clear();
            _transactions.clear();
            initializeGraph(_graph);
            const std::vector<const SystemEntry*> schedule(_schedule.begin(), _schedule.end());
            for (const auto& described : describeSchedule(schedule)) {
                const auto& entry = *described.entry;
                auto description = described.description;
                auto* scene = entry.system->boundScene();
                if (!scene && Services::has<Scene>()) scene = Services::get<Scene>();
                if (!scene) continue;
                auto transaction = std::make_shared<Scene::Transaction>();
                _transactions.push_back(transaction);
                const bool changes = description.structuralChanges;
                auto system = entry.system;
                // Every task is a profiler mark named like the graph node.
                const auto mark = profiler::name(entry.name);
                _graph.task(entry.name, description, [this, system, scene, transaction, changes, description, mark, name = entry.name] {
                    const profiler::Scope profile(mark);
                    transaction->discard();
                    Scene::TaskScope scope(*scene, changes ? transaction.get() : nullptr, &description);
                    auto invoke = [system, scene, &description] {
                        Services::SceneScope binding(scene, &scene->eventBus());
                        Services::TaskScope resources(description);
                        system->run();
                    };
                    if (!description.gpuAccesses.empty()) {
                        if (!_backend) throw std::logic_error("GPU task has no execution backend");
                        _backend->record(name, invoke);
                    } else invoke();
                });
                if (changes) {
                    auto apply = commitDescription(entry.name, description);
                    const auto applyMark = profiler::name(entry.name + "/apply");
                    _graph.task(entry.name + "/apply", std::move(apply), [scene, transaction, applyMark] {
                        const profiler::Scope profile(applyMark);
                        transaction->apply(*scene);
                    });
                }
            }
            _graph.compile();
            if (_backend) _backend->compile(_graph.orderedTasks());
            _inspection = _graph.inspection();
            _dirty = false;
        }
        FrameGraph _graph;
        FrameGraph::Inspection _inspection;
        std::vector<std::shared_ptr<Scene::Transaction>> _transactions;
        std::shared_ptr<TaskBackend> _backend;
        cstdmf::multicore::JobSystem _jobs;
        std::vector<PhaseDescription> _phases;
        std::unordered_map<TypeKey, std::string> _initialStates;
        bool _dirty = true;
        size_t _graphRevision = 0;
        mutable GraphPreview _editorPreview, _gamePreview, _standalonePreview;

        SystemConfiguration _configuration;
        std::optional<SceneContext> _editor, _active;
        bool _playing{}, _initialized{};
        std::atomic<bool> _executing{false};
        std::pair<size_t, size_t> _deferredMove{ BACK_POS, BACK_POS };
        std::vector<SystemEntry> _systems;
        std::vector<SystemEntry*> _schedule;
    };
}
