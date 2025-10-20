#pragma once
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
using Color = DirectX::PackedVector::XMCOLOR;

namespace csyren::render
{
	struct VertexXYZC
	{
		DirectX::XMFLOAT3 pos;
		Color color;
	};
}
