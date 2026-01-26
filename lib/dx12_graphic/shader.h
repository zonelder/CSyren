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
		GraphicShader() = default;
		bool init(Renderer& renderer,from_source_code_t, const std::string& code);
		bool init(Renderer& renderer,from_asset_path_t, const std::string& filepath);
		bool init(Renderer& renderer,const std::string& filepath);

		const std::vector< D3D12_INPUT_ELEMENT_DESC>& getInputLayout() const noexcept { return _inputLayout; };

		D3D12_SHADER_BYTECODE getPSBytecode() const noexcept 
		{ 
			if (!_psBlob) return  { nullptr,0 }; 
			return { _psBlob->GetBufferPointer(), _psBlob->GetBufferSize() };
		}

		D3D12_SHADER_BYTECODE getVSBytecode() const noexcept 
		{
			if (!_vsBlob) return  { nullptr,0 };
			return { _vsBlob->GetBufferPointer(), _vsBlob->GetBufferSize() };
		}
		D3D12_SHADER_BYTECODE getGSBytecode() const noexcept
		{
			if (!_gsBlob) return  { nullptr,0 };
			return { _gsBlob->GetBufferPointer(), _gsBlob->GetBufferSize() };
		}
		D3D12_SHADER_BYTECODE getHSBytecode() const noexcept
		{
			if (!_hsBlob) return  { nullptr,0 };
			return { _hsBlob->GetBufferPointer(), _hsBlob->GetBufferSize() };
		}
		D3D12_SHADER_BYTECODE getDSBytecode() const noexcept
		{
			if (!_dsBlob) return  { nullptr,0 };
			return { _dsBlob->GetBufferPointer(), _dsBlob->GetBufferSize() };
		}

		const LinkedBuffer* getConstantBuffer(details::CBufferUpdateType updateType) const noexcept;

		const SemanticBufferLayout* getSemanticBuffer(details::CBufferUpdateType updateType) const noexcept;
	private:
		bool buildInputLayoutFromReflection();

		bool compileAndInit(Renderer& renderer, const std::string& shaderCode, const std::filesystem::path relativePath);
		bool finalizeInit(Renderer& renderer);
		bool loadPrecompiledAndInit(Renderer& renderer, const std::filesystem::path& relativePath);
		void linkSemantics();
		void buildSemanticLayout();

		Microsoft::WRL::ComPtr< ID3DBlob> _vsBlob;
		Microsoft::WRL::ComPtr< ID3DBlob> _psBlob;
		Microsoft::WRL::ComPtr< ID3DBlob> _gsBlob;
		Microsoft::WRL::ComPtr< ID3DBlob> _hsBlob;
		Microsoft::WRL::ComPtr< ID3DBlob> _dsBlob;

		std::vector<LinkedBuffer>								_linkedBuffers;
		std::vector<SemanticBufferLayout>						_semanticLayouts;

		std::vector<std::string>								_InputLayoutSemantic;
		std::vector< D3D12_INPUT_ELEMENT_DESC>					_inputLayout;
	};
}
