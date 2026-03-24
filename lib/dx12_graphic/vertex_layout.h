#pragma once
#include <vector>
#include <string_view>
#include <cstring>
#include <d3d12.h>
#include <stdexcept>
#include <array>

namespace csyren::render
{

	struct Semantic
	{
		static std::string_view POSITION;
		static std::string_view NORMAL;
		static std::string_view TANGENT;
		static std::string_view BITANGENT;
		static std::string_view COLOR;
		static std::string_view TEXCOORD;
		static std::string_view BLENDINDICES;
		static std::string_view BLENDWEIGHT;
	};

	struct VertexAttribute
	{
		std::string_view semanticName;
		DXGI_FORMAT format;
		uint32_t semanticIndex{ 0 };

		bool operator==(const VertexAttribute& other) const
		{
			return semanticName == other.semanticName && format == other.format && semanticIndex == other.semanticIndex;
		}
	};

	std::string_view makeIndexedSemantic(std::string_view base, uint32_t index);

	class VertexLayout
	{
	public:
		VertexLayout() noexcept = default;

		VertexLayout(std::vector<VertexAttribute>&& attrs);

		VertexLayout(std::initializer_list<VertexAttribute> attrs)
			: VertexLayout(std::vector<VertexAttribute>(attrs.begin(),attrs.end()))
		{}
		const std::vector<D3D12_INPUT_ELEMENT_DESC>& getD3DLayout() const noexcept { return _d3dLayout; }
		uint32_t getStride()						const noexcept { return _stride; }
		size_t getHash()							const noexcept { return _hash; }
		bool operator==(const VertexLayout& other)	const noexcept
		{
			return _attrs == other._attrs;
		}
	private:
		size_t computeHash() const noexcept;

		std::vector<VertexAttribute> _attrs;
		std::vector<D3D12_INPUT_ELEMENT_DESC> _d3dLayout;
		uint32_t _stride;
		size_t _hash;
	};
}
