#pragma once

#include "cstdmf/job_system2.hpp"
#include "cstdmf/job_graph.hpp"
#include "task_description.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace csyren::core {

// Compiled once, executed many times. CPU completion never implies GPU completion.
// Build/compile/execute are owned by the scheduler's main thread.
class FrameGraph {
    using JobSystem = cstdmf::multicore::JobSystem;
    using JobHandle = cstdmf::multicore::JobHandle;
    struct Node final : cstdmf::multicore::IJob {
        FrameGraph* owner;
        std::string name;
        TaskDescription description;
        std::function<void()> callback;
        std::vector<size_t> parents;
        void execute(uint32_t, uint32_t, uint32_t) override {
            if (owner->_failed.load(std::memory_order_acquire)) return;
            try { callback(); }
            catch (...) {
                std::lock_guard lock(owner->_failureMutex);
                if (!owner->_failure) owner->_failure = std::current_exception();
                owner->_failed.store(true, std::memory_order_release);
            }
        }
    };
public:
    struct InspectionTask {
        GraphTask task;
        // Indices into the inspection's topologically ordered task list.
        std::vector<size_t> parents;
    };
    struct Inspection {
        std::vector<PhaseDescription> phases;
        std::vector<InspectionTask> tasks;
        // Transitive reduction of the compiled DAG: identical reachability,
        // with edges already implied by a longer path removed for display.
        std::vector<std::vector<size_t>> immediateParents() const {
            std::vector<std::vector<bool>> ancestors(tasks.size(), std::vector<bool>(tasks.size()));
            std::vector<std::vector<size_t>> result(tasks.size());
            for (size_t child = 0; child < tasks.size(); ++child) {
                for (size_t parent : tasks[child].parents) {
                    ancestors[child][parent] = true;
                    for (size_t i = 0; i < tasks.size(); ++i)
                        ancestors[child][i] = ancestors[child][i] || ancestors[parent][i];
                }
                for (size_t parent : tasks[child].parents) {
                    const bool redundant = std::any_of(tasks[child].parents.begin(), tasks[child].parents.end(),
                        [&](size_t other) { return other != parent && ancestors[other][parent]; });
                    if (!redundant) result[child].push_back(parent);
                }
            }
            return result;
        }
    };
    Inspection inspection() const {
        if (!_compiled) throw std::logic_error("Graph is not compiled");
        Inspection result;
        for (size_t index : _phaseOrder) result.phases.push_back(_phases[index]);
        std::vector<size_t> indices(_nodes.size());
        for (size_t i = 0; i < _order.size(); ++i) indices[_order[i]] = i;
        for (size_t index : _order) {
            const auto& node = *_nodes[index];
            InspectionTask entry{{node.name, node.description}, {}};
            for (size_t parent : node.parents) entry.parents.push_back(indices[parent]);
            std::sort(entry.parents.begin(), entry.parents.end());
            result.tasks.push_back(std::move(entry));
        }
        return result;
    }
    void phase(PhaseDescription description) {
        if (description.name.empty()) throw std::invalid_argument("Empty phase name");
        for (const auto& p : _phases)
            if (p.name == description.name) throw std::invalid_argument("Duplicate phase: " + p.name);
        _phases.push_back(std::move(description));
        _compiled = false;
    }
    void task(std::string name, TaskDescription description, std::function<void()> callback) {
        if (name.empty() || !callback) throw std::invalid_argument("Invalid graph task");
        for (const auto& n : _nodes)
            if (n->name == name) throw std::invalid_argument("Duplicate system: " + name);
        auto n = std::make_unique<Node>();
        n->owner = this;
        n->name = std::move(name);
        n->description = std::move(description);
        n->callback = std::move(callback);
        _nodes.push_back(std::move(n));
        _compiled = false;
    }
    template<class T> void resourceState(std::string state) {
        resourceState(std::type_index(typeid(T)), std::move(state));
    }
    void resourceState(std::type_index type, std::string state) {
        _initialStates.insert_or_assign(type, std::move(state));
        _compiled = false;
    }
    void clear() {
        _execution.reset();
        _nodes.clear(); _phases.clear(); _initialStates.clear(); _order.clear(); _phaseOrder.clear(); _compiled = false;
    }
    void compile() {
        _compiled = false;
        const size_t count = _nodes.size();
        std::unordered_map<std::string, size_t> phases, nodes;
        for (size_t i = 0; i < _phases.size(); ++i) phases.emplace(_phases[i].name, i);
        for (size_t i = 0; i < count; ++i) { nodes.emplace(_nodes[i]->name, i); _nodes[i]->parents.clear(); }
        auto lookup = [](const auto& map, const std::string& name, const char* kind) {
            const auto it = map.find(name);
            if (it == map.end()) throw std::invalid_argument(std::string("Unknown ") + kind + ": " + name);
            return it->second;
        };
        std::vector<std::vector<size_t>> pp(_phases.size());
        auto edge = [](auto& parents, size_t child, size_t parent) {
            if (child == parent) throw std::invalid_argument("Self dependency");
            auto& list = parents[child];
            if (std::find(list.begin(), list.end(), parent) == list.end()) list.push_back(parent);
        };
        for (size_t i = 0; i < _phases.size(); ++i) {
            for (const auto& p : _phases[i].after) edge(pp, i, lookup(phases, p, "phase"));
            for (const auto& p : _phases[i].before) edge(pp, lookup(phases, p, "phase"), i);
        }
        const auto phaseOrder = topological(pp, "phase");
        _phaseOrder = phaseOrder;
        // Transitive phase closure also orders tasks across empty phases.
        std::vector<std::vector<bool>> precedes(pp.size(), std::vector<bool>(pp.size()));
        for (size_t child : phaseOrder) for (size_t parent : pp[child]) {
            precedes[parent][child] = true;
            for (size_t ancestor = 0; ancestor < pp.size(); ++ancestor)
                precedes[ancestor][child] = precedes[ancestor][child] || precedes[ancestor][parent];
        }
        std::vector<size_t> np;
        for (const auto& n : _nodes) np.push_back(lookup(phases, n->description.phase, "phase"));
        std::vector<std::vector<size_t>> parents(count);
        for (size_t i = 0; i < count; ++i) {
            const auto& d = _nodes[i]->description;
            for (size_t j = 0; j < count; ++j) if (precedes[np[j]][np[i]]) edge(parents, i, j);
            for (const auto& n : d.after) edge(parents, i, lookup(nodes, n, "system"));
            for (const auto& n : d.before) edge(parents, lookup(nodes, n, "system"), i);
        }
        // State producers are dependencies even when registered after consumers.
        for (size_t i = 0; i < count; ++i) for (const auto& access : _nodes[i]->description.accesses) {
            if (access.component || access.requiredState.empty()) continue;
            auto initial = _initialStates.find(access.type);
            if (initial == _initialStates.end() || initial->second != access.requiredState) {
                std::vector<size_t> producers;
                for (size_t j = 0; j < count; ++j) if (j != i)
                    for (const auto& candidate : _nodes[j]->description.accesses)
                        if (!candidate.component && candidate.type == access.type && candidate.resultingState == access.requiredState)
                            producers.push_back(j);
                if (producers.size() != 1)
                    throw std::invalid_argument("Missing or ambiguous state producer for " + _nodes[i]->name + ": " + access.requiredState);
                edge(parents, i, producers.front());
            }
            if (access.mode == AccessMode::Write && !access.resultingState.empty() && access.resultingState != access.requiredState)
                for (size_t j = 0; j < count; ++j) if (j != i)
                    for (const auto& reader : _nodes[j]->description.accesses)
                        if (!reader.component && reader.type == access.type && reader.mode == AccessMode::Read && reader.requiredState == access.requiredState)
                            edge(parents, i, j);
        }
        // Let disjoint scene jobs finish together before relocating dense storage.
        // A commit must not be moved behind a task that needs its visible result.
        auto reaches = [&](size_t child, size_t parent) {
            std::vector<bool> visited(count);
            std::vector<size_t> pending{child};
            while (!pending.empty()) {
                auto current = pending.back(); pending.pop_back();
                for (auto p : parents[current]) {
                    if (p == parent) return true;
                    if (!visited[p]) { visited[p] = true; pending.push_back(p); }
                }
            }
            return false;
        };
        for (size_t commit = 0; commit < count; ++commit) {
            const auto& owner = _nodes[commit]->description.structuralCommitFor;
            if (owner.empty()) continue;
            const auto producer = lookup(nodes, owner, "commit owner");
            for (size_t other = 0; other < count; ++other) {
                const auto& description = _nodes[other]->description;
                if (other == producer || other == commit || np[other] != np[commit] ||
                    !description.structuralCommitFor.empty() || !sceneAccess(description)) continue;
                if (!conflicts(_nodes[producer]->description, description) && !reaches(other, commit))
                    edge(parents, commit, other);
            }
        }
        // Respect explicit ordering first. Registration order only breaks otherwise
        // unordered hazards; moving a system cannot reverse explicit dependencies.
        _order = topological(parents, "system");
        for (size_t b = 0; b < _order.size(); ++b)
            for (size_t a = 0; a < b; ++a)
                if (conflicts(_nodes[_order[a]]->description, _nodes[_order[b]]->description))
                    edge(parents, _order[b], _order[a]);
        auto states = _initialStates;
        for (size_t i : _order) {
            const auto& n = *_nodes[i];
            for (const auto& a : n.description.accesses) {
                if (a.component) continue;
                if (!a.requiredState.empty()) {
                    auto state = states.find(a.type);
                    if (state == states.end() || state->second != a.requiredState)
                        throw std::invalid_argument("Unsatisfied resource state for " + n.name + ": " + a.requiredState);
                }
                if (!a.resultingState.empty()) states.insert_or_assign(a.type, a.resultingState);
            }
            _nodes[i]->parents = std::move(parents[i]);
        }
        std::vector<cstdmf::multicore::JobGraph::Task> tasks;
        tasks.reserve(_nodes.size());
        for (auto& node : _nodes) tasks.push_back({node.get(), node->parents,
            node->description.affinity == TaskAffinity::MainThread ? 0u : UINT32_MAX});
        _execution = std::make_unique<cstdmf::multicore::JobGraph>(tasks);
        _compiled = true;
    }
    void execute(JobSystem& scheduler) {
        if (!_compiled) compile();
        _failure = nullptr;
        _failed.store(false, std::memory_order_release);
        _execution->execute(scheduler);
        scheduler.clear();
        if (_failure) std::rethrow_exception(_failure);
    }
    std::vector<GraphTask> orderedTasks() const {
        if (!_compiled) throw std::logic_error("Graph is not compiled");
        std::vector<GraphTask> result;
        for (auto index : _order) result.push_back({_nodes[index]->name, _nodes[index]->description});
        return result;
    }
    bool dependsOn(const std::string& child, const std::string& parent) const {
        if (!_compiled) throw std::logic_error("Graph is not compiled");
        size_t ci = _nodes.size(), pi = ci;
        for (size_t i = 0; i < _nodes.size(); ++i) {
            if (_nodes[i]->name == child) ci = i;
            if (_nodes[i]->name == parent) pi = i;
        }
        if (ci == _nodes.size() || pi == _nodes.size()) throw std::invalid_argument("Unknown system");
        std::vector<bool> visited(_nodes.size());
        std::vector<size_t> pending{ci};
        while (!pending.empty()) {
            const auto i = pending.back(); pending.pop_back();
            for (size_t p : _nodes[i]->parents) {
                if (p == pi) return true;
                if (!visited[p]) { visited[p] = true; pending.push_back(p); }
            }
        }
        return false;
    }
private:
    static bool sceneAccess(const TaskDescription& d) {
        return d.exclusiveScene || d.structuralChanges || std::any_of(d.accesses.begin(), d.accesses.end(),
            [](const TaskAccess& access) { return access.component || access.type == std::type_index(typeid(SceneEntities)); });
    }
    static bool conflicts(const TaskDescription& a, const TaskDescription& b) {
        if ((a.dynamicResources && (!b.accesses.empty() || b.dynamicResources || !b.localResources.empty())) ||
            (b.dynamicResources && (!a.accesses.empty() || a.dynamicResources || !a.localResources.empty()))) return true;
        if ((a.exclusiveScene && sceneAccess(b)) || (b.exclusiveScene && sceneAccess(a))) return true;
        for (const auto& x : a.accesses) for (const auto& y : b.accesses)
            if (x.type == y.type && x.component == y.component &&
                (x.mode == AccessMode::Write || y.mode == AccessMode::Write)) return true;
        return false;
    }
    static std::vector<size_t> topological(const std::vector<std::vector<size_t>>& parents, const char* kind) {
        std::vector<size_t> order;
        std::vector<bool> done(parents.size());
        while (order.size() < parents.size()) {
            bool found = false;
            for (size_t i = 0; i < parents.size(); ++i)
                if (!done[i] && std::all_of(parents[i].begin(), parents[i].end(), [&](size_t p) { return done[p]; })) {
                    done[i] = true; order.push_back(i); found = true;
                    break;
                }
            if (!found) throw std::invalid_argument(std::string("Dependency cycle in ") + kind + " graph");
        }
        return order;
    }
    std::vector<PhaseDescription> _phases;
    std::vector<std::unique_ptr<Node>> _nodes;
    std::unordered_map<std::type_index, std::string> _initialStates;
    std::vector<size_t> _order;
    std::vector<size_t> _phaseOrder;
    std::atomic<bool> _failed{false};
    std::mutex _failureMutex;
    std::exception_ptr _failure;
    bool _compiled = false;
    std::unique_ptr<cstdmf::multicore::JobGraph> _execution;
};
}
