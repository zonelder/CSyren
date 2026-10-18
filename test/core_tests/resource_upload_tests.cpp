#include <gtest/gtest.h>
#include "core/system_manager.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/gpu_resources.h"
#include "dx12_graphic/frame_graph_backend.h"
#include "dx12_graphic/presentation_state.h"
#include "cstdmf/file_system.h"
#include <d3d12sdklayers.h>
#include <atomic>
#include <chrono>
#include <future>

namespace {
using namespace csyren;

TEST(ResourceJobs, BackgroundTaskSurvivesFrameCleanupAndShutdownDrainsIt) {
    cstdmf::multicore::JobSystem jobs;
    jobs.init(2);
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<bool> started = false, finished = false;
    auto backgroundFn = [&](uint32_t, uint32_t, uint32_t) { started = true; gate.wait(); finished = true; };
    cstdmf::multicore::LambdaJob backgroundJob(std::move(backgroundFn));
    auto background = jobs.scheduleAsync(&backgroundJob);
    jobs.submitRoot(background);
    while (!started) std::this_thread::yield();
    auto frameFn = [](uint32_t, uint32_t, uint32_t) {};
    cstdmf::multicore::LambdaJob frame(std::move(frameFn));
    const auto terminal = jobs.schedule(&frame);
    jobs.submitRoot(terminal); jobs.wait(terminal); jobs.clear();
    EXPECT_FALSE(jobs.isComplete(background));
    release.set_value();
    jobs.fini();
    EXPECT_TRUE(finished);
    EXPECT_FALSE(jobs.isComplete(background)); // fini() releases both stores.
}

TEST(ResourceJobs, SingleThreadExecutesAndCapturesFailure) {
    cstdmf::multicore::JobSystem jobs;
    jobs.init(1);
    auto failedFn = [](uint32_t, uint32_t, uint32_t) { throw std::runtime_error("decode failure"); };
    cstdmf::multicore::LambdaJob failedJob(std::move(failedFn));
    auto failed = jobs.scheduleAsync(&failedJob);
    jobs.submitRoot(failed);
    EXPECT_THROW(jobs.wait(failed), std::runtime_error);
    bool executed = false;
    auto nextFn = [&](uint32_t, uint32_t, uint32_t) { executed = true; };
    cstdmf::multicore::LambdaJob nextJob(std::move(nextFn));
    auto next = jobs.scheduleAsync(&nextJob);
    jobs.submitRoot(next);
    jobs.wait(next);
    EXPECT_TRUE(executed);
    jobs.fini();
}

class ResourceUpload : public testing::Test {
protected:
    void SetUp() override {
        systems = &core::details::ServiceRegistry::create<core::SystemManager>();
        core::details::ServiceRegistry::create<render::DescriptorManager>();
        renderer = &core::details::ServiceRegistry::create<render::Renderer>();
        core::details::ServiceRegistry::create<render::GpuResources>();
        resources = &core::details::ServiceRegistry::create<render::ResourceManager>();
        core::details::ServiceRegistry::initializeAll();
        savedMounts = cstdmf::FileSystem::instance().mounts();
        cstdmf::FileSystem::instance().mount("upload-tests", std::filesystem::path(__FILE__).parent_path() / "../../assets");
    }
    void TearDown() override {
        resources->shutdown();
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        if (SUCCEEDED(renderer->device()->QueryInterface(IID_PPV_ARGS(&messages)))) {
            for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
                SIZE_T size = 0;
                messages->GetMessage(i, nullptr, &size);
                std::vector<char> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                messages->GetMessage(i, message, &size);
                EXPECT_GT(message->Severity, D3D12_MESSAGE_SEVERITY_ERROR) << message->pDescription;
            }
        }
        messages.Reset();
        core::details::ServiceRegistry::shutdownAll();
        cstdmf::FileSystem::instance().unmount("upload-tests");
        for (const auto& mount : savedMounts)
            cstdmf::FileSystem::instance().mount(mount.name, mount.root, mount.writable, mount.searchable);
    }
    void settle(render::TextureHandle handle) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (resources->status(handle) == render::Loading && std::chrono::steady_clock::now() < deadline) {
            resources->update();
            // Drive a normal frame terminal/clear with upload jobs in flight.
            core::FrameGraph frame;
            frame.phase({"tick"});
            core::TaskDescription tick; tick.phase = "tick";
            frame.task("tick", tick, [] {});
            frame.execute(systems->jobs());
            std::this_thread::yield();
        }
        ASSERT_NE(resources->status(handle), render::Loading);
    }
    core::SystemManager* systems{};
    render::Renderer* renderer{};
    render::ResourceManager* resources{};
    std::vector<cstdmf::FileSystem::Mount> savedMounts;
};

