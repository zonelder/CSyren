#pragma once
#include "vertex_layout.h"
#include "math/math.h"
#include "vertex_formats.h"
#include "cstdmf/log.h"

#include <vector>
#include <array>

namespace csyren::render
{
	class MeshBuilder
	{
	public:

		struct MeshRawData
		{
			std::vector<uint8_t> vertexBuffer;
			std::vector<vertex_meta::index_type> indices;
			VertexLayout layout;
			uint32_t vertexCount{ 0 };
			//here we place everything we should know about mesh to create a gpu mesh
		};

		MeshBuilder& addVertex(const math::Vector3& v)
		{
			_positions.push_back(v);
			return *this;
		}

		MeshBuilder& addNormal(const math::Vector3& n)
		{
			_normals.push_back(n);
			return *this;
		}

		MeshBuilder& addTangent(const math::Vector4& t)
		{
			_tangents.push_back(t);
			return *this;
		}

		MeshBuilder& addUV(size_t index, const math::Vector2& uv)
		{
			if (index >= vertex_meta::MAX_UV_CHANNELS)
			{
				log::error("MeshBuilder::addUV: invalid uv channel index- {}",index);
				throw std::out_of_range("MeshBuilder::addUV: invalid uv channel index.");
			}
			_uvChannels[index].push_back(uv);
			return *this;
		}

		MeshBuilder& addUV(const math::Vector2& uv)
		{
			return addUV(0, uv);
		}

		MeshBuilder& addColor(const vertex_meta::color_type& color)
		{
			_colors.push_back(color);
			return *this;
		}

		MeshBuilder& addTriangle(vertex_meta::index_type i0, vertex_meta::index_type i1, vertex_meta::index_type i2)
		{
			_indices.push_back(i0);
			_indices.push_back(i1);
			_indices.push_back(i2);
			return *this;
		}

		MeshBuilder& addIndex(vertex_meta::index_type i)
		{
			_indices.push_back(i);
			return *this;
		}

		//-----------------------------bulk methods--------------------------------------------

		template<typename Container>
		MeshBuilder& addVertices(const Container& verts)
		{
			_positions.insert(_positions.end(), verts.begin(), verts.end());
			return *this;
		}
		template<typename Container>
		MeshBuilder& addNormals(const Container& norms)
		{
			_normals.insert(_normals.end(), norms.begin(), norms.end());
			return *this;
		}

		template<typename Container>
		MeshBuilder& addTangents(const Container& tangs)
		{
			_tangents.insert(_tangents.end(), tangs.begin(), tangs.end());
			return *this;
		}

		template<typename Container>
		MeshBuilder& addColors(const Container& colors)
		{
			_colors.insert(_colors.end(), colors.begin(), colors.end());
			return *this;
		}

		template<typename Container>
		MeshBuilder& addUVs(size_t channel, const Container& uvs)
		{
			if (channel >= vertex_meta::MAX_UV_CHANNELS)
				throw std::out_of_range("MeshBuilder::addUVs: invalid UV channel index");
			_uvChannels[channel].insert(_uvChannels[channel].end(), uvs.begin(), uvs.end());
			return *this;
		}

		template<typename Container>
		MeshBuilder& addIndices(const Container& inds)
		{
			_indices.insert(_indices.end(), inds.begin(), inds.end());
			return *this;
		}

		void clear()
		{
			_positions.clear();
			_normals.clear();
			_tangents.clear();
			for (auto& uv : _uvChannels) uv.clear();
			_colors.clear();
			_indices.clear();
		}


		MeshRawData build() const
		{
			MeshRawData raw{};
			raw.layout = buildLayout();
			auto stride = raw.layout.getStride();
			const size_t vertexCount = _positions.size();
			raw.vertexCount = static_cast<uint32_t>(vertexCount);

			if (vertexCount == 0)
				return raw;

			raw.vertexBuffer.resize(vertexCount * stride);

			for (size_t i = 0; i < vertexCount; ++i)
			{
				uint8_t* dst = raw.vertexBuffer.data() + i * stride;
				size_t offset = 0;

				memcpy(dst + offset, &_positions[i], sizeof(math::Vector3));
				offset += sizeof(math::Vector3);

				if (!_normals.empty())
				{
					const math::Vector3 n = (i < _normals.size()) ? _normals[i] : math::Vector3{ 0,0,0 };
					memcpy(dst + offset, &n, sizeof(math::Vector3));
					offset += sizeof(math::Vector3);
				}

				if (!_tangents.empty())
				{
					const math::Vector4 t = (i < _tangents.size()) ? _tangents[i] : math::Vector4{ 0,0,0,1 };
					memcpy(dst + offset, &t, sizeof(math::Vector4));
					offset += sizeof(math::Vector4);
				}

				for (auto& channel : _uvChannels)
				{
					if (channel.empty()) continue;
					const math::Vector2 uv = (i < channel.size()) ? channel[i] : math::Vector2{ 0,0 };
					memcpy(dst + offset, &uv, sizeof(math::Vector2));
					offset += sizeof(math::Vector2);
				}

				if (!_colors.empty())
				{
					const auto color = (i < _colors.size()) ? _colors[i] : vertex_meta::color_type(0,0,0,1);
					memcpy(dst + offset, &color, sizeof(vertex_meta::color_type));
					offset += sizeof(vertex_meta::color_type);
				}
			}

			raw.indices = _indices;
			return raw;
		}

		const std::vector<math::Vector3>& vertices() const noexcept { return _positions; }
		const std::vector<vertex_meta::index_type>& colors() const noexcept { return _indices; }

	private:

		VertexLayout buildLayout() const
		{
			std::vector<VertexAttribute> attrs;
			if (!_positions.empty())
			{
				attrs.emplace_back(Semantic::POSITION, DXGI_FORMAT_R32G32B32_FLOAT);
			}
			if (!_normals.empty())
			{
				attrs.emplace_back(Semantic::NORMAL, DXGI_FORMAT_R32G32B32_FLOAT);
			}
			if (!_tangents.empty())
			{
				attrs.emplace_back(Semantic::TANGENT, DXGI_FORMAT_R32G32B32A32_FLOAT);
			}

			for (size_t i = 0; i < _uvChannels.size(); ++i)
			{
				if (_uvChannels[i].empty()) continue;
				attrs.emplace_back(Semantic::TEXCOORD, DXGI_FORMAT_R32G32_FLOAT, static_cast<uint8_t>(i));
			}

			if (!_colors.empty())
			{
				attrs.emplace_back(Semantic::COLOR, DXGI_FORMAT_R8G8B8A8_UNORM);
			}
			return VertexLayout(std::move(attrs));
		}

		std::vector<math::Vector3> _positions;
		std::vector<math::Vector3> _normals;
		std::vector<math::Vector4> _tangents;
		std::array<std::vector<math::Vector2>,vertex_meta::MAX_UV_CHANNELS> _uvChannels;
		std::vector<vertex_meta::color_type> _colors;
		std::vector<vertex_meta::index_type> _indices;
	};
}
