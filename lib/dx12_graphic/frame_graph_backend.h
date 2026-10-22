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
    uint64_t planRevision() const { return _planRevision; }
private:
    using RecordingData = Renderer::RecordingData;
    struct Storage { std::string queue; std::array<RecordingData, Renderer::FramesInFlight> frames; };
    struct TargetView { D3D12_CPU_DESCRIPTOR_HANDLE handle{}; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; };
    ID3D12Resource* resource(const std::string& name) const;
    TargetView colorTarget(const std::string& name) const;
    TargetView depthTarget(const std::string& name) const;
    void barriers(CommandList& commands, const std::vector<core::GpuExecutionPlan::Barrier>& barriers);
    RenderQueue& queueFor(const std::string& name) const;
    void recordPass(size_t pass,const std::function<void()>& callback);
    void collectRetired();
    struct PassBinding {
        Storage* storage;
        size_t queue;
        std::shared_ptr<CameraRenderView> view;
        // Logical names bound as render targets (slot order = declaration order) and as the depth target.
        std::vector<std::string> colors;
        std::string depth;
        bool frameWait;
        uint32_t mark = 0; // profiler mark of a per-view pass, 0: covered by the task's mark
    };
    std::vector<CameraViewBranch> _branches;
    uint64_t _outputView = 0, _planRevision = 0;
    bool _prepared = false;
    size_t _copyPass = 0;
    std::vector<std::string> _retired;
    std::vector<PassBinding> _passes;
    std::unordered_map<std::string,std::vector<size_t>> _recordings;
    std::vector<RenderQueue*> _usedQueues;
    std::vector<uint64_t> _fences, _queueFences;
    std::vector<bool> _frameWaited;
    std::vector<size_t> _pending;
    std::vector<core::GraphTask> _templates;
    struct ViewResource { std::shared_ptr<CameraRenderView> view; ViewAttachment attachment; };
    std::unordered_map<std::string,ViewResource> _resources;
    bool _copyOutput = false;
    PresentationState& _state;
    core::GpuExecutionPlan _plan;
    uint64_t _previousFrameFence = 0;
    std::unordered_map<std::string, std::unique_ptr<Storage>> _storage;
    RenderQueue& _graphics;
    std::unordered_map<std::string, std::shared_ptr<RenderQueue>> _queues;
    bool _compiled = false;
};
}
