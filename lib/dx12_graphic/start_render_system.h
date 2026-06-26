#pragma once

#include "core/event_bus.h"
#include "core/services.h"
#include "core/system_base.h"
#include "core/scene.h"

#include "core/transform.h"
#include "math/math.h"
#include "core/time.h"
#include "core/camera_service.h"
#include "core/time.h"
#include "core/camera.h"
#include "renderer.h"
#include "math/matrix4x4.h"
/*

DirectX::XMMATRIX createProjection(csyren::core::components::Camera& camera)
{

	using namespace DirectX;
	using namespace csyren::core::components;

	if (camera.projection == ProjectionType::Perspective)
	{
		return csyren::math::Matrix4x4::perspective(camera.fov, camera.aspectRatio, camera.near, camera.far);
	}
	else // Orthographic
	{
		// Interpret FOV as vertical height of view volume
		float viewHeight = camera.fov;
		float viewWidth = viewHeight * camera.aspectRatio;
		return DirectX::XMMatrixOrthographicLH(viewWidth, viewHeight, camera.near, camera.far);
	}
}
*/
namespace csyren::render
{
	class StartRenderSystem : public core::System
	{
	public:
		void update() override
		{
			using namespace csyren::core::components;
			auto renderer = core::Services::get<render::Renderer>();
			/*
			auto cameraEntt = core::Services::get<core::CameraContextService>()->get();
			auto time = core::Services::get<core::Time>();
			auto scene = core::Services::get<core::Scene>();
			auto camera = scene->getComponent<Camera>(cameraEntt);
			auto cameraTransform = scene->getComponent<Transform>(cameraEntt);
			auto engineVariables = renderer->getEngineVariableBuffer();
			engineVariables->totalTime = time->totalTime();
			DirectX::XMMATRIX invView = cameraTransform->world();
			DirectX::XMStoreFloat4x4(&engineVariables->invViewMatrix, invView);

			// viewMatrix = inverse(invViewMatrix)
			DirectX::XMMATRIX view = DirectX::XMMatrixInverse(nullptr, invView);
			DirectX::XMStoreFloat4x4(&engineVariables->viewMatrix, view);

			// projectionMatrix = projection
			DirectX::XMMATRIX proj = createProjection(*camera);
			DirectX::XMStoreFloat4x4(&engineVariables->projectionMatrix, proj);

			// viewProjectionMatrix = view * projection
			DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
			DirectX::XMStoreFloat4x4(&engineVariables->viewProjectionMatrix, viewProj);
			*/
		}
	};

	REGISTER_SYSTEM(StartRenderSystem);

	class EndRenderSystem : public core::System
	{
	public:
		void update() override
		{
			using namespace csyren::core::components;
			auto renderer = core::Services::get<render::Renderer>();
		}
	};

	REGISTER_SYSTEM(EndRenderSystem);
}

