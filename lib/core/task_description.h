#pragma once
#include <functional>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>
#include <typeindex>
#include <type_traits>
#include <stdexcept>

namespace csyren::core {
struct SceneEntities {};

enum class AccessMode { Read, Write };
enum class TaskAffinity { Worker, MainThread };
enum class GpuTaskScope { Frame, View };
struct PhaseDescription {
    std::string name;
    std::vector<std::string> after;
    std::vector<std::string> before;
};
struct TaskAccess {
    std::type_index type;
    bool component;
    AccessMode mode;
    std::string requiredState;
    std::string resultingState;
};
struct GpuAccess {
    std::string resource;
    uint32_t requiredState;
    AccessMode mode;
};
struct TaskDescription {
    std::string phase;
    std::vector<std::string> after;
    std::vector<std::string> before;
    std::vector<TaskAccess> accesses;
    std::vector<GpuAccess> gpuAccesses;
    std::string gpuQueue = "graphics";
    GpuTaskScope gpuScope = GpuTaskScope::Frame;
    TaskDescription& gpu(std::string resource, uint32_t state, AccessMode mode) {
        gpuAccesses.push_back({std::move(resource), state, mode}); return *this;
    }
    TaskAffinity affinity = TaskAffinity::Worker;
    // For reflection/editor code whose component set is only known at runtime.
    bool exclusiveScene = false;
    bool dynamicResources = false;
    std::vector<std::type_index> localResources;
    bool structuralChanges = false;
    std::string structuralCommitFor;

    template<class T> TaskDescription& read() { return access<T>(true, AccessMode::Read); }
    template<class T> TaskDescription& write() { return access<T>(true, AccessMode::Write); }
    template<class T> TaskDescription& readResource(std::string state = {}) {
        return access<T>(false, AccessMode::Read, std::move(state));
    }
    template<class T> TaskDescription& writeResource(std::string required = {}, std::string result = {}) {
        return access<T>(false, AccessMode::Write, std::move(required), std::move(result));
    }
private:
    template<class T> TaskDescription& access(bool component, AccessMode mode,
        std::string required = {}, std::string result = {}) {
        const std::type_index type(typeid(std::remove_cv_t<T>));
        for (const auto& a : accesses)
            if (a.type == type && a.component == component)
                throw std::invalid_argument("Duplicate task access declaration");
        accesses.push_back({type, component, mode, std::move(required), std::move(result)});
        return *this;
    }
};

struct GraphTask { std::string name; TaskDescription description; };
class TaskBackend {
public:
    virtual ~TaskBackend() = default;
    virtual void describe(TaskDescription&) const {}
    virtual void compile(const std::vector<GraphTask>& orderedTasks) = 0;
    virtual void record(const std::string& task, const std::function<void()>& callback) = 0;
};

}
