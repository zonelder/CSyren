#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <unordered_map>
#include <mutex>

#include "material.h" // MaterialStateDesc
#include "shader.h"
#include "vertex_layout.h"
#include "core/services.h"

namespace
{
	using PSODesc = D3D12_GRAPHICS_PIPELINE_STATE_DESC;
}

namespace csyren::render
{
	class ComputeShader;
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
		PSOFactory() noexcept = default;

		void init();

		ID3D12PipelineState* try_get(const GraphicShader* shader, const MaterialStateDesc& desc);
		ID3D12PipelineState* get(ShaderHandle sh,const GraphicShader* shader, const MaterialStateDesc& desc,const VertexLayout& vertexLayout);
		// Compute pipelines are fully described by the shader.
		ID3D12PipelineState* getCompute(ComputeShaderHandle sh, const ComputeShader* shader);

	private:
        std::mutex _mutex;
        ID3D12Device* _device;
		std::unordered_map< PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, PSOKeyHasher > _psoCache;
		std::unordered_map< ComputeShaderHandle, Microsoft::WRL::ComPtr<ID3D12PipelineState> > _computeCache;
	};
}
