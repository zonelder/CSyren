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


namespace csyren::render
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
			ImGui::StyleColorsDark();

			HWND hwnd = core::Services::get<Window>()->hwnd();
			ImGui_ImplWin32_Init(hwnd);

			auto* renderer = core::Services::get<render::Renderer>();
			ID3D12Device* device = renderer->device();

			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors = 1;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_fontSrvHeap));
			auto numFrames = 2;
			// Новый API для ImGui 1.90+
			ImGui_ImplDX12_InitInfo init_info = {};
			init_info.Device = device;
			init_info.CommandQueue = renderer->queue().raw(); 			
			init_info.NumFramesInFlight = numFrames;
			init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
			init_info.SrvDescriptorHeap = s_fontSrvHeap;
			init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* gpu_handle) {
				*cpu_handle = s_fontSrvHeap->GetCPUDescriptorHandleForHeapStart();
				*gpu_handle = s_fontSrvHeap->GetGPUDescriptorHandleForHeapStart();
				};

			init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};

			ImGui_ImplDX12_Init(&init_info);
		}
		void onFrame() override
		{
			// 1. Начинаем новый кадр
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			// 2. Рисуем UI (Тестовое окно + Демо)
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

			ID3D12DescriptorHeap* heaps[] = { s_fontSrvHeap };
			cmdList->SetDescriptorHeaps(1, heaps);
			// 3. Завершаем кадр и рендерим в CommandList
			ImGui::Render();

			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
		}

		void shutdown() override
		{
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();

			// Освобождаем хип
			if (s_fontSrvHeap) {
				s_fontSrvHeap->Release();
				s_fontSrvHeap = nullptr;
			}
		}
	private:
		static ID3D12DescriptorHeap* s_fontSrvHeap;
	};

	ID3D12DescriptorHeap* EditorSystem::s_fontSrvHeap = nullptr;
	REGISTER_SYSTEM(EditorSystem)
}

