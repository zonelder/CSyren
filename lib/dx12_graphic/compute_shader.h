#pragma once
#include "shader_base.h"


namespace csyren::render
{
	class ComputeShader final : public ShaderBase
	{
	public:

		bool init(Renderer& renderer, from_source_code_t, const std::string& code);
		bool init(Renderer& renderer, from_asset_path_t, const std::string& filepath);
		bool init(Renderer& renderer, const std::string& filepath);

		D3D12_SHADER_BYTECODE getCSBytecode() const noexcept {
			if (!_csBlob) return { nullptr, 0 };
			return { _csBlob->GetBufferPointer(), _csBlob->GetBufferSize() };
		}

		// Дополнительные compute-специфичные методы
		UINT getThreadGroupCountX() const;
		UINT getThreadGroupCountY() const;
		UINT getThreadGroupCountZ() const;
	private:
		bool finalizeInit(Renderer& renderer);
		Microsoft::WRL::ComPtr<ID3DBlob> _csBlob;
	};
}