TEST_F(ResourceUpload, StaleSuccessAndFailureCannotTouchReplacement) {
    const std::string name = "upload-tests:/textures/default/white.dds";
    const auto old = resources->get<render::Texture>(name);
    resources->update(); // Start the old job before replacing its handle.
    const auto current = resources->reloadTexture(name);
    ASSERT_EQ(old.id, current.id);
    ASSERT_NE(old.generation, current.generation);
    render::TextureUploadTask stale(old, name);
    stale.onFailure(*resources);
    stale.onSync(*resources);
    EXPECT_EQ(resources->status(current), render::Loading);
    EXPECT_EQ(resources->getTexture(old), nullptr);
    settle(current);
    EXPECT_EQ(resources->status(current), render::Loaded);
    EXPECT_NE(resources->getTexture(current)->getGpuSrvHandle().ptr, 0u);
}

TEST_F(ResourceUpload, FailureIsInspectableAndExplicitlyRetryable) {
    const auto failed = resources->get<render::Texture>("upload-tests:/missing.dds");
    settle(failed);
    EXPECT_EQ(resources->status(failed), render::Failed);
    EXPECT_NE(resources->getTexture(failed), nullptr);
    EXPECT_EQ(resources->get<render::Texture>("upload-tests:/missing.dds"), failed);
    const auto retry = resources->reloadTexture("upload-tests:/missing.dds");
    EXPECT_NE(retry, failed);
    EXPECT_EQ(resources->status(retry), render::Loading);
    resources->shutdown(); // Cancel the queued retry without publishing it.
    resources->update(); resources->shutdown();
    EXPECT_EQ(resources->getTexture(retry), nullptr);
}

TEST_F(ResourceUpload, ShutdownDrainsSubmittedUploadBeforeSchedulerStops) {
    auto old = resources->get<render::Texture>("upload-tests:/textures/default/red.dds");
    resources->update();
    resources->shutdown();
    EXPECT_EQ(resources->getTexture(old), nullptr);
    resources->init();
    const auto current = resources->get<render::Texture>("upload-tests:/textures/default/red.dds");
    EXPECT_NE(current, old);
    settle(current);
    EXPECT_EQ(resources->status(current), render::Loaded);
    systems->shutdown(); // The application's actual shutdown order also works.
    resources->shutdown();
}

// Exercise the real frame submission/join without a swapchain or output thread.
struct ResourceFrame {
    render::details::PresentationState state;
    render::details::FrameGraphBackend graph;
    explicit ResourceFrame(render::Renderer& renderer, std::shared_ptr<render::RenderQueue> secondary = {})
        : graph(state, renderer.queue()) {
        state.width = state.height = 1; state.commandIndex = state.frameIndex = 0;
        if (!state.commands[0].init(renderer.device(), D3D12_COMMAND_LIST_TYPE_DIRECT))
            throw std::runtime_error("Frame final command list init failed");
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 256; desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> scratch;
        if (FAILED(renderer.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&scratch))))
            throw std::runtime_error("Frame scratch buffer init failed");
        core::Services::get<render::GpuResources>()->import("retirement.scratch", scratch,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_DEST);
        core::TaskDescription task;
        task.gpu("retirement.scratch", D3D12_RESOURCE_STATE_COPY_DEST, core::AccessMode::Write);
        if (secondary) { graph.queue("secondary", std::move(secondary)); task.gpuQueue = "secondary"; }
        graph.compile({{"retirement", task}});
    }
    void record(const std::function<void()>& commands) {
        graph.acquire(); graph.prepareViews(); graph.record("retirement", commands);
    }
};

