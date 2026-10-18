#pragma once
#include "dx_main.h"
#include <string>
#include <unordered_map>
#include <stdexcept>

namespace csyren::render {
namespace details { class FrameGraphBackend; }
// Declare persistent resources during initialization. Logical frame/output
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
private:
    void writable() const {
        if (_sealed) throw std::logic_error("Declare GPU resources before the frame graph starts");
    }
    struct Resource {
        details::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES initial, final;
        bool initialized = false;
    };
    std::unordered_map<std::string, Resource> _resources;
    bool _sealed = false;
};
}
