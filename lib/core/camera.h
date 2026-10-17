#pragma once

#include "math/vector4.h"
#include <algorithm>

namespace csyren::core
{
    enum class CameraRenderType { EachFrame, Once };
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
        CameraRenderType render_type = CameraRenderType::EachFrame;
		float far{ 1000.0f };
		float near{ 0.1f };
		float aspectRatio{ 16.0f / 9.0f };
		float fov{ 60.0f };

		int priority{ 0 };
		ProjectionType projection = Perspective;
		math::Vector4 background{ 0.1f, 0.1f, 0.1f, 1.0f };
		ViewportRect viewportRect;//unused

        DirectX::XMMATRIX projectionMatrix() const noexcept
        {
            // Legacy scenes stored near/far in reverse order. Keep their clip
            // volume, but make the depth convention explicit: near=1, far=0.
            const float nearDistance = (std::min)(near, far);
            const float farDistance = (std::max)(near, far);
            if (projection == Perspective)
                return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov),
                    aspectRatio, farDistance, nearDistance);
            // For orthographic cameras FOV is the vertical view volume height.
            return DirectX::XMMatrixOrthographicLH(fov * aspectRatio, fov, farDistance, nearDistance);
        }

		static void describe();
	};

}