TEST_F(ResourceUpload, UnloadDuringRecordingWaitsForFinalJoinedFrameFence) {
    const auto first = resources->get<render::Texture>("upload-tests:/textures/default/white.dds");
    settle(first);
    auto* descriptors = core::Services::get<render::DescriptorManager>();
    const auto active = descriptors->srvUavStats().active;
    auto secondary = std::make_shared<render::RenderQueue>();
    D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(secondary->init(renderer->device(), queueDesc));
    ResourceFrame frame(*renderer, secondary);
    const auto previous = renderer->queue().signal(); renderer->queue().syncAwaitComplete(previous);
    Microsoft::WRL::ComPtr<ID3D12Fence> gate;
    ASSERT_TRUE(SUCCEEDED(renderer->device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))));
    struct ReleaseGate { ID3D12Fence* gate; ~ReleaseGate() { gate->Signal(1); } } release{gate.Get()};
    ASSERT_TRUE(SUCCEEDED(secondary->raw()->Wait(gate.Get(), 1)));
    frame.record([&] {
        resources->unload(first);
        EXPECT_EQ(resources->getTexture(first), nullptr);
        resources->update(); renderer->collectDeferredReleases();
        EXPECT_EQ(descriptors->srvUavStats().active, active);
    });
    renderer->collectDeferredReleases(); // A completed previous fence cannot retire this frame's resource.
    EXPECT_EQ(descriptors->srvUavStats().active, active);
    const auto finalFence = frame.graph.submit();
    EXPECT_TRUE(renderer->queue().isComplete(previous));
    EXPECT_FALSE(renderer->queue().isComplete(finalFence)); // Main frame completion includes secondary work.
    renderer->collectDeferredReleases();
    EXPECT_EQ(descriptors->srvUavStats().active, active);
    ASSERT_TRUE(SUCCEEDED(gate->Signal(1)));
    renderer->queue().syncAwaitComplete(finalFence);
    renderer->collectDeferredReleases();
    EXPECT_EQ(descriptors->srvUavStats().active, active - 1);
}

TEST_F(ResourceUpload, UnloadBetweenFramesIsSealedByTheNextSubmit) {
    const auto texture = resources->get<render::Texture>("upload-tests:/textures/default/white.dds");
    settle(texture);
    ResourceFrame frame(*renderer);
    frame.record([] {});
    renderer->queue().syncAwaitComplete(frame.graph.submit());
    auto* descriptors = core::Services::get<render::DescriptorManager>();
    const auto active = descriptors->srvUavStats().active;
    resources->unload(texture); renderer->collectDeferredReleases();
    EXPECT_EQ(descriptors->srvUavStats().active, active);
    frame.record([] {});
    renderer->queue().syncAwaitComplete(frame.graph.submit());
    renderer->collectDeferredReleases();
    EXPECT_EQ(descriptors->srvUavStats().active, active - 1);
}

TEST_F(ResourceUpload, ShutdownReleasesPendingDeletionWithoutAnotherFrame) {
    const auto texture = resources->get<render::Texture>("upload-tests:/textures/default/white.dds");
    settle(texture);
    auto* descriptors = core::Services::get<render::DescriptorManager>();
    const auto active = descriptors->srvUavStats().active;
    resources->unload(texture); renderer->collectDeferredReleases();
    EXPECT_EQ(descriptors->srvUavStats().active, active);
    resources->shutdown();
    EXPECT_EQ(descriptors->srvUavStats().active, active - 1);
}
}
