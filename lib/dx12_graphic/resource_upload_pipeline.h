#pragma once
#include "upload_task.h"
#include "upload_context.h"
#include "cstdmf/job_system2.hpp"
#include <wrl.h>
#include <future>
#include <vector>

namespace csyren::render {
// Owner-thread requests and publication. Jobs only touch task-owned staging data.
// CPU completion and GPU completion are separate gates; no dedicated worker.
class ResourceUploadPipeline {
public:
    explicit ResourceUploadPipeline(cstdmf::multicore::JobSystem& jobs);
    ~ResourceUploadPipeline();
    void addTask(UploadTaskBase::Ptr task);
    void update(ResourceManager& manager);
    void shutdown();
private:
    struct Entry { UploadTaskBase::Ptr task; bool failed = false; };
    struct Batch final : cstdmf::multicore::IJob {
        Batch(Renderer* renderer, ID3D12CommandQueue* queue) : context(renderer), queue(queue) {}
        void execute(uint32_t, uint32_t, uint32_t) override;
        ID3D12CommandQueue* queue;
        UploadContext context;
        std::vector<Entry> entries;
        std::future<void> gpu;
        bool failed = false;
    };
    cstdmf::multicore::JobSystem& _jobs;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _queue;
    std::vector<UploadTaskBase::Ptr> _pending;
    std::unique_ptr<Batch> _batch;
    cstdmf::multicore::JobHandle _job;
    bool _stopped = false;
};
}
