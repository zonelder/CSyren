#pragma once
#include <functional>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>
#include <type_traits>
#include <stdexcept>
#include <algorithm>
#include "type_key.h"

// Runtime access validation: undeclared component/resource accesses throw while
// it is on (Debug builds by default). Typed systems (core/typed_system.h) are
// checked by the compiler; validation catches the rest: direct Services::get /
// scene() calls inside execute(). Structural changes outside a transaction are
// rejected in every build, because they would mutate the scene under other tasks.
#ifndef CSYREN_ACCESS_VALIDATION
#  if defined(_DEBUG) || defined(DEBUG)
#    define CSYREN_ACCESS_VALIDATION 1
#  else
#    define CSYREN_ACCESS_VALIDATION 0
#  endif
#endif
#if defined(_MSC_VER)
// core.lib and everything linked with it must agree (Scene's checks are inline).
#  if CSYREN_ACCESS_VALIDATION
#    pragma detect_mismatch("CSYREN_ACCESS_VALIDATION", "1")
#  else
#    pragma detect_mismatch("CSYREN_ACCESS_VALIDATION", "0")
#  endif
#endif

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
    TypeKey type;
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
// constexpr form of a GPU access for typed systems' static `gpu` arrays.
struct GpuUse {
    const char* resource;
    uint32_t state;
    AccessMode mode;
};
// constexpr form of a CPU resource state contract for typed systems' static
// `states` arrays (see readResource/writeResource below):
//   StateUse::read<T>("ready")             requires T in "ready"
//   StateUse::write<T>("ready", "done")    requires "ready", leaves "done"
//   StateUse::write<T>("", "ready")        produces "ready" from any state
struct StateUse {
    TypeKey type;
    AccessMode mode;
    const char* required;
    const char* resulting;
    template<class T> static constexpr StateUse read(const char* state) noexcept
    { return { TypeKey::of<T>(), AccessMode::Read, state, "" }; }
    template<class T> static constexpr StateUse write(const char* required, const char* resulting) noexcept
    { return { TypeKey::of<T>(), AccessMode::Write, required, resulting }; }
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
    std::vector<TypeKey> localResources;
    bool structuralChanges = false;
    std::string structuralCommitFor;
    // Component types this task only adds through its scene transaction (to new or
    // existing entities). Staged values stay invisible to other tasks until the
    // structural commit, so inserts never conflict with live readers or writers.
    std::vector<TypeKey> inserts;
    // Component types used only as presence filters. Presence changes only in a
    // structural commit, so filters never conflict with other tasks.
    std::vector<TypeKey> filters;

    template<class T> TaskDescription& read() { return access<T>(true, AccessMode::Read); }
    template<class T> TaskDescription& write() { return access<T>(true, AccessMode::Write); }
    // Adding components requires a transaction, so insert() also sets structuralChanges.
    template<class T> TaskDescription& insert() {
        structuralChanges = true;
        return unique<T>(inserts, "Duplicate task insert declaration");
    }
    template<class T> TaskDescription& filter() { return unique<T>(filters, "Duplicate task filter declaration"); }
    template<class T> TaskDescription& readResource(std::string state = {}) {
        return access<T>(false, AccessMode::Read, std::move(state));
    }
    template<class T> TaskDescription& writeResource(std::string required = {}, std::string result = {}) {
        return access<T>(false, AccessMode::Write, std::move(required), std::move(result));
    }
private:
    template<class T> TaskDescription& access(bool component, AccessMode mode,
        std::string required = {}, std::string result = {}) {
        const auto type = TypeKey::of<T>();
        for (const auto& a : accesses)
            if (a.type == type && a.component == component)
                throw std::invalid_argument("Duplicate task access declaration");
        accesses.push_back({type, component, mode, std::move(required), std::move(result)});
        return *this;
    }
    template<class T> TaskDescription& unique(std::vector<TypeKey>& list, const char* error) {
        const auto type = TypeKey::of<T>();
        if (std::find(list.begin(), list.end(), type) != list.end()) throw std::invalid_argument(error);
        list.push_back(type);
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
