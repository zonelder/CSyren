#pragma once

#include <DirectXMath.h>
#include "math/color.h"
#include "core/serializer.h"

#undef far
#undef near

namespace csyren::core
{
	enum ProjectionType
	{
		Perspective,
		Orthographic
	};

	struct ViewportRect
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float w{ 1.0f };
		float h{ 1.0f };
	};

	struct Camera
	{
		float far{ 0.1f };
		float near{ 1000.0f };
		float aspectRatio{ 16.0f / 9.0f };
		float fov{ 60.0f };

		int priority{ 0 };
		ProjectionType projection = Perspective;
		math::Color background = { 0.1f, 0.1f, 0.1f, 1.0f };
		ViewportRect viewportRect;//unused
	private:
		SERIALIZABLE(Camera, far, near, aspectRatio, fov,priority, projection,background);

	};

}