#pragma once
#include "core/renderer.h"
#include "cstdmf/rectangle.h"

namespace csyren::components
{
	struct SpriteRenderer
	{
		render::TextureHandle texture;
		cstdmf::Rectangle sourceRect{ 0,0,1.0f,1.0f };

		render::MaterialHandle material;
		DirectX::XMFLOAT4 tint;
		int depth;
	};
}
