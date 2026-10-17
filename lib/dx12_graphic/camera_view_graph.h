#pragma once
#include "core/task_description.h"
#include <unordered_map>

namespace csyren::render::details
{
    inline std::string viewResource(uint64_t id,const std::string& suffix)
    { return "view."+std::to_string(id)+"."+suffix; }
    struct CameraViewBranch { uint64_t id; bool render; };
    struct CameraViewGraph
    {
        std::vector<core::GraphTask> tasks;
        std::unordered_map<std::string,std::vector<std::string>> recordings;
        std::unordered_map<std::string,uint64_t> bindings;
    };
    inline CameraViewGraph expandCameraViews(const std::vector<core::GraphTask>& tasks,const std::vector<CameraViewBranch>& views)
    {
        CameraViewGraph result;
        for (const auto& task : tasks) {
            if (task.description.gpuAccesses.empty()) continue;
            auto& recordings = result.recordings[task.name];
            if (task.description.gpuScope == core::GpuTaskScope::View && !views.empty()) {
                for (const auto& view : views) if (view.render) {
                    auto pass = task; pass.name += "@"+std::to_string(view.id);
                    for (auto& usage : pass.description.gpuAccesses)
                        if (usage.resource.starts_with("scene.")) usage.resource = viewResource(view.id,usage.resource.substr(6));
                    recordings.push_back(pass.name); result.bindings.emplace(pass.name,view.id);
                    result.tasks.push_back(std::move(pass));
                }
            } else {
                auto pass = task; pass.description.gpuAccesses.clear();
                for (auto usage : task.description.gpuAccesses) {
                    if (!usage.resource.starts_with("scene.")) pass.description.gpuAccesses.push_back(usage);
                    else if (views.empty()) {
                        usage.resource = "frame."+usage.resource.substr(6); pass.description.gpuAccesses.push_back(usage);
                    } else {
                        if (usage.mode != core::AccessMode::Read) throw std::invalid_argument("Frame pass cannot write all camera views");
                        for (const auto& view : views) {
                            auto read = usage; read.resource = viewResource(view.id,usage.resource.substr(6));
                            pass.description.gpuAccesses.push_back(std::move(read));
                        }
                    }
                }
                recordings.push_back(pass.name); result.tasks.push_back(std::move(pass));
            }
        }
        return result;
    }
}
