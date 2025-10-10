#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#include <d3d12.h>
#include <wrl.h>
#include "d3d12shader.h"
#include "math/math.h"
#include "engine_semantics.h"
#include "shader_meta.h"

namespace csyren::render
{
	class Renderer;
	class Texture;
	class UploadRingBuffer;
	template<typename T> class ResourceStorage;

	struct ShaderResourceInfo
	{
		std::string name;
		UINT shaderRegister;
		UINT rootParameterIndex;
		UINT registerSpace;
	};

	// Новая структура для хранения информации о переменных внутри cbuffer
	struct ConstantBufferVariableInfo
	{
		std::string bufferName;
		UINT offset;
		UINT size;
		std::vector<uint8_t> defaultValue;
		bool needsTranspose;
	};

	struct LinkedVariable
	{
		std::string name;

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
		uint32_t srcOffset; // offset в EngineVariableBuffer
		uint32_t dstOffset; // offset в GPU CBuffer
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

	struct from_asset_path_t {};
	inline constexpr from_asset_path_t from_asset_path{};

	// Тэг для инициализации из исходного кода в строке
	struct from_source_code_t {};
	inline constexpr from_source_code_t from_source_code{};


	class Shader
	{
		friend class ResourceStorage<Shader>;
	public:
		Shader() = default;
		bool init(Renderer& renderer,from_source_code_t, const std::string& code);
		bool init(Renderer& renderer,from_asset_path_t, const std::string& filepath);
		bool init(Renderer& renderer,const std::string& filepath);
		ID3D12RootSignature* getRootSignature() const 
		{
			return  _rootSignature.Get();
		}
		UINT getRootParameterIndex(const std::string& resourceName) const;

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
		void setEngineParameters(const EngineVariableBuffer& engineBuffer,details::CBufferUpdateType updateType);

		void commit(ID3D12GraphicsCommandList* cmd, UploadRingBuffer& uploadBuffer);


		const LinkedBuffer* getConstantBuffer(details::CBufferUpdateType updateType) const noexcept;

		const SemanticBufferLayout* getSemanticBuffer(details::CBufferUpdateType updateType) const noexcept;
	private:
		bool buildRootSignatureFromReflection(ID3D12Device* device, const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps);
		bool buildInputLayoutFromReflection(const D3D12_SHADER_BYTECODE& vs);

		bool compileAndInit(Renderer& renderer, const std::string& shaderCode, const std::filesystem::path relativePath);
		bool finalizeInit(Renderer& renderer, const D3D12_SHADER_BYTECODE& vs,const D3D12_SHADER_BYTECODE& ps);
		bool loadPrecompiledAndInit(Renderer& renderer, const std::filesystem::path& relativePath);

		Microsoft::WRL::ComPtr<ID3DBlob> compileShader(const std::string& source, const char* target, const std::string& entryPoint);

		bool validateMeta(const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps);
		void linkSemantics();
		void buildSemanticLayout();

		Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;

		Microsoft::WRL::ComPtr< ID3DBlob> _vsBlob;
		Microsoft::WRL::ComPtr< ID3DBlob> _psBlob;

		std::unordered_map<std::string, ConstantBufferVariableInfo> _variableInfoMap;
		std::unordered_map<std::string, ShaderResourceInfo>			_resourceMap;

		std::unordered_map<std::string, UINT>					_constantBufferSizes;

		std::vector<LinkedBuffer>								_linkedBuffers;
		std::vector<SemanticBufferLayout>						_semanticLayouts;

		std::vector<std::string>								_InputLayoutSemantic;
		std::vector< D3D12_INPUT_ELEMENT_DESC>					_inputLayout;

		ShaderMetaPtr		_meta;
	};
}
