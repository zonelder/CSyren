#ifndef __CSYREN_MATERIAL__
#define __CSYREN_MATERIAL__

#include <wrl.h>
#include <d3d12.h>
#include "resource_handle.h"


namespace csyren::render
{
	class Renderer;
	
	struct alignas(sizeof(size_t)) MaterialStateDesc
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
		using VariableContainer = std::vector<uint8_t>;
		using ParametersMap = std::unordered_map<std::string, VariableContainer>;
		using TextureMap = std::unordered_map<std::string, TextureHandle>;
		Material() noexcept = default;
		bool init(Renderer& renderer, const std::string& filepath);
		bool init(Renderer& renderer, ShaderHandle shaderHandle, const MaterialStateDesc& states);
		ShaderHandle getShader() const noexcept { return _shaderHandle;}
		const MaterialStateDesc& getStates() const noexcept { return _states; }

		void setShader(ShaderHandle shader) { _shaderHandle = shader; }
		void setFloat(const std::string& name, float v) { setRaw(name, &v, sizeof(float)); }
		void setInt(const std::string& name, int v) { setRaw(name, &v, sizeof(int)); }
		void setBool(const std::string& name, bool v) { setRaw(name, &v, sizeof(bool)); }
		void setVector(const std::string& name, const DirectX::XMFLOAT4& v) { setRaw(name, &v, sizeof(DirectX::XMFLOAT4)); }
		void setMatrix(const std::string& name, const DirectX::XMFLOAT4X4& v) { setRaw(name, &v, sizeof(v)); }
		void setStruct(const std::string& name, const void* ptr, size_t size) { setRaw(name, ptr, size); }
		void setTexture(const std::string& name, TextureHandle v) 
		{
			_textures[name] = v;
			++_version;
		}
		uint64_t version() const noexcept { return _version; }
		bool containsVariable(const std::string& name) const noexcept { return _params.contains(name); }
		bool containsTexture(const std::string& name) const noexcept { return _textures.contains(name); }
		void removeVariable(const std::string& name)
		{
			if (_params.contains(name))
			{
				_params.erase(name);
			}
		}
		VariableContainer getVariable(const std::string& name)
		{
			if (!_params.contains(name))
				return {};

			return _params[name];
		}
	private:

		void setRaw(const std::string& name, const void* data, size_t size)
		{
			auto& buf = _params[name];
			buf.resize(size);
			memcpy(buf.data(), data, size);
			++_version;
		}
		MaterialStateDesc	_states;
		ShaderHandle		_shaderHandle;

		ParametersMap		_params;
		TextureMap			_textures;
		uint64_t			_version = 0;

	};
}

#endif
