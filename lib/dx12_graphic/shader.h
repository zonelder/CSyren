#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <d3d12.h>
#include <wrl.h>
#include "d3d12shader.h"
#include "math/math.h"

namespace csyren::render
{
	class ResourceManager;
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
	};

	class Shader
	{
		friend class ResourceStorage<Shader>;
	public:
		Shader() = default;
		bool init(Renderer& renderer, ResourceManager& resourceManager, const Microsoft::WRL::ComPtr< ID3DBlob> vsBlob, const Microsoft::WRL::ComPtr< ID3DBlob> psBlob);
		bool init(Renderer& renderer, ResourceManager& resourceManager, const std::string& vsCode, const std::string& psCode);
		bool init(Renderer& renderer, ResourceManager& resourceManager, const std::wstring& vsPath, const std::wstring& psPath);
		bool init(Renderer& renderer, ResourceManager& resourceManager, const std::string& filepath);
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

		//TODO implement all this methods.
		bool setFloat(const std::string& name, float v);
		bool setInt(const std::string& name, int v);
		bool setBool(const std::string& name, bool v);
		bool setTexture(const std::string& name, Texture* v);
		bool setVector(const std::string& name, const DirectX::XMVECTOR& v);
		bool setMatrix(const std::string& name, const DirectX::XMMATRIX& v);
		bool setStruct(const std::string& name, const void* ptr, size_t size);

		void commit(ID3D12GraphicsCommandList* cmd, UploadRingBuffer& uploadBuffer);
	private:

		bool buildRootSignatureFromReflection(ID3D12Device* device, const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps);
		bool buildInputLayoutFromReflection(const D3D12_SHADER_BYTECODE& vs);
		Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;

		Microsoft::WRL::ComPtr< ID3DBlob> _vsBlob;
		Microsoft::WRL::ComPtr< ID3DBlob> _psBlob;

		std::unordered_map<std::string, ConstantBufferVariableInfo> _variableInfoMap;
		std::unordered_map<std::string, ShaderResourceInfo> _resourceMap;

		// CPU-copies of constant buffer;
		std::unordered_map<std::string, std::vector<uint8_t>> _constantBuffersData;
		std::unordered_map<std::string, UINT> _constantBufferSizes;
		std::unordered_set<std::string> _dirtyCBs; // names of dirty buffers;

		std::unordered_map<std::string, Texture*> _shaderTextures;

		std::vector<std::string>			   _semanticNames;
		std::vector< D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	};
}
