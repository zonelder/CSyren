#pragma once

#include "math/vector4.h"
#include "reflected_enum.h"
#include <algorithm>
#include "registration.h"

// <Windows.h> defines near/far as empty macros, which would erase the fields below.
// cstdmf/window_clean.h removes them and CSyren.engine.props force-includes it first in
// every translation unit; fail with a clear message if they leak back in anyway.
#if defined(near) || defined(far)
#error "near/far macros from <Windows.h> are defined: include cstdmf/window_clean.h before Windows SDK headers (CSyren.engine.props force-includes it)"
#endif

namespace csyren::core
{
    CS_ENUM(CameraRenderType, int, EachFrame, Once)
    CS_ENUM(ProjectionType, int, Perspective, Orthographic)

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
		ProjectionType projection = ProjectionType::Perspective;
		math::Vector4 background{ 0.1f, 0.1f, 0.1f, 1.0f };
		ViewportRect viewportRect;//unused

        DirectX::XMMATRIX projectionMatrix() const noexcept
        {
            // Legacy scenes stored near/far in reverse order. Keep their clip
            // volume, but make the depth convention explicit: near=1, far=0.
            const float nearDistance = (std::min)(near, far);
            const float farDistance = (std::max)(near, far);
            if (projection == ProjectionType::Perspective)
                return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov),
                    aspectRatio, farDistance, nearDistance);
            // For orthographic cameras FOV is the vertical view volume height.
            return DirectX::XMMatrixOrthographicLH(fov * aspectRatio, fov, farDistance, nearDistance);
        }

		static void describe();
	};
	CS_COMPONENT(Camera);

}
