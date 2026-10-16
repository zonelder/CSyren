#ifndef __CSYREN_DX12_RENDERER__
#define __CSYREN_DX12_RENDERER__

#include "cstdmf/window_clean.h"

#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include "d3dcompiler.h"
#include <DirectXMath.h>
#include <DirectXTex.h>

#include <string>
#include <vector>
#include <memory>

#include "dx_main.h"

#include "sampler_manager.h"
#include "constant_buffer.h"
#include "upload_ring_buffer.h"
#include "engine_semantics.h"
#include "pso_factory.h"
#include "shader_parameter_binder.h"
#include "vertex_layout.h"

#include "render_queue.h"
#include "descriptor_allocation.h"

#include "core/services.h"


namespace csyren::render
{
	//class ResourceManager;

	struct  alignas(16) PerFrameBuffer
	{
		DirectX::XMMATRIX projection;
		DirectX::XMMATRIX view;
		DirectX::XMMATRIX viewProjection;
		DirectX::XMMATRIX invView;
	};

	struct PerEntityBuffer
	{
		DirectX::XMMATRIX world;
	};
	class Texture;

	class Renderer
	{
	public:
		static constexpr UINT FramesInFlight = 3;
		Renderer() noexcept = default;
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool earlyInit();



		void beginResourceUpload();
		void endResourceUpload();

		ID3D12GraphicsCommandList* commandList() const noexcept { return _commands->raw(); }
		ID3D12Device* device() const noexcept { return _device.Get(); }

		UploadRingBuffer* getUploadBuffer() noexcept { return &_perEntityCB; };

		EngineVariableBuffer* getEngineVariableBuffer() noexcept { return &_engineVariableBuffer; };
		EntityVariableBuffer* getEntityVariableBuffer() noexcept { return &_entityVariableBuffer; };

		bool bindMaterial(ResourceManager& rm, MaterialHandle material,const VertexLayout& vertexLayout);

		bool bindEntity(const SemanticBufferLayout* layout);

		RenderQueue& queue() noexcept
		{
			return _mainQueue;
		}
	private:
        friend class FramePrepareSystem;
        friend class PresentationSystem;
		// The frame system owns recording contexts and output resources.
		void setFrameContext(CommandList* commands, UINT uploadSlot);
		void releaseFrameContext() noexcept { _commands = &_uploadCommands; }
		void waitForGpu();
		void enableDebugLayer();
		void createDevice();
		void createCommandQueue();

        details::ComPtr<ID3D12Device> _device;
        CommandList _uploadCommands;
        CommandList* _commands{ &_uploadCommands };
		RenderQueue								_mainQueue;

		UploadRingBuffer						_perEntityCB;
		EngineVariableBuffer					_engineVariableBuffer;
		EntityVariableBuffer					_entityVariableBuffer;
		ShaderParameterBinder					_parameterBinder;

	};
}

#endif
