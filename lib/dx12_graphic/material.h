#ifndef __CSYREN_MATERIAL__
#define __CSYREN_MATERIAL__

#include <wrl.h>
#include <d3d12.h>

namespace csyren::render
{
	class Renderer;

	class Material
	{
	public:
		Material() noexcept = default;
		bool init(Renderer& renderer, const D3D12_SHADER_BYTECODE& vs, const D3D12_SHADER_BYTECODE& ps);
		ID3D12PipelineState* pso() const noexcept { return _pso.Get(); }
		ID3D12RootSignature* rootSig() const noexcept { return _rootSig.Get(); }
	private:
		Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSig;
	};
}

#endif
