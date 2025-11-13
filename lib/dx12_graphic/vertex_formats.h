#pragma once
#include <DirectXPackedVector.h>

#include "math/math.h"

namespace csyren::render::vertex_meta
{
	using index_type = uint16_t;
	using position_type = math::Vector3;
	using normal_type = math::Vector3;
	using tangent_type = math::Vector3;
	using uv_type = math::Vector2;
	//TODO change this to math::color
	using color_type = DirectX::PackedVector::XMCOLOR;

	static constexpr uint8_t MAX_UV_CHANNELS = 1u;
}


namespace csyren::render
{
	struct VertexXYZC
	{
		vertex_meta::position_type pos;
		vertex_meta::color_type color;
	};
}
