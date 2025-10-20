#ifndef __CSYREN_MESH__
#define __CSYREN_MESH__

#include <vector>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

namespace csyren::render
{
	class Renderer;
	class Material;
	template<typename T> class ResourceStorage;


	using MeshIndex = uint16_t;

	class Mesh
	{
		friend class ResourceStorage<Mesh>;
	public:

		enum class Usage
		{
			Static,
			Dynamic,
		};

		Mesh() noexcept = default;
		void bind(Renderer& renderer);
		void draw(Renderer& renderer);
	private:

		bool init(Renderer& renderer,const std::string& filepath);
		bool init(
			Renderer& renderer,
			const void* vertexData,
			size_t vertexDataSize,
			uint32_t vertexStride,
			const std::vector<uint16_t>& indices,
			Usage usage = Usage::Static
		);

		bool update(
			const void* vertexData,
			size_t vertexDataSize,
			const std::vector<uint16_t>& indices
		);
		bool createBuffer(
			ID3D12Device* device,
			D3D12_HEAP_TYPE heapType,
			UINT64 size,
			D3D12_RESOURCE_STATES initialState,
			Microsoft::WRL::ComPtr<ID3D12Resource>& outResource
		);

	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> _indexBuffer;
		D3D12_VERTEX_BUFFER_VIEW _vertexView{};
		D3D12_INDEX_BUFFER_VIEW  _indexView{};
		UINT _indexCount{ 0 };
		Usage _usage = Usage::Static;
	};
}

#endif
