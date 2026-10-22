#pragma once
#include "editor_viewport_state.h"
#include "dx12_graphic/camera_render_target.h"
#include "dx12_graphic/camera_render_registry.h"
#include "dx12_graphic/camera_view_graph.h"
#include "dx12_graphic/swap_chain.h"
#include "dx12_graphic/presentation_state.h"
#include "core/window.h"
#include "dx12_graphic/frame_prepare_system.h"
#include "core/gpu_execution_plan.h"
#include <dxgi1_4.h>

TEST_F(SceneWorkflow, ViewportCoordinatesRespectImageAndLetterboxing)
{
    const auto image = ViewportImage::fit(100,200,800,600,16.f/9);
    EXPECT_FLOAT_EQ(image.width,800); EXPECT_FLOAT_EQ(image.height,450);
    EXPECT_FLOAT_EQ(image.x,100); EXPECT_FLOAT_EQ(image.y,275);
    EXPECT_FALSE(image.contains(400,250)); EXPECT_TRUE(image.contains(500,500));
    EXPECT_FALSE(image.contains(900,500));
    const auto center = image.ndc(500,500); EXPECT_NEAR(center.x,0,1e-6f); EXPECT_NEAR(center.y,0,1e-6f);
    const auto corner = image.ndc(100,275); EXPECT_FLOAT_EQ(corner.x,-1); EXPECT_FLOAT_EQ(corner.y,1);
    const auto tall = ViewportImage::fit(30,40,400,800,1);
    EXPECT_FLOAT_EQ(tall.width,400); EXPECT_FLOAT_EQ(tall.height,400); EXPECT_FLOAT_EQ(tall.y,240);
    EXPECT_FALSE(ViewportImage::fit(0,0,0,500,1).visible);
}

TEST_F(SceneWorkflow, EditorDockPanelsStayAnchoredAndRecoverCorruptedSplits)
{
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; io.DeltaTime = 1.f/60;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    EditorDockLayout layout;
    ImGuiID dock = 0;
    const auto frame = [&](float width,float height) {
        io.DisplaySize = {width,height}; ImGui::NewFrame();
        if (ImGui::BeginMainMenuBar()) { ImGui::TextUnformatted("Edit"); ImGui::EndMainMenuBar(); }
        if (ImGui::BeginViewportSideBar("##ResizeToolbar",ImGui::GetMainViewport(),ImGuiDir_Up,40,ImGuiWindowFlags_NoSavedSettings)) {}
        ImGui::End();
        dock = layout.render();
        for (auto name : {"Scene Hierarchy","inspector","Scene","Game","systems","statistic"}) {
            ImGui::Begin(name); ImGui::TextUnformatted("Panel"); ImGui::End();
        }
        ImGui::Render();
    };
    for (int i = 0; i < 3; ++i) frame(1200,800);
    auto* hierarchy = ImGui::FindWindowByName("Scene Hierarchy");
    auto* inspector = ImGui::FindWindowByName("inspector");
    auto* scene = ImGui::FindWindowByName("Scene");
    const float leftWidth = hierarchy->Size.x,rightWidth = inspector->Size.x;
    const float originalScene = scene->Size.x;
    frame(1800,1000);
    EXPECT_NEAR(hierarchy->Size.x,leftWidth,2);
    EXPECT_NEAR(inspector->Size.x,rightWidth,2);
    EXPECT_GT(scene->Size.x,originalScene+200);
    const auto* root = ImGui::DockBuilderGetNode(dock);
    EXPECT_FLOAT_EQ(root->Size.x,1800);
    EXPECT_NEAR(root->Pos.y+root->Size.y,1000,1);
    const float top = root->Pos.y;
    EXPECT_GE(top,40); EXPECT_FLOAT_EQ(scene->Pos.y,top);
    // User changes the split; subsequent OS resize must retain that ratio.
    auto* left = hierarchy->DockNode;
    ImGui::DockBuilderSetNodeSize(left->ID,ImVec2(450,left->Size.y));
    frame(1800,1000); EXPECT_NEAR(hierarchy->Size.x,450,2);
    frame(900,600); EXPECT_NEAR(inspector->Pos.x+inspector->Size.x,900,2);
    frame(1800,1000); EXPECT_NEAR(hierarchy->Size.x,450,2);
    frame(0,0); frame(1800,1000); EXPECT_NEAR(hierarchy->Size.x,450,2);
    // Recover the one-pixel sidebar state produced by the previous resize code.
    hierarchy->DockNode->SizeRef.x = 1;
    for (int i = 0; i < 3; ++i) frame(1200,800);
    EXPECT_GT(hierarchy->Size.x,100); EXPECT_GT(inspector->Size.x,100);
    EXPECT_LT(scene->Size.x,1000);
    EXPECT_EQ(ImGui::FindWindowByName("Game")->DockId,scene->DockId);
}

