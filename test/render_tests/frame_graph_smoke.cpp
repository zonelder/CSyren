#include "dx12_graphic/renderer.h"
#include "dx12_graphic/presentation_system.h"
#include "dx12_graphic/frame_prepare_system.h"
#include "dx12_graphic/resource_manager.h"
#include "dx12_graphic/gpu_resources.h"
#include "core/window.h"
#include "core/system_manager.h"
#include "core/typed_system.h"
#include "dx12_graphic/recorder.h"
#include "core/scene.h"
#include "core/camera.h"
#include "core/transform.h"
#include "core/time.h"
#include <d3d12sdklayers.h>
#include <atomic>
#include <chrono>
#include <iostream>

using namespace csyren;
using namespace csyren::core;
namespace {
std::atomic<unsigned> entered{0};
std::atomic<bool> overlapped{true};
Microsoft::WRL::ComPtr<ID3D12Resource> source, destination, readback;
void overlap() {
    entered.fetch_add(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (entered.load() < 2) {
        if (std::chrono::steady_clock::now() > deadline) { overlapped = false; return; }
        std::this_thread::yield();
    }
}
class ParallelRenderA : public TypedSystem<ParallelRenderA> {
public:
    static constexpr auto phase = "record";
    static constexpr std::array gpu{ GpuUse{ "frame.color", D3D12_RESOURCE_STATE_RENDER_TARGET, AccessMode::Write } };
    void execute(render::Recorder recorder) { overlap(); recorder->commandList()->SetMarker(0, "A", 1); }
};
class ParallelRenderB : public TypedSystem<ParallelRenderB> {
public:
    static constexpr auto phase = "record";
    static constexpr auto gpuQueue = "secondary";
    static constexpr std::array gpu{
        GpuUse{ "test.source", D3D12_RESOURCE_STATE_GENERIC_READ, AccessMode::Read },
        GpuUse{ "test.destination", D3D12_RESOURCE_STATE_COPY_DEST, AccessMode::Write } };
    void execute(render::Recorder recorder) {
        overlap();
        recorder->commandList()->CopyBufferRegion(destination.Get(), 0, source.Get(), 0, 4);
    }
};
class ReadbackSystem : public TypedSystem<ReadbackSystem> {
public:
    static constexpr auto phase = "record";
    static constexpr std::array gpu{
        GpuUse{ "test.destination", D3D12_RESOURCE_STATE_COPY_SOURCE, AccessMode::Read },
        GpuUse{ "test.readback", D3D12_RESOURCE_STATE_COPY_DEST, AccessMode::Write } };
    void execute(render::Recorder recorder) {
        recorder->commandList()->CopyBufferRegion(readback.Get(), 0, destination.Get(), 0, 4);
    }
};
REGISTER_SYSTEM(ParallelRenderA)
REGISTER_SYSTEM(ParallelRenderB)
REGISTER_SYSTEM(ReadbackSystem)
void buffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state,
    Microsoft::WRL::ComPtr<ID3D12Resource>& result) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = heapType;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result))))
        throw std::runtime_error("Create test buffer failed");
}
}
// Engine systems are registered by the generated registry of an executable; this test
// links only the libraries, so it registers the ones it uses itself.
namespace csyren::render
{
    REGISTER_SYSTEM(FrameAcquireSystem)
    REGISTER_SYSTEM(FramePrepareSystem)
    REGISTER_SYSTEM(PresentationSystem)
}
int main() {
    REGISTER_COMPONENT(Camera); REGISTER_COMPONENT(Transform);
    details::ServiceRegistry::create<Window>(640, 480, L"FrameGraph integration test (hidden)");
    details::ServiceRegistry::create<Time>();
    auto& bus = details::ServiceRegistry::create<EventBus>();
    auto& scene = details::ServiceRegistry::create<Scene>(&bus);
    auto& systems = details::ServiceRegistry::create<SystemManager>();
    details::ServiceRegistry::create<render::DescriptorManager>();
    auto& renderer = details::ServiceRegistry::create<render::Renderer>();
    auto& imports = details::ServiceRegistry::create<render::GpuResources>();
    details::ServiceRegistry::create<render::ResourceManager>();
    details::ServiceRegistry::initializeAll();
    auto secondary = std::make_shared<render::RenderQueue>();
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!secondary->init(renderer.device(), q)) return 2;
    imports.queue("secondary", secondary);
    buffer(renderer.device(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, source);
    buffer(renderer.device(), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, destination);
    buffer(renderer.device(), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, readback);
    void* memory; source->Map(0, nullptr, &memory); *static_cast<uint32_t*>(memory) = 0x12345678; source->Unmap(0, nullptr);
    imports.import("test.source", source, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_GENERIC_READ);
    imports.import("test.destination", destination, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    imports.import("test.readback", readback, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_DEST);
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
    renderer.device()->QueryInterface(IID_PPV_ARGS(&info));
    auto camera = scene.createEntity();
    auto cameraComponent = scene.addComponent<Camera>(camera); scene.addComponent<Transform>(camera);
    systems.phase({"acquire"}); systems.phase({"prepare", {"acquire"}});
    systems.phase({"record", {"prepare"}}); systems.phase({"submit", {"record"}});
    systems.add("FrameAcquireSystem"); systems.add("FramePrepareSystem");
    systems.add("ParallelRenderA"); systems.add("ParallelRenderB"); systems.add("ReadbackSystem");
    systems.add("PresentationSystem"); systems.init();
    auto begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 600; ++frame) {
        MSG msg{};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        if (frame == 200) cameraComponent->background = math::Vector4{0, 0, 1, 1};
        if (frame == 400) cameraComponent->background = math::Vector4{0.1f, 0.7f, 0.2f, 1};
        entered = 0;
        if (frame == 300) systems.move(2, 3); // Recompile with GPU work in flight.
        systems.execute();
    }
    systems.shutdown();
    readback->Map(0, nullptr, &memory); const auto value = *static_cast<uint32_t*>(memory); readback->Unmap(0, nullptr);
    unsigned errors = 0, warnings = 0;
    if (info) for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0; info->GetMessage(i, nullptr, &size); std::vector<char> storage(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data()); info->GetMessage(i, message, &size);
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) ++errors;
        if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING) ++warnings;
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) std::cerr << message->pDescription << '\n';
    }
    const bool healthy = SUCCEEDED(renderer.device()->GetDeviceRemovedReason());
    std::cout << "600 frames, seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()
        << ", parallel=" << overlapped << ", readback=" << std::hex << value << std::dec
        << ", D3D12 errors=" << errors << ", warnings=" << warnings << '\n';
    info.Reset(); secondary.reset(); source.Reset(); destination.Reset(); readback.Reset();
    details::ServiceRegistry::shutdownAll();
    return (!healthy || errors || warnings || !overlapped || value != 0x12345678) ? 1 : 0;
}
