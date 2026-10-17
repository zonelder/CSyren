#pragma once
#include "frame_graph.h"
#include <unordered_set>
#include <optional>

namespace csyren::core {
// Backend-neutral GPU timeline. States are native backend bitmasks. Resource
// identities are logical names resolved to the current frame's physical resources.
class GpuExecutionPlan {
public:
    struct Resource { std::string name; uint32_t initialState; uint32_t finalState; };
    struct Barrier { std::string resource; uint32_t before; uint32_t after; bool uav = false; };
    struct Pass {
        std::string name;
        std::string queue;
        std::vector<Barrier> barriers;
        std::vector<size_t> waits;
    };
    // D3D12 UAV state is supplied by its backend, avoiding a D3D dependency here.
    void compile(const std::vector<GraphTask>& tasks, const std::vector<Resource>& resources, uint32_t uavState) {
        _passes.clear(); _final.clear();
        struct State { uint32_t state; std::vector<size_t> readers; std::optional<size_t> writer; bool write = false; };
        std::unordered_map<std::string, State> states;
        for (const auto& r : resources)
            if (!states.emplace(r.name, State{r.initialState}).second)
                throw std::invalid_argument("Duplicate GPU resource: " + r.name);
        for (const auto& task : tasks) {
            if (task.description.gpuAccesses.empty()) continue;
            Pass pass{task.name, task.description.gpuQueue};
            std::unordered_set<std::string> used;
            const auto index = _passes.size();
            for (const auto& access : task.description.gpuAccesses) {
                if (!used.insert(access.resource).second) throw std::invalid_argument("Duplicate GPU usage: " + access.resource);
                auto it = states.find(access.resource);
                if (it == states.end()) throw std::invalid_argument("Unknown GPU resource: " + access.resource);
                auto& state = it->second;
                const bool transition = state.state != access.requiredState;
                const bool hadUsers = state.writer.has_value() || !state.readers.empty();
                auto wait = [&](size_t previous) {
                    if (_passes[previous].queue != pass.queue && std::find(pass.waits.begin(), pass.waits.end(), previous) == pass.waits.end())
                        pass.waits.push_back(previous);
                };
                if (state.writer) wait(*state.writer);
                if (transition || access.mode == AccessMode::Write) {
                    for (auto reader : state.readers) wait(reader);
                    state.readers.clear();
                    state.writer = index;
                } else state.readers.push_back(index);
                if (transition) pass.barriers.push_back({access.resource, state.state, access.requiredState});
                else if (hadUsers && state.state == uavState && (state.write || access.mode == AccessMode::Write))
                    pass.barriers.push_back({access.resource, state.state, state.state, true});
                state.state = access.requiredState;
                state.write = access.mode == AccessMode::Write;
            }
            _passes.push_back(std::move(pass));
        }
        for (const auto& r : resources) {
            const auto& state = states.at(r.name);
            if (state.state != r.finalState) _final.push_back({r.name, state.state, r.finalState});
        }
    }
    const auto& passes() const { return _passes; }
    const auto& finalBarriers() const { return _final; }
private:
    std::vector<Pass> _passes;
    std::vector<Barrier> _final;
};
}