TEST_F(SceneWorkflow, WorkflowToolbarFitsNarrowWindowsAndLongDocumentNames)
{
    MemoryStorage storage;
    auto& session = core::details::ServiceRegistry::create<SceneSession>(storage);
    core::details::ServiceRegistry::create<EditorViewportState>();
    run(session,SceneOperation::SaveAs,"a/very/long/project/directory/with/a/long_scene_document_name.scene");
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DeltaTime = 1.f/60;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    SceneWorkflowWindow workflow; EditorDockLayout layout;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const auto frame = [&](float width) {
        io.DisplaySize = {width,700}; ImGui::NewFrame();
        if (ImGui::BeginMainMenuBar()) ImGui::EndMainMenuBar();
        workflow.render(); layout.render(); ImGui::Render();
        auto* toolbar = ImGui::FindWindowByName("##SceneToolbar");
        EXPECT_LE(toolbar->DC.CursorMaxPos.x,toolbar->WorkRect.Max.x+1);
        EXPECT_LE(toolbar->DC.CursorMaxPos.y,toolbar->WorkRect.Max.y+1);
        EXPECT_GE(ImGui::FindWindowByName("##EditorDockHost")->Pos.y,toolbar->Pos.y+toolbar->Size.y);
        return toolbar->Size.y;
    };
    frame(1200); const auto wide = frame(1200);
    const auto narrow = frame(600); EXPECT_GT(narrow,wide);
    EXPECT_FLOAT_EQ(frame(1400),wide);
}

TEST_F(SceneWorkflow, OnceCameraOnlyRetiresAfterSubmissionAndCanBeRequestedAgain)
{
    using render::details::CameraRenderSchedule;
    CameraRenderSchedule once,continuous;
    EXPECT_TRUE(once.due(CameraRenderType::Once));
    const auto first = once.ticket();
    EXPECT_TRUE(once.due(CameraRenderType::Once)); // recording/failure is not submission
    once.submitted(first); EXPECT_FALSE(once.due(CameraRenderType::Once));
    once.request(); const auto second = once.ticket(); EXPECT_TRUE(once.due(CameraRenderType::Once));
    once.request(); once.submitted(second); // a request during recording belongs to the next frame
    EXPECT_TRUE(once.due(CameraRenderType::Once)); once.submitted(once.ticket());
    EXPECT_FALSE(once.due(CameraRenderType::Once));
    EXPECT_TRUE(once.due(CameraRenderType::Once,true)); // resize invalidates the cached output
    once.submitted(once.ticket()); EXPECT_FALSE(once.due(CameraRenderType::Once));
    EXPECT_TRUE(once.due(CameraRenderType::EachFrame)); once.submitted(once.ticket());
    EXPECT_TRUE(once.due(CameraRenderType::EachFrame)); EXPECT_TRUE(once.due(CameraRenderType::Once));
    for (int frame = 0; frame < 4; ++frame) {
        EXPECT_TRUE(continuous.due(CameraRenderType::EachFrame)); continuous.submitted(continuous.ticket());
    }
}

TEST_F(SceneWorkflow, CameraFramesKeepTheirOwnMatricesAspectAndRenderSchedule)
{
    Camera camera; camera.aspectRatio = 3.f;
    Transform editor,game; editor.position = math::Vector3{1,2,3}; game.position = math::Vector3{10,20,30};
    render::details::CameraRenderView first,second;
    first.engine = render::details::cameraFrame(camera,editor,800,400,1,render::RenderEnvironment{});
    second.engine = render::details::cameraFrame(camera,game,400,400,1,render::RenderEnvironment{});
    EXPECT_NE(first.id,second.id); EXPECT_NE(first.target,second.target);
    EXPECT_FLOAT_EQ(first.engine.cameraPosition.x,1); EXPECT_FLOAT_EQ(second.engine.cameraPosition.x,10);
    EXPECT_FLOAT_EQ(first.engine.invViewMatrix._42,2); EXPECT_FLOAT_EQ(second.engine.invViewMatrix._42,20);
    EXPECT_NE(first.engine.projectionMatrix._11,second.engine.projectionMatrix._11);
    EXPECT_FLOAT_EQ(camera.aspectRatio,3.f); // render-view dimensions do not mutate the component
    EXPECT_TRUE(first.schedule.due(CameraRenderType::Once)); first.schedule.submitted(first.schedule.ticket());
    EXPECT_FALSE(first.schedule.due(CameraRenderType::Once));
    EXPECT_TRUE(second.schedule.due(CameraRenderType::Once));
}

