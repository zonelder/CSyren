#ifndef __CSYREN_MESH__
#define __CSYREN_MESH__

#include <vector>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include "vertex_layout.h"
#include "mesh_builder.h"

namespace csyren::render
{
	class Renderer;
	class Material;
	template<typename T> class ResourceStorage;

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

		const VertexLayout& getLayout() const { return _layout; }
		const math::MeshBounds& bounds() const noexcept { return _bounds; }
		const std::vector<math::Vector3>& positions() const noexcept { return _positions; }
		const std::vector<vertex_meta::index_type>& indices() const noexcept { return _indices; }
	private:

		bool init(const std::string& filepath);
		bool init(
			const MeshBuilder& builder,
			Usage usage = Usage::Static
		);

		bool update( const MeshBuilder::MeshRawData& rw );
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
		VertexLayout	_layout;
		math::MeshBounds _bounds;
		std::vector<math::Vector3> _positions;
		std::vector<vertex_meta::index_type> _indices;
	};
}

#endif
