#include "pch.h"
#include "resource_upload_pipeline.h"
#include "resource_manager.h"
#include "cstdmf/profiler.h"
#include <chrono>
#include <objbase.h>

namespace csyren::render {
ResourceUploadPipeline::ResourceUploadPipeline(cstdmf::multicore::JobSystem& jobs) : _jobs(jobs) {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(core::Services::get<Renderer>()->device()->CreateCommandQueue(&desc, IID_PPV_ARGS(&_queue))))
        throw std::runtime_error("Failed to create resource upload queue");
}
ResourceUploadPipeline::~ResourceUploadPipeline() { shutdown(); }

void ResourceUploadPipeline::addTask(UploadTaskBase::Ptr task) {
    if (_stopped) throw std::logic_error("Resource uploads are stopped");
    if (!task) throw std::invalid_argument("Null resource upload task");
    _pending.push_back(std::move(task));
}

void ResourceUploadPipeline::update(ResourceManager& manager) {
    if (_stopped) return;
    if (_batch) {
        if (_job.isValid()) {
            if (!_jobs.isComplete(_job)) return;
            _jobs.wait(_job);
            _jobs.clear(_job); _job = {};
        }
        if (_batch->gpu.valid()) {
            if (_batch->gpu.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            try { _batch->gpu.get(); }
            catch (const std::exception& error) {
                log::error("Resource upload GPU completion failed: {}", error.what());
                _batch->failed = true;
            }
        }
        for (auto& entry : _batch->entries) {
            if (!entry.task->isCurrent(manager)) continue;
            try {
                if (_batch->failed || entry.failed) entry.task->onFailure(manager);
                else entry.task->onSync(manager);
            } catch (const std::exception& error) {
                log::error("Resource publication failed: {}", error.what());
                entry.task->onFailure(manager);
            }
        }
        _job = {};
        _batch.reset();
    }
    std::erase_if(_pending, [&](const auto& task) { return !task->isCurrent(manager); });
    if (_pending.empty()) return;
    _batch = std::make_unique<Batch>(core::Services::get<Renderer>(), _queue.Get());
    for (auto& task : _pending) _batch->entries.push_back({std::move(task)});
    _pending.clear();
    try {
        _job = _jobs.scheduleAsync(_batch.get());
        _jobs.submitRoot(_job);
    } catch (...) {
        _jobs.clear(_job); _job = {};
        for (auto& entry : _batch->entries) entry.task->onFailure(manager);
        _batch.reset();
        throw;
    }
}

void ResourceUploadPipeline::Batch::execute(uint32_t, uint32_t, uint32_t) {
    CS_PROFILE_SCOPE("Resource upload batch");
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope { bool initialized; ~ComScope() { if (initialized) CoUninitialize(); } } scope{SUCCEEDED(com)};
    try {
        if (FAILED(com) && com != RPC_E_CHANGED_MODE) throw std::runtime_error("Upload COM initialization failed");
        context.batcher().Begin();
        for (auto& entry : entries) {
            try { entry.task->onUpload(context); }
            catch (const std::exception& error) {
                entry.failed = true;
                log::error("Resource upload failed: {}", error.what());
            } catch (...) { entry.failed = true; }
        }
        gpu = context.batcher().End(queue);
    } catch (const std::exception& error) {
        log::error("Resource upload batch failed: {}", error.what());
        failed = true;
    } catch (...) { failed = true; }
}

void ResourceUploadPipeline::shutdown() {
    if (_stopped) return;
    _stopped = true;
    _pending.clear();
    if (_job.isValid()) { _jobs.wait(_job); _jobs.clear(_job); _job = {}; }
    if (_batch && _batch->gpu.valid()) {
        try { _batch->gpu.get(); }
        catch (const std::exception& error) { log::error("Resource upload shutdown: {}", error.what()); }
    }
    _job = {};
    _batch.reset(); // Cancel publication only after CPU and GPU completion.
}
}
