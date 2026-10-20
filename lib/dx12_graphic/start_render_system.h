#pragma once

#include "core/event_bus.h"
#include "core/services.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "core/transform.h"
#include "math/math.h"
#include "core/time.h"
#include "core/camera_service.h"
#include "core/time.h"
#include "core/camera.h"
#include "renderer.h"
#include "math/matrix4x4.h"

#include "imGui/imgui.h"
#include "imGui/imgui_internal.h"
#include "imGui/backends/imgui_impl_dx12.h"
#include "imGui/backends/imgui_impl_win32.h"

#include "base_editor_window.h"
#include "scene_hierarchy.h"
#include "inspector_window.h"
#include "system_window.h"
#include "statistic_window.h"
#include "scene_workflow_window.h"
#include "scene_viewport.h"
#include "editor_layout.h"
#include "editor_overlays.h"


extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace csyren::editor
{
	class EditorSystem : public core::System
	{
	public:
        core::TaskDescription describe() const override {
            core::TaskDescription d; d.phase = "render"; d.affinity = core::TaskAffinity::MainThread;
            d.exclusiveScene = true; d.dynamicResources = true; d.structuralChanges = true;
            d.after = {"MeshRenderSystem"};
            d.writeResource<render::ResourceManager>();
            d.gpu("frame.color", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write);
            d.gpu("scene.color", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read); return d;
        }


		void init() override
		{
			IMGUI_CHECKVERSION();

			if (!ImGui::GetCurrentContext()) {
				ImGui::CreateContext();
			}

			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
			ImGui::StyleColorsDark();
			auto window = core::Services::get<core::Window>();
			HWND hwnd = window->hwnd();
			ImGui_ImplWin32_Init(hwnd);

			auto* renderer = core::Services::get<render::Renderer>();
            auto& view = *core::Services::get<EditorViewportState>();
            view.viewportInput = true;
            _cameraView = renderer->addCameraView([] {
                auto* state = core::Services::get<EditorViewportState>();
                return std::pair<const core::Camera*,const core::Transform*>{&state->camera,&state->transform};
            });
            _viewport.cameraView(_cameraView);
            renderer->presentCameraOutput(false);
			auto* shaderHeap = core::Services::get<render::DescriptorManager>()->shaderHeap();
			ID3D12Device* device = renderer->device();
			auto numFrames = render::Renderer::FramesInFlight;

			ImGui_ImplDX12_InitInfo init_info = {};
			init_info.Device = device;
			init_info.CommandQueue = renderer->queue().raw(); 			
			init_info.NumFramesInFlight = numFrames;
			init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
			init_info.SrvDescriptorHeap = shaderHeap;
			init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* gpu_handle) {
				auto* descriptorManager = core::Services::get<render::DescriptorManager>();
				render::DescriptorAllocation alloc = descriptorManager->allocateRawSRV();
				*cpu_handle = alloc.cpu;
				*gpu_handle = alloc.gpu;
				s_imguiAllocations.emplace_back(std::move(alloc));
				};
			
			init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
				auto* descriptorManager = core::Services::get<render::DescriptorManager>();

				//TODO fast search
				for (auto it = s_imguiAllocations.begin(); it != s_imguiAllocations.end(); ++it) {
					if (it->cpu.ptr == cpu.ptr) {
						descriptorManager->freeRawSRV(*it);
						s_imguiAllocations.erase(it);
						break;
					}
				}
				};

			window->addPreMessageCallback([](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
				{
					if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
						return true;

					if (ImGui::GetCurrentContext())
					{
						const ImGuiIO& io = ImGui::GetIO();
                        auto& view = *core::Services::get<EditorViewportState>();
                        auto& session = *core::Services::get<SceneSession>();
                        POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(hWnd,&cursor);
                        const bool gameMouse = session.playing() && view.gameViewport.contains(float(cursor.x),float(cursor.y));
                        const bool gameKeys = gameMouse && view.gameViewport.focused;
					
						bool isMouseMessage = (msg >= WM_MOUSEMOVE && msg <= WM_MOUSEWHEEL) ||
							(msg >= WM_LBUTTONDOWN && msg <= WM_MBUTTONDBLCLK);
						bool isKeyboardMessage = (msg >= WM_KEYDOWN && msg <= WM_DEADCHAR) ||
							(msg >= WM_SYSKEYDOWN && msg <= WM_SYSDEADCHAR);

						// Если ImGui хочет этот тип инпута — блокируем
                        const bool mouseRelease = msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP;
                        const bool keyRelease = msg == WM_KEYUP || msg == WM_SYSKEYUP;
						if (isMouseMessage && io.WantCaptureMouse && !gameMouse && !mouseRelease)
							return true;
						if (isKeyboardMessage && io.WantCaptureKeyboard && !gameKeys && !keyRelease)
							return true;
					}
					return false;
				});


			_windows.push_back(std::make_unique<SceneHierarchyWindow>());
            _workflow = std::make_unique<SceneWorkflowWindow>();
			_windows.push_back(std::make_unique<InspectorWindow>());
			_windows.push_back(std::make_unique<SystemWindow>());
			_windows.push_back(std::make_unique<StatisticWindow>());
			ImGui_ImplDX12_Init(&init_info);
		}
		void execute() override
		{
            if (IsIconic(core::Services::get<core::Window>()->hwnd())) {
                const float background[]{.08f,.08f,.09f,1};
                core::Services::get<render::Renderer>()->clearColor(background);
                auto& view = *core::Services::get<EditorViewportState>();
                view.viewport = {}; view.gameViewport = {}; view.navigation = {};
                return;
            }
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
            const float background[]{.08f,.08f,.09f,1};
            core::Services::get<render::Renderer>()->clearColor(background);
            synchronizeSelection();
            if (!_viewport.manipulating()) _workflow->shortcuts();

			if (ImGui::BeginMainMenuBar()) 
			{
                _workflow->menus();
				if (ImGui::BeginMenu("View")) 
				{
                    ImGui::MenuItem("Scene",nullptr,&_viewportOpen);
                    ImGui::MenuItem("Game",nullptr,&_gameOpen);
                    if (ImGui::MenuItem("Reset layout")) _resetLayout = true;
					for (auto& window : _windows) 
					{
						bool isOpen = window->isOpen();
						if (ImGui::MenuItem(window->title().data(), nullptr, &isOpen)) {
							window->setOpen(isOpen);
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}

            _workflow->render();
            dockspace();
			// Рендерим все окна
			for (auto& window : _windows) 
			{
				window->render();
			}
            _viewport.render(&_viewportOpen);
            _gameViewport.render(&_gameOpen,ViewportCamera::Game);
            if (core::Services::has<EditorOverlays>()) core::Services::get<EditorOverlays>()->draw();
            if (!ImGui::IsAnyItemActive() && !_viewport.manipulating()) core::Services::get<SceneSession>()->finishEdit();
			auto* renderer = core::Services::get<render::Renderer>();
			ID3D12GraphicsCommandList* cmdList = renderer->commandList();

			ImGui::Render();

			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
			const ImGuiIO& io = ImGui::GetIO();
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::UpdatePlatformWindows();
				ImGui::RenderPlatformWindowsDefault(nullptr, (void*)cmdList);
			}
		}

		void shutdown() override
		{
			core::Services::get<render::Renderer>()->queue().syncAwaitLastComplete();
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
            auto& view = *core::Services::get<EditorViewportState>();
            view.viewportInput = false; view.viewport = {}; view.gameViewport = {}; view.navigation = {};
            auto* renderer = core::Services::get<render::Renderer>();
            renderer->removeCameraView(_cameraView);
            renderer->presentCameraOutput(true);
            _viewport.cameraView({}); _cameraView.reset();
            _windows.clear(); _workflow = nullptr;
		}
	private:
        void dockspace()
        {
            _layout.render(_resetLayout); _resetLayout = false;
        }
        EditorDockLayout _layout;
        bool _viewportOpen = true,_gameOpen = true,_resetLayout = false;
        std::shared_ptr<render::details::CameraRenderView> _cameraView;
        SceneViewport _viewport;
        SceneViewport _gameViewport;
        std::unique_ptr<SceneWorkflowWindow> _workflow;
		std::vector<std::unique_ptr<BaseEditorWindow>> _windows;
		static std::vector<render::DescriptorAllocation> s_imguiAllocations;
	};

	std::vector<render::DescriptorAllocation> EditorSystem::s_imguiAllocations{};
	REGISTER_SYSTEM(EditorSystem)
}

