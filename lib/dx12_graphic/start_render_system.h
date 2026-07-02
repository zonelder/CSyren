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
#include "imGui/backends/imgui_impl_dx12.h"
#include "imGui/backends/imgui_impl_win32.h"

#include "base_editor_window.h"
#include "scene_hierarchy.h"
#include "inspector_window.h"



extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace csyren::editor
{
	class EditorSystem : public core::System
	{
	public:

		void init() override
		{
			IMGUI_CHECKVERSION();

			if (!ImGui::GetCurrentContext()) {
				ImGui::CreateContext();
			}

			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
			ImGui::StyleColorsDark();
			auto window = core::Services::get<Window>();
			HWND hwnd = window->hwnd();
			ImGui_ImplWin32_Init(hwnd);

			auto* renderer = core::Services::get<render::Renderer>();
			auto* shaderHeap = core::Services::get<render::DescriptorManager>()->shaderHeap();
			ID3D12Device* device = renderer->device();
			auto numFrames = 2;

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
					
						bool isMouseMessage = (msg >= WM_MOUSEMOVE && msg <= WM_MOUSEWHEEL) ||
							(msg >= WM_LBUTTONDOWN && msg <= WM_MBUTTONDBLCLK);
						bool isKeyboardMessage = (msg >= WM_KEYDOWN && msg <= WM_DEADCHAR) ||
							(msg >= WM_SYSKEYDOWN && msg <= WM_SYSDEADCHAR);

						// Если ImGui хочет этот тип инпута — блокируем
						if (isMouseMessage && io.WantCaptureMouse)
							return true;
						if (isKeyboardMessage && io.WantCaptureKeyboard)
							return true;
					}
					return false;
				});


			_windows.push_back(std::make_unique<SceneHierarchyWindow>());
			_windows.push_back(std::make_unique<InspectorWindow>());
			ImGui_ImplDX12_Init(&init_info);
		}
		void onFrame() override
		{
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			if (ImGui::BeginMainMenuBar()) 
			{
				if (ImGui::BeginMenu("View")) 
				{
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

			// Рендерим все окна
			for (auto& window : _windows) {
				window->render();
			}

			ImGui::ShowDemoWindow();

			ImGui::Begin("CSyren Editor Test");
			ImGui::Text("Hello from ECS EditorSystem!");
			if (ImGui::Button("Test Button")) 
			{
				log::info("Button clicked!");
			}
			ImGui::End();
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
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}
	private:
		std::vector<std::unique_ptr<BaseEditorWindow>> _windows;
		static std::vector<render::DescriptorAllocation> s_imguiAllocations;
	};

	std::vector<render::DescriptorAllocation> EditorSystem::s_imguiAllocations{};
	REGISTER_SYSTEM(EditorSystem)
}

