#ifndef __CSYREN_MATERIAL__
#define __CSYREN_MATERIAL__

#include <wrl.h>
#include <d3d12.h>
#include "resource_handle.h"


namespace csyren::render
{
	class Renderer;
    class ResourceManager;
	
	struct MaterialStateDesc
	{
		D3D12_BLEND_DESC blendState;
		D3D12_RASTERIZER_DESC rasterizerState;
		D3D12_DEPTH_STENCIL_DESC depthStencilState;
		D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType;
		UINT numRenderTargets;
		DXGI_FORMAT rtvFormats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
		DXGI_FORMAT dsvFormat;
        MaterialStateDesc();
	};

	class Material
	{
	public:
		Material() noexcept = default;
		bool init(Renderer& renderer, ResourceManager& rm, const std::string& filepath);
		bool init(Renderer& renderer,ResourceManager& rm, ShaderHandle shaderHandle, const MaterialStateDesc& states);
		ID3D12PipelineState* pso() const noexcept { return _pso.Get(); }
		ShaderHandle getShader() const noexcept { return _shaderHandle;}
	private:
		ShaderHandle _shaderHandle;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
	};
}

#endif
