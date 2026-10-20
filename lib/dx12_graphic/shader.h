#pragma once

#include "forward_decl.h"
#include "math/math.h"
#include "engine_semantics.h"
#include "shader_meta.h"
#include "shader_base.h"

#include <d3d12.h>
#include <wrl.h>
#include <d3d12shader.h>

namespace csyren::render
{
	struct LinkedVariable
	{
		std::string name;
		std::string semantic;

		//duple of SemanticInfo for cache friedly behaviour;
		details::SemanticDataType type;
		size_t offset;
		size_t size;
		bool needTranspose;
	};

	struct LinkedBuffer
	{
		std::string bufferName;
		details::CBufferUpdateType type;
		size_t size;
		size_t rootParameterIndex;
		std::vector<LinkedVariable> variables;
	};

	struct SemanticCopyCommand
	{
		uint32_t srcOffset; // offset â EngineVariableBuffer
		uint32_t dstOffset; // offset â GPU CBuffer
		uint32_t size;		// size of copying
		bool transpose{ false };
	};

	struct SemanticBufferLayout
	{
		details::CBufferUpdateType type;
		uint32_t size;
		uint32_t rootParameterIndex;
		std::vector<SemanticCopyCommand> copyCommands;
	};




	class GraphicShader final : public ShaderBase
	{
		friend class ResourceStorage<GraphicShader>;
	public:
        GraphicShader() : ShaderBase(ShaderType::Graphics) {}

        const std::vector<D3D12_INPUT_ELEMENT_DESC>& getInputLayout() const noexcept { return _inputLayout; }
        D3D12_SHADER_BYTECODE getPSBytecode() const noexcept { return getBytecode(ShaderStage::Pixel); }
        D3D12_SHADER_BYTECODE getVSBytecode() const noexcept { return getBytecode(ShaderStage::Vertex); }
        D3D12_SHADER_BYTECODE getGSBytecode() const noexcept { return getBytecode(ShaderStage::Geometry); }
        D3D12_SHADER_BYTECODE getHSBytecode() const noexcept { return getBytecode(ShaderStage::Hull); }
        D3D12_SHADER_BYTECODE getDSBytecode() const noexcept { return getBytecode(ShaderStage::Domain); }

		const LinkedBuffer* getConstantBuffer(details::CBufferUpdateType updateType) const noexcept;
		// Number of SV_TARGET outputs of the pixel shader. Deferred (GBuffer)
		// materials write 3 targets, forward materials write 1.
		UINT renderTargetCount() const noexcept { return _renderTargetCount; }
		bool isDeferred() const noexcept { return _renderTargetCount >= 3; }

		const SemanticBufferLayout* getSemanticBuffer(details::CBufferUpdateType updateType) const noexcept;
	private:
		bool buildInputLayoutFromReflection();

        bool finalizeInit(ID3D12Device* device) override;
        void linkSemantics();
        void buildSemanticLayout();
        void countRenderTargets();

		std::vector<LinkedBuffer>								_linkedBuffers;
		std::vector<SemanticBufferLayout>						_semanticLayouts;

		std::vector<std::string>								_InputLayoutSemantic;
		UINT													_renderTargetCount = 1;
		std::vector< D3D12_INPUT_ELEMENT_DESC>					_inputLayout;
	};
}
