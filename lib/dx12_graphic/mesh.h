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

	using Color = DirectX::PackedVector::XMCOLOR;

	class Mesh
	{
		friend class ResourceStorage<Mesh>;
	public:
		struct Vertex
		{
			DirectX::XMFLOAT3 pos;
			Color color;
		};
		Mesh() noexcept = default;
		void draw(Renderer& renderer);
	private:

		bool init(const std::string& filepath);
		bool init(Renderer& renderer, const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices);

	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> _indexBuffer;
		D3D12_VERTEX_BUFFER_VIEW _vertexView{};
		D3D12_INDEX_BUFFER_VIEW  _indexView{};
		UINT _indexCount{ 0 };
	};
}

#endif
