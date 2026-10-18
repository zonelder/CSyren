#pragma once
#include "core/gpu_execution_plan.h"
#include "renderer.h"
#include "camera_view_graph.h"
#include <array>
#include <unordered_map>
namespace csyren::render::details {
struct PresentationState;
struct FrameRecording {};
// Private backend: systems declare logical GPU usages, never receive frame/output ownership.
class FrameGraphBackend final : public core::TaskBackend {
public:
    FrameGraphBackend(PresentationState& state, RenderQueue& graphics) : _state(state), _graphics(graphics) {}
    ~FrameGraphBackend() override;
    void describe(core::TaskDescription& description) const override;
    void compile(const std::vector<core::GraphTask>& tasks) override;
    void record(const std::string& task, const std::function<void()>& callback) override;
    void acquire();
    void prepareViews();
    uint64_t submit();
    void queue(std::string name, std::shared_ptr<RenderQueue> queue);
private:
    using RecordingData = Renderer::RecordingData;
    struct Storage { std::string queue; std::array<RecordingData, Renderer::FramesInFlight> frames; };
    ID3D12Resource* resource(const std::string& name) const;
    void barriers(CommandList& commands, const std::vector<core::GpuExecutionPlan::Barrier>& barriers);
    RenderQueue& queueFor(const std::string& name) const;
    void recordPass(const std::string& task,const std::function<void()>& callback);
    std::vector<core::GraphTask> _templates;
    CameraViewGraph _expanded;
    std::unordered_map<uint64_t,std::shared_ptr<CameraRenderView>> _views;
    std::unordered_map<std::string,std::shared_ptr<CameraRenderView>> _resources;
    bool _copyOutput = false;
    PresentationState& _state;
    core::GpuExecutionPlan _plan;
    uint64_t _previousFrameFence = 0;
    std::unordered_set<std::string> _mutableResources;
    std::unordered_map<std::string, std::vector<core::GpuAccess>> _usages;
    std::unordered_map<std::string, std::unique_ptr<Storage>> _storage;
    RenderQueue& _graphics;
    std::unordered_map<std::string, std::shared_ptr<RenderQueue>> _queues;
    bool _compiled = false;
};
}
