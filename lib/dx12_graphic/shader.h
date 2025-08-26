#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include <d3d12.h>
#include <wrl.h>
#include "d3d12shader.h"

namespace csyren::render
{
	template<typename T> class ResourceStorage;

	struct ShaderResourceInfo
	{
		std::string name;
		UINT shaderRegister;
		UINT rootParameterIndex;
		UINT registerSpace;
	};

	class Shader
	{
		friend class ResourceStorage<Shader>;
	public:
		Shader() = default;
		bool init(ID3D12Device* device, const Microsoft::WRL::ComPtr<ID3DBlob> vs, const Microsoft::WRL::ComPtr<ID3DBlob> ps,const std::wstring& name);
		bool init(ID3D12Device* device, const std::string& vsCode, const std::string& psCode, const std::wstring& name);
		bool init(ID3D12Device* device, const std::wstring& vsPath, const std::wstring& psPath);
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
	private:

		bool buildRootSignatureFromReflection(ID3D12Device* device, const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps);
		bool buildInputLayoutFromReflection(const D3D12_SHADER_BYTECODE& vs);
		Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;

		Microsoft::WRL::ComPtr< ID3DBlob> _vsBlob;
		Microsoft::WRL::ComPtr< ID3DBlob> _psBlob;

		std::unordered_map<std::string, ShaderResourceInfo> _resourceMap;
		std::vector<std::string>			   _semanticNames;
		std::vector< D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	};
}