TEST_F(SceneWorkflow, CameraBranchesAreIndependentAndOnceBranchLeavesNextFramePlan)
{
    using namespace render::details;
    TaskDescription clear; clear.gpuScope = GpuTaskScope::View;
    clear.gpu("scene.color",D3D12_RESOURCE_STATE_RENDER_TARGET,AccessMode::Write);
    TaskDescription mesh = clear;
    mesh.gpu("shared.meshes",D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,AccessMode::Read);
    TaskDescription ui;
    ui.gpu("scene.color",D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,AccessMode::Read);
    ui.gpu("frame.color",D3D12_RESOURCE_STATE_RENDER_TARGET,AccessMode::Write);
    std::vector<GraphTask> templates{{"clear",clear},{"mesh",mesh},{"ui",ui}};
    const auto first = expandCameraViews(templates,{{11,true},{22,true}});
    ASSERT_EQ(first.tasks.size(),5u); EXPECT_EQ(first.recordings.at("mesh").size(),2u);
    EXPECT_EQ(first.bindings.at("mesh@11"),11u); EXPECT_EQ(first.bindings.at("mesh@22"),22u);
    EXPECT_EQ(first.tasks[2].description.gpuAccesses[0].resource,"view.11.color");
    EXPECT_EQ(first.tasks[3].description.gpuAccesses[0].resource,"view.22.color");
    EXPECT_EQ(first.tasks[2].description.gpuAccesses[1].resource,"shared.meshes");
    std::vector<GpuExecutionPlan::Resource> resources{
        {"view.11.color",D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {"view.22.color",D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {"shared.meshes",D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {"frame.color",D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE}};
    GpuExecutionPlan plan; plan.compile(first.tasks,resources,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(plan.passes().back().barriers.size(),3u);
    const auto next = expandCameraViews(templates,{{11,true},{22,false}});
    ASSERT_EQ(next.tasks.size(),3u); EXPECT_EQ(next.recordings.at("mesh").size(),1u);
    plan.compile(next.tasks,resources,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (const auto& pass : plan.passes()) for (const auto& barrier : pass.barriers)
        EXPECT_NE(barrier.resource,"view.22.color"); // cached image is still sampled by UI
    EXPECT_EQ(next.tasks.back().description.gpuAccesses.size(),3u);
    const auto repeated = expandCameraViews(templates,{{11,true},{22,true}});
    EXPECT_EQ(repeated.recordings.at("mesh"),first.recordings.at("mesh"));
    const auto removed = expandCameraViews(templates,{{11,true}});
    EXPECT_EQ(removed.tasks.back().description.gpuAccesses.size(),2u);
}

TEST_F(SceneWorkflow, CameraRenderTypeRoundTripsAndLegacyScenesDefaultToEachFrame)
{
    MemoryStorage storage; SceneSession session(storage);
    const auto id = session.editorScene().createEntity("once camera");
    session.editorScene().addComponent<Camera>(id)->render_type = CameraRenderType::Once;
    run(session,SceneOperation::SaveAs,"camera policy");
    run(session,SceneOperation::Play);
    for (auto [entity,camera] : session.activeContext().scene.view<const Camera>()) EXPECT_EQ(camera.render_type,CameraRenderType::Once);
    run(session,SceneOperation::Stop); run(session,SceneOperation::Open,"camera policy");
    for (auto [entity,camera] : session.editorScene().view<const Camera>()) EXPECT_EQ(camera.render_type,CameraRenderType::Once);
    auto legacy = nlohmann::json::parse(storage.files.at("camera policy"));
    for (auto& entity : legacy["entities"]) if (entity["components"].contains("Camera")) entity["components"]["Camera"].erase("render_type");
    storage.files["legacy camera"] = legacy.dump(); run(session,SceneOperation::Open,"legacy camera");
    for (auto [entity,camera] : session.editorScene().view<const Camera>()) EXPECT_EQ(camera.render_type,CameraRenderType::EachFrame);
}

TEST_F(SceneWorkflow, CameraRenderRegistryFollowsComponentAndSceneLifetimes)
{
    static_assert(std::is_trivially_copyable_v<Camera>);
    MemoryStorage storage; SceneSession session(storage); EditorViewportState editor;
    auto& scene = session.editorScene();
    const auto id = scene.createEntity("game camera");
    scene.addComponent<Transform>(id);
    auto camera = scene.addComponent<Camera>(id);
    render::details::CameraRenderRegistry registry; registry.attach(scene);
    auto original = registry.target(*camera); ASSERT_TRUE(original);
    scene.eventBus().commit_batch();
    EXPECT_EQ(registry.target(*camera),original); // pending create on attach is idempotent
    run(session,SceneOperation::SaveAs,"camera views");
    EXPECT_EQ(storage.files.at("camera views").find("renderResources"),std::string::npos);
    std::weak_ptr<render::CameraRenderTarget> retired = original; original.reset();
    scene.removeComponent<Camera>(id); scene.flush(); camera = scene.addComponent<Camera>(id);
    scene.eventBus().commit_batch();
    EXPECT_TRUE(retired.expired()); EXPECT_EQ(registry.size(),1u);
    auto replacement = registry.target(*camera); ASSERT_TRUE(replacement);
    // Outgoing scene subscriptions/resources must be detached before replacement.
    registry.detach(); EXPECT_EQ(registry.size(),0u);
    run(session,SceneOperation::Play); registry.attach(session.activeContext().scene);
    auto runtimeCamera = session.activeContext().scene.getComponent<Camera>(id);
    auto* runtime = runtimeCamera.get();
    ASSERT_NE(runtime,nullptr);
    EXPECT_NE(runtime,camera.get()); EXPECT_NE(registry.target(*runtime),replacement);
    EXPECT_NE(runtime,&editor.camera);
    registry.detach(); run(session,SceneOperation::Stop);
    registry.attach(scene); EXPECT_NE(registry.target(*camera),replacement);
    scene.removeComponent<Camera>(id); scene.flush(); scene.eventBus().commit_batch(); EXPECT_EQ(registry.size(),0u);
    registry.detach(); run(session,SceneOperation::Open,"camera views");
    registry.attach(session.editorScene()); EXPECT_EQ(registry.size(),1u);
    registry.detach();
}

TEST_F(SceneWorkflow, ViewportNavigationIgnoresNativeInputOutsideTheImage)
{
    auto& devices = core::details::ServiceRegistry::create<Devices>();
    core::details::ServiceRegistry::create<Time>();
    auto& view = core::details::ServiceRegistry::create<EditorViewportState>(); view.viewportInput = true;
    auto& mouse = const_cast<MouseDevice&>(devices.mouse());
    mouse.setButtonState(MouseButton::Right,true); mouse.setMousePosition(100,100);
    const auto initial = view.transform.rotation;
    EditorCameraControllerSystem controller; controller.run();
    EXPECT_TRUE(view.transform.rotation.exactEqual(initial));
    view.navigation.active = true; view.navigation.captured = true; view.navigation.dx = .1f;
    controller.run(); EXPECT_FALSE(view.transform.rotation.exactEqual(initial));
    EXPECT_FLOAT_EQ(view.navigation.dx,0);
    const auto once = view.transform.rotation;
    controller.run(); EXPECT_TRUE(view.transform.rotation.exactEqual(once));
}

TEST_F(SceneWorkflow, ViewportRotationPreservesNativePixelSensitivity)
{
    auto& devices = core::details::ServiceRegistry::create<Devices>();
    core::details::ServiceRegistry::create<Time>();
    auto& view = core::details::ServiceRegistry::create<EditorViewportState>();
    auto& mouse = const_cast<MouseDevice&>(devices.mouse());
    mouse.setButtonState(MouseButton::Right,true); mouse.setMousePosition(20,10);
    EditorCameraControllerSystem controller; controller.run();
    const auto native = view.transform.rotation;
    view.transform.rotation = math::Quaternion{}; view.viewportInput = true;
    view.navigation.active = true; view.navigation.dx = 20; view.navigation.dy = 10;
    controller.run(); EXPECT_TRUE(view.transform.rotation.exactEqual(native));
}

TEST_F(SceneWorkflow, CameraColorIsSampledBeforeEditorOutputIsPresented)
{
    render::FramePrepareSystem prepare;
    auto mesh = prepare.describe(); mesh.phase = "render";
    TaskDescription ui; ui.phase = "render";
    ui.gpu("scene.color",D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,AccessMode::Read)
      .gpu("frame.color",D3D12_RESOURCE_STATE_RENDER_TARGET,AccessMode::Write);
    GpuExecutionPlan plan;
    plan.compile({{"clear",prepare.describe()},{"mesh",mesh},{"editor",ui}},
        {{"scene.color",D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
         {"scene.depth",D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_WRITE},
         {"frame.color",D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE}},D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(plan.passes().size(),3u);
    const auto& barriers = plan.passes().back().barriers;
    EXPECT_TRUE(std::any_of(barriers.begin(),barriers.end(),[](const auto& b) {
        return b.resource == "scene.color" && b.before == D3D12_RESOURCE_STATE_RENDER_TARGET && b.after == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }));
    ASSERT_EQ(plan.finalBarriers().size(),1u);
    EXPECT_EQ(plan.finalBarriers().front().resource,"frame.color");
    EXPECT_EQ(plan.finalBarriers().front().after,D3D12_RESOURCE_STATE_COPY_SOURCE);
}

TEST_F(SceneWorkflow, WarpCameraTargetsResizeIndependentlyWithoutDescriptorLeaks)
{
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    ASSERT_TRUE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    ASSERT_TRUE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    ASSERT_TRUE(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    render::DescriptorManager descriptors; descriptors.init(device.Get());
    render::RenderQueue queue; D3D12_COMMAND_QUEUE_DESC desc{}; desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(queue.init(device.Get(),desc));
    {
        render::CameraRenderTarget scene,game;
        scene.request(320,240); game.request(192,108);
        scene.prepare(device.Get(),descriptors,queue); game.prepare(device.Get(),descriptors,queue);
        EXPECT_NE(scene.color(),game.color()); EXPECT_NE(scene.depth(),game.depth());
        EXPECT_EQ(scene.width(),320u); EXPECT_EQ(game.width(),192u);
        const auto srv = scene.srv().ptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> old = scene.color();
        for (unsigned int i = 0; i < 6; ++i)
        {
            scene.request(200+i*10,150+i*10); scene.prepare(device.Get(),descriptors,queue);
            EXPECT_EQ(scene.srv().ptr,srv); EXPECT_EQ(game.width(),192u);
            EXPECT_EQ(descriptors.srvUavStats().active,2u); EXPECT_EQ(descriptors.rtvStats().active,2u);
            EXPECT_EQ(descriptors.dsvStats().active,2u);
        }
        EXPECT_NE(scene.color(),old.Get());
        auto* same = scene.color(); scene.prepare(device.Get(),descriptors,queue); EXPECT_EQ(scene.color(),same);
        scene.request(0,0); scene.prepare(device.Get(),descriptors,queue); EXPECT_EQ(scene.width(),1u); EXPECT_EQ(scene.height(),1u);
    }
    EXPECT_EQ(descriptors.srvUavStats().active,0u); EXPECT_EQ(descriptors.rtvStats().active,0u); EXPECT_EQ(descriptors.dsvStats().active,0u);
}

TEST_F(SceneWorkflow, NativeEditorWindowAndSwapChainResizeTogether)
{
    core::Window window(800,600,L"CSyren resize test");
    const auto hwnd = window.earlyInit(); ASSERT_NE(hwnd,nullptr);
    struct CloseWindow { HWND hwnd; ~CloseWindow() { DestroyWindow(hwnd); } } close{hwnd};
    const auto style = static_cast<DWORD>(GetWindowLongPtr(hwnd,GWL_STYLE));
    EXPECT_NE(style & WS_THICKFRAME,0u); EXPECT_NE(style & WS_MAXIMIZEBOX,0u);
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    ASSERT_TRUE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    ASSERT_TRUE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    ASSERT_TRUE(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    render::RenderQueue queue; D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(queue.init(device.Get(),queueDesc));
    render::SwapChain swap;
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 800; desc.Height = 600;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferCount = 2;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.SampleDesc.Count = 1;
    ASSERT_TRUE(swap.init(factory.Get(),device.Get(),queue.raw(),hwnd,desc));
    for (const auto size : {SIZE{900,700},SIZE{1200,800},SIZE{640,480}}) {
        RECT rect{0,0,size.cx,size.cy}; ASSERT_TRUE(AdjustWindowRect(&rect,style,FALSE));
        ASSERT_TRUE(SetWindowPos(hwnd,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
        EXPECT_EQ(window.width(),size.cx); EXPECT_EQ(window.height(),size.cy);
        ASSERT_TRUE(swap.resize(window.width(),window.height(),true));
        EXPECT_EQ(swap.currentBackBuffer()->GetDesc().Width,static_cast<UINT64>(size.cx));
        EXPECT_EQ(swap.currentBackBuffer()->GetDesc().Height,static_cast<UINT>(size.cy));
    }
    EXPECT_FALSE(swap.resize(0,0,true)); EXPECT_NE(swap.currentBackBuffer(),nullptr);
    SendMessage(hwnd,WM_SIZE,SIZE_MINIMIZED,0);
    EXPECT_EQ(window.width(),0); EXPECT_EQ(window.height(),0);
}

TEST_F(SceneWorkflow, MailboxPresentsConsecutiveFramesAfterEveryResize)
{
    core::Window window(800,600,L"CSyren presentation resize test");
    const auto hwnd = window.earlyInit(); ASSERT_NE(hwnd,nullptr);
    struct CloseWindow { HWND hwnd; ~CloseWindow() { DestroyWindow(hwnd); } } close{hwnd};
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    ASSERT_TRUE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    ASSERT_TRUE(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    ASSERT_TRUE(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    render::DescriptorManager descriptors; descriptors.init(device.Get());
    render::RenderQueue graphics; D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ASSERT_TRUE(graphics.init(device.Get(),queueDesc));
    render::details::PresentationState output; output.init(device.Get(),graphics,descriptors,window);
    render::CommandList draw; ASSERT_TRUE(draw.init(device.Get(),queueDesc.Type));
    const auto waitPresented = [&](UINT64 sequence) {
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard lock(output.mutex);
                if (output.outputError) std::rethrow_exception(output.outputError);
                if (output.lastPresented >= sequence) return true;
            }
            MSG message{};
            while (PeekMessage(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessage(&message); }
            MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT);
        }
        return false;
    };
    for (auto size : {SIZE{800,600},SIZE{1200,800},SIZE{640,480},SIZE{1400,900}}) {
        (void)output.swapChain.ready(); // may consume a latency signal without Present
        output.resize(size.cx,size.cy);
        EXPECT_EQ(output.swapChain.meta().width,static_cast<UINT>(size.cx));
        EXPECT_EQ(output.depth->GetDesc().Height,static_cast<UINT>(size.cy));
        for (int frame = 0; frame < 3; ++frame) {
            graphics.syncAwaitLastComplete(); output.presentationQueue.syncAwaitLastComplete();
            auto& target = output.frames[0];
            { std::lock_guard lock(output.mutex); target.recording = true; }
            const float color[]{float(frame)/3,.2f,.4f,1}; output.prepareTarget(target,color);
            draw.reset(); D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = target.color.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            draw.resourceBarrier(barrier); draw.raw()->ClearRenderTargetView(target.rtv.cpu,color,0,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); draw.resourceBarrier(barrier); draw.close();
            graphics.addCommands(&draw); graphics.execute();
            UINT64 sequence;
            {
                std::lock_guard lock(output.mutex); sequence = ++output.sequence;
                target.renderFence = draw.lastFenceValue(); target.sequence = sequence; target.recording = false;
            }
            output.changed.notify_one();
            ASSERT_TRUE(waitPresented(sequence)) << "No Present after resize " << size.cx << "x" << size.cy << " frame " << frame;
        }
    }
    output.shutdown();
    EXPECT_EQ(descriptors.rtvStats().active,0u); EXPECT_EQ(descriptors.dsvStats().active,0u);
}

TEST_F(SceneWorkflow, ExternalCameraViewsHaveExplicitIndependentLifetimes)
{
    render::Renderer renderer;
    Camera camera; Transform transform;
    auto source = [&] { return std::pair<const Camera*,const Transform*>{&camera,&transform}; };
    auto first = renderer.addCameraView(source);
    auto second = renderer.addCameraView(source);
    ASSERT_NE(first,second);
    EXPECT_NE(first->target,second->target);
    EXPECT_TRUE(renderer.presentsCameraOutput());
    renderer.presentCameraOutput(false);
    EXPECT_FALSE(renderer.presentsCameraOutput());
    std::weak_ptr<render::details::CameraRenderView> firstLifetime = first, secondLifetime = second;
    renderer.removeCameraView(first);
    first.reset();
    EXPECT_TRUE(firstLifetime.expired());
    second.reset();
    EXPECT_FALSE(secondLifetime.expired());
    renderer.removeCameraView(secondLifetime.lock());
    EXPECT_TRUE(secondLifetime.expired());
    renderer.presentCameraOutput(true);
    EXPECT_TRUE(renderer.presentsCameraOutput());
}
