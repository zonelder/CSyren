#include "pch.h"
#include "frame_graph_backend.h"
#include "presentation_state.h"
#include "descriptor_allocation.h"
#include "gpu_resources.h"
#include "camera_render_target.h"
#include "core/services.h"

namespace csyren::render::details {
FrameGraphBackend::~FrameGraphBackend() {
    _graphics.syncAwaitLastComplete();
    for (auto& [name, queue] : _queues) queue->syncAwaitLastComplete();
}
void FrameGraphBackend::queue(std::string name, std::shared_ptr<RenderQueue> value) {
    if (_compiled) throw std::logic_error("Declare GPU queues before compiling the frame graph");
    if (!value || name.empty() || name == "graphics" || value->type() != D3D12_COMMAND_LIST_TYPE_DIRECT)
        throw std::invalid_argument("Expected a named direct GPU queue");
    if (!_queues.emplace(std::move(name), std::move(value)).second)
        throw std::invalid_argument("Duplicate GPU queue");
}
RenderQueue& FrameGraphBackend::queueFor(const std::string& name) const {
    if (name == "graphics") return _graphics;
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
    _prepared = false;
    if (core::Services::has<GpuResources>()) {
        auto& imports = *core::Services::get<GpuResources>();
        imports._sealed = true;
    }
    for (const auto& task : tasks)
        if (!task.description.gpuAccesses.empty()) (void)queueFor(task.description.gpuQueue);
    _compiled = true;
    // Graph edits occur between CPU frames, while previous GPU work can still be in flight.
    _graphics.syncAwaitLastComplete();
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
    _passes.clear(); _recordings.clear(); _retired.clear();
    _storage.clear(); _resources.clear();
}
void FrameGraphBackend::prepareViews() {
    auto& renderer = *core::Services::get<Renderer>();
    collectRetired();
    const auto& views = renderer.cameraViews();
    const auto output = renderer.presentsCameraOutput() && renderer.outputView() ? renderer.outputView()->id : 0;
    if (_prepared && output == _outputView && views.size() == _branches.size() &&
        std::equal(views.begin(), views.end(), _branches.begin(), [](const auto& view, const auto& branch) {
            return view->id == branch.id && view->render == branch.render;
        })) return;
    _prepared = false;
    std::vector<CameraViewBranch> branches;
    std::vector<core::GpuExecutionPlan::Resource> resources{
        {"frame.color",D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE},
        {"frame.depth",D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_WRITE}};
    std::unordered_map<uint64_t,std::shared_ptr<CameraRenderView>> viewsById;
    _resources.clear();
    for (const auto& view : renderer.cameraViews()) {
        branches.push_back({view->id,view->render}); viewsById.emplace(view->id,view);
        // Every per-camera attachment is a logical resource resting in its own state.
        for (size_t i = 0; i < size_t(ViewAttachment::Count); ++i) {
            const auto attachment = ViewAttachment(i);
            const auto name = viewResource(view->id,CameraRenderTarget::suffix(attachment));
            const auto resting = static_cast<uint32_t>(CameraRenderTarget::restingState(attachment));
            resources.push_back({name,resting,resting});
            _resources.emplace(name,ViewResource{view,attachment});
        }
    }
    if (core::Services::has<GpuResources>())
        for (const auto& [name,imported] : core::Services::get<GpuResources>()->_resources)
            resources.push_back({name,static_cast<uint32_t>(imported.final),static_cast<uint32_t>(imported.final)});
    auto expanded = expandCameraViews(_templates,branches);
    _copyOutput = renderer.presentsCameraOutput() && bool(renderer.outputView());
    if (_copyOutput) {
        core::TaskDescription copy;
        copy.gpu(viewResource(renderer.outputView()->id,"color"),D3D12_RESOURCE_STATE_COPY_SOURCE,core::AccessMode::Read);
        copy.gpu("frame.color",D3D12_RESOURCE_STATE_COPY_DEST,core::AccessMode::Write);
        expanded.tasks.push_back({"$CameraOutput",copy});
    }
    _plan.compile(expanded.tasks,resources,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::unordered_set<std::string> mutableResources;
    std::unordered_map<std::string,const std::vector<core::GpuAccess>*> usagesByPass;
    for (const auto& task : expanded.tasks) {
        usagesByPass.emplace(task.name,&task.description.gpuAccesses);
        for (const auto& usage : task.description.gpuAccesses)
            if (usage.mode == core::AccessMode::Write) mutableResources.insert(usage.resource);
    }
    for (const auto& barrier : _plan.finalBarriers()) mutableResources.insert(barrier.resource);
    std::unordered_set<std::string> alive;
    for (const auto& pass : _plan.passes()) alive.insert(pass.name);
    _retired.clear();
    for (const auto& [name, storage] : _storage)
        if (!alive.contains(name)) _retired.push_back(name);
    collectRetired();
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
    // Bind names, resource usages and queues once. Frame recording only follows
    // indices; physical targets are still resolved from the current view/mailbox.
    _passes.clear(); _recordings.clear(); _usedQueues = {&_graphics};
    std::unordered_map<std::string,size_t> indices;
    for (const auto& pass : _plan.passes()) {
        const auto index = _passes.size(); indices.emplace(pass.name,index);
        auto* queue = &queueFor(pass.queue);
        auto found = std::find(_usedQueues.begin(),_usedQueues.end(),queue);
        const size_t queueIndex = size_t(found-_usedQueues.begin());
        if (found == _usedQueues.end()) _usedQueues.push_back(queue);
        std::shared_ptr<CameraRenderView> view;
        if (auto binding = expanded.bindings.find(pass.name); binding != expanded.bindings.end()) view = viewsById.at(binding->second);
        const auto& usages = *usagesByPass.at(pass.name);
        PassBinding binding{_storage.at(pass.name).get(),queueIndex,view,{},{},false};
        for (const auto& usage : usages) {
            const bool frameTarget = usage.resource == "frame.color" || usage.resource == "frame.depth";
            const auto viewIt = _resources.find(usage.resource);
            if (!frameTarget && viewIt == _resources.end()) continue; // imported resources are bound by the system
            const bool depthResource = usage.resource == "frame.depth" ||
                (viewIt != _resources.end() && CameraRenderTarget::isDepth(viewIt->second.attachment));
            const bool buffer = viewIt != _resources.end() && CameraRenderTarget::isBuffer(viewIt->second.attachment);
            if (buffer) continue;
            if (depthResource) {
                if (usage.requiredState == D3D12_RESOURCE_STATE_DEPTH_WRITE || usage.requiredState == D3D12_RESOURCE_STATE_DEPTH_READ)
                    binding.depth = usage.resource;
            } else if (usage.requiredState == D3D12_RESOURCE_STATE_RENDER_TARGET) {
                if (binding.colors.size() >= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
                    throw std::invalid_argument("Too many render targets in pass " + pass.name);
                binding.colors.push_back(usage.resource);
            }
        }
        binding.frameWait = queue != &_graphics && std::any_of(usages.begin(),usages.end(),[&](const auto& u) {
            return mutableResources.contains(u.resource);
        });
        _passes.push_back(std::move(binding));
    }
    for (const auto& [name, recordings] : expanded.recordings)
        for (const auto& pass : recordings) _recordings[name].push_back(indices.at(pass));
    // Keep empty entries for Once branches which have no recording this frame.
    for (const auto& [name, recordings] : expanded.recordings) _recordings.try_emplace(name);
    if (_copyOutput) _copyPass = indices.at("$CameraOutput");
    _fences.resize(_passes.size()); _pending.reserve(_passes.size());
    _queueFences.resize(_usedQueues.size()); _frameWaited.resize(_usedQueues.size());
    _branches = std::move(branches); _outputView = output;
    ++_planRevision; _prepared = true;
}
void FrameGraphBackend::collectRetired() {
    for (auto it = _retired.begin(); it != _retired.end();) {
        auto found = _storage.find(*it);
        const auto& storage = *found->second;
        auto& queue = queueFor(storage.queue);
        if (std::all_of(storage.frames.begin(),storage.frames.end(),[&](const auto& frame) {
            return queue.isComplete(frame.commands.lastFenceValue());
        })) { _storage.erase(found); it = _retired.erase(it); }
        else ++it;
    }

}
ID3D12Resource* FrameGraphBackend::resource(const std::string& name) const {
    if (auto found = _resources.find(name); found != _resources.end())
        return found->second.view->target->resource(found->second.attachment);
    if (name == "frame.color") return _state.frames[_state.frameIndex].color.Get();
    if (name == "frame.depth") return _state.depth.Get();
    if (core::Services::has<GpuResources>()) {
        const auto& imports = core::Services::get<GpuResources>()->_resources;
        auto found = imports.find(name);
        if (found != imports.end()) return found->second.resource.Get();
    }
    throw std::invalid_argument("Unknown GPU resource: " + name);
}
FrameGraphBackend::TargetView FrameGraphBackend::colorTarget(const std::string& name) const {
    if (name == "frame.color") return {_state.frames[_state.frameIndex].rtv.cpu,DXGI_FORMAT_R8G8B8A8_UNORM};
    const auto& entry = _resources.at(name);
    return {entry.view->target->rtv(entry.attachment),CameraRenderTarget::targetFormat(entry.attachment)};
}
FrameGraphBackend::TargetView FrameGraphBackend::depthTarget(const std::string& name) const {
    if (name == "frame.depth") return {_state.dsv.cpu,DXGI_FORMAT_D32_FLOAT};
    const auto& entry = _resources.at(name);
    return {entry.view->target->dsv(),CameraRenderTarget::targetFormat(entry.attachment)};
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
    for (auto pass : _recordings.at(task)) recordPass(pass,callback);
}
void FrameGraphBackend::recordPass(size_t index,const std::function<void()>& callback) {
    auto& renderer = *core::Services::get<Renderer>();
    const auto& bindingData = _passes.at(index);
    auto& data = bindingData.storage->frames[_state.commandIndex];
    const auto& pass = _plan.passes()[index];
    _usedQueues[bindingData.queue]->syncAwaitComplete(data.commands.lastFenceValue());
    data.upload.beginFrame(0);
    const auto& view = bindingData.view;
    data.engine = view ? view->engine : renderer._engineVariableBuffer;
    data.background = view ? view->background : math::Vector4{_state.background[0],_state.background[1],_state.background[2],_state.background[3]};
    data.target = view ? view->target.get() : nullptr;
    data.commands.reset();
    barriers(data.commands, pass.barriers);
    const auto target = view ? view->target : nullptr;
    data.colorCount = 0;
    for (auto& handle : data.colorTargets) handle = {};
    for (auto& format : data.colorFormats) format = DXGI_FORMAT_UNKNOWN;
    for (const auto& name : bindingData.colors) {
        const auto color = colorTarget(name);
        data.colorTargets[data.colorCount] = color.handle;
        data.colorFormats[data.colorCount] = color.format;
        data.commands.setRenderTarget(static_cast<uint8_t>(data.colorCount), color.handle);
        ++data.colorCount;
    }
    data.colorTarget = data.colorCount ? data.colorTargets[0] : D3D12_CPU_DESCRIPTOR_HANDLE{};
    if (!bindingData.depth.empty()) {
        const auto depth = depthTarget(bindingData.depth);
        data.depthTarget = depth.handle; data.depthFormat = depth.format;
    } else { data.depthTarget = {}; data.depthFormat = DXGI_FORMAT_UNKNOWN; }
    if (data.depthTarget.ptr) data.commands.setDepthStencil(data.depthTarget);
    if (data.colorCount || data.depthTarget.ptr) data.commands.submitRenderTargets();
    const auto width = target ? target->width() : _state.width;
    const auto height = target ? target->height() : _state.height;
    data.width = width; data.height = height;
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
    if (_copyOutput) recordPass(_copyPass,[&] {
        renderer.commandList()->CopyResource(_state.frames[_state.frameIndex].color.Get(),renderer.outputView()->target->color());
    });
    auto& graphics = renderer.queue();
    const auto& passes = _plan.passes();
    std::fill(_fences.begin(),_fences.end(),0);
    std::fill(_queueFences.begin(),_queueFences.end(),0);
    std::fill(_frameWaited.begin(),_frameWaited.end(),false);
    _pending.clear();
    size_t pendingQueue = size_t(-1);
    const auto flush = [&] {
        if (_pending.empty()) return;
        _usedQueues[pendingQueue]->execute();
        for (auto index : _pending) {
            const auto fence = _passes[index].storage->frames[_state.commandIndex].commands.lastFenceValue();
            _fences[index] = fence; _queueFences[pendingQueue] = fence;
        }
        _pending.clear();
    };
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto& pass = passes[i];
        const auto& binding = _passes[i];
        auto& target = *_usedQueues[binding.queue];
        const bool frameWait = binding.frameWait && !_frameWaited[binding.queue];
        if (pendingQueue != binding.queue || frameWait || !pass.waits.empty()) flush();
        if (frameWait) {
            target.awaitFence(graphics,_previousFrameFence);
            _frameWaited[binding.queue] = true;
        }
        for (auto previous : pass.waits) target.awaitFence(*_usedQueues[_passes[previous].queue],_fences[previous]);
        pendingQueue = binding.queue;
        target.addCommands(&binding.storage->frames[_state.commandIndex].commands);
        _pending.push_back(i);
    }
    if (pendingQueue != 0) flush();
    for (size_t i = 1; i < _usedQueues.size(); ++i) graphics.awaitFence(*_usedQueues[i],_queueFences[i]);
    auto& final = _state.commands[_state.commandIndex];
    final.reset(); barriers(final, _plan.finalBarriers()); final.close();
    graphics.addCommands(&final);
    graphics.execute();
    _previousFrameFence = final.lastFenceValue();
    renderer.completeFrame(_previousFrameFence);
    renderer.completeCameraViews();
    return _previousFrameFence;
}
}
