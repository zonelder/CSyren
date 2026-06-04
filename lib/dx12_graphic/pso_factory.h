#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include "material.h" // Äëÿ MaterialStateDesc
#include "shader.h"
#include "vertex_layout.h"

namespace
{
	using PSODesc = D3D12_GRAPHICS_PIPELINE_STATE_DESC;
}

namespace csyren::render::details
{
	using VertexLayoutHash = size_t;
	struct PSOKey
	{
		ShaderHandle shaderHandle;
		MaterialStateDesc materialState;
		VertexLayoutHash vertexLayoutHash;
		bool operator==(const PSOKey& other) const 
		{
			return shaderHandle == other.shaderHandle &&
				memcmp(&materialState, &other.materialState, sizeof(MaterialStateDesc)) == 0 && vertexLayoutHash == other.vertexLayoutHash;
		}
	};

	struct PSOKeyHasher
	{
		size_t operator()(const PSOKey& desc) const;
	};


	class PSOFactory
	{
	public:
		PSOFactory(ID3D12Device* device) noexcept : _device(device) {};

		ID3D12PipelineState* try_get(GraphicShader* shader, const MaterialStateDesc& desc);
		ID3D12PipelineState* get(ShaderHandle sh,GraphicShader* shader, const MaterialStateDesc& desc,const VertexLayout& vertexLayout);

	private:
		ID3D12Device* _device;
		std::unordered_map< PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, PSOKeyHasher > _psoCache;
	};
}
