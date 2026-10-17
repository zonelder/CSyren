#include "pch.h"
#include "frame_graph_backend.h"
#include "presentation_state.h"
#include "descriptor_allocation.h"
#include "gpu_resources.h"
#include "camera_render_target.h"
#include "core/services.h"

namespace csyren::render::details {
FrameGraphBackend::~FrameGraphBackend() {
    for (auto& [name, queue] : _queues) queue->syncAwaitLastComplete();
}
void FrameGraphBackend::queue(std::string name, RenderQueue& value) {
    if (value.type() != D3D12_COMMAND_LIST_TYPE_DIRECT)
        throw std::invalid_argument("FrameGraph currently supports direct GPU queues only");
    _queues.insert_or_assign(std::move(name), &value);
}
RenderQueue& FrameGraphBackend::queueFor(const std::string& name) const {
    auto it = _queues.find(name);
    if (it == _queues.end()) throw std::invalid_argument("Unknown GPU queue: " + name);
    return *it->second;
}
void FrameGraphBackend::describe(core::TaskDescription& description) const {
    if (!description.gpuAccesses.empty()) {
        description.readResource<FrameRecording>("ready");
        description.localResources.emplace_back(typeid(Renderer));
        // The PSO cache is internally synchronized; calls cannot mutate resource contents.
        description.localResources.emplace_back(typeid(PSOFactory));
    }
}
void FrameGraphBackend::compile(const std::vector<core::GraphTask>& tasks) {
    auto& renderer = *core::Services::get<Renderer>();
    _templates = tasks;
    queue("graphics",renderer.queue());
    if (core::Services::has<GpuResources>()) {
        auto& imports = *core::Services::get<GpuResources>();
        for (const auto& [name, importedQueue] : imports._queues) queue(name, *importedQueue);
        imports._sealed = true;
    }
    for (const auto& task : tasks)
        if (!task.description.gpuAccesses.empty()) (void)queueFor(task.description.gpuQueue);
    // Graph edits occur between CPU frames, while previous GPU work can still be in flight.
    for (auto& [name, queue] : _queues) queue->syncAwaitLastComplete();
    if (core::Services::has<GpuResources>()) {
        auto& imports = *core::Services::get<GpuResources>();
        std::vector<core::GpuExecutionPlan::Barrier> initialBarriers;
        for (auto& [name, imported] : imports._resources) {
            if (!imported.initialized && imported.initial != imported.final)
                initialBarriers.push_back({name, static_cast<uint32_t>(imported.initial), static_cast<uint32_t>(imported.final)});
        }
        if (!initialBarriers.empty()) {
            CommandList initialize;
            if (!initialize.init(renderer.device(), D3D12_COMMAND_LIST_TYPE_DIRECT))
                throw std::runtime_error("Failed to initialize imported GPU resources");
            initialize.reset(); barriers(initialize, initialBarriers); initialize.close();
            renderer.queue().addCommands(&initialize); renderer.queue().execute();
            renderer.queue().syncAwaitComplete(initialize.lastFenceValue());
        }
        for (auto& [name, imported] : imports._resources) imported.initialized = true;
    }
    _storage.clear(); _views.clear(); _resources.clear();
}
void FrameGraphBackend::prepareViews() {
    auto& renderer = *core::Services::get<Renderer>();
    std::vector<CameraViewBranch> branches;
    std::vector<core::GpuExecutionPlan::Resource> resources{
        {"frame.color",D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE},
        {"frame.depth",D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_WRITE}};
    _views.clear(); _resources.clear();
    for (const auto& view : renderer.cameraViews()) {
        branches.push_back({view->id,view->render}); _views.emplace(view->id,view);
        const auto color = viewResource(view->id,"color"),depth = viewResource(view->id,"depth");
        resources.push_back({color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE});
        resources.push_back({depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_WRITE});
        _resources.emplace(color,view); _resources.emplace(depth,view);
    }
    if (core::Services::has<GpuResources>())
        for (const auto& [name,imported] : core::Services::get<GpuResources>()->_resources)
            resources.push_back({name,static_cast<uint32_t>(imported.final),static_cast<uint32_t>(imported.final)});
    _expanded = expandCameraViews(_templates,branches);
    _copyOutput = renderer.presentsCameraOutput() && bool(renderer.outputView());
    if (_copyOutput) {
        core::TaskDescription copy;
        copy.gpu(viewResource(renderer.outputView()->id,"color"),D3D12_RESOURCE_STATE_COPY_SOURCE,core::AccessMode::Read);
        copy.gpu("frame.color",D3D12_RESOURCE_STATE_COPY_DEST,core::AccessMode::Write);
        _expanded.tasks.push_back({"$CameraOutput",copy});
    }
    _plan.compile(_expanded.tasks,resources,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    _mutableResources.clear(); _usages.clear();
    for (const auto& task : _expanded.tasks) {
        _usages.emplace(task.name,task.description.gpuAccesses);
        for (const auto& usage : task.description.gpuAccesses)
            if (usage.mode == core::AccessMode::Write) _mutableResources.insert(usage.resource);
    }
    for (const auto& barrier : _plan.finalBarriers()) _mutableResources.insert(barrier.resource);
    std::unordered_set<std::string> alive;
    for (const auto& pass : _plan.passes()) alive.insert(pass.name);
    for (auto it = _storage.begin(); it != _storage.end();) {
        if (alive.contains(it->first)) { ++it; continue; }
        const auto& queue = queueFor(it->second->queue);
        const bool retired = std::all_of(it->second->frames.begin(),it->second->frames.end(),[&](const auto& frame) {
            return queue.isComplete(frame.commands.lastFenceValue());
        });
        // Inactive/removed branches retire asynchronously. Keep only their image.
        if (retired) it = _storage.erase(it); else ++it;
    }
    for (const auto& pass : _plan.passes()) if (!_storage.contains(pass.name)) {
        auto storage = std::make_unique<Storage>();
        storage->queue = pass.queue;
        for (auto& frame : storage->frames) {
            if (!frame.commands.init(renderer.device(),D3D12_COMMAND_LIST_TYPE_DIRECT) ||
                !frame.upload.init(renderer.device(),2*1024*1024,1))
                throw std::runtime_error("Failed to allocate camera pass recording storage");
        }
        _storage.emplace(pass.name,std::move(storage));
    }
}
ID3D12Resource* FrameGraphBackend::resource(const std::string& name) const {
    if (auto found = _resources.find(name); found != _resources.end())
        return name.ends_with(".color") ? found->second->target->color() : found->second->target->depth();
    if (name == "frame.color") return _state.frames[_state.frameIndex].color.Get();
    if (name == "frame.depth") return _state.depth.Get();
    if (core::Services::has<GpuResources>()) {
        const auto& imports = core::Services::get<GpuResources>()->_resources;
        auto found = imports.find(name);
        if (found != imports.end()) return found->second.resource.Get();
    }
    throw std::invalid_argument("Unknown GPU resource: " + name);
}
void FrameGraphBackend::barriers(CommandList& commands, const std::vector<core::GpuExecutionPlan::Barrier>& list) {
    for (const auto& planned : list) {
        D3D12_RESOURCE_BARRIER barrier{};
        if (planned.uav) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource(planned.resource);
        } else {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource(planned.resource);
            barrier.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(planned.before);
            barrier.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(planned.after);
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        commands.resourceBarrier(barrier);
    }
}
void FrameGraphBackend::acquire() {
    // Retire only the reused allocator slot. Each pass retires its own upload storage.
    auto& commands = _state.commands[_state.commandIndex];
    core::Services::get<Renderer>()->queue().syncAwaitComplete(commands.lastFenceValue());
}
void FrameGraphBackend::record(const std::string& task, const std::function<void()>& callback) {
    for (const auto& pass : _expanded.recordings.at(task)) recordPass(pass,callback);
}
void FrameGraphBackend::recordPass(const std::string& task,const std::function<void()>& callback) {
    auto& renderer = *core::Services::get<Renderer>();
    const auto& storage = _storage.at(task);
    auto& data = storage->frames[_state.commandIndex];
    const auto& passes = _plan.passes();
    const auto pass = std::find_if(passes.begin(), passes.end(), [&](const auto& p) { return p.name == task; });
    if (pass == passes.end()) throw std::logic_error("GPU pass missing from compiled graph");
    queueFor(pass->queue).syncAwaitComplete(data.commands.lastFenceValue());
    data.upload.beginFrame(0);
    std::shared_ptr<CameraRenderView> view;
    if (auto bound = _expanded.bindings.find(task); bound != _expanded.bindings.end()) view = _views.at(bound->second);
    data.engine = view ? view->engine : renderer._engineVariableBuffer;
    data.background = view ? view->background : math::Vector4{_state.background[0],_state.background[1],_state.background[2],_state.background[3]};
    data.commands.reset();
    barriers(data.commands, pass->barriers);
    const auto& usages = _usages.at(task);
    const bool color = std::any_of(usages.begin(), usages.end(), [](const auto& u) {
        return u.resource == "frame.color" && u.requiredState == D3D12_RESOURCE_STATE_RENDER_TARGET;
    });
    const bool depth = std::any_of(usages.begin(), usages.end(), [](const auto& u) {
        return u.resource == "frame.depth" && (u.requiredState == D3D12_RESOURCE_STATE_DEPTH_WRITE || u.requiredState == D3D12_RESOURCE_STATE_DEPTH_READ);
    });
    const auto target = view ? view->target : nullptr;
    const bool viewColor = view && std::any_of(usages.begin(),usages.end(),[&](const auto& u) {
        return u.resource == viewResource(view->id,"color") && u.requiredState == D3D12_RESOURCE_STATE_RENDER_TARGET;
    });
    const bool viewDepth = view && std::any_of(usages.begin(),usages.end(),[&](const auto& u) {
        return u.resource == viewResource(view->id,"depth") && (u.requiredState == D3D12_RESOURCE_STATE_DEPTH_WRITE || u.requiredState == D3D12_RESOURCE_STATE_DEPTH_READ);
    });
    data.colorTarget = viewColor ? target->rtv() : color ? _state.frames[_state.frameIndex].rtv.cpu : D3D12_CPU_DESCRIPTOR_HANDLE{};
    data.depthTarget = viewDepth ? target->dsv() : depth ? _state.dsv.cpu : D3D12_CPU_DESCRIPTOR_HANDLE{};
    if (data.colorTarget.ptr) data.commands.setRenderTarget(0,data.colorTarget);
    if (data.depthTarget.ptr) data.commands.setDepthStencil(data.depthTarget);
    if (data.colorTarget.ptr || data.depthTarget.ptr) data.commands.submitRenderTargets();
    const auto width = target ? target->width() : _state.width;
    const auto height = target ? target->height() : _state.height;
    data.commands.setViewport({0,0,float(width),float(height),0,1});
    data.commands.setScissorRect({0,0,LONG(width),LONG(height)});
    ID3D12DescriptorHeap* heaps[]{core::Services::get<DescriptorManager>()->shaderHeap()};
    data.commands.raw()->SetDescriptorHeaps(1, heaps);
    Renderer::RecordingBinding binding{&renderer, &data};
    auto* previous = Renderer::_recording;
    Renderer::_recording = &binding;
    try { callback(); }
    catch (...) { Renderer::_recording = previous; throw; }
    Renderer::_recording = previous;
    data.commands.close();
}
uint64_t FrameGraphBackend::submit() {
    core::Services::TaskScope internal;
    auto& renderer = *core::Services::get<Renderer>();
    if (_copyOutput) recordPass("$CameraOutput",[&] {
        renderer.commandList()->CopyResource(_state.frames[_state.frameIndex].color.Get(),renderer.outputView()->target->color());
    });
    auto& graphics = renderer.queue();
    const auto& passes = _plan.passes();
    std::vector<uint64_t> fences(passes.size());
    RenderQueue* pendingQueue = nullptr;
    std::vector<size_t> pending;
    std::unordered_set<RenderQueue*> frameWaited;
    auto flush = [&] {
        if (!pendingQueue || pending.empty()) return;
        pendingQueue->execute();
        for (auto index : pending)
            fences[index] = _storage.at(passes[index].name)->frames[_state.commandIndex].commands.lastFenceValue();
        pending.clear();
    };
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& pass = passes[i];
        auto& target = queueFor(pass.queue);
        const auto& usages = _usages.at(pass.name);
        const bool frameWait = &target != &graphics && !frameWaited.contains(&target) &&
            std::any_of(usages.begin(), usages.end(), [&](const auto& usage) { return _mutableResources.contains(usage.resource); });
        if (pendingQueue != &target || frameWait || !pass.waits.empty()) flush();
        if (frameWait) {
            target.awaitFence(graphics, _previousFrameFence);
            frameWaited.insert(&target);
        }
        for (auto previous : pass.waits) target.awaitFence(queueFor(passes[previous].queue), fences[previous]);
        pendingQueue = &target;
        target.addCommands(&_storage.at(pass.name)->frames[_state.commandIndex].commands);
        pending.push_back(i);
    }
    if (pendingQueue != &graphics) flush();
    // Wait once for each foreign queue's last batch before exporting final states.
    std::unordered_map<RenderQueue*, uint64_t> foreignFences;
    for (size_t i = 0; i < passes.size(); ++i) {
        auto* queue = &queueFor(passes[i].queue);
        if (queue != &graphics) foreignFences[queue] = std::max(foreignFences[queue], fences[i]);
    }
    // If a graphics batch is already queued, its recorded commands precede the
    // final barriers in the same ExecuteCommandLists. Cross-queue waits may
    // conservatively precede the batch, but do not stall CPU recording.
    for (auto [queue, fence] : foreignFences) graphics.awaitFence(*queue, fence);
    auto& final = _state.commands[_state.commandIndex];
    final.reset(); barriers(final, _plan.finalBarriers()); final.close();
    graphics.addCommands(&final);
    graphics.execute();
    _previousFrameFence = final.lastFenceValue();
    renderer.completeCameraViews();
    return _previousFrameFence;
}
}
