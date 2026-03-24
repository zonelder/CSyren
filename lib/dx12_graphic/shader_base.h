#pragma once

#include "forward_decl.h"
#include "shader_meta.h"

#include <d3d12.h>
#include <wrl.h>
#include <d3d12shader.h>

#include <unordered_map>
#include <vector>
#include <string>

namespace csyren::render
{
	struct from_asset_path_t {};
	inline constexpr from_asset_path_t from_asset_path{};

	struct from_source_code_t {};
	inline constexpr from_source_code_t from_source_code{};

	struct ShaderResourceInfo
	{
		std::string name;
		UINT shaderRegister;
		UINT rootParameterIndex;
		UINT registerSpace;
	};

	struct ConstantBufferVariableInfo
	{
		std::string bufferName;
		UINT offset;
		UINT size;
		std::vector<uint8_t> defaultValue;
		bool needsTranspose;
	};

	class ShaderBase
	{
	public:
		ID3D12RootSignature* getRootSignature() const 
		{
			return _rootSignature.Get();
		}
		UINT getRootParameterIndex(const std::string& resourceName) const;

		bool buildRootSignatureFromReflection(ID3D12Device* device,
			const std::vector<std::pair<D3D12_SHADER_VISIBILITY, ID3DBlob*>>& shaderBlobs);

		static Microsoft::WRL::ComPtr<ID3DBlob> compileShader(
			const std::string& source,
			const char* target,
			const std::string& entryPoint,
			bool ignoreMissing = false);

	protected:
        Microsoft::WRL::ComPtr<ID3D12RootSignature>					_rootSignature;
        std::unordered_map<std::string, ShaderResourceInfo>			_resourceMap;
        std::unordered_map<std::string, ConstantBufferVariableInfo> _variableInfoMap;
        std::unordered_map<std::string, UINT>						_constantBufferSizes;
        ShaderMetaPtr												_meta;

	};
}