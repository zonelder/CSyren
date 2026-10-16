#pragma once
#include "render_queue.h"
#include <string>
#include <unordered_map>
#include <stdexcept>

namespace csyren::render {
namespace details { class FrameGraphBackend; }
// Declare persistent resources/queues during initialization. Logical frame/output
// resources are imported privately by their owning systems.
class GpuResources {
    friend class details::FrameGraphBackend;
public:
    void import(std::string name, details::ComPtr<ID3D12Resource> resource,
        D3D12_RESOURCE_STATES initial, D3D12_RESOURCE_STATES final) {
        writable();
        if (!resource || name.starts_with("frame.")) throw std::invalid_argument("Invalid imported GPU resource");
        if (!_resources.emplace(std::move(name), Resource{std::move(resource), initial, final}).second)
            throw std::invalid_argument("Duplicate imported GPU resource");
    }
    void queue(std::string name, std::shared_ptr<RenderQueue> queue) {
        writable();
        if (!queue || name == "graphics" || queue->type() != D3D12_COMMAND_LIST_TYPE_DIRECT)
            throw std::invalid_argument("Expected a named direct GPU queue");
        if (!_queues.emplace(std::move(name), std::move(queue)).second)
            throw std::invalid_argument("Duplicate GPU queue");
    }
private:
    void writable() const {
        if (_sealed) throw std::logic_error("Declare GPU resources and queues before the frame graph starts");
    }
    struct Resource {
        details::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES initial, final;
        bool initialized = false;
    };
    std::unordered_map<std::string, Resource> _resources;
    std::unordered_map<std::string, std::shared_ptr<RenderQueue>> _queues;
    bool _sealed = false;
};
}
